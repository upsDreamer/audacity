# 03 音频链路内存与 CPU 开销评估

> 研究文档。基线 master `be46fba74`。配套：[02-audio-pipeline.md](./02-audio-pipeline.md)（链路结构）、`../benchmarks/`（实测代码）。
>
> **置信度标注**：✅实测 = 本工程微基准测得（AMD Ryzen 7 5800H，MSVC 19.42 /O2 x64，单线程）；📐推算 = 静态分析推算（含出处）；⚠️未测 = 需第二批基准（见文末待裁决项）。数字量级以 44.1kHz 为基准。

---

## 1. 实测基准结果（第一批 + 第二批，全部真实 au3 源码）

全部先过正确性验证（往返误差、规范响度、压缩生效性等 20 项 PASS），下表为稳态吞吐：

| 基准 | 场景 | 结果 | 说明 |
|---|---|---|---|
| SampleFormat | float→float 纯拷贝 | ✅0.111 ns/样本 | memcpy 下限参照 |
| SampleFormat | int16→float32 | ✅0.375 ns/样本 | 播放读出路径 |
| SampleFormat | int24→float32 | ✅0.286 ns/样本 | |
| SampleFormat | float→int16（无抖动） | ✅2.63 ns/样本 | 含逐样本 clip 检查 |
| SampleFormat | float→int16 + rectangle/triangle | ✅9.87 / 9.91 ns/样本 | rand() 是主要成本 |
| SampleFormat | float→int16 + shaped | ✅18.8 ns/样本 | **3×rand + 5 抽头 FIR 误差反馈** |
| SampleFormat | int24→int16 + shaped | ✅18.8 ns/样本 | 导出窄化路径 |
| SampleFormat | int16→int24 | ✅0.55 ns/样本 | `<<8` 快路径，无抖动 |
| Biquad | 单节（时域 IIR，double 状态） | ✅4.22 ns/样本 | 串行依赖链限制 ILP |
| Biquad | 4 节级联（8 阶 Butterworth） | ✅17.4 ns/样本 | ≈4.35 ns/节，线性叠加 |
| RealFFTf | 正变换 4096 点 | ✅4.27 ns/样本 | O(N log N)，表驱动，就地 |
| RealFFTf | 逆变换路径（打包+逆+重排） | ✅4.72 ns/样本 | |
| EBUR128 | 立体声逐样本测量 | ✅17.2 ns/样本 | 每声道 2×Biquad(double) + 块能量 |
| EBUR128 | 单声道参照 | ✅12.2 ns/样本 | |
| **soxr** | 恒率 44.1↔48，QQ→VHQ 四档 | ✅2.5–7.1 ns/样本 | Fast/Best 钥匙默认均为 VHQ(3) |
| **soxr** | **变速率模式（HQ\|VR，块间变 factor）** | ✅**73.7 ns/样本** | **恒率的 13–29 倍**；1024 块协议同 MixerSource |
| **pffft** | 功率谱 1024/2048/4096 | ✅1.07–1.31 ns/样本 | SIMD；对照 RealFFTf 路径 |
| **pffft vs RealFFTf** | 同任务对照 | ✅快 ≈3.3 倍 | RealFFTf 路径 3.9–4.4 ns/样本 |
| **DRC 压缩器** | 512 块 × 立体声 | ✅22.6 ns/样本 | 峰值检测+软拐点+前瞻 3ms |
| **DRC 限制器** | 同上 | ✅22.9 ns/样本 | ratio=∞、前瞻 1ms |
| **TimeAndPitch** | 1.25x/2x 变速，1024 块立体声 | ✅61.5–62.1 ns/样本 | FFT 4096 / overlap 4；共振峰保持几乎无增量 |
| **TimeAndPitch** | 1.0x + 200 音分变调 | ✅73.2 ns/样本 | |
| **Sqlite 块写**（等价复刻） | 1MB 块 × 64，WAL+NORMAL | ✅4.33 ns/样本（923 MB/s） | 含 summary 计算；临时模式 3.33 ns |
| **Sqlite 块读**（等价复刻） | 顺序 SELECT + memcpy | ✅2.65 ns/样本（≈1.5 GB/s） | 读回逐位一致 |

关键比值：**soxr 变速率模式与 TimeAndPitch（变速 clip 回放）是链路中仅有的两个"数量级"级单项**（60–75 ns）；**shaped 抖动是无抖动转换的 7 倍**（18.8 vs 2.6）；pffft 相对手写 FFT 快 3.3 倍。

---

## 2. 基础常量（静态确认）

