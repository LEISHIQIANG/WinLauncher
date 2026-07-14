#pragma once

#include <string>

class ConfigFileStore
{
public:
    static std::wstring ReadUtf8(const std::wstring& path);
    static bool AtomicWriteUtf8(const std::wstring& path, const std::wstring& content, bool& changed);
    static bool IsPathUnderDirectory(const std::wstring& directory, const std::wstring& path);
};
