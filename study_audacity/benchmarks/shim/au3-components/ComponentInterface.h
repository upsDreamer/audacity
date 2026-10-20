// 环境垫片：Resample.cpp include 了 ComponentInterface.h 但未使用其中任何符号；
// 真实头会拖入真实 au3-strings（其 Internat.h 依赖 wx/longlong.h）。
// 这里以空头遮蔽，切断该依赖链。
#pragma once
