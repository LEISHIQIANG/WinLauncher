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
    std::string content = "{\n  \"plugins\": [\n";
    bool first = true;
    for (const auto& [id, state] : states)
    {
        if (!first) content += ",\n";
        first = false;
        std::wstring line =
            L"    {\"id\":\"" + EscapeJsonString(id) +
            L"\",\"enabled\":" + (state.enabled ? L"true" : L"false") +
            L",\"quarantined\":" + (state.quarantined ? L"true" : L"false") +
            L",\"failureCount\":" + std::to_wstring(state.failureCount) +
            L",\"lastError\":\"" + EscapeJsonString(state.lastError) + L"\"}";
        content += ToUtf8(line);
    }
    content += "\n  ]\n}\n";

    const std::wstring statePath = StateFilePath();
    std::ifstream existing(statePath, std::ios::binary);
    std::string existingContent((std::istreambuf_iterator<char>(existing)), {});
    if (existingContent == content) return true;
    const std::wstring tempPath = statePath + L".tmp";
    DeleteFileW(tempPath.c_str());
    std::ofstream fs(tempPath, std::ios::binary | std::ios::trunc);
    if (!fs)
        return false;
    fs.write(content.data(), static_cast<std::streamsize>(content.size()));
    fs.close();
    if (MoveFileExW(tempPath.c_str(), statePath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return true;
    DeleteFileW(tempPath.c_str());
    return false;
}

void PluginStateStore::AppendError(const std::wstring& pluginId, const std::wstring& stage, const std::wstring& message)
{
    // PluginManager emits the same failure to the central JSONL logger.  Keep
    // the last error in plugins_state.json and remove the old unbounded side log.
    (void)pluginId; (void)stage; (void)message;
    DeleteFileW((m_stateDirectory + L"\\plugin_errors.jsonl").c_str());
}

std::wstring PluginStateStore::StateFilePath() const { return m_stateDirectory + L"\\plugins_state.json"; }

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
