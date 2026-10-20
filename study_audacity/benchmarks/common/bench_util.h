// 基准公共设施：确定性数据生成与 ns/样本 计时。
#pragma once

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

namespace bench
{

// 确定性伪随机数据：同一基准每次运行数据一致，便于横向对比。
inline std::vector<float> MakeSignal(size_t len, float amplitude = 0.5f)
{
    std::mt19937 rng { 20261020u };
    std::uniform_real_distribution<float> dist { -1.0f, 1.0f };
    std::vector<float> data(len);
    for (auto& v : data) {
        v = amplitude * dist(rng);
    }
    return data;
}

// 反复执行 fn 直到累计时长达到 minMs，返回每次操作的纳秒数。
template <typename F>
double NsPerOp(size_t opsPerIter, F&& fn, double minMs = 300.0)
{
    using clock = std::chrono::steady_clock;
    // 预热：让缓存/分支预测进入稳态
    for (int i = 0; i < 3; ++i) {
        fn();
    }
    const auto minDur = std::chrono::duration_cast<clock::duration>(
        std::chrono::duration<double, std::milli>(minMs));
    size_t iters = 1;
    auto total = clock::duration::zero();
    for (;;) {
        const auto t0 = clock::now();
        for (size_t i = 0; i < iters; ++i) {
            fn();
        }
        const auto t1 = clock::now();
        total = t1 - t0;
        if (total >= minDur) {
            break;
        }
        // 指数增加迭代数直到达到最小计时窗口
        iters *= 2;
    }
    const double ns = std::chrono::duration<double, std::nano>(total).count();
    return ns / (static_cast<double>(iters) * static_cast<double>(opsPerIter));
}

inline void Report(const char* name, double nsPerSample)
{
    std::printf("  %-46s %10.3f ns/样本\n", name, nsPerSample);
}

inline void Header(const char* name)
{
    std::printf("\n== %s ==\n", name);
}

} // namespace bench
