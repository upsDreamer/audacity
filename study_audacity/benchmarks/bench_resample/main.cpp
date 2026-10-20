// 基准：Resample 重采样（au3-math/Resample.cpp + 真实 libsoxr 0.1.3，用户裁决拉取）。
// 验证：44.1→48 恒率谱峰保持、变速率模式有限输出；然后按四种质量档计时
//（recipe "\0\1\4\6" → QQ/LQ/HQ/VHQ，经垫片的 g_enumSettingOverride 强制）。
// 块协议与 MixerSource 一致：1024 样本子块、输出缓冲 65536。
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "Resample.h"
#include "au3-preferences/Prefs.h" // 垫片：g_enumSettingOverride（质量档强制）

#include "../common/bench_util.h"

namespace {

constexpr double kInRate = 44100.0;
constexpr double kOutRate = 48000.0;
constexpr size_t kQueueLen = 65536;
constexpr size_t kBlock = 1024;

size_t Failures = 0;

void Check(bool ok, const char* what)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        ++Failures;
    }
}

// 模拟 MixerSource 的块协议：把 inLen 个输入样本全部喂进去，返回产出数
size_t DrainAll(Resample& resample, double factor, const float* in, size_t inLen, bool last,
                float* out, size_t outCap)
{
    size_t inPos = 0, outPos = 0;
    while (outPos < outCap) {
        auto [used, produced] = resample.Process(
            factor, in + inPos, inLen - inPos, last && inPos + kBlock >= inLen,
            out + outPos, outCap - outPos);
        if (produced == 0 && used == 0) {
            break;
        }
        inPos += used;
        outPos += produced;
        if (last && inPos >= inLen && produced < kBlock) {
            // 冲刷阶段继续拉到没有产出为止
        }
        if (inPos >= inLen && !last) {
            break;
        }
    }
    return outPos;
}

} // namespace

int main()
{
    std::printf("bench_resample — Resample.cpp + libsoxr 0.1.3（真实代码，MSVC /O2）\n");

    bench::Header("正确性验证");

    // 1) 44.1→48 恒率：1kHz 正弦，谱峰保持、长度按比率、无 NaN
    {
        g_enumSettingOverride = 3; // Best/VHQ
        const size_t N = size_t(kInRate); // 1 秒
        auto in = bench::MakeSignal(N, 0.5f);
        for (size_t i = 0; i < N; ++i) {
            in[i] = 0.5f * std::sinf(2.0f * float(M_PI) * 1000.0f * float(i) / float(kInRate));
        }
        Resample r(false, kOutRate / kInRate, kOutRate / kInRate);
        std::vector<float> out(kQueueLen);
        const size_t produced = DrainAll(r, kOutRate / kInRate, in.data(), N, true, out.data(), kQueueLen);
        const double expected = double(N) * kOutRate / kInRate;
        bool finite = true, nanFree = true;
        for (size_t i = 0; i < produced; ++i) {
            if (!std::isfinite(out[i])) {
                nanFree = false;
            }
        }
        double peak = 0.0;
        for (size_t i = 0; i < produced; ++i) {
            peak = std::max(peak, std::fabs(double(out[i])));
        }
        Check(nanFree, "输出无 NaN");
        Check(std::abs(double(produced) - expected) < 16.0, "输出长度符合采样率比");
        Check(std::fabs(peak - 0.5) < 0.05, "正弦峰值保持（≈0.5）");
        g_enumSettingOverride = -1;
    }

    // 2) 变速率模式（min≠max → SOXR_HQ|SOXR_VR）：有限输出、长度近似
    {
        Resample r(false, 0.9, 1.1);
        const size_t N = kBlock * 20;
        auto in = bench::MakeSignal(N, 0.5f);
        std::vector<float> out(kQueueLen);
        size_t produced = 0, inPos = 0;
        double factor = 0.95;
        while (inPos < N && produced < kQueueLen) {
            auto [used, got] = r.Process(factor, in.data() + inPos, std::min(kBlock, N - inPos),
                                         inPos + kBlock >= N, out.data() + produced, kQueueLen - produced);
            if (used == 0 && got == 0) {
                break;
            }
            inPos += used;
            produced += got;
            factor = factor < 1.0 ? 1.05 : 0.95;
        }
        bool finite = true;
        for (size_t i = 0; i < produced; ++i) {
            if (!std::isfinite(out[i])) {
                finite = false;
            }
        }
        Check(finite && produced > N / 2 && produced < N * 2, "变速率模式输出有限且量级合理");
    }

    if (Failures) {
        std::printf("\n%zu 项验证失败，跳过计时。\n", Failures);
        return 1;
    }

    // ---------- 计时：四种质量档 × 上/下行 ----------
    const size_t N = kInRate * 2; // 2 秒
    auto in = bench::MakeSignal(N, 0.5f);
    std::vector<float> out(kQueueLen);

    struct MethodSpec { int method; const char* name; };
    const MethodSpec methods[] = {
        { 0, "QQ(0)" }, { 1, "LQ(1)" }, { 2, "HQ(4)" }, { 3, "VHQ(6)" },
    };

    bench::Header("恒率重采样 44.1kHz -> 48kHz（1024 子块协议）");
    for (auto [method, name] : methods) {
        g_enumSettingOverride = method;
        Resample r(false, kOutRate / kInRate, kOutRate / kInRate);
        const double ns = bench::NsPerOp(N, [&] {
            DrainAll(r, kOutRate / kInRate, in.data(), N, true, out.data(), kQueueLen);
        }, 200.0);
        bench::Report(name, ns);
    }
    g_enumSettingOverride = -1;

    bench::Header("恒率重采样 48kHz -> 44.1kHz（1024 子块协议）");
    for (auto [method, name] : methods) {
        g_enumSettingOverride = method;
        Resample r(false, kInRate / kOutRate, kInRate / kOutRate);
        const double ns = bench::NsPerOp(N, [&] {
            DrainAll(r, kInRate / kOutRate, in.data(), N, true, out.data(), kQueueLen);
        }, 200.0);
        bench::Report(name, ns);
    }
    g_enumSettingOverride = -1;

    bench::Header("变速率模式（SOXR_HQ|SOXR_VR，包络式 factor 交替）");
    {
        Resample r(false, 0.9, 1.1);
        const double ns = bench::NsPerOp(N, [&] {
            size_t produced = 0, inPos = 0;
            double factor = 0.95;
            while (inPos < N && produced < kQueueLen) {
                auto [used, got] = r.Process(factor, in.data() + inPos,
                                             std::min(kBlock, N - inPos), inPos + kBlock >= N,
                                             out.data() + produced, kQueueLen - produced);
                if (used == 0 && got == 0) {
                    break;
                }
                inPos += used;
                produced += got;
                factor = factor < 1.0 ? 1.05 : 0.95;
            }
        }, 200.0);
        bench::Report("VR(HQ 基底)", ns);
    }

    std::printf("\n完成。\n");
    return 0;
}
