// 环境垫片：Dither.cpp / Resample.cpp 需要完整的 EnumSetting 定义来构造静态
// 设置对象；TimeAndPitchExperimentalSettings.cpp 需要 gPrefs->Read。
// 真实实现读写全局偏好文件；这里实现"无偏好存储时的默认值语义"——
// 构造时记录默认项，ReadEnum() 返回默认项。这与真实代码在"用户未改过偏好"
// 时的行为一致（Dither Fast=none/Best=shaped；Resample 两把钥匙默认均为 Best）。
// 被测基准路径全部显式传参，不经过这些对象；g_enumSettingOverride 仅供
// 基准强制选择质量档位（0..3），-1 表示不干预。
#pragma once

#include <initializer_list>
#include <vector>

#include <wx/defs.h>
#include "au3-strings/Internat.h" // TranslatableString, EnumValueSymbol

// 基准强制覆盖：>=0 时所有 EnumSetting::ReadEnum 返回该值（见 bench_resample）
inline int g_enumSettingOverride = -1;

// 最小偏好读接口：始终返回调用方给的默认值（与"未设置过偏好"一致）
struct ShimFileConfig
{
    template<typename T>
    bool Read(const char* /*key*/, T* out, T def) const
    {
        if (out) {
            *out = def;
        }
        return true;
    }
};

inline ShimFileConfig gPrefsObj;
// 真实代码经 gPrefs-> 访问，此处保持指针形态
inline ShimFileConfig* const gPrefs = &gPrefsObj;

class EnumSettingBase
{
protected:
    explicit EnumSettingBase(int defaultIndex)
        : mDefaultIndex(defaultIndex)
    {}

    int mDefaultIndex;
};

template <typename Enum>
class EnumSetting : public EnumSettingBase
{
public:
    // 与真实签名对应：键名、可选值符号表、默认下标、枚举值表（迁移用旧键）
    EnumSetting(const char* /*key*/, std::initializer_list<EnumValueSymbol> /*choices*/,
                int defaultIndex, std::initializer_list<Enum> intChoices,
                const char* /*oldKey*/ = nullptr)
        : EnumSettingBase(defaultIndex)
        , mIntChoices(intChoices)
    {}

    Enum ReadEnum() const
    {
        if (g_enumSettingOverride >= 0) {
            return static_cast<Enum>(g_enumSettingOverride);
        }
        return mIntChoices[mDefaultIndex];
    }

private:
    std::vector<Enum> mIntChoices;
};
