# Audacity 音频处理微基准（第一批+第二批）
# 独立工程：直接编译 au3/ 真实源码 + 环境垫片 + 用户裁决拉取的第三方源码；不改动 Audacity 源码。
cmake_minimum_required(VERSION 3.20)
project(audacity_study_benchmarks C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(AU3_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../au3)
set(MATH ${AU3_DIR}/libraries/au3-math)
set(FFT ${AU3_DIR}/libraries/au3-fft)
set(SHIM ${CMAKE_CURRENT_SOURCE_DIR}/shim)

# 环境垫片 + au3 头文件根。垫片目录排最前，wx/* 与 au3-*/Prefs.h 等解析到垫片。
add_library(bench_env INTERFACE)
target_include_directories(bench_env INTERFACE
    ${SHIM}
    ${AU3_DIR}/libraries
    ${MATH}
    ${FFT}
)
# 这些 *_API 宏在真实构建中由导出配置生成；静态/单二进制场景下为空。
# _USE_MATH_DEFINES：真实构建（au3defs.cmake）为 MSVC 全局提供，源码用到 M_PI。
target_compile_definitions(bench_env INTERFACE
    MATH_API=
    FFT_API=
    UTILITY_API=
    DYNAMIC_RANGE_PROCESSOR_API=
    TIME_AND_PITCH_API=
    _USE_MATH_DEFINES
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)
if(MSVC)
    target_compile_options(bench_env INTERFACE /utf-8 /EHsc /W3)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ---- bench_sample_format：SampleFormat.cpp + Dither.cpp ----
add_executable(bench_sample_format
    bench_sample_format/main.cpp
    ${MATH}/SampleFormat.cpp
    ${MATH}/Dither.cpp
)
target_link_libraries(bench_sample_format PRIVATE bench_env)

# ---- bench_biquad：Biquad.cpp ----
add_executable(bench_biquad
    bench_biquad/main.cpp
    ${MATH}/Biquad.cpp
)
target_link_libraries(bench_biquad PRIVATE bench_env)

# ---- bench_ebur128：EBUR128.cpp（依赖 Biquad）----
add_executable(bench_ebur128
    bench_ebur128/main.cpp
    ${MATH}/EBUR128.cpp
    ${MATH}/Biquad.cpp
)
target_link_libraries(bench_ebur128 PRIVATE bench_env)

# ---- bench_realfftf：RealFFTf.cpp（wx/thread.h -> std::mutex 垫片）----
add_executable(bench_realfftf
    bench_realfftf/main.cpp
    ${FFT}/RealFFTf.cpp
)
target_link_libraries(bench_realfftf PRIVATE bench_env)

# ================= 第二批（用户裁决 2026-10-20：拉取真实 soxr/pffft 源码） =================

set(VENDOR ${CMAKE_CURRENT_SOURCE_DIR}/vendor)
set(PFFFT_DIR ${VENDOR}/soxr-0.1.3/src)  # soxr 自带的经典布局 pffft 拷贝（见 README）
set(TAP ${AU3_DIR}/libraries/au3-time-and-pitch)
set(DRC ${AU3_DIR}/libraries/au3-dynamic-range-processor)

# soxr 0.1.3（真实库，来自 vendor/dl 下载包的解压）
set(BUILD_SHARED_LIBS OFF)
set(BUILD_TESTS OFF)
set(BUILD_EXAMPLES OFF)
set(BUILD_UTILS OFF)
set(WITH_OPENMP OFF)
set(WITH_LSR_BINDINGS OFF)
list(APPEND CMAKE_MODULE_PATH ${VENDOR}/soxr-0.1.3/cmake/Modules)
add_subdirectory(${VENDOR}/soxr-0.1.3 soxr_build EXCLUDE_FROM_ALL)

# ---- bench_resample：Resample.cpp + 真实 soxr ----
add_executable(bench_resample
    bench_resample/main.cpp
    ${MATH}/Resample.cpp
)
target_include_directories(bench_resample PRIVATE ${VENDOR}/soxr-0.1.3/src)
target_link_libraries(bench_resample PRIVATE bench_env soxr)

# ---- bench_pffft：PowerSpectrumGetter + pffft（SIMD）对照 RealFFTf ----
# pffft.c 经 /FI 并入各消费 TU（soxr 版 pffft.h 的 API 原型为 static）
add_executable(bench_pffft
    bench_pffft/main.cpp
    ${FFT}/PowerSpectrumGetter.cpp
    ${FFT}/RealFFTf.cpp
)
target_include_directories(bench_pffft PRIVATE ${PFFFT_DIR})
target_compile_options(bench_pffft PRIVATE /FI${SHIM}/pffft_decls.h)
target_link_libraries(bench_pffft PRIVATE bench_env)

# ---- bench_drc：CompressorProcessor + SimpleCompressor ----
add_executable(bench_drc
    bench_drc/main.cpp
    ${DRC}/CompressorProcessor.cpp
    ${DRC}/SimpleCompressor/GainReductionComputer.cpp
    ${DRC}/SimpleCompressor/LookAheadGainReduction.cpp
)
target_include_directories(bench_drc PRIVATE ${DRC})
target_link_libraries(bench_drc PRIVATE bench_env)

# ---- bench_time_and_pitch：StaffPadTimeAndPitch 全链 + pffft ----
add_executable(bench_time_and_pitch
    bench_time_and_pitch/main.cpp
    ${TAP}/StaffPadTimeAndPitch.cpp
    ${TAP}/TimeAndPitchInterface.cpp
    ${TAP}/TimeAndPitchExperimentalSettings.cpp
    ${TAP}/FormantShifter.cpp
    ${TAP}/FormantShifterLogger.cpp
    ${TAP}/DummyFormantShifterLogger.cpp
    ${TAP}/AudioContainer.cpp
    ${TAP}/StaffPad/TimeAndPitch.cpp
    ${TAP}/StaffPad/FourierTransform_pffft.cpp
)
target_include_directories(bench_time_and_pitch PRIVATE ${TAP} ${PFFFT_DIR})
target_compile_options(bench_time_and_pitch PRIVATE /FI${SHIM}/pffft_decls.h)
target_link_libraries(bench_time_and_pitch PRIVATE bench_env)

# ---- bench_sqlite_block：SqliteSampleBlock 写/读路径等价复刻（用户裁决）----
add_executable(bench_sqlite_block
    bench_sqlite_block/main.cpp
    ${VENDOR}/sqlite-amalgamation-3450100/sqlite3.c
)
target_include_directories(bench_sqlite_block PRIVATE ${VENDOR}/sqlite-amalgamation-3450100)
target_link_libraries(bench_sqlite_block PRIVATE bench_env)
