#include "PluginConfigStore.h"
#include "../Services/ConfigPath.h"
#include "../Services/JsonImportHelper.h"
#include <windows.h>
#include <fstream>
#include <map>

namespace
{
    std::string ToUtf8(const std::wstring& value)
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

    std::wstring EscapeJsonString(const std::wstring& value)
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

    std::wstring JoinPath(const std::wstring& dir, const std::wstring& file)
    {
        if (dir.empty())
            return file;
        if (file.empty())
            return dir;
        if (dir.back() == L'\\' || dir.back() == L'/')
            return dir + file;
        return dir + L"\\" + file;
    }
}

bool PluginConfigStore::IsSafeKey(const std::wstring& key)
{
    if (key.empty() || key.size() > 96)
        return false;
    for (wchar_t ch : key)
    {
        if ((ch >= L'a' && ch <= L'z') ||
            (ch >= L'A' && ch <= L'Z') ||
            (ch >= L'0' && ch <= L'9') ||
            ch == L'.' || ch == L'_' || ch == L'-')
        {
            continue;
        }
        return false;
    }
    return true;
}

std::wstring PluginConfigStore::GetConfigPath(const std::wstring& pluginDataDir)
{
    if (pluginDataDir.empty())
        return L"";
    return JoinPath(pluginDataDir, L"config.json");
}

bool PluginConfigStore::ReadValue(const std::wstring& pluginDataDir, const std::wstring& key, const std::wstring& defaultValue, std::wstring& value)
{
    if (!IsSafeKey(key))
        return false;

    value = defaultValue;
    JsonImport::JsonValue root = JsonImport::ParseJsonFile(GetConfigPath(pluginDataDir));
    if (root.type != JsonImport::JsonValue::Object)
        return true;

    if (auto* entry = root.Get(key); entry && entry->type == JsonImport::JsonValue::String)
        value = entry->stringValue;
    return true;
}

bool PluginConfigStore::WriteValue(const std::wstring& pluginDataDir, const std::wstring& key, const std::wstring& value)
{
    if (!IsSafeKey(key))
        return false;

    std::map<std::wstring, std::wstring> values;
    std::wstring path = GetConfigPath(pluginDataDir);
    JsonImport::JsonValue root = JsonImport::ParseJsonFile(path);
    if (root.type == JsonImport::JsonValue::Object)
    {
        for (const auto& [entryKey, entryValue] : root.objectValue)
        {
            if (IsSafeKey(entryKey) && entryValue.type == JsonImport::JsonValue::String)
                values[entryKey] = entryValue.stringValue;
        }
    }
    values[key] = value;

    std::wstring parent = path.substr(0, path.find_last_of(L"\\/"));
    ConfigPath::EnsureDirectoryExists(parent);
    std::wstring tempPath = path + L".tmp";
    DeleteFileW(tempPath.c_str());
    std::ofstream fs(tempPath, std::ios::binary | std::ios::trunc);
    if (!fs)
        return false;

    fs << "{\n";
    bool first = true;
    for (const auto& [entryKey, entryValue] : values)
    {
        if (!first)
            fs << ",\n";
        first = false;
        std::wstring line = L"  \"" + EscapeJsonString(entryKey) + L"\": \"" + EscapeJsonString(entryValue) + L"\"";
        fs << ToUtf8(line);
    }
    fs << "\n}\n";
    fs.close();
    if (fs.fail())
    {
        DeleteFileW(tempPath.c_str());
        return false;
    }
    if (MoveFileExW(tempPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return true;
    DeleteFileW(tempPath.c_str());
    return false;
}
