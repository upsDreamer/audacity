// 基准：Biquad 双二阶滤波（au3-math，真实代码）。
// 验证：Butterworth 低通级联的 DC 增益≈1、Nyquist 增益≈0；然后对 1M 样本计时。
// 注意：ArrayOf<Biquad> 继承 unique_ptr<T[]>，不带长度，节数须自行记录。
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Biquad.h"

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

// 把滤波器级联应用一遍（原地逐节处理）
void ApplyCascade(ArrayOf<Biquad>& chain, size_t nSections, const float* in, float* out, int len)
{
    if (out != in) {
        std::memcpy(out, in, sizeof(float) * len);
    }
    for (size_t s = 0; s < nSections; ++s) {
        chain[s].Process(out, out, len);
    }
}

} // namespace

int main()
{
    std::printf("bench_biquad — Biquad.cpp（真实 au3 源码，MSVC /O2）\n");

    // fn 语义 = 奈奎斯特频率（ScienFilterBase::CalcFilter 传 mNyquist，Biquad.cpp 内
    // fNorm = fc/fn 后 tan(PI*fNorm/2) 为标准双线性）。截止 4.8kHz@48kHz 采样率。
    const double FnNyquist = 48000.0 / 2.0;
    const double Fc = 4800.0;    // 截止频率
    const int Order = 8;
    const size_t nSections = size_t(Order / 2);

    bench::Header("正确性验证");
    auto chain = Biquad::CalcButterworthFilter(Order, FnNyquist, Fc, Biquad::kLowPass);
    Check(bool(chain), "CalcButterworthFilter 返回非空级联");

    // DC 增益：喂恒定 1.0，稳态输出趋近 1
    {
        const int N = 4096;
        std::vector<float> in(N, 1.0f), out(N);
        ApplyCascade(chain, nSections, in.data(), out.data(), N);
        const double dcGain = out[N - 1];
        Check(std::fabs(dcGain - 1.0) < 1e-3, "低通 DC 增益 -> 1");
    }
    // Nyquist 增益：喂 ±1 交替，稳态幅度趋近 0
    {
        const int N = 8192;
        std::vector<float> in(N), out(N);
        for (int i = 0; i < N; ++i) {
            in[i] = (i % 2 == 0) ? 1.0f : -1.0f;
        }
        ApplyCascade(chain, nSections, in.data(), out.data(), N);
        double tail = 0.0;
        for (int i = N - 256; i < N; ++i) {
            tail = std::max(tail, std::fabs(double(out[i])));
        }
        Check(tail < 1e-3, "低通 Nyquist 增益 -> 0");
    }
    // 8 阶低通在截止频率处增益 ≈ -3.01dB（Butterworth 特性）
    {
        const double w = 2.0 * kPi * Fc / 48000.0;
        const int N = 16384;
        std::vector<float> in(N), out(N);
        for (int i = 0; i < N; ++i) {
            in[i] = std::sin(w * i);
        }
        ApplyCascade(chain, nSections, in.data(), out.data(), N);
        double peak = 0.0;
        for (int i = N - 512; i < N; ++i) {
            peak = std::max(peak, std::fabs(double(out[i])));
        }
        const double gainDb = 20.0 * std::log10(peak);
        Check(std::fabs(gainDb + 3.0103) < 0.5, "截止频率处增益 ≈ -3.01 dB（Butterworth 特性）");
    }

    if (Failures) {
        std::printf("\n%zu 项验证失败，跳过计时。\n", Failures);
        return 1;
    }

    // ---------- 计时 ----------
    const int Len = 1 << 20;
    auto in = bench::MakeSignal(Len, 0.8f);
    std::vector<float> out(Len);

    bench::Header("Biquad::Process（1M 样本，时域逐样本 IIR，双精度状态）");
    bench::Report("1 节（单双二阶）",
        bench::NsPerOp(Len, [&] { chain[0].Process(in.data(), out.data(), Len); }));
    bench::Report("4 节级联（8 阶 Butterworth）",
        bench::NsPerOp(Len, [&] { ApplyCascade(chain, nSections, in.data(), out.data(), Len); }));

    std::printf("\n完成。\n");
    return 0;
}
