# 音频处理研究：算法目录 / 链路说明 / 性能评估 / 微基准

Status: ready-for-agent

## 背景

对 Audacity 仓库（Audacity 4：Qt/QML 新架构 + `au3/` 旧核心）做一次学习研究：整理音频处理算法、讲清音频链路联通方式、评估链路的内存与 CPU 开销。**研究性质，不修改任何源码。**

## 目标交付物

| 交付物 | 位置 |
|---|---|
| 算法目录（按功能类别） | `study_audacity/docs/01-algorithms-catalog.md` |
| 音频链路联通说明 | `study_audacity/docs/02-audio-pipeline.md` |
| 内存/CPU 性能评估 | `study_audacity/docs/03-performance.md` |
| 微基准与验证程序 | `study_audacity/benchmarks/`（独立 CMake 工程） |
| 总入口 | `study_audacity/README.md` |
| 领域术语 | 根 `GLOSSARY.md`（惰性创建，中文+英文括注，无实现细节） |

## 范围

- 覆盖：内置效果器、生成器/分析器、核心 DSP 基础设施（重采样/混音/波形存储/包络/时间伸缩）、实时播放与录制链路。
- 导入导出编解码：只在链路文档说明挂接位置，不深入各编解码器。
- 第三方插件（VST/AU/LV2/Nyquist/LADSPA）：只记录宿主侧接口。
- 深度切分：算法目录只写 `au3/` 侧（分类沿用 `EffectGroup` 官方分组）；链路文档以 au3 核心部件为主线，`src/` 新架构层只在挂接点出现；性能评估只覆盖 au3 数据通路。

## 关键决策（grilling 已确认）

1. 全部中文；基线 = master `be46fba74`。
2. 性能方法 = 静态分析为主 + 微基准实测为辅；推算与实测明确区分标注。
3. 性能结论分层：每环节定性评级 + 量化推算（每样本操作数/拷贝次数/内存公式）+（有实测的）实测数据；整体给内存模型（如 1 分钟 44.1kHz 立体声）与 CPU 热点排序。
4. 问题按"现状 → 影响 → 可选改进"记录，只写建议不改代码。
5. 微基准：真实代码优先；**依赖拆不开的案例，动手萃取前必须先报告用户裁决**。
6. 微基准第一批（真实代码、零外部依赖）：SampleFormat+Dither、Biquad、EBUR128、RealFFTf/PowerSpectrumGetter（配 `thirdparty/pffft`；与 au3-math 有链接冲突 ⇒ 各自独立可执行目标）。第二批（Resample/soxr、StaffPadTimeAndPitch、DynamicRangeProcessor、Envelope、SqliteSampleBlock）逐个拆解后单独报用户裁决。
7. 算法条目统一模板：功能一句话、类别、关键文件/类、输入输出、复杂度与内存备注。

## 约束

- 不修改 Audacity 源码与现有构建系统。
- 本机 `muse/`、`muse_deps/` 子模块未检出：`src/` 不可编译；微基准仅覆盖 au3 侧自包含代码。
- 工具链：CMake 3.28 + Visual Studio 2022 Community（微基准可实际编译运行）。
- soxr 为外部构建依赖（本机无构建产物），涉及它的基准走决策 5。
- 引用而非重写仓库已有文档：`docs/effect-view-architecture.md`、`au3/libraries/au3-stretching-sequence/readme.md`、PortAudio 延迟分析等。

## 执行顺序

01 算法目录 → 02 链路文档 → 03 微基准（第一批） → 04 性能评估（汇总静态+实测）。

## Comments

- 2026-10-20 grilling 完成，用户逐条确认共识；本 spec 按其确认内容建立。
- 2026-10-20 四张工单全部 resolved：算法目录、链路文档、性能评估、微基准第一批（四基准全 PASS 出数）。根 GLOSSARY.md 已按约定惰性创建。第二批基准（soxr/pffft/TimeAndPitch/DRC/Envelope/SqliteSampleBlock）待用户逐项裁决（Q13 规则）。
- 2026-10-20 用户裁决：拉真实 soxr、拉 pffft 源码、第二批继续。工单 05 resolved：soxr 恒率 2.5–7.1 / VR 73.7 ns、pffft 快 RealFFTf 3.3 倍、DRC 22.6、TimeAndPitch 61.5–73.2 ns/样本。Envelope 与 SqliteSampleBlock 判定拆不开，选项报用户（03 文档 §9）。
- 2026-10-20 第三批裁决并完成（工单 06）：SqliteSampleBlock 复刻等价写路径（写 856–923 MB/s、WAL 放大 ≈1.26×、读 2.5 ns/样本、逐位一致）；Envelope 按用户方案 Python 绘图（docs/img/envelope_interpolation.png + CSV 导出）。全部交付物完成，九个基准 + 一个可视化脚本，验证全 PASS。
