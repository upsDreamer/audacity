// 编译期强制注入（MSVC /FI）：soxr 版 pffft.h 把 API 原型声明为 static 且
// 对齐分配的定义整块 #if 0（soxr 构建自供）。消费方（PowerSpectrumGetter、
// FourierTransform_pffft）单独编译时两者都缺。
// 解法：pffft.c 并入本编译单元 + 此处提供等价的对齐分配实现
//（与 pffft.c 中被 #if 0 的经典实现一致：64 字节对齐，头部存原始指针）。
#pragma once

#include <cstdlib>
#include <cstdint>

// 64 字节对齐分配（等价于 pffft.c 中被 #if 0 的实现）
inline void* pffft_aligned_malloc(size_t nb_bytes)
{
    void* p0 = std::malloc(nb_bytes + 64);
    if (!p0) {
        return nullptr;
    }
    void* p = reinterpret_cast<void*>(
        (reinterpret_cast<uintptr_t>(p0) + 64) & ~uintptr_t(63));
    *(reinterpret_cast<void**>(p) - 1) = p0;
    return p;
}

inline void pffft_aligned_free(void* p)
{
    if (p) {
        std::free(*(reinterpret_cast<void**>(p) - 1));
    }
}

// pffft.c 并入本编译单元（其 API 原型在 soxr 版 pffft.h 中为 static）
#include "pffft.c"
