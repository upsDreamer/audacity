# DSH 权限与 PowerShell 7 · follow-up 2：供给任务（交提权复核 agent）

Status: open

> 承接 `.scratch/acl-review-brief.md`、`.scratch/acl-review-report.md`、`.scratch/acl-review-followup.md`。
> 执行者：具备 **提权** Git Bash 的复核 agent（上一轮已证明可写 DACL）。
> DSH 会话侧（非提权、沙箱内）**做不了**这三件事：装到 Program Files 需管理员；父目录授权在工作区之外；
> 本会话 shell 的网络调用会被挂死（三次实测）、winget 在本机不可用（退出码异常、空输出）。

---

## 任务 A：安装 PowerShell 7 到 DSH 会探测的位置

**为什么是这个位置**：`@deepseek-ai/dsh-pwsh-local` 的解析源只有一个固定候选（asar 内 `src/resolve.ts`）：

```js
const candidates = [join(programFiles, "PowerShell", "7", "pwsh.exe")];
```

解析顺序 = 显式 `pwshPath` 配置 → **`%ProgramFiles%\PowerShell\7\pwsh.exe`** → PATH → Windows PowerShell 5.1 兜底。
本机当前**没有**任何 PS7（`C:\Program Files\PowerShell\7\pwsh.exe` 不存在，PATH 上也没有 `pwsh`），所以工具一直用 5.1（`PSEdition=Desktop`）。

**安装（任选其一，都需要管理员）**：

```bash
# 首选 winget（若本机 winget 可用）
winget install --id Microsoft.PowerShell --exact --scope machine --accept-package-agreements --accept-source-agreements
# 备选：官方 MSI（把 <ver> 换成实际版本，如 7.5.x）
# https://github.com/PowerShell/PowerShell/releases/download/v<ver>/PowerShell-<ver>-win-x64.msi
msiexec /i "PowerShell-<ver>-win-x64.msi" /quiet ADD_PATH=1 ENABLE_PSREMOTING=0 REGISTER_MANIFEST=1
```

**验证**：

```bash
"/c/Program Files/PowerShell/7/pwsh.exe" -NoProfile -Command '$PSVersionTable.PSVersion.ToString(); $PSVersionTable.PSEdition'
```

期望：`7.x` + `Core`。

**DSH 侧生效**：解析结果被缓存（"仅当存储的 `pwshPath` 与当前可执行文件所依据的值不同时才重新探测"）。
若 DSH 会话里 `$PSVersionTable` 仍是 5.1，按顺序处理：
1. 在 DSH 设置页的 shell 执行器条目里显式填 `pwshPath = C:\Program Files\PowerShell\7\pwsh.exe`（该字段只存在于 PowerShell 执行器）；
2. 或重启 DSH 应用；
3. 然后在 DSH 会话里跑 `$PSVersionTable` 复核（预期变 7.x）。

**注意**：装 PS7 只是工效改善（`&&`/`??`/UTF-8 等），**与权限故障无关**——权限故障发生在 spawn 之前。

---

## 任务 B：父目录可继承授权（防复发）

**背景**：`D:\` 只给用户 `Authenticated Users:(M)`，所以 **D: 上新建的每个工作区都会重复缺陷 1**。
`D:\Work\project` 的属主 = `upstr`（已确认），因此非提权但不受限的进程也能写它的 DACL；你提权更没问题。

```bash
export MSYS_NO_PATHCONV=1
target='D:\Work\project'; bkdir='D:\Work\_acl-backups'
mkdir -p "$(cygpath -u "$bkdir")"
stamp=$(date +%Y%m%d-%H%M%S)

# 备份（注意：/save 存的是相对名；回滚时 /restore 的首参是**父目录**）
icacls.exe "$target" /save "$(cygpath -w "$bkdir")\\project-acl-$stamp.bak"
powershell.exe -NoProfile -Command "(Get-Acl -LiteralPath '$target').Sddl" > "$(cygpath -u "$bkdir")/project-sddl-$stamp.txt"
icacls.exe "$target"

# 授权（可继承；含 WRITE_DAC 与 WRITE_OWNER）
icacls.exe "$target" /grant 'upstr:(OI)(CI)(F)'
icacls.exe "$target"

