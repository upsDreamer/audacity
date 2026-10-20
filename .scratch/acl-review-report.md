# DSH 工作区权限故障 · 独立复核报告

> 对 `.scratch/acl-review-brief.md` 所述任务的执行产出。
> 复核执行:2026-10-20,Claude Code 会话(非 DSH 会话)。
> 约束遵守:全程只读诊断,未修改任何真实目录权限,未执行修复;探针目录(`.acl-probe-4264`、`.acl-probe2-4316`、`.acl-probe3`)测后即删,已验证无残留;未提权修改任何对象。

**执行者环境声明**:本会话 Git Bash 实测运行于**提权令牌**(`Mandatory Label\High`,Administrators 组 Enabled,SeDebug/SeBackup Enabled),与简报要求的"普通用户上下文"不符——所有依赖令牌语义的实验结论均已据此打折标注。P1(DSH 侧复现)按简报要求跳过并如实标注。

---

## 1. 结论表

| 问题 | 结论 | 置信度 |
|---|---|---|
| Q1 复现与范围 | 无法直接复现(非 DSH 会话)。间接证据指向"**该工作区首次授权物化失败**"而非全局故障:C: 盘默认工作区**同日授权成功**,D: 盘 audacity 工作区无任何授权痕迹;故障只应影响 shell 工具(授权门是 spawn,read/grep/glob 不经过) | 中 |
| Q2 根因 | D: 链对用户只有 `Authenticated Users:(M)`(无任何用户显式 F);`grantWrite` 需要 WRITE_DAC,在**受限令牌**(restricting 列表不含用户 SID)下属主隐含 WRITE_DAC 不生效 → `SetNamedSecurityInfoW` 返回 Win32 5。机制成立,但"DSH 在本机以何种令牌执行授权"这一环**证据不足** | 中 |
| Q3 C1/C2/C3 | 均成立(代码级证实,见 §3) | 高 |
| Q3 C5 | 主假设机制**成立**;但"调用方并非属主"与"显式拒绝项/包 SID 污染"两个次假设被**反证**;C: 工作区成功案例使主假设不足以单独解释全部观测 | 中 |
| Q4 修复方案 | 设计质量高(边界/备份/预检/回滚均到位),**在本机大概率对症但非无条件**:若授权执行者确为受限令牌,补 `upstr:(F)` 救不了 pass-2 检查 | 中 |

## 2. 关键证据(命令 + 原始输出 + 路径)

### E1 — D: 链 ACL 形态:只有 Modify,无拒绝项,无包 SID,无 OWNER RIGHTS,无完整性标签

```
icacls 'D:\'; icacls 'D:\Work'; icacls 'D:\Work\project'; icacls 'D:\Work\project\audacity'
→ D:\  Authenticated Users:(M) / (OI)(CI)(IO)(M)   ← 唯一给用户的写权限来源
     Administrators:(F) / SYSTEM:(F) / Users:(RX,GE)
→ D:\Work、D:\Work\project、D:\Work\project\audacity
     全部为 (I) 继承副本,零显式 ACE,零 DENY,零 S-1-15-2-*,零 S-1-3-4(OWNER RIGHTS),零 S-1-16(完整性标签)
```

### E2 — 属主:audacity 属于用户本人(否决"调用方非属主"次假设)

```
cmd /c dir /q /a D:\Work\project
→ 2026/10/20 12:24 <DIR> LAPTOP-MHVJ999R\upstr  audacity
→ 同目录下 Administrators 属主与 upstr 属主混杂(LLM_WIKI、audio_sdk 等为 Administrators)
```

### E3 — 决定性对照:C: 默认工作区授权物化成功,且链上用户是显式 FullControl

```
icacls C:\Users\upstr\Documents\deepseek-harness\default-workspace
→ Everyone:(CI)(DENY)(DC)                          ← grantWrite 的 FILE_DELETE_CHILD 拒绝项
→ S-1-4-517383209-940346346:(OI)(CI)(W,D,DC)       ← 确定性工作区 SID 允许项
→ Mandatory Label\Low Mandatory Level:(OI)(CI)(NW) ← Low 标签
→ LAPTOP-MHVJ999R\upstr:(I)(OI)(CI)(F)             ← C: 链继承给用户 (F)!
```

