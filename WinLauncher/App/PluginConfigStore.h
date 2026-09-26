#pragma once

#include <string>

class PluginConfigStore
{
public:
    static bool IsSafeKey(const std::wstring& key);
    static std::wstring GetConfigPath(const std::wstring& pluginDataDir);
    static bool ReadValue(const std::wstring& pluginDataDir, const std::wstring& key, const std::wstring& defaultValue, std::wstring& value);
    static bool WriteValue(const std::wstring& pluginDataDir, const std::wstring& key, const std::wstring& value);
};
