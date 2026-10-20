# DSH 工作区权限故障：独立复核提示词

Status: open

> 用途：交付给**另一个具备 Git Bash 的复核 agent**，独立复核本机 DSH 的「工作区权限」故障诊断，并审计提议的修复方案。
> 来源：DSH 桌面端会话（工作区 `D:\Work\project\audacity`）实测观测 + 对安装包 `app.asar` 的只读检索。
> 建立：2026-10-20。时效注意：文中提到的修复脚本位于 skill 临时目录，可能已被清理；备选提取方式见 P7。
> 以下从 `# 任务：` 开始即完整提示词，可整段复制转发。

---

# 任务：独立复核一台 Windows 机器上 DSH 的「工作区权限」故障（只读诊断，禁止修复）

你在一台 Windows 机器上有 Git Bash，请**独立复核**另一个 agent 给出的诊断，并审计它提出的修复方案。
要求：**证伪优先**。不要因为下面的结论写得确定就接受它们；能反证就反证。

====================================================================
## 0. 你的操作环境（Git Bash 的坑，先看这段）
====================================================================
- 给 **Windows 原生程序**（icacls / powershell / cmd / dir / attrib / fsutil / whoami / tasklist）
  传 **Windows 路径**：`D:\Work\...`。Git Bash 自己的命令（ls/grep/cat/rm/mkdir/cygpath）用 `/d/Work/...`。
- 先执行：`export MSYS_NO_PATHCONV=1`，否则 `/grant`、`/all`、`/q` 这类开关会被改写成路径。
- PowerShell 在 Git Bash 里引号极易出错：**把脚本写进 .ps1 文件，再 `powershell.exe -NoProfile -ExecutionPolicy Bypass -File` 执行**，不要拼长 `-Command`。
- 在你的**普通用户上下文**里跑：不要提权、不要 UAC、不要管理员。先确认 `whoami.exe /all` 里没有提升标记。
- **禁止**修改任何真实目录的权限：不许 `takeown`、`icacls /reset`、`icacls /remove`、`/setowner`，
  不许改所有者或继承设置。唯一允许的写操作见 P6 的探针目录（自建、测完立即删除）。
- 不要执行任何"修复"命令，包括下面提到的修复脚本。你的产出是证据与判断。

====================================================================
## 1. 已知观测（另一个 agent 在 DSH 桌面端会话里实测得到）
====================================================================
- 症状：在这个会话里调用 DSH 的 shell 工具（工具名 `pwsh`）**执行任何命令都失败**，每次返回：
      SetNamedSecurityInfoW failed (Win32 5): grantWrite(D:\Work\project\audacity)
  Win32 5 = ERROR_ACCESS_DENIED。工作区 = `D:\Work\project\audacity`。
- 该失败发生在"命令还没跑起来"之前：看起来是 DSH 为沙箱做**工作区授权**（grantWrite）时被拒。
- DSH 安装：`D:\Program Files (x86)\DSH`（Electron 桌面端，nightly，win-x64，
  包版本 0.2.1-rc.2，更新源 `.../dsh-desk/feeds/win-x64/`）。
- 同一会话里，DSH 的文件类工具（read/grep/glob）**可以正常读**该工作区；写入路径未测试。
- 该会话的沙箱模式为 workspace-write（工作区内可写）。

### 另一个 agent 的结论（你要复核的对象）
- C1. 故障点在 DSH 的 Windows 沙箱 provider：它为工作区物化一个确定性写权限身份（SID），
  调 `grantWrite` 读现有 DACL、合并后写回；这一步被拒，所以任何 shell 命令都起不来。
- C2. 该故障与"用哪个 shell"无关（bash / Git Bash 也一样卡在这一步），因为授权发生在 spawn 之前。
- C3. DSH 在 win32 上沙箱走 ACL 受限令牌 runner（包 `dsh-sandbox-windows-acl`）；
  Linux / macOS 才用 bwrap / Landlock / Seatbelt。
