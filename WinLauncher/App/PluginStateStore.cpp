#include "PluginStateStore.h"
#include "../Services/ConfigPath.h"
#include "../Services/JsonImportHelper.h"
#include <Windows.h>
#include <fstream>

PluginStateStore::PluginStateStore(std::wstring stateDirectory)
    : m_stateDirectory(std::move(stateDirectory))
{
}

bool PluginStateStore::Load(std::map<std::wstring, PluginState>& states)
{
    states.clear();
    JsonImport::JsonValue root = JsonImport::ParseJsonFile(StateFilePath());
    if (root.type != JsonImport::JsonValue::Object)
        return false;

    const JsonImport::JsonValue* plugins = root.Get(L"plugins");
    if (!plugins || plugins->type != JsonImport::JsonValue::Array)
        return false;

    for (const auto& item : plugins->arrayValue)
    {
        if (item.type != JsonImport::JsonValue::Object)
            continue;

        std::wstring id = item.GetString(L"id");
        if (id.empty())
            continue;

        PluginState state;
        state.enabled = item.GetBool(L"enabled", false);
        state.quarantined = item.GetBool(L"quarantined", false);
        state.failureCount = item.GetInt(L"failureCount", 0);
        state.lastError = item.GetString(L"lastError");
        states[id] = state;
    }
    return true;
}

bool PluginStateStore::Save(const std::map<std::wstring, PluginState>& states)
{
    ConfigPath::EnsureDirectoryExists(m_stateDirectory);
    std::ofstream fs(StateFilePath(), std::ios::binary | std::ios::trunc);
    if (!fs)
        return false;

    fs << "{\n  \"plugins\": [\n";
    bool first = true;
    for (const auto& [id, state] : states)
    {
        if (!first)
            fs << ",\n";
        first = false;
        std::wstring line =
            L"    {\"id\":\"" + EscapeJsonString(id) +
            L"\",\"enabled\":" + (state.enabled ? L"true" : L"false") +
            L",\"quarantined\":" + (state.quarantined ? L"true" : L"false") +
            L",\"failureCount\":" + std::to_wstring(state.failureCount) +
            L",\"lastError\":\"" + EscapeJsonString(state.lastError) + L"\"}";
        fs << ToUtf8(line);
    }
    fs << "\n  ]\n}\n";
    return true;
}

void PluginStateStore::AppendError(const std::wstring& pluginId, const std::wstring& stage, const std::wstring& message)
{
    ConfigPath::EnsureDirectoryExists(m_stateDirectory);

    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t timeBuf[40]{};
    swprintf_s(timeBuf, L"%04u-%02u-%02uT%02u:%02u:%02u",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    std::wstring line =
        L"{\"time\":\"" + std::wstring(timeBuf) +
        L"\",\"pluginId\":\"" + EscapeJsonString(pluginId) +
        L"\",\"stage\":\"" + EscapeJsonString(stage) +
        L"\",\"message\":\"" + EscapeJsonString(message) + L"\"}\n";

    std::ofstream fs(ErrorLogPath(), std::ios::binary | std::ios::app);
    if (fs)
        fs << ToUtf8(line);
}

std::wstring PluginStateStore::StateFilePath() const
{
    return m_stateDirectory + L"\\plugins_state.json";
}

std::wstring PluginStateStore::ErrorLogPath() const
{
    return m_stateDirectory + L"\\plugin_errors.jsonl";
}

std::string PluginStateStore::ToUtf8(const std::wstring& value)
{
    if (value.empty())
        return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), (int)value.size(), nullptr, 0, nullptr, nullptr);
    if (len <= 0)
        return {};
    std::string result(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), (int)value.size(), &result[0], len, nullptr, nullptr);
    return result;
}

std::wstring PluginStateStore::EscapeJsonString(const std::wstring& value)
{
    std::wstring out;
    out.reserve(value.size());
    for (wchar_t ch : value)
    {
        switch (ch)
        {
        case L'\\': out += L"\\\\"; break;
        case L'"': out += L"\\\""; break;
        case L'\n': out += L"\\n"; break;
        case L'\r': out += L"\\r"; break;
        case L'\t': out += L"\\t"; break;
        default: out += ch; break;
        }
    }
    return out;
}
