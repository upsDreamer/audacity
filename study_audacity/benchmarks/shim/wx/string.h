// 环境垫片：最小 wxString——只为 FileNames::ConfigDir().ToStdString() 这类
// 链式调用提供载体，不模拟任何 wx 语义。
#pragma once

#include <string>

class wxString
{
public:
    wxString() = default;
    wxString(const char* s)
        : mStr(s)
    {}
    wxString(const std::string& s)
        : mStr(s)
    {}

    std::string ToStdString() const { return mStr; }

private:
    std::string mStr;
};
