// 环境垫片：Biquad.cpp 只用到 wxMax/wxMin 两个宏。
#pragma once

#include <algorithm>

#include <wx/defs.h> // wxASSERT 等宏（真实 wx/utils.h 也会传递引入）

template <typename T>
constexpr const T& wxMax(const T& a, const T& b) { return a > b ? a : b; }

template <typename T>
constexpr const T& wxMin(const T& a, const T& b) { return a < b ? a : b; }
