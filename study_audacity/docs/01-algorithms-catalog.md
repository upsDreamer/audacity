# 01 音频处理算法目录

> 研究文档（非官方文档）。范围与切分约定见 `.scratch/study-audio-docs/spec.md`。
> 基线：master `be46fba74`（2026-10-20）。引用的外部材料不再重写：`docs/effect-view-architecture.md`（新架构效果视图）、`au3/libraries/au3-stretching-sequence/readme.md`（拉伸序列设计）、`au3/src/effects/How to make an effect stateless.txt`（效果无状态化指南）。

**格局**：本仓库是 Audacity 4——Qt/QML 新架构（`src/`）+ 完整嵌入的 Audacity 3 核心（`au3/`，约 70 个 `libraries/au3-*` 静态库）。**全部音频算法本体都在 `au3/`**；`src/` 通过 `src/au3wrap` 桥接，只承担编排/视图职责。本目录只写 `au3/` 侧算法；`src/` 的挂接方式见 [02-audio-pipeline.md](./02-audio-pipeline.md)。

---

## 0. 效果框架与注册机制

**分类体系**（`au3/libraries/au3-components/EffectInterface.h`）：
- `EffectType`：None / Hidden / Generate / Process / Analyze / Tool
- `EffectGroup`：Unspecified / None / VolumeAndCompression / Fading / PitchAndTempo / EqAndFilters / NoiseRemovalAndRepair / DelayAndReverb / DistortionAndModulation / Special / SpectralTools / Legacy —— 本目录的章节划分即沿用此官方分组。

**注册**：`BuiltinEffectsModule::Registration<T>` 静态对象在加载时把条目追加进全局注册表（`au3/libraries/au3-effects/LoadEffects.h/.cpp`），按符号字符串查表。AU4 侧新架构效果在 `src/effects/builtin_collection/internal/builtincollectionloader.cpp` 的 `preInit()` 注册；AU3 legacy 在 `au3/src/effects/*.cpp` 匿名命名空间注册。**同一符号两套并存时，先注册者占位**（详见各条目的"版本"备注）。

**两种处理数据流**（离线应用效果时）：
- `PerTrackEffect`：固定块流式管线——`EffectOutputTracks` 先把选中轨 `Duplicate(Backup)` 成影子轨，然后 `WideSampleSource → instance.ProcessBlock(in, out, blockLen) → WaveTrackSink` 循环（`au3/libraries/au3-effects/PerTrackEffect.cpp:35`），实例成员携带流状态（Echo 历史线、双二阶系数等），生命周期为 `ProcessInitialize / ProcessBlock / ProcessFinalize`。
- `StatefulEffect`：状态就在效果对象里（`MakeInstance()` 返回包装自身的 Instance），效果整体自由访问整段音频（`GetFloats/SetFloats` 甚至 `ClearAndPaste`），不走固定块管道（Reverse/Repeat/Normalize 等）。
- `StatefulPerTrackEffect`：有状态 UI + 按轨块处理的混合形态，多数 `*Base` 算法类的基类。

---

## 1. 音量与动态（VolumeAndCompression）

