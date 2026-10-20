// 基准：Dynamic Range Processor（au3-dynamic-range-processor，真实代码）。
// Compressor/Limiter 实时/离线共用的核心引擎，块 ≤512（与 RealtimeEffectState
// 子块粒度一致）。
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "CompressorProcessor.h"

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

} // namespace

int main()
{
    std::printf("bench_drc — CompressorProcessor.cpp（真实 au3 源码，MSVC /O2）\n");

    const int Rate = 48000;
    constexpr int Block = 512;
    constexpr int Channels = 2;

    bench::Header("正确性验证");

    // 压缩生效性：同一段响信号，ratio=∞ 的输出应显著小于 ratio=1（线性）
    // （默认设置含 makeup 补偿，直接看绝对增益没有意义）
    double linMs = 0.0, compMs = 0.0;
    {
        CompressorSettings cs;
        cs.makeupGainDb = 0; // 排除 makeup 干扰
        cs.compressionRatio = 1;
        CompressorProcessor lin { DynamicRangeProcessorSettings { cs } };
        lin.Init(Rate, Channels, Block);

        CompressorSettings cs2;
        cs2.makeupGainDb = 0;
        cs2.compressionRatio = std::numeric_limits<double>::infinity();
        CompressorProcessor comp { DynamicRangeProcessorSettings { cs2 } };
        comp.Init(Rate, Channels, Block);

        auto in = bench::MakeSignal(Block, 0.9f);
        std::vector<float> outL(Block), outR(Block);
        const int blocks = Rate / Block;
        for (int b = 0; b < blocks; ++b) {
            const float* inPtrs[Channels] = { in.data(), in.data() };
            float* outPtrs[Channels] = { outL.data(), outR.data() };
            lin.Process(inPtrs, outPtrs, Block);
            for (int i = 0; i < Block; ++i) {
                linMs += double(outL[i]) * outL[i];
            }
            comp.Process(inPtrs, outPtrs, Block);
            for (int i = 0; i < Block; ++i) {
                compMs += double(outL[i]) * outL[i];
            }
        }
    }
    bool finite = linMs > 0 && compMs > 0;
    Check(finite, "输出有限");
    Check(10.0 * std::log10(compMs / linMs) < -1.0,
          "ratio=∞ 比 ratio=1 输出显著更小（压缩生效）");

    // 阈值之上不压缩 + makeup 精确：静音级输入、threshold=0dB、makeup=6dB
    // → 输出 ≈ 输入 × 2（10^(6/20)）
    {
        CompressorSettings cs;
        cs.thresholdDb = 0;
        cs.makeupGainDb = 6;
        CompressorProcessor proc { DynamicRangeProcessorSettings { cs } };
        proc.Init(Rate, Channels, Block);
        std::vector<float> quiet(Block, 0.01f), outL(Block), outR(Block);
        const int blocks = Rate / Block;
        for (int b = 0; b < blocks; ++b) {
            const float* inPtrs[Channels] = { quiet.data(), quiet.data() };
            float* outPtrs[Channels] = { outL.data(), outR.data() };
            proc.Process(inPtrs, outPtrs, Block);
        }
        double inMs = 0.0, outMs = 0.0;
        for (int i = 0; i < Block; ++i) {
            inMs += double(quiet[i]) * quiet[i];
            outMs += double(outL[i]) * outL[i];
        }
        const double gainDb = 10.0 * std::log10(outMs / inMs);
        Check(std::fabs(gainDb - 6.0) < 0.5, "低于阈值时增益 = makeup（6dB 设置，实测 ≈6dB）");
    }

    if (Failures) {
        std::printf("\n%zu 项验证失败，跳过计时。\n", Failures);
        return 1;
    }

    // ---------- 计时 ----------
    bench::Header("CompressorProcessor::Process（512 样本块 × 立体声）");
    {
        DynamicRangeProcessorSettings settings { CompressorSettings {} };
        CompressorProcessor proc(settings);
        proc.Init(Rate, Channels, Block);
        auto in = bench::MakeSignal(Block, 0.8f);
        std::vector<float> outL(Block), outR(Block);
        const double ns = bench::NsPerOp(Block, [&] {
            const float* inPtrs[Channels] = { in.data(), in.data() };
            float* outPtrs[Channels] = { outL.data(), outR.data() };
            proc.Process(inPtrs, outPtrs, Block);
        });
        bench::Report("压缩器（默认设置，峰值检测+前瞻）", ns);
    }
    {
        CompressorSettings cs;
        cs.thresholdDb = limiterThresholdDbDefault;
        cs.makeupGainDb = 0;
        cs.kneeWidthDb = limiterKneeWidthDbDefault;
        cs.compressionRatio = std::numeric_limits<double>::infinity();
        cs.lookaheadMs = limiterLookaheadMsDefault;
        cs.attackMs = 0;
        cs.releaseMs = limiterReleaseMsDefault;
        DynamicRangeProcessorSettings settings { cs };
        CompressorProcessor proc(settings);
        proc.Init(Rate, Channels, Block);
        auto in = bench::MakeSignal(Block, 0.98f);
        std::vector<float> outL(Block), outR(Block);
        const double ns = bench::NsPerOp(Block, [&] {
            const float* inPtrs[Channels] = { in.data(), in.data() };
            float* outPtrs[Channels] = { outL.data(), outR.data() };
            proc.Process(inPtrs, outPtrs, Block);
        });
        bench::Report("限制器（LimiterSettings 等效参数）", ns);
    }

    std::printf("\n完成。\n");
    return 0;
}
