// 环境垫片：仅提供被测 au3 源码所需的环境宏，不包含任何算法逻辑。
// 用法说明见本目录 README.md 的"垫片清单"一节。
#pragma once

#include <cassert>

#define wxASSERT(x) assert(x)
#define wxASSERT_MSG(x, msg) assert(x)
#define wxT(x) x
#define wxT_NoLog(x) x
#define wxUnused(x) (void)(x)
#define WXUNUSED(x) x