三件套 = `grantWrite` 文档宣称的三编辑,分毫不差。而 D: 链用户只有 M,且 audacity 上**没有**这三件套 → 首次物化从未成功。

### E4 — 机制确认(app.asar 内嵌文档与代码,字节偏移 ~90.8M)

安装包:`D:\Program Files (x86)\DSH\resources\app.asar`(182,794,869 字节)

- `grantWrite` = `GetNamedSecurityInfoW`(读 DACL+标签)→ `SetEntriesInAclW`(合并)→ `SetNamedSecurityInfoW`(**一次调用写 DACL + Low 标签**),失败抛 `Win32Error`(API 名 + 错误码 + 路径)——与观测错误文本 `SetNamedSecurityInfoW failed (Win32 5): grantWrite(D:\Work\project\audacity)` 完全对应
- 授权在 `AclSandbox.init()` 内、`createRestrictedToken` **之前**,由 seam/宿主进程自己的令牌执行;`manageDacls:false` 时 runner 不授权——**授权与用什么 shell 无关,C2 成立**
- `CreateRestrictedToken` flags=13 = `DISABLE_MAX_PRIVILEGE | LUA_TOKEN | WRITE_RESTRICTED`;**restricting 列表 = [logon SID, Everyone, workspace SID, temp SID],不含用户 SID** → WRITE_RESTRICTED 的 pass-2 只看 restricting 列表,属主隐含 WRITE_DAC 所依赖的用户 SID 被忽略 → 即使属主本人也 ACCESS_DENIED。**C5 机制逐字成立**
- 前置拒绝规则("工作区等于/包含临时根则授权前拒绝")存在,但本机 `TMP=C:\Users\upstr\AppData\Local\Temp` 不在工作区内,未命中 → 根因不是它
- 并发控制:每次授权在 per-path `LockFileEx` 锁下做读-合-写

### E5 — 干扰因素全部排除

```
cmd /c dir /al 'D:\Work\project'  → File Not Found(链上无 junction/符号链接)
fsutil reparsepoint query 'D:\Work\project\audacity' → Error 4390(非重解析点)
cmd /c attrib 'D:\Work\project\audacity' → 无 R/S 属性
Get-PSDrive D / Get-Volume → Fixed、NTFS、卷标 Data(非 exFAT/FAT32、非映射盘)
Get-MpPreference → EnableControlledFolderAccess: 0(受控文件夹访问关闭)
Get-WinEvent Defender 1123/1124 → 无记录
Get-CimInstance SecurityCenter2 → 仅 Windows Defender
```

### E6 — 实验(均带局限性标注)

- **P6 探针**(自建 `/d/Work/project/.acl-probe-4264`,测后即删):提权令牌下 `/grant:F` 成功——只能证明"卷/父目录未一刀切禁止权限写入",**不能**外推到 DSH 的授权路径(简报已预警,如实标注)
- **SAFER 降权实验**(`runas /trustlevel:0x20000`,探针 `.acl-probe3`,测后即删):对本人属主目录 `/grant` 与 `/setintegritylevel L` 均成功 → ①设置强制标签**不需要** SeSecurityPrivilege(标签理论排除);②SAFER 令牌 ≠ UAC 过滤令牌——对 Administrators 属主目录授权也成功了,不合过滤令牌预期,该实验**不能**模拟 DSH 场景
- **explorer 转启真 UAC 过滤令牌实验**:未产出(启动被拦截),**该对照缺失**

### E7 — DSH 侧日志检索结果

`~/.dsh/` 无 log 文件;`%APPDATA%\@deepseek-ai\dsh-desktop\logs` 为空目录;`~/.dsh/sessions/--D-Work-project-audacity--/` 下有 4 个会话目录(2026-10-20 15:44–15:55)但 grep `grantWrite|SetNamedSecurityInfoW|SANDBOX` 无命中——失败堆栈未能获取。`~/.dsh/storages/workspace.json` 证实两个工作区:`D:\Work\project\audacity` 与 `C:\Users\upstr\Documents\deepseek-harness\default-workspace`(默认)。