- C4. 由此提出的修复（**尚未执行**）：跑一个 DSH 自带的诊断/修复脚本，它
  (a) 在链条上"缺少变更权限/取得所有权"的目录上，给当前用户补一条完全控制的允许项；
  (b) 在来源处移除其它 AppContainer 应用包留下的允许项（`S-1-15-2-*`，排除以 1 或 2 结尾的通用组）；
  (c) 每个改动前备份并打印可回滚命令，改动后回读验证；失败则自动反序回滚。
  脚本路径（skill 临时目录，可能已被清理）：
      `C:\Users\upstr\AppData\Local\Temp\dsh-acl-skill-eQMFoX\scripts\diagnose-windows-sandbox-acl.ps1`
  若不存在，脚本源码也内嵌在 app.asar 里：`grep -a -n 'diagnose-windows-sandbox-acl'` 那个 asar。
- C5. 另一个 agent 对根因的**主假设**：工作区（或链上某层）对当前用户只授予了 Modify 类权限，
  而 Modify **不含**"更改权限 / 取得所有权"；DSH 的授权步骤用的是受限令牌，受限令牌下
  所有者隐含的更改权限不生效，于是 ACCESS_DENIED。次假设：链上有显式拒绝项、或有
  其它应用包的允许项污染、或调用方并非该目录的所有者。

====================================================================
## 2. 你要回答的四个问题
====================================================================
- Q1 复现与范围：故障能否稳定复现？影响面是"只有这个目录"、"只有这个卷/父目录"还是"所有工作区"？
  是否只影响 shell 工具，而文件工具（写入）正常？
- Q2 根因：最可能的根因是什么？给出能被独立复现的证据链。
- Q3 上述 C1–C3、C5 是否成立？逐条给"成立 / 不成立 / 证据不足"。
- Q4 修复方案（C4）是否正确、是否足够、是否有副作用或更安全的替代？在什么条件下**不该**跑它？

====================================================================
## 3. 检查清单（按顺序执行，保留原始输出）
====================================================================

### P0 环境与身份
```bash
export MSYS_NO_PATHCONV=1
whoami.exe
whoami.exe /all          # 关注：用户 SID、组成员、是否提升、完整性级别(Mandatory Label)
cmd /c ver
echo "DSH_HOME=$DSH_HOME"; echo "TMP=$TMP"; echo "TEMP=$TEMP"
# 确认临时目录不在工作区内部（DSH 有"工作区等于或包含临时根则拒绝授权"的规则）
```

### P1 复现（在 DSH 侧）
如果你自己也是一个 DSH 会话（有 shell 工具）：
- 用它跑一条无害命令（如 `whoami`），记录成功/失败与完整错误文本；
- 试一次在 `D:\Work\project\audacity` 内写入一个临时文件（DSH 的 write 工具或 shell），记录结果。
  → 这能区分"故障只影响 shell 工具"还是"所有沙箱执行路径都受影响"。
- 再换一个**同卷的其它目录**作为工作区（新建一个空目录试）复现一次。
  → 这能区分"目录特定"还是"卷 / 用户 / 令牌特定"。这是最强的判别实验。

如果你不是 DSH 会话，跳过本项并说明。

### P2 目录链的权限快照（只读，四个对象）
把下面存成 `acl-review.ps1`，再用 `powershell.exe -File` 执行，参数传这四个路径：
`'D:\'`、`'D:\Work'`、`'D:\Work\project'`、`'D:\Work\project\audacity'`

