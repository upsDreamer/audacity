# study_audacity — Audacity 音频处理研究

对 Audacity 4 仓库（Qt/QML 新架构 `src/` + `au3/` 旧核心）音频处理体系的**学习研究文档**，不含对源码的任何修改。基线：master `be46fba74`（2026-10-20）。

## 文档导航

| 文档 | 内容 |
|---|---|
| [docs/01-algorithms-catalog.md](docs/01-algorithms-catalog.md) | 全部音频处理算法目录：按官方 `EffectGroup` 分类（音量/动态、变调变速、EQ/滤波、降噪/修复、延迟/混响、失真/调制、特殊），生成器、分析器、核心 DSP 基础设施（重采样/FFT/存储模型/包络/实时效果框架），插件宿主与导入导出挂接位置 |
| [docs/02-audio-pipeline.md](docs/02-audio-pipeline.md) | 音频链路联通：总图 + 播放链（主线程启动→音频线程填充→PA 回调消费）、录制链、离线渲染链逐步调用流（函数名+文件行号）、环节契约表、格式转换点、新架构挂接点 |
| [docs/03-performance.md](docs/03-performance.md) | 内存与 CPU 开销：实测基准 + 静态推算（明确区分）、播放/录制/渲染逐环节成本、内存占用模型（"项目时长与常驻内存解耦"）、CPU 热点排序、9 条改进建议 |

核心结论速览：
- 算法本体 100% 在 `au3/`；`src/` 只做编排（本机 muse 子模块未检出，不可编译）。
- **波形数据不常驻内存**（SQLite 块存储），内存占用与项目时长解耦；典型播放常驻 ≈1–1.5MB/轨。
- 播放链 float 直通、无抖动，基线 ≈2–5 ns/样本（<0.05% 核心）。
- **真正的数量级热点只有两个，都与"变速"有关**：soxr 变速率模式 73.7 ns/样本、TimeAndPitch 变速 clip 回放 61–73 ns/样本；恒率重采样其实只有 5–7 ns。次级热点是 DRC 压缩器（22.6）与 shaped 抖动（18.8，rand() 是主因）。
- pffft(SIMD) 功率谱比手写 RealFFTf 快 3.3 倍——仓库已备好 PowerSpectrumGetter 但主线未接线。

## 微基准

`benchmarks/`：独立 CMake 工程，直接编译 `au3/` 真实源码（环境垫片见其 README），**全部先正确性验证后计时**。八个基准覆盖第一批（格式转换/抖动、Biquad、EBUR128、RealFFTf）与第二批（soxr 重采样、pffft 功率谱对照、DRC 压缩器引擎、TimeAndPitch 变速内核；后两者经用户裁决拉取真实 soxr/pffft 源码）。

```bash
cmake -S benchmarks -B benchmarks/build -A x64
cmake --build benchmarks/build --config Release
benchmarks/build/bin/Release/bench_sample_format.exe
benchmarks/build/bin/Release/bench_biquad.exe
benchmarks/build/bin/Release/bench_ebur128.exe
benchmarks/build/bin/Release/bench_realfftf.exe
benchmarks/build/bin/Release/bench_resample.exe
benchmarks/build/bin/Release/bench_pffft.exe
benchmarks/build/bin/Release/bench_drc.exe
benchmarks/build/bin/Release/bench_time_and_pitch.exe
```

环境：CMake 3.28 + VS2022（AMD Ryzen 7 5800H 实测）。

剩余待裁决（Q13 规则）：Envelope 与 SqliteSampleBlock 依赖拆不开，选项见 `docs/03-performance.md` §9。

## 相关跟踪

任务规格与工单：`.scratch/study-audio-docs/`（spec + 4 工单）。