## 3. C1–C5 逐条比对

| 条目 | 判定 | 依据 |
|---|---|---|
| C1 故障点在工作区授权,grantWrite 读-合-写回被拒 | **成立** | E4 代码 + E1/E3(D: 无痕迹、C: 有痕迹) |
| C2 与 shell 类型无关 | **成立** | 授权在 spawn 之前(init 内、创建受限令牌之前);错误先于任何命令执行 |
| C3 win32 走 ACL 受限令牌,类 Unix 才 bwrap/Landlock/Seatbelt | **成立** | asar 含 `@deepseek-ai/dsh-sandbox-windows-acl`(koffi 绑定 advapi32、`CreateRestrictedToken`);seam 文档并列 bwrap/landlock-run/sandbox-exec |
| C5 主假设:仅 Modify + 受限令牌下属主隐含权不生效 | **机制成立;触发环节证据不足** | M 不含 WDAC ✓(E1);restricting 列表无用户 SID → pass-2 杀掉属主隐含权 ✓(E4 代码)。但 C: 工作区同日成功(E3)说明并非无条件失败——C: 链显式 `upstr:(F)`(含 WDAC)可能是它能通过检查的原因,也可能是首次物化时的调用上下文不同,现有证据分不出 |
| C5 次假设 a:显式拒绝项 | **反证** | E1:全链零 DENY |
| C5 次假设 b:其它应用包 SID 污染 | **反证** | E1:全链零 `S-1-15-2-*`(E3 的 C: SID 是 `S-1-4-*`,属 DSH 自有授权痕迹,非污染) |
| C5 次假设 c:调用方非属主 | **反证** | E2:属主就是 upstr |

## 4. 未排除的可能性(及所需验证)

1. **DSH harness 在本机执行授权时的实际令牌类型**——链条上唯一没钉死的环。验证:DSH 运行中时由提权 shell 查询其进程令牌(Mandatory Label / Groups),或在 DSH 会话里跑修复脚本,看其 `CALLER SID=... INTEGRITY=...` 自报输出(脚本自带此探针)
2. **范围问题**(Q1):换同卷另一目录作工作区能否复现——需在 DSH 内操作(简报 P1 的判别实验),本复核无法执行
3. **C: 工作区授权成功的发生时刻与上下文**(是否提权实例、是否安装期完成)
4. **DSH 侧日志**:未找到——失败堆栈未能获取

## 5. 对修复方案(C4)的审计与建议

脚本位置:`C:\Users\upstr\AppData\Local\Temp\dsh-acl-skill-eQMFoX\scripts\diagnose-windows-sandbox-acl.ps1`(799 行,复核时仍在,未按简报预期被清理)。逐项:

1. **边界**:可靠。`-AllowRoot` + `Test-DangerousRoot`(盘根 / 用户目录根 / Windows / WindowsApps / Packages / 重解析点全部拒绝),授权目标限于链上目录;`WRITE_DAC` 预检发生在**任何修改之前**(全部 grantTargets 先检后改)——不存在"改一半才发现没权限"的窗口
2. **备份/回滚**:备份只存自有 ACE + 保护位(代码注释解释了为何不含继承项:继承项跟随父目录,回存会强制写回且父目录重推导后失配),恢复脚本独立落盘于 `-Out`,恢复后按"自有 ACE 集合 + 保护位"验证——回滚语义诚实。残留风险:脚本进程被强杀则需手工执行 `-Out` 里的 ROLLBACK 命令
3. **删 `S-1-15-2-*`**:正则 `^S-1-15-2-(?![12]$)` 正确排除通用组;DSH 自有工作区 SID 是 `S-1-4-*` 前缀,**不会被误删**(E3 实证)。已知副作用真实(该包对树失去访问,依赖该授权的低完整性子进程可能变不可读),但**本机 D: 链无任何包 ACE,此步骤为空操作,无害**
4. **自动回滚**:异常 → finally 反序恢复 attempted 项,验证失败即停——覆盖良好
5. **对症性**:若授权执行者是**普通 Medium 令牌**——正对症(补的 `FullControl` 含 WDAC/WO;ACE 无继承标志、只落目录本身,改动最小)。若执行者是**受限令牌**——**不对症**(pass-2 的 restricting 列表不含 upstr,补 `upstr:(F)` 无用),脚本会诚实报 `REPAIR_REFUSED needs WRITE_DAC` 然后原样停下
6. **更小替代**:`icacls D:\Work\project\audacity /grant "upstr:(OI)(CI)F"`(提权上下文,可先备份 SDDL、可 `/restore` 回滚)即可验证根因并修复,不碰包 SID、不做 2000 目录子树扫描

