// 基准：RealFFTf 实数 FFT（au3-fft，真实代码）。
// 验证：正变换谱峰位置/幅度正确、逆变换往返误差达标；然后对 4096 点变换计时。
// 注意：pffft 路径（PowerSpectrumGetter）暂缺源码，待用户裁决后补充。
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>

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

} // namespace

int main()
{
    std::printf("bench_realfftf — RealFFTf.cpp（真实 au3 源码，MSVC /O2）\n");

    const size_t N = 4096;
    const double Rate = 48000.0;
    const size_t Bin = 100; // 100 * 48000/4096 ≈ 1172 Hz

    bench::Header("正确性验证");

    // 1) 单频正弦：谱峰在 Bin 处，幅度 ≈ A*N/2
    {
        std::vector<float> data(N);
        const double w = 2.0 * kPi * double(Bin) / double(N);
        for (size_t i = 0; i < N; ++i) {
            data[i] = float(std::cos(w * double(i)));
        }
        auto hFFT = GetFFT(N);
        RealFFTf(data.data(), hFFT.get());
        std::vector<float> re(N), im(N);
        ReorderToFreq(hFFT.get(), data.data(), re.data(), im.data());
        double bestMag = 0.0;
        size_t bestBin = 0;
        for (size_t k = 0; k <= N / 2; ++k) {
            const double mag = std::hypot(re[k], im[k]);
            if (mag > bestMag) {
                bestMag = mag;
                bestBin = k;
            }
        }
        Check(bestBin == Bin, "谱峰位于正确频点");
        Check(std::fabs(bestMag - N / 2.0) < N * 1e-3, "谱峰幅度 ≈ A*N/2");
    }

    // 2) 往返：真实路径 = RealFFTf → ReorderToFreq（顺序谱）→ 按 FFT.cpp::InverseRealFFT
    //    的打包约定回填 → InverseRealFFTf → ReorderToTime。逆变换原语吃顺序 bin，
    //    直接把正变换的位反序输出喂回去是错误用法（SpectrumTransformer 亦然）。
    {
        auto sig = bench::MakeSignal(N, 0.8f);
        std::vector<float> work(sig);
        std::vector<float> re(N / 2 + 1), im(N / 2 + 1);
        auto hFFT = GetFFT(N);
        RealFFTf(work.data(), hFFT.get());
        ReorderToFreq(hFFT.get(), work.data(), re.data(), im.data());
        std::vector<float> p(N, 0.0f);
        for (size_t k = 1; k < N / 2; ++k) {
            p[2 * k] = re[k];
            p[2 * k + 1] = im[k];
        }
        p[0] = re[0];
        p[1] = re[N / 2];
        InverseRealFFTf(p.data(), hFFT.get());
        std::vector<float> time(N);
        ReorderToTime(hFFT.get(), p.data(), time.data());
        float maxErr = 0.0f;
        for (size_t i = 0; i < N; ++i) {
            maxErr = std::max(maxErr, std::fabs(time[i] - sig[i]));
        }
        std::printf("  实测往返最大误差 = %.2e（float 精度，N=4096 约 12 级蝶形累加）\n", maxErr);
        Check(maxErr < 1e-3f, "正/逆变换往返最大误差 < 1e-3（float 精度）");
    }

    if (Failures) {
        std::printf("\n%zu 项验证失败，跳过计时。\n", Failures);
        return 1;
    }

    // ---------- 计时 ----------
    auto sig = bench::MakeSignal(N, 0.8f);
    auto hFFT = GetFFT(N);

    // 预备一份顺序谱（模拟"已拿到频谱数据"的逆变换入口）
    std::vector<float> work0(sig);
    RealFFTf(work0.data(), hFFT.get());
    std::vector<float> re0(N / 2 + 1), im0(N / 2 + 1);
    ReorderToFreq(hFFT.get(), work0.data(), re0.data(), im0.data());

    bench::Header("RealFFTf（4096 点 float，迭代基-2，惰性建表）");
    bench::Report("正变换 RealFFTf",
        bench::NsPerOp(N, [&] {
            std::vector<float> work(sig);
            RealFFTf(work.data(), hFFT.get());
        }, 100.0));
    bench::Report("逆变换路径（打包 + InverseRealFFTf + ReorderToTime）",
        bench::NsPerOp(N, [&] {
            std::vector<float> p(N, 0.0f);
            for (size_t k = 1; k < N / 2; ++k) {
                p[2 * k] = re0[k];
                p[2 * k + 1] = im0[k];
            }
            p[0] = re0[0];
            p[1] = re0[N / 2];
            InverseRealFFTf(p.data(), hFFT.get());
            std::vector<float> time(N);
            ReorderToTime(hFFT.get(), p.data(), time.data());
        }, 100.0));

    std::printf("\n完成。\n");
    return 0;
}