```powershell
param([string[]]$Paths)
foreach ($p in $Paths) {
  Write-Output "===== $p"
  try {
    $a = Get-Acl -LiteralPath $p
    Write-Output ("OWNER={0}`nPROTECTED={1}" -f $a.Owner, $a.AreAccessRulesProtected)
    Write-Output ("SDDL={0}" -f $a.Sddl)     # 语言中立，便于比对
    $a.Access | ForEach-Object {
      Write-Output ("ACE type={0} id={1} rights={2} inherited={3} inherit={4} prop={5}" -f `
        $_.AccessControlType, $_.IdentityReference, $_.FileSystemRights, $_.IsInherited,
        $_.InheritanceFlags, $_.PropagationFlags)
    }
  } catch { Write-Output ("ERROR: {0}" -f $_.Exception.Message) }
}
```

同时用 icacls 看一遍（它会显示完整性标签）：
```bash
icacls.exe 'D:\'; icacls.exe 'D:\Work'; icacls.exe 'D:\Work\project'; icacls.exe 'D:\Work\project\audacity'
```

判据（逐条给结论 + 证据行）：
- a) 当前用户（或其组）拿到的是 **完全控制(F)** 还是 **修改(M)** 或更少？
  → 关键：M 不含"更改权限"和"取得所有权"。若只有 M，且调用方是受限令牌，就会 ACCESS_DENIED。
  这与"所有者本人手动 icacls 授权却能成功"并不矛盾，务必区分这两种令牌情形。
- b) 是否存在显式**拒绝**项（针对当前用户 / Users / Everyone / Authenticated Users），
  尤其含"删除子项"之类；继承来的拒绝项也要记录来源层。
- c) 是否存在 `S-1-15-2-*` 的**允许**项（AppContainer 应用包 SID；排除以 1 或 2 结尾的通用组）。
  记录完整 SID、是否继承、出现在哪一层（源 or 继承副本）。
  注意：要区分"应用包 SID(`S-1-15-2-*`)"与"能力 SID(`S-1-15-3-*`)"——这决定修复脚本会不会误删东西。
- d) 是否有 `OWNER RIGHTS (S-1-4-*)` 条目、是否有 `Mandatory Label\Low`（`S-1-16-4096`）。
  预期会看到：DSH 自己的授权会留下这类痕迹，别直接当成病因。
- e) 继承是否被切断（`PROTECTED=True`）？父目录是否给了子目录任何权限？

### P3 结构性/策略性因素（这些都能独立造成同样症状，必须排除）
```bash
cmd /c dir /al 'D:\Work\project'                      # 链上是否有 junction / 符号链接
fsutil.exe reparsepoint query 'D:\Work\project\audacity'
cmd /c attrib 'D:\Work\project\audacity'              # 只读 / 系统等属性
powershell.exe -NoProfile -Command "Get-PSDrive D | Format-List Name,Root,DisplayRoot; Get-Volume -DriveLetter D | Format-List DriveType,FileSystem,FileSystemLabel"
   # D: 是不是 subst / 映射盘 / VHD？文件系统是不是 exFAT / FAT32（FAT 根本没有 ACL）？
powershell.exe -NoProfile -Command "Get-MpPreference | Select-Object EnableControlledFolderAccess,ControlledFolderAccessProtectedFolders,ControlledFolderAccessAllowedApplications | Format-List"
   # Defender「受控文件夹访问」（勒索软件防护）会拦写入，且症状酷似权限问题
powershell.exe -NoProfile -Command "Get-WinEvent -LogName 'Microsoft-Windows-Windows Defender/Operational' -MaxEvents 200 | Where-Object { $_.Id -in 1123,1124 } | Select-Object -First 20 TimeCreated,Id,Message | Format-List"
powershell.exe -NoProfile -Command "Get-CimInstance -Namespace root/SecurityCenter2 -ClassName AntiVirusProduct | Select-Object displayName,productState"
```

### P4 读 DSH 的实现，验证机制（不要凭文档转述下结论）
```bash
ASAR='/d/Program Files (x86)/DSH/resources/app.asar'   # 注意路径含空格与括号，务必加引号
grep -a -n 'grantWrite' "$ASAR" | head -40
grep -a -n 'SetNamedSecurityInfoW' "$ASAR" | head -20
grep -a -n 'CreateRestrictedToken\|SAFER\|restricted' "$ASAR" | head -40
```
重点回答：
- (a) 授权动作是**宿主进程**做的，还是**受限令牌子进程**做的？（决定"所有者隐含权限是否生效"）
- (b) 需要授权的目标集合（工作区根？临时目录？两者？）
- (c) 失败文本 `SetNamedSecurityInfoW failed … grantWrite(path)` 与代码的对应关系。
- (d) 是否存在"工作区等于/包含临时根则拒绝"之类的**前置拒绝**规则——若命中，根因就完全不同。