**建议:改条件后再跑。** 条件:

- (a) 允许提权或确认调用令牌为普通 Medium(非提权空跑预期 `REFUSED`);
- (b) `-AllowRoot` 精确设为 `D:\Work\project\audacity`,`-Out` 指到仓库外;
- (c) 跑完后**立即用原操作验证**(在 DSH 会话里再执行一次 shell 命令)——脚本自己声明"只有重跑原受限操作才能确认治愈"。

**不建议现在直接跑的理由**:先做 §4-1 的令牌鉴别;若确为受限令牌,脚本与 icacls 替代都会无效,应改向 DSH 侧反馈(按代码设计,授权本应在受限化之前完成——若观测与之矛盾,值得作为 bug 报给 DSH)。

## 6. 与原诊断的分歧声明

不同意 C5 完整表述之处仅一处:它把"仅 Modify + 受限令牌"当作充分根因,但 C: 工作区同日在同样仅靠属主身份的起点上授权成功了——"仅 Modify"不构成充分条件,失败还需要"授权执行于受限/异常令牌"这一附加环节。该环节在哪个进程、为何只发生在 D: 工作区,是原诊断和本复核都未证明的部分。机制本身(受限列表不含用户 SID → 属主隐含 WRITE_DAC 失效)经代码证实,与原诊断一致。

---

# 附录(2026-10-20 追加):最小授权试验交付 + 供给任务

## A1. 最小授权试验(`acl-review-followup.md`)——A 分支坐实,缺陷 1 治愈

**前置**:
- 基线复测(用户执行,2026-10-20 18:07):仍失败,错误文本与 15:48 首次观测逐字一致,故障点无漂移;DSH 保持运行未重启
- 回滚链路演练:发现 `icacls /restore` 的**第一参数是父目录**(`ICACLS directory /restore aclfile`,备份内是相对名,解析到该目录之下)。演练两次失败样本 → 修正后验证通过(save → grant F → restore → F 消失)
- **回滚命令(正确形态,已演练验证)**:`icacls.exe 'D:\Work\project' /restore 'D:\Work\project\audacity.acl.bak'`
- 另:Git Bash 给 icacls 传 `/d/...` 形式只会打印帮助,必须 `cygpath -w` 转换(比简报 §0 预警更具体的形态)

**备份**(改动前 18:01:53):
- `D:\Work\project\audacity.acl.bak`(340 字节,内含相对名 `audacity`)
- SDDL:`O:S-1-5-21-453050060-3210176897-2709093357-1001G:...-1001D:(A;ID;FA;;;BA)...` —— 再次实锤属主 = upstr 本人

**改动**(18:08:17,仅一对象):`icacls.exe 'D:\Work\project\audacity' /grant 'upstr:(F)'`
- 回读:仅新增 `LAPTOP-MHVJ999R\upstr:(F)`(无继承标志),其余 8 条 ACE 与基线逐字一致
- 备注:DACL 头部 `D:` → `D:AI`(icacls 重写描述符的自动继承标记,无访问语义)

**判定:A 分支(治愈)**。DSH 侧普通 workspace-write 模式命令成功;工作区根出现 DSH 授权完整三件套(本报告 §2-E3 所述形态):
```
Everyone:(CI)(DENY)(DC) + S-1-4-101348277-349246831:(OI)(CI)(W,D,DC) + Mandatory Label\Low Mandatory Level:(OI)(CI)(NW)
```
该允许项**保留**(与修复脚本步骤 (a) 等价)。§5 令牌鉴别不再需要。

