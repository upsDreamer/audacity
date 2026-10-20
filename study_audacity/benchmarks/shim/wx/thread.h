// 环境垫片：RealFFTf.cpp 只用 wxCriticalSection 保护惰性建表，
// 语义与 std::mutex 完全一致，直接映射。
#pragma once

#include <mutex>

using wxCriticalSection = std::mutex;
using wxCriticalSectionLocker = std::lock_guard<std::mutex>;
