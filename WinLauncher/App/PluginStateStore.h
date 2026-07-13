#pragma once

#include <map>
#include <string>

struct PluginState
{
    bool enabled = false;
    bool quarantined = false;
    int failureCount = 0;
    std::wstring lastError;
};

class PluginStateStore
{
public:
    explicit PluginStateStore(std::wstring stateDirectory);

    bool Load(std::map<std::wstring, PluginState>& states);
    bool Save(const std::map<std::wstring, PluginState>& states);
    void AppendError(const std::wstring& pluginId, const std::wstring& stage, const std::wstring& message);

private:
    std::wstring StateFilePath() const;
    static std::string ToUtf8(const std::wstring& value);
    static std::wstring EscapeJsonString(const std::wstring& value);

    std::wstring m_stateDirectory;
};