# 继承验证：新建目录应当带上继承来的 upstr:(F)
mkdir -p '/d/Work/project/.acl-inherit-probe'
icacls.exe 'D:\Work\project\.acl-inherit-probe'
rmdir '/d/Work/project/.acl-inherit-probe'
```

**回滚**（首参是父目录，已演练验证过这个语义）：

```bash
icacls.exe 'D:\Work' /restore "$(cygpath -w "$bkdir")\\project-acl-$stamp.bak"
```

**副作用预期**：带 `(OI)(CI)` 的授权会被传播到 `D:\Work\project` 的**既有子树**（Windows 自动继承传播），
即该子树下所有对象的 DACL 都会多出一条 `upstr` 的继承条目。这是期望行为（此后新工作区直接可用），
但耗时取决于文件数量；请记录耗时与失败对象数（`icacls` 会汇总 "Failed processing N files"）。

---

## 任务 C（可选，先问用户再决定）：把 DSH 的工作区授权传播到既有仓库树

**背景（本轮新发现的 DSH 缺陷 2）**：DSH 授权成功后，工作区 SID 的 ACE 与 Low 标签**只落在工作区根目录**，
没有传播到既有子树。实测在受限 shell 里：

```
.scratch / docs / study_audacity / study_audacity\docs / study_audacity\benchmarks / au3  → writable=False（全部六个）
那些目录的 icacls：只有从 D:\ 继承的条目，没有 S-1-4-* ACE，没有 Mandatory Label
工作区根：S-1-4-101348277-349246831:(OI)(CI)(W,D,DC) + Mandatory Label\Low:(OI)(CI)(NW)  → 根目录与新建目录可写
```

后果：`workspace-write` 实际退化为"只有工作区根目录可写"，任何写入既有目录的操作（构建、测试、git、格式化）都会失败。

**若用户要求立刻恢复仓库内可写**（否则等 DSH 上游修）：

```bash
export MSYS_NO_PATHCONV=1
root='D:\Work\project\audacity'
bkdir='D:\Work\_acl-backups'; mkdir -p "$(cygpath -u "$bkdir")"
stamp=$(date +%Y%m%d-%H%M%S)

# 0) 必做：先做**全树** DACL 备份（/T），否则这一步的大范围改动不可逆
icacls.exe "$root" /save "$(cygpath -w "$bkdir")\\audacity-tree-$stamp.bak" /T
"备份文件大小：$(stat -c %s "$(cygpath -u "$bkdir")/audacity-tree-$stamp.bak") 字节（应显著大于单目录备份）"

# 1) 读当前工作区 SID（每个工作区不同，不要照抄）
icacls.exe "$root" | grep -o 'S-1-4-[0-9-]*'
ws_sid='<上一步读到的 S-1-4-...>'

# 2) 传播 ACE 与 Low 标签
icacls.exe "$root" /grant "*$ws_sid:(OI)(CI)(W,D,DC)" /T /C /Q
icacls.exe "$root" /setintegritylevel "(OI)(CI)L" /T /C /Q
```

**回滚**：

```bash
# DACL：首参是父目录，注意相对名语义
icacls.exe 'D:\Work\project' /restore "$(cygpath -w "$bkdir")\\audacity-tree-$stamp.bak"
# 完整性标签（/save 不保存标签，只能显式改回；"显式 Medium" 与 "原本无标签" 不完全等价）
icacls.exe "$root" /setintegritylevel "(OI)(CI)M" /T /C /Q
```

**完成后必须复测**（在 DSH 会话里，用受沙箱约束的 shell 逐个写文件）：
`.scratch`、`docs`、`study_audacity`、`study_audacity\docs`、`study_audacity\benchmarks`、`au3`
——本 follow-up 之前实测全部 `writable=False`，传播后应全部为 `True`；并把结果记入交付。

**严禁**回滚 `D:\Work\project\audacity.acl.bak`：那里面那条 `upstr:(F)` 是当前工作区可用的命脉。

**必须先告知用户的风险**：
1. 这是**大范围** ACL + 完整性标签改动（对象数未知，`study_audacity\benchmarks\build` 等构建产物很多）；
2. `icacls /save` **只保存 DACL，不保存完整性标签**，因此标签无法用它回滚——要撤销需再跑
   `icacls.exe "$root" /setintegritylevel "(OI)(CI)M" /T /C /Q`（且"显式 Medium"与"原本无标签"并不完全等价）；
3. 部分对象可能因权限不足失败（`/C` 会继续），需记录失败数；
4. 更正统的做法是让 DSH 自己修（见 `.scratch/dsh-bug-report.md`）。

---

## 交付

1. 任务 A：安装位置、版本、验证输出；DSH 会话侧 `$PSVersionTable` 是否已变为 7.x（含是否改过 `pwshPath`/重启）。
2. 任务 B：备份文件路径、改动前后 `icacls`、继承探针结果、耗时与失败数、回滚命令（未执行则标注）。
3. 任务 C：是否执行（若执行：DACL/标签改动范围、失败数、回滚方式）。
4. 任何与本文预期不符之处，如实记录。