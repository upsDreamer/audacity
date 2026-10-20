# Audacity 音频处理微基准

独立 CMake 工程，直接编译仓库 `au3/` 中的真实 DSP 源文件，不修改 Audacity 源码与构建系统。

## 构建（Visual Studio 2022）

```bash
cmake -S . -B build -A x64
cmake --build build --config Release
```

## 运行

```bash
build/bin/Release/bench_sample_format.exe    # 逐样本格式转换 + 抖动
build/bin/Release/bench_biquad.exe           # 双二阶滤波
build/bin/Release/bench_ebur128.exe          # EBU R128 响度
build/bin/Release/bench_realfftf.exe         # 手写实数 FFT
build/bin/Release/bench_resample.exe         # soxr 重采样（恒率四档 + 变速率）
build/bin/Release/bench_pffft.exe            # pffft(SIMD) vs RealFFTf 功率谱对照
build/bin/Release/bench_drc.exe              # 压缩器/限制器引擎
build/bin/Release/bench_time_and_pitch.exe   # 变速/变调 clip 回放内核
build/bin/Release/bench_sqlite_block.exe     # SqliteSampleBlock 写/读路径（等价复刻）
python plot_envelope.py                      # Envelope 插值数据导出 + 绘图（用户裁决方案）
```

每个程序先跑正确性验证（PASS/FAIL，失败即退出码 1 并跳过计时），再输出 ns/样本。
`plot_envelope.py` 输出 `docs/img/envelope_interpolation.png` 与 `benchmarks/output/envelope_values.csv`（可选参数：控制点 CSV，每行 `time,gain`）。

实测环境：CMake 3.28 + VS2022（MSVC 19.42 /O2 x64），AMD Ryzen 7 5800H。

## 覆盖与来源

| 目标 | 被测源码（真实文件） | 外部依赖 |
|---|---|---|
| bench_sample_format | `au3-math/SampleFormat.cpp` + `Dither.cpp` | — |
| bench_biquad | `au3-math/Biquad.cpp` | — |
| bench_ebur128 | `au3-math/EBUR128.cpp` + `Biquad.cpp` | — |
| bench_realfftf | `au3-fft/RealFFTf.cpp` | — |
| bench_resample | `au3-math/Resample.cpp` | **soxr 0.1.3**（用户裁决 2026-10-20 网络拉取） |
| bench_pffft | `au3-fft/PowerSpectrumGetter.cpp` + `RealFFTf.cpp` | **pffft**（取 soxr 0.1.3 自带拷贝；与 au3-fft 上游所用为不同版本的同一经典库，API 同源） |
| bench_drc | `au3-dynamic-range-processor/CompressorProcessor.cpp` + `SimpleCompressor/*` | — |
| bench_time_and_pitch | `au3-time-and-pitch/` 全链（StaffPad 内核 + FormantShifter） | pffft |
| bench_sqlite_block | **等价复刻**（非真实代码）：按 `SqliteSampleBlock.cpp` 的 PRAGMA/表结构/summary 布局/INSERT 协议重写 | **sqlite3 3.45**（sqlite.org amalgamation，用户裁决 2026-10-20） |
| plot_envelope.py | **数据可视化**（非真实代码）：复刻 `Envelope::GetValues` 插值语义绘图 | matplotlib |

第三方源码：`vendor/dl/`（原始下载包）→ `vendor/soxr-0.1.3/`、`vendor/sqlite-amalgamation-3450100/`（解压）。下载地址：
- soxr: `https://github.com/chirlu/soxr/archive/refs/tags/0.1.3.tar.gz`（0.1.3 与 Audacity 上游一致）
- pffft: 使用 soxr 自带拷贝（`vendor/soxr-0.1.3/src/pffft.c`）。注：仓库 `thirdparty/pffft` 期望上游原始布局（pffft.c/h + pfsimd_macros.h，源码由 muse 子模块机制拉取，本机未检出）；hayguen fork 的现版布局不匹配，故用 soxr 拷贝
- sqlite: `https://www.sqlite.org/2024/sqlite-amalgamation-3450100.zip`。

## 垫片清单（环境代码，非算法）

`shim/` 提供被测源码引用的环境符号，**不包含任何算法逻辑**：

| 垫片 | 替代 | 说明 |
|---|---|---|
| `wx/defs.h` | wxWidgets 宏 | `wxASSERT`→`assert`、`wxT`→原样 |
| `wx/utils.h` | wxWidgets 宏 | `wxMax/wxMin`→std 等价；传递 wx/defs.h |
| `wx/thread.h` | wx 临界区 | `wxCriticalSection`→`std::mutex`（RealFFTf 惰性建表保护） |
| `wx/string.h` | wxString | 最小载体（ToStdString） |
| `au3-strings/Internat.h` | au3-strings | 最小 `TranslatableString`/`EnumValueSymbol`（后者双成员，兼容 Resample/Dither 两种初始化形态） |
| `au3-preferences/Prefs.h` | au3-preferences | 最小 `EnumSetting`（无偏好时返回构造默认值，与真实"用户未改偏好"行为一致）+ `gPrefs->Read`（返回调用方默认值）+ `g_enumSettingOverride`（仅基准强制质量档用） |
| `au3-files/FileNames.h` | au3-files | `ConfigDir()` 返回空路径 |
| `au3-components/ComponentInterface.h` | 空头 | Resample.cpp include 但未使用其中符号；遮蔽以切断真实 au3-strings（依赖 wx/longlong.h） |
| `pffft_decls.h`（/FI 注入） | — | 对齐分配等价实现（pffft.c 中被 #if 0）+ 将 pffft.c 并入消费 TU（soxr 版 pffft.h 原型为 static） |

被测路径原则：**算法代码零改动**；垫片只提供"无偏好/无环境"下的默认行为，且被测路径全部显式传参。

`bench_sqlite_block` 与 `plot_envelope.py` 为**等价复刻/可视化**（用户裁决 2026-10-20，因原代码依赖全链拆不开），已在文档中明确标注，不代表真实源码的逐指令性能。