### P5 DSH 侧日志（找真实堆栈，而不是只看工具返回的包装文本）
```bash
echo "$DSH_HOME"                      # 空则默认 ~/.dsh
ls -la ~/.dsh/logs 2>/dev/null | tail -20
grep -rIl --binary-files=without-match 'grantWrite\|SetNamedSecurityInfoW\|SANDBOX_UNAVAILABLE' ~/.dsh 2>/dev/null | head
ls -la "$APPDATA" 2>/dev/null | grep -i dsh      # Electron userData 侧日志
```

### P6 权限探针（唯一允许的写操作；测完立即删除）
目的：区分"这卷上完全不能改权限" vs "只有目标目录不行"。
```bash
probe="/d/Work/project/.acl-probe-$$"
mkdir -p "$probe"
icacls.exe "$(cygpath -w "$probe")"                                   # 基线
icacls.exe "$(cygpath -w "$probe")" /grant:r "$(whoami.exe):(F)"      # 普通令牌能否写 DACL
icacls.exe "$(cygpath -w "$probe")"
rm -rf "$probe"                                                       # 必须清理
```
重要限制：普通令牌下"所有者隐含的更改权限"生效，所以**探针成功不能证明 DSH 的授权路径也能成功**。
它只能排除"整卷 / 父目录拒绝一切权限写入"。请如实标注这个局限，不要过度解读。

### P7 审计修复方案（只读，不执行）
- 找到并通读那个脚本（skill 临时目录，或从 `app.asar` 提取）。
- 逐条审计并给结论：
  1. `-AllowRoot` 边界是否真的限制住所有改动？盘根 / 用户目录 / WindowsApps / 重解析点是否被拒？
  2. 备份内容是什么（自有 ACE + 保护位？继承项是否包含）？回滚脚本能否真正还原？
     若备份不含继承项，恢复后父目录语义变化会不会导致"看起来回滚了其实没有"？
  3. "移除 `S-1-15-2-*` 允许项"会不会误删 **DSH 自己正在使用**的授权，或其它合法应用的授权？
     删掉后该目录对某些应用是否变不可读（已知：另一个 AppContainer 工具 ACL 过的树，
     低完整性子进程读不到）？
  4. 自动回滚的触发条件是否覆盖所有失败路径？失败时是否会留下"半修复"状态？
  5. 若真正根因是"只有 Modify + 受限令牌"，它的修复（补一条你本人完全控制的允许项）是否正对症？
     副作用是什么（继承标志、是否覆盖拒绝项、是否影响子项）？
  6. 有没有**更小、更可控**的替代方案（例如只给工作区根补一条精确的允许项，而不是同时清理包 SID）？

====================================================================
## 4. 交付格式（请严格照此输出）
====================================================================
1. **结论表**：Q1–Q4 各一行，结论 + 置信度（高 / 中 / 低）。
2. **证据**：每条结论后面附「命令 + 原始输出片段 + 涉及路径」。不要只写"检查过了，没问题"。
3. **与 C1–C5 的逐条比对**：成立 / 不成立 / 证据不足；不成立时给出反证。
4. **未排除的可能性**：明确列出你没能验证的分支，以及验证它们需要什么。
5. **对修复方案的建议**：跑 / 改条件后再跑 / 不要跑，各给理由；如需修改，给出具体改动点。
6. 如果结论与另一个 agent 不一致，直接说"我不同意，因为……"。

约束复述：不修改真实权限、不提权、不执行修复、不在用户仓库里留文件（探针目录必须删除）。