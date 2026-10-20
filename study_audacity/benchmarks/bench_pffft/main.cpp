// 基准：PowerSpectrumGetter（au3-fft + pffft SIMD，用户裁决拉源码）。
// 对象：RealFFTf（手写迭代基-2）vs pffft（SIMD）两条功率谱路径。
// pffft 源取自 soxr 0.1.3 自带拷贝（与 au3-fft 上游所用为不同版本的同一经典库，
// API 同源；README 有说明）。
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "PowerSpectrumGetter.h"
#include "RealFFTf.h"

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

// RealFFTf 路径的功率谱（等价 FFT.cpp::PowerSpectrum 的核心）
void PowerViaRealFFTf(const float* in, size_t N, float* out)
{
    auto hFFT = GetFFT(N);
    std::vector<float> work(in, in + N);
    RealFFTf(work.data(), hFFT.get());
    std::vector<float> re(N / 2 + 1), im(N / 2 + 1);
    ReorderToFreq(hFFT.get(), work.data(), re.data(), im.data());
    for (size_t k = 0; k <= N / 2; ++k) {
        out[k] = re[k] * re[k] + im[k] * im[k];
    }
}

} // namespace

int main()
{
    std::printf("bench_pffft — PowerSpectrumGetter.cpp + pffft.c（SIMD，MSVC /O2）\n");

    const size_t N = 4096;
    const double Rate = 48000.0;
    const size_t Bin = 100;

    bench::Header("正确性验证");

    // 1) 1kHz 正弦功率谱峰位置一致
    std::vector<float> sig(N);
    {
        const double w = 2.0 * kPi * double(Bin) / double(N);
        for (size_t i = 0; i < N; ++i) {
            sig[i] = float(std::cos(w * double(i)));
        }
        PowerSpectrumGetter getter{ static_cast<int>(N) }; // 花括号避免最令人头痛的解析
        PffftFloatVector buf(sig.begin(), sig.end());
        PffftFloatVector pout(size_t(N / 2 + 1));
        getter(buf.aligned(), pout.aligned());
        size_t best = 0;
        float bestV = 0.0f;
        for (size_t k = 0; k <= N / 2; ++k) {
            if (pout[k] > bestV) {
                bestV = pout[k];
                best = k;
            }
        }
        Check(best == Bin, "pffft 功率谱峰位于正确频点");

        // 2) 与 RealFFTf 路径的数值一致性（相对差 < 1e-3）
        std::vector<float> rout(size_t(N / 2 + 1));
        PowerViaRealFFTf(sig.data(), N, rout.data());
        double maxRelDiff = 0.0;
        for (size_t k = 0; k <= N / 2; ++k) {
            const double denom = std::max(1.0, double(rout[k]));
            maxRelDiff = std::max(maxRelDiff, std::fabs(double(pout[k]) - rout[k]) / denom);
        }
        Check(maxRelDiff < 1e-3, "pffft 与 RealFFTf 功率谱相对差 < 1e-3");
    }

    if (Failures) {
        std::printf("\n%zu 项验证失败，跳过计时。\n", Failures);
        return 1;
    }

    // ---------- 计时 ----------
    bench::Header("功率谱（正变换→逐 bin 平方和）");
    for (const size_t size : { size_t(1024), size_t(2048), size_t(4096) }) {
        std::vector<float> data(size);
        const double w = 2.0 * kPi * 997.0 / Rate;
        for (size_t i = 0; i < size; ++i) {
            data[i] = float(std::sin(w * double(i)));
        }
        PowerSpectrumGetter getter{ static_cast<int>(size) };
        PffftFloatVector buf(size);
        PffftFloatVector pout(size / 2 + 1);
        const double nsPffft = bench::NsPerOp(size, [&] {
            std::copy(data.begin(), data.end(), buf.aligned().get());
            getter(buf.aligned(), pout.aligned());
        }, 100.0);
        bench::Report((std::string("pffft   N=") + std::to_string(size)).c_str(), nsPffft);

        const double nsHand = bench::NsPerOp(size, [&] {
            std::vector<float> out(size / 2 + 1);
            PowerViaRealFFTf(data.data(), size, out.data());
        }, 100.0);
        bench::Report((std::string("RealFFTf N=") + std::to_string(size)).c_str(), nsHand);
    }

    std::printf("\n完成。\n");
    return 0;
}