| 常量 | 值 | 出处 |
|---|---|---|
| Sequence 磁盘块 | 1MB；float 轨 128K–256K 样本/块 | `Sequence.cpp:46,56-57` |
| 播放 RingBuffer | float，容量 ≈0.25s×rate（≥2×硬件延迟） | `AudioIO.cpp:1325,1340-1348` |
| 播放填充批量 / 队列下限 | 各 0.05s×rate | `PlaybackSchedule.cpp:33-54`；`AudioIO.cpp:1319,1397` |
| 音频线程轮询 | 10ms | `PlaybackSchedule.cpp:83-87` |
| 捕获 RingBuffer | (4.5+0.5·min(16,ch))s × rate，int16 或 float | `AudioIO.cpp:1327,1472-1493` |
| 捕获消费批量 | 0.2s | `AudioIO.cpp:1329` |
| MixerSource 重采样队列 | 65536 float/声道（256KB）；子块 1024 | `MixerSource.cpp`（sQueueMaxLen/sProcessLen） |
| MixerSource 包络值缓冲 | max(65536, bufferSize) 个 double | `MixerSource.cpp:139-141,309` |
| 实时效果块 | 512 样本（内置效果普遍 SetBlockSize(512)） | `Mix.cpp:49-82` |
| EffectStage 中间缓冲 | 每效果级 3×blockSize float | `Mix.cpp:428-430` |
| SQLite 页 / 模式 | 64KB；WAL + synchronous=NORMAL + 自动检查点关闭（自有线程） | `DBConnection.cpp:27-42` |
| 块 summary 元数据 | ≈块大小 2.9%（两级 min/max/RMS） | `SqliteSampleBlock.cpp:88-89,864-874` |

---

## 3. 播放链逐环节 CPU 开销

以"立体声 float 项目、无重采样、无效果"为基线，逐环节每样本成本（生产侧 + 回调侧合计）：

| # | 环节 | 成本/样本 | 依据 |
|---|---|---|---|
| 1 | 轨道读出（块→float） | ✅0.11–0.38（float/int16 存储） | 实测 SamplesToFloats 族 |
| 2 | 包络乘法 | 📐~0.5（线性域增量法） | `Envelope.cpp:1126-1131` 每样本 1 FMA；dB 域每样本两次 pow(10,·)（最贵分支，:1120-1129） |
| 3 | soxr 重采样（仅率不符时） | ✅恒率 5–7（默认 VHQ 档）；**变速率/变光标 73.7** | 实测 §1；变速 clip 与 scrub 用的 VR 模式每块 `soxr_set_io_ratio` |
| 4 | Mixer 输出拷贝（float→float） | ✅~0.11 | 实测 memcpy 下限 |
| 5 | copy_n 进 mProcessingBuffers | 📐~0.1 | `AudioIO.cpp:2148-2166` |
| 6 | 实时效果（每效果） | ✅典型 4–8（BassTreble 级 ~12 FLOP；压缩器 10–15 运算+10^x） | 实测 Biquad 外推 + 📐算法结构 |
| 7 | 音量乘 + master 累加 | 📐~0.5 | `AudioIO.cpp:2286-2291` |
| 8 | RingBuffer Put/Get（各一次 float→float） | 📐~0.2–0.5 | `RingBuffer.cpp:94,250` |
| 9 | 回调交错写入 + 渐变 + 限幅 | 📐~0.5–1 | `AudioIO.cpp:2904-2926` |
| 10 | 电平表旁路（per-track ring 的 Get，每回调） | 📐~0.3/声道 | `AudioIO.cpp:3268-3285` |

**基线合计：≈2–5 ns/样本**（生产+消费全程）→ 60 秒双声道音频约 **16–26 ms 单核时间（<0.05% 核心）**。播放链的常数开销设计是健康的：float 直通、无 dither（低质默认 none）、批量 50ms 摊薄轮询成本。

重采样激活时成本由 #3 主导：**恒率失配仅 +5–7 ns/样本（可忽略）**；真正的坑是**变速率模式（VR）——73.7 ns/样本**，即变速 clip（StretchingSequence→TimeAndPitch 路径本身又是 61–73 ns）叠加变率重采样的场景，单是这两项就能吃掉一个核心约 5–8% 的实时预算（44.1kHz 立体声按每样本计：62+74 ns × 88200 样本/s ≈ 12% 单核）。

## 4. 录制链开销

- 回调侧：解交织 + int16 clamp（📐~0.5/样本）+ Put + 每回调 Flush（`AudioIO.cpp:3057-3099`）。
- 音频线程（每 0.2s 一次）：Get → 设备率→项目率 soxr（✅恒率 5–7 ns，Best 钥匙默认 VHQ 档）→ 通道混合 → `Append`。
- **落盘路径**：AppendBuffer（内存，256K 样本）→ 达理想块长 DoAppend → CalcSummary（📐O(n) 两遍，~1–2 ns/样本）→ 单条 INSERT 写 1MB BLOB（✅顺序写吞吐受 SQLite/WAL 主导；无压缩，WAL 期磁盘瞬时占用 ≈2×）。
- 录制抖动：仅当 stored 格式收窄时高质 shaped（📐18.8）；默认录制到 float/同格式不抖动。

