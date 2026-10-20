# DSH 权限故障 · follow-up：最小授权试验（决定性判别）

Status: resolved（A 分支，见 §7）

> 承接 `.scratch/acl-review-report.md`（独立复核报告）与 `.scratch/acl-review-brief.md`（原始简报）。
> 目的：用**一次最小、可回滚**的改动，把根因二分——"缺显式更改权限" vs "授权路径看不见用户 SID"。
> 执行者：具备 Git Bash 的复核 agent（你，当前为提权令牌）。
> DSH 侧验证：由 DSH 会话中的 agent 执行，不由你执行（见 §3）。

---

## 0. 判定目标（一次二分）

复核报告把分歧收在一处：**"仅 Modify"不足以解释全部观测**——C: 的默认工作区授权成功、D: 的 audacity 从未留下授权痕迹。
本次实验用最小改动回答一个问题：

> 在 `D:\Work\project\audacity` 上补一条**当前用户的完全控制允许项**之后，
> DSH 在**普通模式**下能否成功执行命令？

- **A 分支（能）** → 根因就是"缺显式更改权限（更改权限/取得所有权）"，修复脚本的步骤 (a) **正对症**，收工。
- **B 分支（不能）** → 授权路径根本看不到用户 SID（受限令牌 / 非用户令牌 / 属主隐含权被忽略），
  补 `upstr:(F)` 无效 → 按报告建议作为 **DSH 侧缺陷**上报（§5 供证据）。

预期机制（供你判断观测是否符合预期）：`grantWrite` 需要**更改权限（WRITE_DAC）**；
`Modify` 不含它；属主隐含的更改权限在**受限令牌**（restricting 列表不含用户 SID）下不生效。

---

## 1. 前置（必做，全部只读或可回滚）

1. **确认现场仍失败**：请用户先在 DSH 会话里跑一条无害命令（如 `whoami`），
   记录完整错误文本与时间戳。工作区必须是 `D:\Work\project\audacity`，且**DSH 保持运行**。
2. **演练回滚链路**（在自建探针目录上，不碰真实目标）：
   ```bash
   export MSYS_NO_PATHCONV=1
   probe="/d/Work/project/.acl-saveprobe-$$"; mkdir -p "$probe"
   wp=$(cygpath -w "$probe")
   icacls.exe "$wp" /save "$(cygpath -w /d/Work/project/.acl-saveprobe.bak)" /T   # 保存
   icacls.exe "$wp" /grant "$(whoami.exe):(F)"                                    # 改动
   icacls.exe "$wp"                                                               # 回读
   icacls.exe "$wp" /restore "$(cygpath -w /d/Work/project/.acl-saveprobe.bak)"    # 回滚
   icacls.exe "$wp"                                                               # 确认还原
   rm -rf "$probe" /d/Work/project/.acl-saveprobe.bak
   ```
   → **必须先证明这条链路真的能还原**，再动真实目录。若 `/restore` 报错，先解决它，不要继续。
3. **备份真实目标**（两重备份，缺一不可）：
   ```bash
   target='D:\Work\project\audacity'
   icacls.exe "$target"                                                      # 改动前全文，存进报告
   icacls.exe "$target" /save 'D:\Work\project\audacity.acl.bak'
   powershell.exe -NoProfile -Command "(Get-Acl -LiteralPath '$target').Sddl"  # SDDL 文本另存
   ```
   把备份文件路径写进报告；确认文件非空。

---

## 2. 最小改动（只改**一个**对象）

```bash
icacls.exe 'D:\Work\project\audacity' /grant 'upstr:(F)'
```

- **故意不带继承标志**：ACE 只落在该目录对象本身。这与修复脚本的授权形态一致
  （脚本用三参数构造 `FileSystemAccessRule`，同样不带继承标志），也是最小改动。
- 不要顺带清理包 SID、不要跑子树扫描、不要动 `Everyone:(CI)(DENY)(DC)` 之类的既有项——
  这些都不是本次实验的变量。
- 回读并对比：
  ```bash
  icacls.exe 'D:\Work\project\audacity'
  powershell.exe -NoProfile -Command "(Get-Acl -LiteralPath 'D:\Work\project\audacity').Sddl"
  ```
  确认：只多出一条 `upstr` 的完全控制允许项，其余条目未变。
- 如果 DSH 之后的报错**点名了别的路径**（例如链上的上级目录或临时目录），
  就**只**在那个路径上重复本节（一次一个对象），并逐个记录。

---

## 3. 判定（在 DSH 侧执行，不在你的 Git Bash 里）

把"权限已补、可以测了"告诉用户，然后由 **DSH 会话中的 agent**：

1. 用**普通模式**（workspace-write，**不要**解除沙箱限制）跑一条无害命令；
2. 记录成功/失败与完整错误文本。

