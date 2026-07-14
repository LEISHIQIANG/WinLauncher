#include "IniConfigDocument.h"

std::wstring IniConfigDocument::Escape(const std::wstring& value)
{
    std::wstring result;
    result.reserve(value.size());
    for (wchar_t ch : value)
    {
        switch (ch)
        {
        case L'\\': result += L"\\\\"; break;
        case L'\r': result += L"\\r"; break;
        case L'\n': result += L"\\n"; break;
        case L'\t': result += L"\\t"; break;
        default: result.push_back(ch); break;
        }
    }
    return result;
}

std::wstring IniConfigDocument::Unescape(const std::wstring& value)
{
    std::wstring result;
    result.reserve(value.size());
    bool escaping = false;
    for (wchar_t ch : value)
    {
        if (!escaping)
        {
            if (ch == L'\\') escaping = true;
            else result.push_back(ch);
            continue;
        }
        switch (ch)
        {
        case L'\\': result.push_back(L'\\'); break;
        case L'r': result.push_back(L'\r'); break;
        case L'n': result.push_back(L'\n'); break;
        case L't': result.push_back(L'\t'); break;
        default: result.push_back(L'\\'); result.push_back(ch); break;
        }
        escaping = false;
    }
    if (escaping) result.push_back(L'\\');
    return result;
}
