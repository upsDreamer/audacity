# 05 微基准（第二批）：soxr / pffft / DRC / TimeAndPitch

Status: resolved

## 任务

经用户裁决（2026-10-20：拉取真实 soxr 与 pffft 源码；第二批继续），完成四个基准：

1. bench_resample — `au3-math/Resample.cpp` + 真实 libsoxr 0.1.3（vendor 拉取）
2. bench_pffft — `au3-fft/PowerSpectrumGetter.cpp` + pffft（soxr 自带拷贝）对照 RealFFTf
3. bench_drc — `au3-dynamic-range-processor`（依赖仅 header-only MathApprox，拆得开）
4. bench_time_and_pitch — `au3-time-and-pitch` 全链（依赖 pffft + 微垫片）

每项：真实代码、先正确性验证后计时。

## 验收标准

- [x] 四基准编译通过、验证 PASS、出数
- [x] soxr 质量四档（QQ/LQ/HQ/VHQ）+ 变速率模式均实测
- [x] pffft 与 RealFFTf 同任务对照
- [x] 第三方来源与垫片增量记录于 benchmarks/README.md
- [x] 未修改 Audacity 源码

## 结果摘要

- soxr 恒率 2.5–7.1 ns/样本（QQ→VHQ）；**变速率模式 73.7 ns（13–29 倍）**
- pffft 功率谱 1.07–1.31 ns/样本，比 RealFFTf 路径快 ≈3.3 倍
- DRC 压缩器/限制器 ≈22.6/22.9 ns/样本（512 块立体声）
- TimeAndPitch 变速 61.5–62.1、变调 73.2 ns/样本

## Comments

- 2026-10-20 建立（用户裁决后）；同日完成。
- 遗留：Envelope（wx 绘图类同 TU）与 SqliteSampleBlock（DBConnection/WaveTrack 全链）判定拆不开，选项已写入 `docs/03-performance.md` §9 待用户裁决。
