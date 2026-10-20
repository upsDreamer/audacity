// 环境垫片：TimeAndPitchExperimentalSettings.cpp 只用 FileNames::ConfigDir()
// 拼一个缓存目录字符串；基准环境没有用户配置目录，返回空路径即可。
#pragma once

#include <wx/string.h>

namespace FileNames
{
inline wxString ConfigDir()
{
    return wxString {};
}
} // namespace FileNames