| 效果 | 类（生效版本） | 算法文件 | 原理 | 域 / 模式 | 复杂度与内存 |
|---|---|---|---|---|---|
| Amplify 放大 | `AmplifyEffect`（AU4） | `src/effects/builtin_collection/amplify/amplifyeffect.cpp` | 常数增益逐样本相乘；初始化按选区峰值建议不削波增益 | 时域逐样本；就地流式 | 1 乘法/样本；无额外缓冲（Init 扫峰一次） |
| Normalize 标准化 | `NormalizeEffect`（AU4） | `src/effects/builtin_collection/normalize/normalizeeffect.cpp` | 两遍：先求各通道峰值（可选先去 DC），再统一乘增益到目标 dB | 时域；两遍 | ~1 比较+1 乘法/样本·遍；仅标量状态 |
| Loudness 响度标准化 | `NormalizeLoudnessEffect`（AU4） | `.../loudness/normalizeloudnesseffect.cpp` + `au3-math/EBUR128.cpp` | 两遍：EBU R128 K 加权门控积分响度（或 RMS）测量，再乘固定增益 | 时域（测量含 K 加权滤波）；两遍 | 测量约 6 次双二阶运算/样本（见 §11 EBUR128）；无全量缓冲 |
| Compressor 压缩器 | `CompressorEffect` + `CompressorInstance`（AU4，旧版已删除） | `.../dynamics/compressor/`；算法库 `au3-dynamic-range-processor/` | 峰值检测 → dB 域软拐点 + 一阶攻击/释放平滑 → 前瞻延迟线渐入 → 乘到延迟后音频 | 时域分块（≤512）；流式，支持实时 | ~10–15 运算/样本；`mEnvelope[512]` + 每通道前瞻向量（改 lookahead 才重分配）。算法结构详见 §11.9 |
| Limiter 限制器 | `LimiterEffect`（AU4） | `.../dynamics/limiter/limitereffect.cpp`；同一算法库 | 同一引擎特例：ratio=∞（砖墙）、attack=0、输出目标 −1 dB | 同上 | 同上 |
| Legacy Compressor 旧压缩器 | `EffectLegacyCompressor`（仅 AU3 侧注册） | `au3-builtin-effects/LegacyCompressorBase.cpp` + `TwoPassSimpleMono.cpp` | 两遍：峰值跟随包络（+100 点滑动 RMS 判噪门）压缩；再按全程最大值归一化 | 时域；两遍 | ~10 运算/样本；双包络缓冲 ≈ 块长 |
| Auto Duck 自动闪避 | `AutoDuckEffect`（AU4） | `.../autoduck/autoduckeffect.cpp` | 读控制轨求包络，在闪避区间生成线性斜坡增益乘到目标轨 | 时域；两遍（先全量分析控制轨） | 1 乘法+比较/样本；每块缓冲 |
| Remove DC Offset 去直流 | `RemoveDCOffsetEffect`（AU4） | `.../removedcoffset/removedcoffseteffect.cpp` | 两遍：求选区均值，再逐样本减去 | 时域；两遍 | 每遍 ~1 运算/样本；标量累计器 |

## 2. 淡变（Fading）

| 效果 | 类 | 算法文件 | 原理 | 复杂度与内存 |
|---|---|---|---|---|
| Fade In / Out 淡入淡出 | `FadeInEffect` / `FadeOutEffect` | `src/effects/builtin_collection/fade/fadeeffect.cpp` | 线性斜坡 `s·(n/N)` 逐样本相乘 | 1 乘 1 除/样本；样本计数器 |

## 3. 变调与变速（PitchAndTempo）

| 效果 | 类 | 算法文件 | 原理 | 域 / 模式 | 复杂度与内存 |
|---|---|---|---|---|---|
| Change Pitch 变调 | `ChangePitchEffect` → `ChangePitchBase`（AU4 注册，算法沿用 AU3） | `au3-builtin-effects/ChangePitchBase.cpp` + `SBSMSBase.cpp` 或 `SoundTouchBase.cpp` | SBSMS（异步 STFT 频域拉伸）或 SoundTouch WSOLA；只改音高 | SBSMS=频域 / WSOLA=时域；需全量离线 | 高（几十至上百运算/样本）；帧+重采样缓冲（块 512） |
| Sliding Stretch 滑动伸缩 | `SlidingStretchEffect` → `SBSMSBase` | `src/effects/builtin_collection/slidingstretch/` | SBSMS + 滑动包络：速度/音高比随时间滑动（8 种 SlideType） | 分块 STFT 频域；需全量离线 | 高；帧缓冲+参数插值 |
| Change Tempo 变速 | `EffectChangeTempo` → `ChangeTempoBase`/`SoundTouchBase`（仅 AU3 侧注册） | `au3-builtin-effects/ChangeTempoBase.cpp`、`SoundTouchBase.cpp` | WSOLA 整拍搜索拼接（或 SBSMS 引擎）；只改时长 | 时域；需全量离线 | 中高；SoundTouch 窗口缓冲 |
| Change Speed 变速变调 | `EffectChangeSpeed` → `ChangeSpeedBase`（仅 AU3 侧注册） | `au3-builtin-effects/ChangeSpeedBase.cpp` | 直接重采样（`Resample`/soxr 插值），时长音高同步变 | 时域；需全量离线，逐块 | 中；≈factor×块长缓冲 |
| Paulstretch 极限拉伸 | `PaulstretchEffect`（AU4，内嵌 `PaulStretch` 类） | `src/effects/builtin_collection/paulstretch/paulstretcheffect.cpp` | FFT 幅度保持+随机相位+IFFT+OLA；输出长=输入×stretch | 频域（大 FFT）；需全量 | 高；FFT 尺寸 = 2^round(log2(rate·TimeResolution/2)) 的多组缓冲 |