若仍失败，再按顺序排除干扰后重试一次：

- **重启 DSH 应用**再试 → 排除"每个工作区每服务器生命周期一次"的授权复用缓存影响；
- 确认工作区路径没被改动过（`~/.dsh/storages/workspace.json` 里的记录）。

两次结果都要记录，并注明是否重启过。

### 正向确认（A 分支的额外证据，很有价值）

若 DSH 侧命令**成功**，请立刻回读该目录：

```bash
icacls.exe 'D:\Work\project\audacity'
```

预期能看到 **DSH 自己的授权痕迹**出现（对照 C: 默认工作区上那份完整三件套）：

- 一条 `S-1-4-*` 的确定性工作区 SID 允许项（形如 `(OI)(CI)(W,D,DC)`）；
- 一条 `Mandatory Label\Low Mandatory Level:(OI)(CI)(NW)` 低完整性标签；
- 可能还有 `Everyone:(CI)(DENY)(DC)`。

**看到它们 = 直接证明 `grantWrite` 真的执行成功了**，比"命令能跑"更硬的证据。

---

## 4. 分支处置

- **A（治愈）**：保留这条允许项——它正是修复脚本步骤 (a) 会做的事。
  可选收尾：再跑一次修复脚本，获得**整个链条**的覆盖 + 脚本自带的正式备份与回滚记录
  （脚本参数：`-AllowRoot` 精确设为 `D:\Work\project\audacity`，`-Out` 指到仓库外）。
- **B（未治愈）**：不要继续加权限试探（加多少都无用）。转 §5 取证据，然后按 DSH 缺陷上报。
  **回滚**：`icacls.exe 'D:\Work\project\audacity' /restore 'D:\Work\project\audacity.acl.bak'`，
  或按 §1-3 保存的 SDDL 还原；回滚后回读确认。

---

## 5. 令牌鉴别（B 分支必须；A 分支可选，但能解释 C: 对照）

**目标**：找出"执行授权"的那个进程用的是哪种令牌。候选：Electron 主进程、`dsh-desktop-host`、
以及任何 dsh 相关子进程（授权按报告发生在 `AclSandbox.init()` 内、创建受限令牌**之前**）。

**先枚举进程**：

```bash
powershell.exe -NoProfile -Command "Get-CimInstance Win32_Process | Where-Object { \$_.Name -match 'dsh|electron' -or \$_.ExecutablePath -like '*DSH*' } | Select-Object ProcessId,ParentProcessId,Name,ExecutablePath | Format-Table -Auto -Wrap"
powershell.exe -NoProfile -Command "Get-Process -IncludeUserName | Where-Object { \$_.ProcessName -match 'dsh|electron' } | Select-Object Id,ProcessName,UserName | Format-Table -Auto"
```

**需要的事实项**（对每个候选 PID）：令牌用户 SID（是否 `upstr`）、完整性级别、
**是否受限令牌**、限制 SID 列表、是否 AppContainer、提升类型。

**工具优先级**：已有 Process Explorer / `accesschk`（Sysinternals）就先用它，最省事；
否则自写 P/Invoke。下面是 `GetTokenInformation` 的关键常量与结构布局，可直接用：

| 信息类 | 值 | 结构 |
|---|---|---|
| `TokenUser` | 1 | `SID_AND_ATTRIBUTES { IntPtr Sid; UInt32 Attributes; }` |
| `TokenRestrictedSids` | 11 | `TOKEN_GROUPS { UInt32 GroupCount; SID_AND_ATTRIBUTES Groups[]; }` |
| `TokenElevationType` | 18 | `DWORD`（1=Default 2=Full 3=Limited） |
| `TokenElevation` | 20 | `DWORD`（0/1） |
| `TokenHasRestrictions` | 21 | `DWORD`（0/1）← **判"受限令牌"的关键** |
| `TokenIntegrityLevel` | 25 | `TOKEN_MANDATORY_LABEL { SID_AND_ATTRIBUTES Label; }`（取最后一级 subauthority） |
| `TokenIsAppContainer` | 29 | `DWORD` |

- 打开方式：`OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION=0x1000)` → `OpenProcessToken(TOKEN_QUERY=0x0008)`；
  你是提权令牌，跨进程查询没问题；**只读**，不要改任何令牌。
- SID 转名字：`GetLengthSid` + `Marshal.Copy` → `new SecurityIdentifier(bytes,0).Translate([NTAccount])`。
- 逐个 PID `try/catch` 跳过打不开的进程，并记录失败原因（进程已退出 / 拒绝访问）。

