// 环境垫片：被测源码只把 Internat.h 当作 TranslatableString/EnumValueSymbol 的
// 提供者。这里给出最小值语义（保留原字符串指针），不做任何翻译/格式化。
// EnumValueSymbol 双成员（标识符 + 标签）与真实结构对应。
#pragma once

#include <cstddef>
#include <string>

#include <wx/defs.h> // wxASSERT 等宏（真实 Internat.h 也会传递引入）

class TranslatableString
{
public:
    TranslatableString() = default;
    TranslatableString(const char* /*context*/, const char* s, const char* /*ref*/ = nullptr)
        : mStr(s)
    {}

    const char* Str() const { return mStr; }

private:
    const char* mStr = nullptr;
};

// 兼容两种初始化形态的标识符：{ wxT("key"), TS(...) }（Resample）与
// { TS(...) }（Dither）。真实类型为 au3-preferences 中的 Identifier。
struct EnumValueSymbolIdentifier
{
    EnumValueSymbolIdentifier(const char* s)
        : str(s)
    {}
    EnumValueSymbolIdentifier(const TranslatableString& t)
        : str(t.Str() ? t.Str() : "")
    {}

    std::string str;
};

struct EnumValueSymbol
{
    EnumValueSymbolIdentifier Identifier;
    TranslatableString Str;
};