**播放内联的时间伸缩**（非效果，clip 变速/变调回放管线）：`StaffPadTimeAndPitch`（`au3-time-and-pitch/StaffPad/TimeAndPitch.cpp`）——相位锁定声码器 + OLA：FFT 4096（开共振峰保持 2048）、overlap=4、Hann 窗；逐帧谱峰"生长"展开相位锁定，低置信区旋转伪随机相位；共振峰保持用倒谱法（quefrency 0.002s）只移谱细节不移包络；伸缩/变调因子独立，直通（ratio=1）零开销。设计文档：`au3-stretching-sequence/readme.md`。

## 4. EQ 与滤波（EqAndFilters）

| 效果 | 类 | 算法文件 | 原理 | 域 / 模式 | 复杂度与内存 |
|---|---|---|---|---|---|
| Filter Curve EQ | `FilterCurveEq` → `EqualizationBase` | `src/effects/builtin_collection/filtercurveeq/`；`au3-builtin-effects/EqualizationBase.cpp`、`EqualizationFilter.cpp` | 曲线 → 16384 点 FFT 反演为 M 阶 FIR（Hamming 窗截断）→ 逐 16384 点窗频域快卷积 + overlap-add | 频域；就地分块 | O(N log N)；thisWindow/lastWindow 两级 16384 缓冲 + FFT 表 |
| Graphic EQ | `GraphicEq` → `EqualizationBase` | `src/effects/builtin_collection/graphiceq/` | 同上，输入为对数频带滑杆 | 同上 | 同上 |
| Bass and Treble | `BassTrebleEffect` → `BassTrebleBase` | `.../basstreble/basstrebleeffect.cpp`；`au3-builtin-effects/BassTrebleBase.cpp` | 低架+高架双二阶 IIR 级联，系数仅参数变化时重算 | 时域逐样本；流式，支持实时 | ~12 乘加/样本；每通道两组双二阶状态 |
| Scientific Filter | `EffectScienFilter` → `ScienFilterBase`（仅 AU3 侧注册） | `au3-builtin-effects/ScienFilterBase.cpp` | Butterworth / Chebyshev I/II 型 IIR，(order+1)/2 节双二阶级联（双线性预畸变） | 时域逐样本；流式 | 5 乘加×节数/样本；设计器见 §11.3 Biquad |

## 5. 降噪与修复（NoiseRemovalAndRepair）

| 效果 | 类 | 算法文件 | 原理 | 域 / 模式 | 复杂度与内存 |
|---|---|---|---|---|---|
| Noise Reduction 降噪 | `NoiseReductionEffect` | `src/effects/builtin_collection/noisereduction/noisereductioneffect.cpp`；STFT 框架 `au3-wave-track-fft/TrackSpectrumTransformer` | 两遍：①噪声画像（逐 bin 均值/均方）②谱减门限：每 bin 判定+衰减（默认 −18dB 级）、多窗时间平滑、跨 bin 对数域频率平滑、ISTFT OLA 重构 | 分块 STFT 频域；两遍，逐窗流式 | 高；每窗 `mGains[size/2+1]` + 滑动历史窗 |
| Click Removal | `ClickRemovalEffect` | `src/effects/builtin_collection/clickremoval/clickremovaleffect.cpp` | 8192 窗内检测超阈值孤立尖峰，按宽度（≤2049）邻域内插替换 | 时域分块；需整窗缓冲 | 中；8192 窗缓冲 |
| Repair 修复 | `Repair` | `src/effects/builtin_collection/repair/repaireffect.cpp`；核心 `au3-math/InterpolateAudio` | ≤128 样本坏段，取两侧 ≤4×128 样本做 LSAR 带限插值 | 时域；局部全量缓冲 | 低；小临时缓冲（算法本体见 §11.5） |

## 6. 延迟与混响（DelayAndReverb）