**判读**：
- 若授权进程的令牌 `TokenHasRestrictions=1` 且限制列表不含 `upstr` → **B 分支机制坐实**，
  这正是需要上报的缺陷：受限于"先有工作区 SID 才能授权、而首次授权又需要该 SID"的循环。
- 若令牌是普通 Medium 且未受限，却仍失败 → 说明失败另有原因（例如授权其实发生在别处），
  回到 §0 重新定位。

---

## 6. 交付（追加到复核报告或另开一节）

1. **结论**：A 还是 B；置信度。
2. **证据**：现场复现输出、改动前后 `icacls` 全文与 SDDL、`/save` 备份文件路径、
   DSH 侧成功/失败输出（含是否重启过）、A 分支下新出现的 DSH 授权痕迹。
3. **回滚路径**：确切命令 + 是否已执行。
4. **令牌事实**（若做了 §5）：每个候选进程的 PID/用户/完整性/受限标志/限制 SID 列表。
5. **建议的最终修复形态**：脚本 + 参数，或"上报 DSH 缺陷"。

约束复述：只读诊断为主；唯一允许的改动是 §2 那一条允许项（可回滚，且已备份）；
**不**清理包 SID、**不**做子树扫描、**不**提权修改其它对象、探针目录测后即删。

---

## 7. 结果（A 分支，已判定 · 2026-10-20 18:09）

**判定：A 分支——补一条显式完全控制允许项即治愈。** 普通 workspace-write 模式下命令已可执行，**无需重启 DSH**。

### DSH 侧证据（同一命令、同一模式，改动前后对比）

```
whoami                              → laptop-mhvj999r\upstr        （改动前：SetNamedSecurityInfoW failed (Win32 5): grantWrite(...)）
PS=5.1.26100.9711 Lang=FullLanguage DSH_SHELL=1 CWD=D:\Work\project\audacity
工作区内写入探针                     → wrote=7 bytes, cleaned=True    （沙箱内写入正常）
工作区外写入探针（%USERPROFILE%）     → DENIED (UnauthorizedAccessException)  （沙箱仍在围栏，隔离未被削弱）
```

### 正向确认：DSH 自己的授权痕迹已出现

改动后回读 `icacls 'D:\Work\project\audacity'`，新增了完整三件套：

```
S-1-4-101348277-349246831:(OI)(CI)(W,D,DC)         ← 本工作区的确定性工作区 SID（对照：C: 上为另一个值）
Mandatory Label\Low Mandatory Level:(OI)(CI)(NW)  ← Low 完整性标签
LAPTOP-MHVJ999R\upstr:(F)                          ← §2 补入的那一条
```

→ `grantWrite` 确实执行成功（DACL + Low 标签一次写入），与代码复核的预测一致；
C: 默认工作区上观察到的那份三件套，在本工作区原样复现。

### 根因（经本次实验收窄）

`grantWrite` 需要 **WRITE_DAC 与 WRITE_OWNER**；Windows 给属主的隐含权限只有 `READ_CONTROL | WRITE_DAC`，
**不含 WRITE_OWNER**；而 D: 链对用户只授予 `Authenticated Users:(M)`，Modify 同样不含 WRITE_OWNER。
→ 首次授权因缺 WRITE_OWNER 被拒（Win32 5）；补 `(F)`（含 WRITE_OWNER）后通过。
这也解释了 C: 对照为何一直成功：那条链对 `upstr` 是显式 `(F)`。

**由此反证 B 分支**：`upstr:(F)` 生效说明授权上下文**认可用户 SID**，不是 write-restricted 的 pass-2 检查；
报告 §5 担心的"受限令牌死循环"不成立。§5 令牌鉴别降为可选（仅供机制记录）。

### 遗留与建议

1. **会在新工作区复发**：`D:\` 对用户只给 Modify，因此在 D: 上新建的每个工作区都可能重复此故障。
   若要根治，需在**共同父目录**上给 `upstr` 一条可继承的完全控制项（例如 `D:\Work\project`），
   代价是权限面扩大到该子树——由使用者权衡。
2. **不需要再跑修复脚本**：本次这条 ACE 正是脚本步骤 (a) 的产物，脚本在该路径上预计为 no-op。
   已有 `/save` 备份 + SDDL 文本；回滚：`icacls.exe 'D:\Work\project' /restore 'D:\Work\project\audacity.acl.bak'`。
3. **值得上报 DSH**：win32 沙箱的首次授权在"用户仅有 Modify 的普通目录"上必然失败
   （属主隐含权不含 WRITE_OWNER），而这类目录正是最常见的用户工作区形态。
4. **环境旁注**：本机 `pwsh` 工具解析到的是 **Windows PowerShell 5.1**（`PS=5.1.26100.9711`），
   不是 PowerShell 7；`&&`、`??` 之类 7.x 语法不可用，脚本需按 5.1 写。