## 5. 离线渲染链开销

- **MixAndRender / 导出**：MixerSource 读出+重采样（同播放）→ EffectStage 逐效果（3×blockSize 缓冲）→ **最终格式转换带 dither**：导出高质量=shaped ✅18.8 ns/样本——1 小时双声道导出的纯抖动成本 ≈ 60×44100×2×18.8ns ≈ **6.0 秒**（相对编码时间通常可忽略，但可测量）。
- **效果应用**：`EffectOutputTracks` 先整轨 Duplicate（📐内存 ≈ 选区磁盘量级，进程内存瞬时翻倍）→ 块大小 = min(maxBlockSize×2, 效果实例块) → 逐块 ProcessBlock。STFT 类（降噪/滤波曲线 EQ）每窗 2 次 RealFFTf ✅8.5 ns/样本 + 每窗判定/平滑。
- **两遍效果**（Normalize/Loudness/降噪画像）读两遍音频：分析遍成本 = ✅EBUR128 17.2 ns/样本（Loudness）或 📐~1 ns（峰值扫描）。

## 6. 内存占用模型

### 6.1 项目数据（1 分钟 44.1kHz 立体声 float）

| 项 | 占用 | 说明 |
|---|---|---|
| 磁盘样本 | 📐20.2 MiB | 60×44100×2ch×4B，无压缩 |
| 磁盘 summary | 📐+2.9%（≈0.6 MiB） | 两级 min/max/RMS |
| WAL 瞬时 | 📐再×1（写期间） | checkpoint 线程异步回收 |
| **打开项目常驻内存** | **📐≈0（块不驻留）** | 样本在 SQLite；按需读块，float 视图 weak_ptr 缓存仅覆盖最近访问块 |
| 静音优化 | 不占盘 | 静音块负 id 全局共享（`SqliteSampleBlock`） |

这是"内存利用效率"的核心结论：**波形数据不常驻内存，内存占用与项目时长解耦**——代价是播放读取依赖 SQLite 块读 + 1MB 粒度，冷启动第一声前有预填充（250ms ring）掩护。

### 6.2 播放常驻内存（44.1kHz，per 实例）

| 项 | 占用 | 说明 |
|---|---|---|
| master RingBuffer ×2 | 📐88 KB | 0.25s×44100×4B×2ch |
| per-track 电平表 RingBuffer | 📐88 KB/轨 | 与 master 同容量，仅喂表 |
| processing/master/scratch 缓冲 | 📐~0.5 MB 合计 | reserve 0.25s；scratch=2×ch+1 份 |
| MixerSource 队列（重采样时） | 📐256 KB/声道/源 | 65536 float |
| **包络值缓冲** | 📐**512 KB/源** | max(65536, bufferSize) 个 double——**无条件预分配**，无包络轨也占 |
| EBUR128 直方图（分析效果） | 📐256 KB/实例 | 65536×long |
| 效果延迟线 | 📐~1–2 KB | 如压缩器 3ms×2ch |

典型单轨播放 ≈ **1–1.5 MB**；十轨工程 ≈ **8–12 MB**（重采样全开的最坏情形）。对现代机器均可忽略；比例上最大的是包络缓冲与电平表环。

### 6.3 录制常驻内存

捕获环 📐5.5s×44100×4B×2ch ≈ **1.9 MB**（float 捕获；int16 减半）+ AppendBuffer 1 MB/声道 + SQLite 写缓冲。`bad_alloc` 时四类缓冲整体减半重试（`AudioIO.cpp:1500-1519`）——降级路径存在。

## 7. CPU 热点排序（链路视角，全部实测）