| 效果 | 类 | 算法文件 | 原理 | 复杂度与内存 |
|---|---|---|---|---|
| Echo 回声 | `EffectEcho` → `EchoBase`（仅 AU3 侧注册） | `au3-builtin-effects/EchoBase.cpp` | 反馈延迟线 `y[n]=x[n]+decay·hist[n−delay]` | 2 乘加/样本；历史环形缓冲 = rate×delaySec（Init 一次分配） |
| Reverb 混响 | `ReverbEffect`（AU4；libSoX 算法） | `src/effects/builtin_collection/reverb/reverbeffect.cpp`；`au3-math/Reverb_libSoX.h` | 预延迟 + 8 comb + 4 allpass 网络 + 前后级色调滤波 + 立体声去相关 | 几十运算/样本；每通道 comb/allpass 延迟线；块 16384，支持实时 |

## 7. 失真与调制（DistortionAndModulation）

| 效果 | 类 | 算法文件 | 原理 | 复杂度与内存 |
|---|---|---|---|---|
| Distortion 失真 | `EffectDistortion` → `DistortionBase`（仅 AU3 侧注册） | `au3-builtin-effects/DistortionBase.cpp` | 查表波形整形（12 种曲线），DC 阻断队列，每 1000 样本重算表 | ~1 查表+插值/样本；1001 点表；支持实时 |
| Phaser 相位器 | `EffectPhaser` → `PhaserBase`（仅 AU3 侧注册） | `au3-builtin-effects/PhaserBase.cpp` | LFO 调制 stages 级一阶全通，反相混合+反馈 | 每级 ~4 运算×stages；支持实时 |
| Wahwah 哇音 | `EffectWahwah` → `WahWahBase`（仅 AU3 侧注册） | `au3-builtin-effects/WahWahBase.cpp` | LFO 调制谐振带通双二阶中心频率 | ~15 运算/样本；支持实时 |
| Tremolo 颤音 | 无 C++ 版，仅 Nyquist `tremolo.ny` | `share/nyquist-plug-ins/tremolo.ny` | LFO 幅度调制 | — |

## 8. 特殊（Special）与轨道级操作

| 效果 | 类 | 算法文件 | 原理 | 复杂度与内存 |
|---|---|---|---|---|
| Invert 反相 | `InvertEffect`（AU4） | `.../invert/inverteffect.cpp` | `y=−x` | 1 运算/样本 |
| Reverse 倒放 | `ReverseEffect`（AU4） | `.../reverse/reverseeffect.cpp`（底层 `WaveTrackUtilities::Reverse`） | 选区样本整段倒序（含标签搬移） | 每样本 1 次搬运；需全量 |
| Repeat 重复 | `EffectRepeat`（仅 AU3 侧注册） | `au3-builtin-effects/RepeatBase.cpp` | 选区复制后逐次 Paste（无 DSP） | 纯内存搬运 |
| Truncate Silence 截断静音 | `TruncateSilenceEffect` → `TruncSilenceBase`（AU4） | `.../truncatesilence/`；`au3-builtin-effects/TruncSilenceBase.cpp` | 按阈值+最短时长检测静音区，截短/按比例压缩后整轨重排 | 两遍（Analyze 生成 Region 列表→重排）；每样本 ~1 比较 |
| Stereo to Mono | `StereoToMono`（AU3 注册，菜单隐藏） | `au3-builtin-effects/StereoToMono.cpp` | 各通道求和平均 | 1 加法/样本·通道 |

## 9. 生成器（EffectTypeGenerate）

均经 `Generator` 基类：写临时轨再 `ClearAndPaste` 回原轨。

| 生成器 | 类 | 算法文件 | 原理 | 复杂度 |
|---|---|---|---|---|
| Tone 音调 | `ToneEffect` → `ToneGenBase` | `src/effects/builtin_collection/tonegen/toneeffect.cpp`；`au3-builtin-effects/ToneGenBase.cpp` | 相位累加器合成正弦/方/锯/三角；"无锯齿方波"用 ≤200 次谐波加频域 Hann 窗抗混叠 | 1 sin/样本（谐波版 ~k 次 sin） |
| Chirp 扫频 | `ChirpEffect` : `ToneEffect` | `.../tonegen/chirpeffect.cpp` | 同 Tone，频率线性/对数扫描 | 同上 |
| Noise 噪声 | `NoiseGenerator` | `.../noisegen/noisegenerator.cpp` | 白=均匀 rand；粉=Paul Kellet 7 级一阶滤波；棕=白噪声积分 | 白 1 rand；粉 ~10 运算；棕 ~3 |
| Silence 静音 | `SilenceGenerator` → `SilenceBase` | `.../silencegen/silencegenerator.cpp` | 写零 | 0 |
| DTMF Tones | `DtmfGenerator` | `.../dtmfgen/dtmfgenerator.cpp` | 每按键双正弦（低群 697–941 × 高群 1209–1633 Hz）+斜坡 AM+音间静音 | 2 sin+包络/样本 |

