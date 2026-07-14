#pragma once

#include <string>

// INI text primitives kept independent from file paths, watchers and UI.
class IniConfigDocument
{
public:
    static std::wstring Escape(const std::wstring& value);
    static std::wstring Unescape(const std::wstring& value);
};
