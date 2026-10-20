// 基准：StaffPadTimeAndPitch（au3-time-and-pitch，真实代码 + pffft）。
// 变速/变调 clip 的回放内核：相位锁定声码器（FFT 4096@44.1k，overlap=4）。
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "StaffPadTimeAndPitch.h"

#include "../common/bench_util.h"

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kRate = 44100.0;
constexpr size_t kChannels = 2;

size_t Failures = 0;

void Check(bool ok, const char* what)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        ++Failures;
    }
}

// 确定性正弦源：Pull 顺序给出立体声样本
class SineSource final : public TimeAndPitchSource
{
public:
    SineSource(double freq, double rate, size_t totalSamples, float amp = 0.5f)
        : mTotal(totalSamples)
        , mAmp(amp)
        , mOmega(float(2.0 * kPi * freq / rate))
    {}

    void Pull(float* const* buffers, size_t numSamples) override
    {
        for (size_t i = 0; i < numSamples; ++i) {
            const float s = (mPos < mTotal)
                ? mAmp * std::sinf(mOmega * float(mPos++))
                : 0.0f;
            buffers[0][i] = s;
            buffers[1][i] = s;
        }
    }

    size_t Consumed() const { return mPos; }

private:
    size_t mTotal;
    float mOmega;
    float mPos = 0;
    float mAmp;
};

} // namespace

int main()
{
    std::printf("bench_time_and_pitch — StaffPadTimeAndPitch.cpp（真实 au3 源码 + pffft，MSVC /O2）\n");

    const size_t Total = size_t(kRate) * 5; // 5 秒源

    bench::Header("正确性验证");

    // 1) 直通模式（ratio=1）：输出 == 输入
    {
        SineSource src(440.0, kRate, Total);
        TimeAndPitchInterface::Parameters params; // 全默认 1.0
        StaffPadTimeAndPitch proc(int(kRate), kChannels, src, params);
        std::vector<float> outL(1024), outR(1024);
        float* outPtrs[2] = { outL.data(), outR.data() };
        proc.GetSamples(outPtrs, 1024);
        // 与源的前 1024 样本比对
        SineSource ref(440.0, kRate, Total);
        std::vector<float> refL(1024), refR(1024);
        float* refPtrs[2] = { refL.data(), refR.data() };
        ref.Pull(refPtrs, 1024);
        float maxErr = 0.0f;
        for (size_t i = 0; i < 1024; ++i) {
            maxErr = std::max(maxErr, std::fabs(outL[i] - refL[i]));
        }
        Check(maxErr < 1e-3f, "直通模式输出与输入一致");
    }

    // 2) 2 倍拉伸：输出有限、源消耗 ≈ 一半
    {
        SineSource src(440.0, kRate, Total);
        TimeAndPitchInterface::Parameters params;
        params.timeRatio = 2.0;
        StaffPadTimeAndPitch proc(int(kRate), kChannels, src, params);
        std::vector<float> outL(1024), outR(1024);
        float* outPtrs[2] = { outL.data(), outR.data() };
        bool finite = true;
        size_t pulled = 0;
        for (int b = 0; b < 100; ++b) {
            proc.GetSamples(outPtrs, 1024);
            pulled += 1024;
            for (size_t i = 0; i < 1024; ++i) {
                if (!std::isfinite(outL[i])) {
                    finite = false;
                }
            }
        }
        Check(finite, "2x 拉伸输出有限");
        const double expected = double(pulled) / 2.0;
        const double consumed = double(src.Consumed());
        Check(std::fabs(consumed - expected) < expected * 0.1,
              "2x 拉伸源消耗 ≈ 输出的一半（STFT 延迟内）");
    }

    if (Failures) {
        std::printf("\n%zu 项验证失败，跳过计时。\n", Failures);
        return 1;
    }

    // ---------- 计时 ----------
    bench::Header("GetSamples（1024 样本块 × 立体声，44.1kHz，FFT 4096 / overlap 4）");
    for (const auto& [name, timeRatio, pitchRatio, formants] : {
            std::make_tuple("1.25x 变速（不变调）", 1.25, 1.0, false),
            std::make_tuple("2.0x 变速（不变调）", 2.0, 1.0, false),
            std::make_tuple("2.0x 变速 + 共振峰保持", 2.0, 1.0, true),
            std::make_tuple("1.0x + 上移 200 音分", 1.0, std::pow(2.0, 200.0 / 1200.0), false) }) {
        SineSource src(440.0, kRate, Total);
        TimeAndPitchInterface::Parameters params;
        params.timeRatio = timeRatio;
        params.pitchRatio = pitchRatio;
        params.preserveFormants = formants;
        StaffPadTimeAndPitch proc(int(kRate), kChannels, src, params);
        std::vector<float> outL(1024), outR(1024);
        const double ns = bench::NsPerOp(1024, [&] {
            float* outPtrs[2] = { outL.data(), outR.data() };
            proc.GetSamples(outPtrs, 1024);
        }, 200.0);
        bench::Report(name, ns);
    }

    std::printf("\n完成。\n");
    return 0;
}
