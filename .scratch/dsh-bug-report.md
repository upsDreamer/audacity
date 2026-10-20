# DSH 缺陷报告：win32 沙箱工作区授权（可发送给 DSH 方面）

> 用途：把本机实测到的两个 win32 沙箱缺陷发给 DSH 方面（建议用 `/feedback` 发送：粘贴本文件摘要 + 指向本文件路径）。
> 证据来源：本机实测 + 对 `app.asar` 的只读代码检索；独立复核报告见 `.scratch/acl-review-report.md`。

## 环境

- Windows 11（10.0.26100），`D:` 为 NTFS 数据卷（卷标 Data，固定磁盘，非映射盘/exFAT）
- DSH 桌面端 nightly win-x64，包版本 `0.2.1-rc.2`
- 工作区：`D:\Work\project\audacity`，属主 = 当前用户 `upstr`
- 该目录 DACL 全部继承自 `D:\`：`Authenticated Users:(M)`、`Administrators:(F)`、`SYSTEM:(F)`、`Users:(RX)`
- 运行令牌：**非提权**（DSH 不提升用户权限）；沙箱模式 `workspace-write`
- 环境已排除的干扰：无显式 DENY、无 `S-1-15-2-*` 应用包 SID、链上无重解析点、`attrib` 无只读/系统属性、
  Defender 受控文件夹访问为关闭、无第三方杀软、非 FAT/映射盘

---

## 缺陷 1：首次授权要求 WRITE_OWNER，在"用户仅有 Modify"的目录上必然失败

**现象**：在该工作区里，任何 shell 工具调用立即失败（命令根本没启动）：

```
SetNamedSecurityInfoW failed (Win32 5): grantWrite(D:\Work\project\audacity)
```

**最小复现**：

1. 在 NTFS 数据卷上建一个属于当前用户的目录（DACL 继承父目录，用户只有 `Modify`）；
2. 以它为工作区，在 `workspace-write` 模式下执行任意 shell 命令 → 失败（`Win32 5`，拒绝访问）；
3. 给该目录加一条显式完全控制允许项（`icacls <dir> /grant "<user>:(F)"`）后，**同一命令立即成功**；
4. 成功后可观察到 DSH 自己的授权痕迹出现在该目录上：
   `S-1-4-<workspace>:(OI)(CI)(W,D,DC)` + `Mandatory Label\Low Mandatory Level:(OI)(CI)(NW)`。

**根因**：`grantWrite` 用一次 `SetNamedSecurityInfoW` 写 DACL **和** Low 标签，因此需要 **WRITE_DAC 与 WRITE_OWNER**；
但 Windows 给属主的隐含权限只有 `READ_CONTROL | WRITE_DAC`，**不含 WRITE_OWNER**；而 `Modify` 同样不含。
于是"用户自己拥有、但只被授予 Modify"的目录（数据卷上最常见的形态）首次授权必失败。

**旁证**：`C:\Users\<user>\Documents\deepseek-harness\default-workspace` 一直可用——它的链上对用户存在
**显式 `(F)`**（含 WRITE_OWNER）。也就是说：默认工作区所在路径恰好绕过了该缺陷。

**影响**：用户数据盘（如 `D:`）上的工作区**完全不可用**（该工作区内所有 shell 命令都失败）；
错误文本只含 API 名与 Win32 码，没有任何可执行指引。

**建议**：把标签写入与 DACL 写入拆分（先只写 DACL，标签失败时降级并告警）；或在预检失败时返回可执行指引
（例如"需要在该目录上具备更改权限与取得所有权，或先执行 `icacls <dir> /grant <user>:(F)`"）。

---

## 缺陷 2：授权未传播到既有子树，`workspace-write` 退化为"只有根目录可写"

**现象**：缺陷 1 修好后，受限 shell **只能写工作区根目录**（以及此后新建的目录）；
仓库里**既有的**子目录全部不可写。

**证据**（在 `workspace-write` 模式下的 shell 内逐个实测写入）：

```
.scratch                  writable=False
docs                      writable=False
study_audacity            writable=False
study_audacity\docs       writable=False
study_audacity\benchmarks writable=False
au3                       writable=False
```

这些目录的 `icacls` 只列出从 `D:\` 继承的条目——**没有** `S-1-4-<workspace>` 的 ACE，**也没有** `Mandatory Label`；
而工作区根目录两者都有，根目录下新建的目录/文件可写（继承生效）。

**后果**：任何需要写入既有目录的操作（构建、测试、`git`、格式化、写日志）都会失败。
实际语义从"工作区内可写"退化为"只有工作区根目录可写"——对已存在的项目目录几乎不可用。

**建议**：首次物化授权时对既有子树做一次传播；或写 DACL 时带上自动继承请求位，让 Windows 自行传播到既有子项；
若出于性能考虑有意跳过传播，请在文档与状态里明示"既有子树不可写"，并提供一个一次性传播入口。

---

## 附：本机的临时处置（供参考）

1. 对工作区根目录补了一条显式完全控制允许项（带 `/save` 备份与 SDDL 记录，可回滚）→ 缺陷 1 已绕过；
2. 沙箱隔离未被削弱：工作区外写入仍被拒（`%USERPROFILE%` 与工作区父目录实测均 `Access denied`）；
3. 缺陷 2 未处置（等 DSH 侧判定，或另行传播）；
4. 顺带一提：本机 `pwsh` 工具解析到 **Windows PowerShell 5.1**（`C:\Program Files\PowerShell\7\pwsh.exe` 不存在，
   PATH 上也无 `pwsh`）——`dsh-pwsh-local` 的候选位置只有 `%ProgramFiles%\PowerShell\7\pwsh.exe`，
   没有覆盖 Store/MSIX 与 per-user 安装位置，建议把那些位置也纳入候选。