# 03 微基准（第一批）：真实代码、零外部依赖

Status: resolved

## 任务

在 `study_audacity/benchmarks/` 建独立 CMake 工程（不改仓库构建系统），用 VS2022/CMake 实际编译运行。第一批基准：

1. `SampleFormat` 逐样本拷贝/格式转换 + `Dither`（au3-math）
2. `Biquad` 滤波（au3-math）
3. `EBUR128` 响度测量（au3-math）
4. `RealFFTf` / `PowerSpectrumGetter`（au3-fft + `thirdparty/pffft`；与 au3-math 链接冲突 ⇒ 独立可执行目标）

每个基准程序先做正确性 sanity 验证，再计时输出。

**第二批（Resample/soxr、StaffPadTimeAndPitch、DynamicRangeProcessor、Envelope、SqliteSampleBlock）不在本工单范围**：每项拆解依赖后、动手萃取前，必须先报告用户裁决。

## 验收标准

- [ ] 独立 CMake 工程编译通过（VS2022）
- [ ] 全部基准可运行并输出计时结果
- [ ] 正确性 sanity 验证通过（与已知性质/参考值比对）
- [ ] README 写明构建与运行方法
- [ ] 未修改 Audacity 源码

## Comments

- 2026-10-20 建立。
- 2026-10-20 完成第一批：`study_audacity/benchmarks/`（独立 CMake + VS2022 x64 Release，Ryzen 7 5800H 实测）。四基准全部编译、全部正确性验证 PASS、全部出数：SampleFormat/Dither（shaped 18.8 vs 无抖动 2.6 ns/样本）、Biquad（4.2/节）、EBUR128（立体声 17.2 ns/样本，规范点 −23.00 LUFS）、RealFFTf（4096 点正变换 4.27 ns/样本）。未修改 Audacity 源码；环境垫片清单见 benchmarks/README.md（仅宏/互斥/最小 EnumSetting，被测算法零改动）。
- 2026-10-20 偏差记录：`thirdparty/pffft` 仅提交了 CMakeLists（源码主构建时拉取），PowerSpectrumGetter 基准暂缺源码——连同 soxr 一起列入待用户裁决项（03 文档 §9）。