1. **soxr 变速率模式（HQ|VR）** ✅73.7 ns——变速 clip、时间包络播放、scrub；每块 `soxr_set_io_ratio` + 滤波状态重置。**恒率重采样并不贵（5–7），"重采样贵"的直觉只对 VR 成立。**
2. **TimeAndPitch（变速/变调 clip 回放内核）** ✅61.5–73.2 ns——与 #1 叠加时是播放链绝对主导。
3. **DRC 压缩器/限制器** ✅22.6–22.9 ns——实时挂载的动态效果单件成本。
4. **shaped 抖动** ✅18.8 ns——导出/格式收窄必经；rand() 是主因（无快速 RNG、全局状态串行化）。
5. **EBUR128 类双精度加权分析** ✅17.2 ns——Loudness 效果分析遍。
6. **soxr 恒率** ✅5–7 ns——轨道率≠流率时的播放/录制路径。
7. **STFT 效果族** ✅~8.5（每窗 2×RealFFTf）+ 📐每窗判定/平滑——降噪、谱编辑；**换 pffft 后变换部分可降到 ~2.6**（✅1.3×2，仓库已有就绪的 PowerSpectrumGetter 但主线未接线）。
8. **Biquad IIR 族** ✅4.2/节——EQ/加权/调制效果内核。
9. **格式转换与缓冲搬运** ✅0.1–2.6——链路中 6 处 float→float 拷贝，合计 <2 ns。
10. **每块固定开销**（小样本量时放大）：EffectStage 每次 Acquire 构造两个临时 vector（`EffectStage.cpp:324-345`）；RealtimeEffectState 每调用 weak_ptr lock（`RealtimeEffectState.cpp:621`）+ 停用效果仍逐 512 块穿透（:642-671）；音频线程 10ms 轮询空转。

## 8. 发现与改进建议（只记录，不改码）

| # | 现状 | 影响 | 可选改进 |
|---|---|---|---|
| 1 | shaped 抖动用全局 `rand()` + 全局状态、每次 Apply 前 Reset（`Dither.cpp:59-63,309-311`） | 18.8 ns/样本中 rand 占大头；非线程安全；多通道转换间状态互相污染 | 线程局部 fast RNG（xorshift）；仅格式/算法变化时 Reset |
| 2 | `MixerSource` 包络值缓冲无条件预分配 65536 double（512KB/源） | 十轨工程白占 5MB；缓存不友好 | 惰性分配/缩小到 bufferSize |
| 3 | per-track 电平表独立 RingBuffer（与 master 同容量）+ 每回调额外 Get | 内存 ×2/轨；每回调多一轮拷贝 | 表数据直接从 mProcessingBuffers 采样（低频抽取） |
| 4 | `Sequence::GetMinMaxRMS` 部分区间不走 summary（源码 TODO，`SqliteSampleBlock.cpp`） | 局部重绘拉整块 1MB float 并重算 | 聚合 summary256 帧即可 |
| 5 | EffectStage 每 Acquire 构造临时 vector（离线路径热循环） | 块小（512）时分配开销占比上升 | 缓冲提升为 stage 成员 |
| 6 | 停用实时效果仍逐 512 块 `RealtimePassThrough` + 每调用 weak_ptr lock | 效果链越长，空转成本线性涨 | 禁用链短路为 memcpy 旁路 |
| 7 | 回调内 `alloca(framesPerBuffer)`/声道 | framesPerBuffer 未指定，后端给大值时有栈溢出风险 | 建议上限检查或用预分配临时缓冲 |
| 8 | 采样无压缩（BLOB 原始字节）+ WAL | 磁盘占用线性、写期间瞬时 ×2 | 可选块级压缩（如 FLAC/WavPack 帧级）换 CPU；云同步模块已有 WavPackCompressor 先例 |
| 9 | dB 域包络每样本两次 `pow(10,·)`（`Envelope.cpp:1120-1129`） | dB 包络轨读取路径成本抬升一个台阶 | 增量法在指数域同样适用（乘法代替 pow） |

## 9. 基准覆盖状态（全部裁决完毕）

**已完成**（三轮，全部真实代码或经用户裁决的等价复刻）：
- 第一批：SampleFormat/Dither、Biquad、EBUR128、RealFFTf（真实代码零依赖）
- 第二批（裁决：拉真实 soxr/pffft）：soxr 重采样、pffft 功率谱对照、DRC 压缩器、TimeAndPitch
- 第三批（裁决：Envelope 用 Python 绘图呈现、SqliteSampleBlock 复刻等价写路径）：
  - **SqliteSampleBlock 写/读路径**（等价复刻：真实 sqlite3 3.45 + 相同 PRAGMA/表结构/summary 布局）：✅写 923 MB/s（4.33 ns/样本，WAL+NORMAL 已保存模式）；临时/未保存模式（journal OFF）1201 MB/s（3.33 ns）；✅读 2.65 ns/样本（≈1.5 GB/s）；✅写期间 WAL 使磁盘瞬时占用 ≈1.26×；✅读回逐位一致（无压缩直存证实）
  - **Envelope 数据可视化**（非基准）：`benchmarks/plot_envelope.py` 复刻 `GetValues` 插值语义 → [img/envelope_interpolation.png](../docs/img/envelope_interpolation.png)（线性域 vs dB 域曲线与差异）+ `benchmarks/output/envelope_values.csv`（2406 行导出）；量化：三种演示包络的域间最大差异 0.006–0.023 增益