## 10. 分析器（EffectTypeAnalyze）与菜单工具

| 工具 | 类 | 算法文件 | 原理 | 复杂度 |
|---|---|---|---|---|
| Find Clipping | `EffectFindClipping` → `FindClippingBase` | `au3-builtin-effects/FindClippingBase.cpp` | 单遍统计连续满幅样本段，超阈值在标签轨打标 | 1 比较/样本 |
| Plot Spectrum | `PlotSpectrumBase` + `SpectrumAnalyst`（+UI `au3/src/FreqWindow.cpp`） | `au3-builtin-effects/PlotSpectrumBase.cpp`；`au3-fft/SpectrumAnalyst.cpp` | 选区（≤约 10.6M 样本）Hann 窗 FFT 多段平均幅谱；支持自相关/倒谱 | O(N log N) 一次 |
| Contrast | `ContrastBase`（+UI `Contrast.cpp`） | `au3-builtin-effects/ContrastBase.cpp` | 前景/背景选区各测 RMS 换算 dB 差 | 1 乘加/样本 |
| Nyquist analyze | beat.ny、label-sounds.ny、rms.ny | `share/nyquist-plug-ins/` | 见 §12 Nyquist 宿主 | — |

## 11. 核心 DSP 基础设施

### 11.1 Resample 重采样（`au3-math/Resample.{h,cpp}`）
libsoxr 的单声道流式封装。质量四档映射 soxr recipe `"\0\1\4\6"` → QQ/LQ/HQ/VHQ；**"Fast/Best"两把偏好钥匙默认均为 3（Best）**；变速率（dMin≠dMax）固定 `SOXR_HQ|SOXR_VR`，每块先 `soxr_set_io_ratio` 再 `soxr_process`。`Process` 返回 {消耗输入, 产出输出}，调用方循环搬运。包装层零额外分配；跳转（Reposition skipping）时整体重建实例（规避 libsoxr 0.1.3 flush bug）。**所有重采样都按内存连续单声道 float 流处理。**
调用方：`MixerSource::MixVariableRates`（播放，每 1024 样本一次）、`WaveClip::Resample`（离线整 clip）、录制 `DrainRecordBuffers`、ChangeSpeed。

### 11.2 FFT（`au3-fft`）
- **RealFFTf**（`RealFFTf.cpp`）：Van Baren 迭代基-2 实数 FFT，预计算 bit-reversal 表 + sin/cos 表（表池 `MAX_HFFT=10` 个常驻，`wxCriticalSection` 保护）；只算一半点数利用实信号对称；DC 与 Fs/2 打包进 `buffer[0]/[1]`；**正变换输出位反序**，需 `ReorderToFreq/ReorderToTime` 还原。正变换无缩放，逆变换原语**吃顺序 bin 输入**（见 `FFT.cpp::InverseRealFFT` 的打包）。O(N log N)，原缓冲就地。
- **FFT.cpp**：复数 Cooley-Tukey（twiddle 递推，位反表 ≤2^16）；`RealFFT/InverseRealFFT/PowerSpectrum` 是 RealFFTf 的方便包装（每次临时分配 `Floats pFFT{N}`）；10 种窗函数。
- **PowerSpectrumGetter**：pffft（SIMD）功率谱，plan 逐帧复用零分配；输入输出须 64B 对齐（`PffftFloatVector`）。当前 au3 主线无调用点（新式接口备用；老路径走 `FFT.cpp::PowerSpectrum`）。
- **SpectrumTransformer**：STFT 分析-修改-合成框架（窗→RealFFTf→改系数→InverseRealFFTf→OLA）；hop=windowSize/stepsPerWindow（降噪用 4 即 75% 重叠）；窗口对象 Start 时分配后 rotate 复用。唯一消费者：谱编辑（`SpectralDataManager`，windowSize 2048）。