**根因表述修正**(相对本报告 §1-Q2):缺口精确说是 **WRITE_OWNER**——`grantWrite` 一次 `SetNamedSecurityInfoW` 同时写 DACL 与标签,要求 WRITE_DAC **和** WRITE_OWNER;属主隐含权只有 READ_CONTROL + WRITE_DAC(不含 WO),Modify 两者皆无,故"用户自有 + 仅 Modify"的目录首次授权必败。C: 默认工作区幸免是因为链上有显式 `(F)`(含 WO)。C5 的"受限令牌 pass-2"机制在本案中未被触发(授权者为普通非提权进程),作为机制性备注保留。`.scratch/dsh-bug-report.md` 缺陷 1 的表述与此一致。

## A2. 供给任务(`followup-2-provisioning.md`)执行记录

**任务 B(完成)**:父目录可继承授权,防 D: 盘新工作区复发
- 备份:`D:\Work\_acl-backups\project-acl-20261020-183636.bak`(338 字节)+ `project-sddl-20261020-183636.txt`
- 改动(18:36):`icacls.exe 'D:\Work\project' /grant 'upstr:(OI)(CI)(F)'`
- 传播耗时 **75 秒**,icacls 汇总失败 0(注:自动继承传播的子对象失败不体现在该汇总中,已抽查补证)
- 抽查:`audacity\au3`、`audacity\docs`、`audacity\.git`、`audacity\study_audacity\benchmarks`、兄弟目录 `LLM_WIKI`(Administrators 属主)全部带继承 `upstr:(I)(OI)(CI)(F)`;新建目录继承探针 ✓
- 回滚(**未执行**):`icacls.exe 'D:\Work' /restore 'D:\Work\_acl-backups\project-acl-20261020-183636.bak'`(首参=父目录 `D:\Work`);子树中的继承项随父目录恢复自动消失,无需单独处理

**任务 A(完成)**:安装 PowerShell 7 到 `C:\Program Files\PowerShell\7`
- winget 1.29.380:找到 7.6.7、安装包哈希验证通过,但 **MSI 安装器报 0x80070005(拒绝访问)** ——winget 自身调用路径的问题
- 改直装官方 MSI(v7.6.7 release):
  - 下载两次中断(`curl 18`,Content-Length 117,514,240 字节),断点续传补完
  - 哈希校验:`hashes.sha256` 为 **UTF-16LE** 编码,ASCII grep 匹配不到(坑:半截文件的哈希 `37dc...` 曾被误当官方值);转码后取到真官方哈希,本地 SHA256 = `ec575cef9ec56fcd905de0d5eeac9cb7f81a0a65abb87bb5cd0e87fdfe2b8cc0` **严格匹配**
  - `msiexec /i /quiet /norestart /l* ADD_PATH=1 ENABLE_PSREMOTING=0 REGISTER_MANIFEST=1` → exit 0,耗时 14 秒(与 winget 路径的 0x80070005 形成对照,问题定位在 winget 一侧)
- 验证:`pwsh.exe -NoProfile` 输出 **7.6.7 + Core** ✓;安装包临时目录已清理
- 待 DSH 侧生效:重启 DSH 或显式 `pwshPath = C:\Program Files\PowerShell\7\pwsh.exe`,然后 `$PSVersionTable` 应显示 7.6.7

**任务 C(未执行)**:按文档要求先征求用户决定;执行者建议先不做(理由见交付对话:普通工具经任务 B 已全树可写、标签回滚不干净、工作区 SID 是一次性死数据、上游修才是正解)。

## A3. 遗留清单

1. PS7 装完后:DSH 侧 `$PSVersionTable` 复核(需重启 DSH 或显式 `pwshPath`)
2. 缺陷 2 处置决策(任务 C 跑/不跑)
3. 备份保留:`audacity.acl.bak`、`project-acl-20261020-183636.bak`(+SDDL 文本)——确认稳定后可清理
4. `.scratch/dsh-bug-report.md` 待发给 DSH 方面
