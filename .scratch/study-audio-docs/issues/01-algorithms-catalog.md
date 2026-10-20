# 01 算法目录：按功能类别整理音频处理算法

Status: resolved

## 任务

产出 `study_audacity/docs/01-algorithms-catalog.md`：覆盖 `au3/` 侧全部音频处理算法，分类沿用 `EffectGroup` 官方分组（音量/动态、EQ/滤波、延迟/混响、降噪/修复、变调/变速、失真/调制、频谱工具等），另加生成器、分析器、核心 DSP 基础设施章节。

每个算法一个条目，统一模板：功能一句话、类别、关键文件/类、输入输出、复杂度与内存备注。

## 验收标准

- [ ] 内置效果全覆盖（以 `BuiltinEffectsModule` 注册表与 `au3-builtin-effects` 为准，含各 `*Base.cpp`）
- [ ] 生成器（Tone/Chirp/Noise/Silence/DTMF 及 Nyquist 生成器宿主）与分析器（FindClipping/PlotSpectrum/Contrast 等）各有章节
- [ ] 核心 DSP 基础设施章节：Resample、au3-fft、SampleFormat/Dither、Biquad、EBUR128、Envelope、WaveTrack/Sequence/SampleBlock 存储、StretchingSequence/StaffPadTimeAndPitch、DynamicRangeProcessor、RealtimeEffect*
- [ ] 第三方插件只记宿主侧接口；导入导出不深入
- [ ] 引用已有文档（effect-view-architecture.md 等）而非重写

## Comments

- 2026-10-20 建立。
- 2026-10-20 完成：`study_audacity/docs/01-algorithms-catalog.md`。覆盖 8 大 EffectGroup 类别 + 生成器/分析器 + 核心 DSP 基础设施 10 节 + 宿主/导入导出挂接；附录含两遍效果清单与新架构重写对照。验收项全部达成（第三方插件仅宿主侧、导入导出仅挂接、引用而非重写已有文档）。
