#pragma once
#include <string>

class PinyinHelper
{
public:
    // Get pinyin initials (e.g. L"微信" -> L"wx", L"计算器" -> L"jsq")
    static std::wstring GetInitials(const std::wstring& text);

    // Get full pinyin (e.g. L"微信" -> L"weixin", L"计算器" -> L"jisuanqi")
    static std::wstring GetFullPinyin(const std::wstring& text);

    // Matches query against text (or text's pinyin initials or full pinyin)
    // returns true if matched, and sets flags
    static bool Match(const std::wstring& text, const std::wstring& queryLower, bool& isInitialsMatch, bool& isFullPinyinMatch);
};
