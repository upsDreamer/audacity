// 基准：SampleFormat 逐样本拷贝/格式转换 + Dither（au3-math，真实代码）。
// 模拟链路必经点：项目格式(int16/24/float) <-> 混音 float <-> 设备格式(int16)。
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "SampleFormat.h"
#include "Dither.h"

#include "../common/bench_util.h"

namespace {

size_t Failures = 0;

void Check(bool ok, const char* what)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        ++Failures;
    }
}

// 用已知值生成 int16 源
std::vector<char> MakeInt16(size_t len, float amplitude)
{
    std::vector<char> buf(len * SAMPLE_SIZE(int16Sample));
    std::vector<float> f(len);
    for (size_t i = 0; i < len; ++i) {
        f[i] = amplitude * std::sin(0.01 * double(i));
    }
    // float -> int16 用 none 抖动直拷
    CopySamples(reinterpret_cast<constSamplePtr>(f.data()), floatSample,
                buf.data(), int16Sample, len, DitherType::none);
    return buf;
}

} // namespace

int main()
{
    std::printf("bench_sample_format — SampleFormat.cpp / Dither.cpp（真实 au3 源码，MSVC /O2）\n");

    const size_t Len = 1u << 20; // 1M 样本

    InitDitherers();

    // ---------- 正确性验证 ----------
    bench::Header("正确性验证");

    // 1) SamplesToFloats: int16 -> float 精确
    {
        auto src = MakeInt16(1024, 0.9f);
        std::vector<float> dst(1024, -42.0f);
        SamplesToFloats(src.data(), int16Sample, dst.data(), 1024);
        bool ok = true;
        for (int i = 0; i < 1024; ++i) {
            if (std::fabs(dst[i]) > 1.0f) { ok = false; break; }
        }
        Check(ok, "SamplesToFloats(int16->float) 输出幅度有界");
    }

    // 2) float -> int16(none 抖动) -> float 往返误差 <= 0.5 LSB
    {
        auto f = bench::MakeSignal(65536, 0.5f);
        std::vector<char> q(65536 * SAMPLE_SIZE(int16Sample));
        CopySamples(reinterpret_cast<constSamplePtr>(f.data()), floatSample,
                    q.data(), int16Sample, 65536, DitherType::none);
        std::vector<float> back(65536);
        SamplesToFloats(q.data(), int16Sample, back.data(), 65536);
        double maxErr = 0.0;
        for (size_t i = 0; i < 65536; ++i) {
            maxErr = std::max(maxErr, std::fabs(double(back[i]) - double(f[i])));
        }
        const double lsb = 1.0f / 32767.0f;
        Check(maxErr <= 0.5 * lsb + 1e-9, "float->int16(none)->float 往返误差 <= 0.5 LSB");
    }

    // 3) shaped 抖动：误差有界且非零（确实在抖）
    {
        auto f = bench::MakeSignal(65536, 0.5f);
        std::vector<char> q(65536 * SAMPLE_SIZE(int16Sample));
        Dither d;
        d.Apply(DitherType::shaped,
                reinterpret_cast<constSamplePtr>(f.data()), floatSample,
                q.data(), int16Sample, 65536);
        std::vector<float> back(65536);
        SamplesToFloats(q.data(), int16Sample, back.data(), 65536);
        double maxErr = 0.0;
        double sumErr = 0.0;
        for (size_t i = 0; i < 65536; ++i) {
            const double e = back[i] - f[i];
            maxErr = std::max(maxErr, std::fabs(e));
            sumErr += e;
        }
        const double lsb = 1.0f / 32767.0f;
        Check(maxErr <= 16.0 * lsb, "shaped 抖动误差有界（<=16 LSB，噪声整形允许瞬时较大）");
        Check(std::fabs(sumErr) < 0.01, "shaped 抖动误差均值近零");
    }

    // 4) int24 往返
    {
        auto f = bench::MakeSignal(65536, 0.5f);
        std::vector<char> q(65536 * SAMPLE_SIZE(int24Sample));
        CopySamples(reinterpret_cast<constSamplePtr>(f.data()), floatSample,
                    q.data(), int24Sample, 65536, DitherType::none);
        std::vector<float> back(65536);
        SamplesToFloats(q.data(), int24Sample, back.data(), 65536);
        double maxErr = 0.0;
        for (size_t i = 0; i < 65536; ++i) {
            maxErr = std::max(maxErr, std::fabs(double(back[i]) - double(f[i])));
        }
        const double lsb = 1.0f / 8388607.0f;
        Check(maxErr <= 0.5 * lsb + 1e-9, "float->int24(none)->float 往返误差 <= 0.5 LSB");
    }

    if (Failures) {
        std::printf("\n%zu 项验证失败，跳过计时。\n", Failures);
        return 1;
    }

    // ---------- 计时 ----------
    auto f = bench::MakeSignal(Len);
    std::vector<char> dstNarrow(Len * SAMPLE_SIZE(int16Sample));
    std::vector<char> dst24(Len * SAMPLE_SIZE(int24Sample));
    std::vector<char> src16 = MakeInt16(Len, 0.5f);
    std::vector<float> dstF(Len);

    bench::Header("SamplesToFloats（窄格式 -> float，无抖动）");
    bench::Report("int16 -> float32",
        bench::NsPerOp(Len, [&] { SamplesToFloats(src16.data(), int16Sample, dstF.data(), Len); }));
    {
        // int24 源：借 CopySamples 生成
        CopySamples(reinterpret_cast<constSamplePtr>(f.data()), floatSample,
                    dst24.data(), int24Sample, Len, DitherType::none);
        bench::Report("int24 -> float32",
            bench::NsPerOp(Len, [&] { SamplesToFloats(dst24.data(), int24Sample, dstF.data(), Len); }));
    }
    bench::Report("float32 -> float32（纯拷贝下限）",
        bench::NsPerOp(Len, [&] { SamplesToFloats(reinterpret_cast<constSamplePtr>(f.data()), floatSample, dstF.data(), Len); }));

    bench::Header("CopySamples float32 -> int16（不同抖动算法）");
    for (const auto& [type, name] : {
            std::pair { DitherType::none, "dither=none（纯转换）" },
            { DitherType::rectangle, "dither=rectangle" },
            { DitherType::triangle, "dither=triangle" },
            { DitherType::shaped, "dither=shaped（噪声整形）" } }) {
        bench::Report(name,
            bench::NsPerOp(Len, [&] {
                CopySamples(reinterpret_cast<constSamplePtr>(f.data()), floatSample,
                            dstNarrow.data(), int16Sample, Len, type);
            }));
    }

    bench::Header("CopySamples 跨格式组合（shaped）");
    bench::Report("float32 -> int24",
        bench::NsPerOp(Len, [&] {
            CopySamples(reinterpret_cast<constSamplePtr>(f.data()), floatSample,
                        dst24.data(), int24Sample, Len, DitherType::shaped);
        }));
    bench::Report("int16 -> int24（收窄方向为 int24->int16 才抖动，此处无抖动）",
        bench::NsPerOp(Len, [&] {
            CopySamples(src16.data(), int16Sample,
                        dst24.data(), int24Sample, Len, DitherType::shaped);
        }));
    bench::Report("int24 -> int16（窄化，应用 shaped）",
        bench::NsPerOp(Len, [&] {
            CopySamples(dst24.data(), int24Sample,
                        dstNarrow.data(), int16Sample, Len, DitherType::shaped);
        }));

    std::printf("\n完成。\n");
    return 0;
}