### 11.3 Biquad 双二阶（`au3-math/Biquad.{h,cpp}`）
5 个 double 系数 + 4 状态；`ProcessOne` 用 double 累积防舍入灾难（每样本 5 乘 4 加）。设计器：Butterworth / Chebyshev I/II（双线性变换预畸变，1–10 阶，返回 `(order+1)/2` 节）。**fn 参数语义是奈奎斯特频率**（`ScienFilterBase` 传 `mNyquist`）。消费者：EBUR128 K 加权、ScienFilter、BassTreble（各 effect 自管系数）。

### 11.4 SampleFormat / Dither（`au3-math/SampleFormat.{h,cpp}`、`Dither.{h,cpp}`）
- 三格式：int16(2B) / int24(内存 4B、盘上 3B) / float32(4B)。
- `CopySamples` 总入口；**仅三种窄化组合才抖动**：float→int16、float→int24、int24→int16；同格式 memcpy，→float 只除系数，int16→int24 走 `<<8` 快路径。
- `Dither`：none / rectangle（1×rand）/ triangle（2×rand）/ shaped（Lipshitz 5 抽头 FIR 误差反馈 + 8 槽环形缓冲，3×rand）。**全局单例状态**：`triangle/shaped` 每次 Apply 前 Reset()，且 `rand()` 非线程安全。`InitDitherers()` 设全局默认：低质=none、高质=shaped。
- `SampleFormats{Effective ≤ Stored}`：窄化回原格式时免抖动（导出位精确）。

### 11.5 InterpolateAudio（`au3-math/InterpolateAudio.{h,cpp}`）
LSAR（最小二乘自回归）修复坏样本段：估 P 阶 AR 系数（O(len·P²)+求逆 O(P³)），解 Toeplitz 系统补段；退化时线性插值。临时矩阵分配较重（Repair 效果专用，段 ≤128 样本）。

### 11.6 Envelope 包络（`au3-mixer/Envelope.{h,cpp}`）
压缩存储：只存控制点 `vector<EnvPoint{t,val}>`，段间分段线性（mDB 时指数域）。`GetValues` 批量求值：跨段一次二分（带上次位置缓存），段内增量法每样本 ~1 FMA（dB 域每样本两次 `pow(10,·)`，最贵分支）。解析积分 `Average/Integral/IntegralOfInverse` 供时间轨 warp 与变率混音。

### 11.7 波形存储模型（`au3-wave-track` + `au3-project-file-io`）
层级：`WaveTrack`（≤2 通道）→ `WaveClip`（每通道一个 `Sequence` + 剪辑偏移/包络/速率）→ `Sequence`（单通道 `BlockArray = deque<SeqBlock>`，二分定位）→ `SampleBlock`（抽象）→ **`SqliteSampleBlock`**。
- 块大小：`sMaxDiskBlockSize = 1MB`；`mMinSamples = 1MB/SAMPLE_SIZE/2`，`mMax = ×2`——**float 轨每块 128K–256K 样本**（int16 轨 32K–64K）。
- 写：`Append` 先进内存 `mAppendBuffer`（容量 mMaxSamples），达 `GetIdealAppendLen()` 时 `DoAppend` 落块；`SqliteSampleBlock::SetSamples` 内存 memcpy 原格式字节（**无压缩**）→ `CalcSummary`（O(n) 两遍）→ 单条 INSERT bind_blob，随后立即释放内存副本。**静音块不入库**（负 id 编码长度全局共享）。
- 读：按 blockid SELECT，`GetBlob` 指针直接 `CopySamples` 到目标（基本不抖动）；float 视图有 `weak_ptr` 懒加载缓存（双检锁）。
- Summary：每块两级 min/max/RMS（256 样本/帧 + 65536 样本/帧，各 ×12B）≈ 每块 2.9% 元数据。注意：`Sequence::GetMinMaxRMS` 对部分区间**未用 summary，直接拉样本重算**（源码 TODO）。
- SQLite：页 64KB、WAL、synchronous=NORMAL、wal_autocheckpoint=0（自有 CheckpointThread PASSIVE 检查点）；块工厂 `mAllBlocks` weak_ptr map 去重复用。

### 11.8 StretchingSequence（`au3-stretching-sequence`）
`PlayableSequence` 装饰器（与 WaveTrack 同接口，可无缝进 Mixer/AudioIO）：把 clip 列表切成 `[SilenceSegment | ClipSegment(内含 StaffPadTimeAndPitch) | …]`。连续前向每样本 1 次 push；**反向用方向感知反转 push 序列；随机访问（循环/跳转）需重置重拉伸**（readme 的设计权衡）。渲染/导出时也用它（`ExportPluginHelpers::CreateMixer`）。

