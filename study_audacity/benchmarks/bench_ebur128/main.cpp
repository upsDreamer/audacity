// 基准：EBUR128 响度测量（au3-math，真实代码）。
// 验证：997Hz -23dBFS 立体声正弦的整合响度 ≈ -23 LUFS（EBU R128 规范测试条件）；
// 然后对 10s 立体声流计时。
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "EBUR128.h"

#include "../common/bench_util.h"

namespace {

constexpr double kPi = 3.14159265358979323846;

size_t Failures = 0;

void Check(bool ok, const char* what)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        ++Failures;
    }
}

} // namespace

int main()
{
    std::printf("bench_ebur128 — EBUR128.cpp（真实 au3 源码，MSVC /O2）\n");

    const double Rate = 48000.0;
    const size_t Channels = 2;

    bench::Header("正确性验证（EBU R128 规范条件）");

    // 997Hz、-23 dBFS（每通道）立体声正弦 → 整合响度应约 -23 LUFS。
    // 容差放宽到 ±0.5 LU：K-filter 系数近似 + 门限/块边界效应。
    {
        const double durSec = 20.0;
        const size_t N = size_t(durSec * Rate);
        const double amp = std::pow(10.0, -23.0 / 20.0); // -23 dBFS
        const double w = 2.0 * kPi * 997.0 / Rate;

        EBUR128 meter(Rate, Channels);
        for (size_t i = 0; i < N; ++i) {
            const float s = float(amp * std::sin(w * double(i)));
            meter.ProcessSampleFromChannel(s, 0);
            meter.ProcessSampleFromChannel(s, 1);
            meter.NextSample();
        }
        const double lu = meter.IntegrativeLoudnessToLUFS(meter.IntegrativeLoudness());
        std::printf("  实测整合响度 = %.2f LUFS（期望 -23）\n", lu);
        Check(std::fabs(lu - (-23.0)) < 0.5, "997Hz -23dBFS 立体声 → ≈ -23 LUFS");
    }

    // 静音 → 低于绝对门限，输出 -inf 或极小值
    {
        EBUR128 meter(Rate, Channels);
        const size_t N = size_t(5 * Rate);
        for (size_t i = 0; i < N; ++i) {
            meter.ProcessSampleFromChannel(0.0f, 0);
            meter.ProcessSampleFromChannel(0.0f, 1);
            meter.NextSample();
        }
        const double loud = meter.IntegrativeLoudness();
        Check(loud <= 0.0 || loud < 1e-12, "静音 → 无有效响度（低于绝对门限）");
    }

    if (Failures) {
        std::printf("\n%zu 项验证失败，跳过计时。\n", Failures);
        return 1;
    }

    // ---------- 计时 ----------
    const size_t Len = size_t(10 * Rate); // 10 秒
    auto mono = bench::MakeSignal(Len, 0.2f);

    bench::Header("EBUR128 处理（10s 立体声 48kHz，K 加权 + 直方图）");
    {
        const double ns = bench::NsPerOp(Len, [&] {
            EBUR128 meter(Rate, Channels);
            for (size_t i = 0; i < Len; ++i) {
                meter.ProcessSampleFromChannel(mono[i], 0);
                meter.ProcessSampleFromChannel(mono[i], 1);
                meter.NextSample();
            }
            meter.IntegrativeLoudness();
        });
        bench::Report("立体声逐样本处理（含求响度收尾）", ns);
    }
    {
        // 单通道（声道减半的参照）
        const double ns = bench::NsPerOp(Len, [&] {
            EBUR128 meter(Rate, 1);
            for (size_t i = 0; i < Len; ++i) {
                meter.ProcessSampleFromChannel(mono[i], 0);
                meter.NextSample();
            }
            meter.IntegrativeLoudness();
        });
        bench::Report("单通道参照", ns);
    }

    std::printf("\n完成。\n");
    return 0;
}