### 11.9 Dynamic Range Processor（`au3-dynamic-range-processor`）
Compressor/Limiter 共用引擎，块 ≤512：`UpdateEnvelope`（多通道峰值检测，非 RMS）→ `GainReductionComputer`（dB 域软拐点 + 一指数平滑 attack/release；Rudrich SimpleCompressor）→ `LookAheadGainReduction`（环形延迟线 3ms/1ms，大衰减渐入防过冲）→ `ApplyEnvelope`（音频延迟后乘 `10^((gain+makeup)/20)`）。`CompressorInstance` 经 `LockFreeQueue` 向 QML 面板发电平包数据。

### 11.10 实时效果框架（`au3-realtime-effects`）
`RealtimeEffectList`（有序 state 表，spinlock；挂在项目（master 链）与每 ChannelGroup（轨道链）上）→ `RealtimeEffectState`（槽位：工厂 + weak 实例 + **双份设置 NonInterfering 交换协议**（主线程写/音频线程换入）+ 延迟量）→ `RealtimeEffectManager`（每项目单例；`InitializationScope/ProcessingScope` RAII 管生命周期）。处理形态：`ProcessStart`（换设置/恢复挂起）→ 逐 512 子块 `RealtimeProcess`（状态间 ibuf/obuf 指针交换，奇数个效果 memcpy 回原缓冲）→ 返回可丢弃延迟样本数。**实时播放不走 EffectStage**（见 02 文档）；EffectStage 服务离线渲染。

## 12. 插件宿主（只记接口位置）

| 协议 | AU3 宿主 | AU4 侧 |
|---|---|---|
| Nyquist | `au3-nyquist-effects/NyquistBase`（内嵌 libnyquist Lisp 解释器，流式回调取/还音频） | `src/effects/nyquist/`（扫描/加载/Prompt）；脚本 28 个于 `share/nyquist-plug-ins/`（process 18 / analyze 3 / generate 3 / tool 4） |
| LADSPA / LV2 | `au3-ladspa` / `au3-lv2` | — |
| VST / VST3 | `au3-vst` / `au3-vst3` | `src/effects/vst`（含 vst3effectloader） |
| AudioUnit (macOS) | `au3-audio-unit` | `src/effects/audio_unit` |
| Vamp 分析 | `au3/src/effects/vamp` | `src/effects/vamp/todo`（未移植） |

## 13. 导入导出（只记挂接位置）

编解码插件在 `au3/modules/import-export/mod-*`（ffmpeg/flac/mp3/mpg123/ogg/opus/pcm/wavpack/aup/cl/lof）；框架 `au3-import-export`（ImportPlugin/ExportPluginRegistry）；原始导入与格式识别 `au3/src/import/FormatClassifier`、导出对话框 `au3/src/export/ExportAudioDialog`。新架构门面 `src/importexport/{import,export}`（`iimporter.h`/`iexporter.h`）。导出混音经 `ExportPluginHelpers::CreateMixer`（带每轨+主控效果 StageSpecification，见 02 文档离线渲染链）。

## 附录 A：两遍（分析+处理）效果清单

Normalize、Loudness Normalization、Remove DC Offset、Legacy Compressor、Noise Reduction（画像+处理，用户分两次操作）、Truncate Silence、Auto Duck；纯分析类：Find Clipping、Plot Spectrum、Contrast 及 Nyquist analyze 类。

## 附录 B：新架构（AU4）重写对照

有重写版：amplify、autoduck、basstreble、changepitch、clickremoval、dynamics(compressor+limiter)、fade、filtercurveeq、graphiceq、invert、loudness、noisegen、noisereduction、normalize、paulstretch、removedcoffset、repair、reverb、reverse、silencegen、slidingstretch、tonegen(Tone+Chirp)、truncatesilence、dtmfgen。
无重写版（仍 AU3 侧注册）：ChangeTempo、ChangeSpeed、Distortion、Phaser、Wahwah、Echo、Repeat、ScienFilter、LegacyCompressor、FindClipping、StereoToMono、Nyquist 宿主。
新架构类三种形态：直接继承 au3 基类（如 `BassTrebleEffect→BassTrebleBase`）；完全自写 DSP（如 Amplify/Normalize/Reverb）；经 `CompressorInstance` 用 au3-dynamic-range-processor。
