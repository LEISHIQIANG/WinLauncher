#include "PluginManifest.h"
#include "../Services/JsonImportHelper.h"
#include <Windows.h>
#include <algorithm>
#include <cwctype>
#include <sstream>

namespace
{
    std::wstring ParentDirectory(const std::wstring& path)
    {
        size_t pos = path.find_last_of(L"\\/");
        if (pos == std::wstring::npos)
            return L"";
        return path.substr(0, pos);
    }

    std::wstring JoinPath(const std::wstring& base, const std::wstring& rel)
    {
        if (base.empty()) return rel;
        if (base.back() == L'\\' || base.back() == L'/')
            return base + rel;
        return base + L"\\" + rel;
    }

    bool FileExists(const std::wstring& path)
    {
        DWORD attrs = GetFileAttributesW(path.c_str());
        return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
    }

    bool IsKnownPermission(const std::wstring& permission)
    {
        static const wchar_t* kKnown[] = {
            L"app.info",
            L"log.write",
            L"plugin.config.read",
            L"plugin.config.write",
            L"clipboard.read",
            L"clipboard.write",
            L"open.url",
            L"open.file",
            L"file.read",
            L"file.write",
            L"ui.input",
            L"ui.filepick",
            L"ui.notify",
            L"network.request",
            L"process.run",
            L"ui.settings",
            L"background.worker"
        };
        for (const wchar_t* known : kKnown)
        {
            if (permission == known)
                return true;
        }
        return false;
    }

    bool IsValidSlashName(const std::wstring& value)
    {
        if (value.empty() || value.size() > 48)
            return false;
        for (wchar_t ch : value)
        {
            if ((ch >= L'a' && ch <= L'z') ||
                (ch >= L'0' && ch <= L'9') ||
                ch == L'-')
            {
                continue;
            }
            return false;
        }
        return true;
    }

    bool IsSafeRelativeAssetPath(const std::wstring& path)
    {
        if (path.empty())
            return true;
        if (path.find(L":") != std::wstring::npos)
            return false;
        if (path.rfind(L"\\\\", 0) == 0)
            return false;
        if (path.find(L"..") != std::wstring::npos)
            return false;
        if (path.front() == L'\\' || path.front() == L'/')
            return false;
        return true;
    }

    bool ReadStringArray(const JsonImport::JsonValue& object, const std::wstring& key, std::vector<std::wstring>& out, std::wstring& error)
    {
        auto* values = object.Get(key);
        if (!values)
            return true;
        if (values->type != JsonImport::JsonValue::Array)
        {
            error = key + L" 必须是字符串数组";
            return false;
        }
        for (const auto& value : values->arrayValue)
        {
            if (value.type != JsonImport::JsonValue::String || value.stringValue.empty())
            {
                error = key + L" 中存在无效字符串";
                return false;
            }
            out.push_back(value.stringValue);
        }
        return true;
    }

    bool IsValidSettingKey(const std::wstring& key)
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

    std::wstring JsonValueToConfigString(const JsonImport::JsonValue& value)
    {
        switch (value.type)
        {
        case JsonImport::JsonValue::String:
            return value.stringValue;
        case JsonImport::JsonValue::Bool:
            return value.boolValue ? L"true" : L"false";
        case JsonImport::JsonValue::Number:
        {
            double number = value.numberValue;
            if (number == (long long)number)
                return std::to_wstring((long long)number);
            std::wostringstream ss;
            ss << number;
            return ss.str();
        }
        default:
            return L"";
        }
    }
}

bool PluginManifestReader::LoadFromFile(const std::wstring& manifestPath, PluginManifest& outManifest, std::wstring& error)
{
    outManifest = PluginManifest{};
    outManifest.manifestPath = manifestPath;
    outManifest.rootDirectory = ParentDirectory(manifestPath);

    JsonImport::JsonValue root = JsonImport::ParseJsonFile(manifestPath);
    if (root.type != JsonImport::JsonValue::Object)
    {
        error = L"plugin.json 不是有效 JSON 对象";
        return false;
    }

    outManifest.id = root.GetString(L"id");
    outManifest.name = root.GetString(L"name");
    outManifest.version = root.GetString(L"version");
    outManifest.description = root.GetString(L"description");
    outManifest.entry = root.GetString(L"entry");
    outManifest.minHostVersion = root.GetString(L"minHostVersion");
    outManifest.targetHostVersion = root.GetString(L"targetHostVersion");
    outManifest.abiVersion = (unsigned int)root.GetInt(L"abiVersion", 1);

    if (!IsValidPluginId(outManifest.id))
    {
        error = L"插件 id 无效，只能使用字母、数字、点、下划线和连字符";
        return false;
    }
    if (outManifest.name.empty())
    {
        error = L"插件名称不能为空";
        return false;
    }
    if (outManifest.version.empty())
    {
        error = L"插件版本不能为空";
        return false;
    }
    if (outManifest.abiVersion != 1)
    {
        error = L"插件 ABI 版本不兼容";
        return false;
    }
    if (!IsSafeRelativeDllPath(outManifest.entry))
    {
        error = L"插件入口必须是安全的相对 DLL 路径";
        return false;
    }

    outManifest.entryPath = JoinPath(outManifest.rootDirectory, outManifest.entry);
    if (!FileExists(outManifest.entryPath))
    {
        error = L"插件 DLL 入口文件不存在";
        return false;
    }

    if (auto* permissions = root.Get(L"permissions"))
    {
        if (permissions->type != JsonImport::JsonValue::Array)
        {
            error = L"permissions 必须是数组";
            return false;
        }
        for (const auto& value : permissions->arrayValue)
        {
            if (value.type != JsonImport::JsonValue::String || value.stringValue.empty())
            {
                error = L"permissions 中存在无效权限";
                return false;
            }
            if (!IsKnownPermission(value.stringValue))
            {
                error = L"未知插件权限: " + value.stringValue;
                return false;
            }
            outManifest.permissions.push_back(value.stringValue);
        }
    }

    if (auto* commands = root.Get(L"commands"))
    {
        if (commands->type != JsonImport::JsonValue::Array)
        {
            error = L"commands 必须是数组";
            return false;
        }
        for (const auto& value : commands->arrayValue)
        {
            if (value.type != JsonImport::JsonValue::Object)
            {
                error = L"commands 中存在无效命令";
                return false;
            }

            PluginCommandManifest command;
            command.id = value.GetString(L"id");
            command.title = value.GetString(L"title");
            command.description = value.GetString(L"description");
            if (!IsValidPluginId(command.id) || command.title.empty())
            {
                error = L"插件命令缺少有效 id 或标题";
                return false;
            }
            if (command.id.rfind(outManifest.id + L".", 0) != 0)
            {
                error = L"插件命令 id 必须使用插件命名空间: " + outManifest.id + L".";
                return false;
            }
            outManifest.commands.push_back(std::move(command));
        }
    }

    if (auto* slashCommands = root.Get(L"slashCommands"))
    {
        if (slashCommands->type != JsonImport::JsonValue::Array)
        {
            error = L"slashCommands 必须是数组";
            return false;
        }
        for (const auto& value : slashCommands->arrayValue)
        {
            if (value.type != JsonImport::JsonValue::Object)
            {
                error = L"slashCommands 中存在无效命令";
                return false;
            }

            PluginSlashCommandManifest command;
            command.id = value.GetString(L"id");
            command.command = value.GetString(L"command");
            command.title = value.GetString(L"title");
            command.description = value.GetString(L"description");
            command.usage = value.GetString(L"usage");
            command.icon = value.GetString(L"icon");
            if (!IsSafeRelativeAssetPath(command.icon))
            {
                error = L"/ 命令 icon 必须是安全的相对路径";
                return false;
            }
            if (!ReadStringArray(value, L"keywords", command.keywords, error) ||
                !ReadStringArray(value, L"aliases", command.aliases, error))
            {
                return false;
            }
            if (!IsValidPluginId(command.id) || !IsValidSlashName(command.command) || command.title.empty())
            {
                error = L"/ 命令缺少有效 id、命令名或标题";
                return false;
            }
            if (command.id.rfind(outManifest.id + L".", 0) != 0)
            {
                error = L"/ 命令 id 必须使用插件命名空间: " + outManifest.id + L".";
                return false;
            }
            outManifest.slashCommands.push_back(std::move(command));
        }
    }

    if (auto* settings = root.Get(L"settings"))
    {
        if (settings->type != JsonImport::JsonValue::Array)
        {
            error = L"settings 必须是数组";
            return false;
        }
        for (const auto& value : settings->arrayValue)
        {
            if (value.type != JsonImport::JsonValue::Object)
            {
                error = L"settings 中存在无效配置项";
                return false;
            }

            PluginSettingManifest setting;
            setting.key = value.GetString(L"key");
            setting.type = value.GetString(L"type", L"string");
            setting.title = value.GetString(L"title");
            if (auto* defaultValue = value.Get(L"default"))
                setting.defaultValue = JsonValueToConfigString(*defaultValue);
            setting.hasMin = value.Get(L"min") != nullptr;
            setting.hasMax = value.Get(L"max") != nullptr;
            setting.minValue = value.GetInt(L"min", 0);
            setting.maxValue = value.GetInt(L"max", 0);

            if (!IsValidSettingKey(setting.key) || setting.title.empty())
            {
                error = L"settings 中存在无效 key 或标题";
                return false;
            }
            if (setting.type != L"string" && setting.type != L"integer" && setting.type != L"boolean")
            {
                error = L"settings 仅支持 string、integer 和 boolean";
                return false;
            }
            if (setting.hasMin && setting.hasMax && setting.minValue > setting.maxValue)
            {
                error = L"settings 中 min 不能大于 max";
                return false;
            }
            outManifest.settings.push_back(std::move(setting));
        }
    }

    return true;
}

bool PluginManifestReader::IsValidPluginId(const std::wstring& id)
{
    if (id.empty() || id.size() > 96)
        return false;

    for (wchar_t ch : id)
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

bool PluginManifestReader::IsSafeRelativeDllPath(const std::wstring& path)
{
    if (path.empty())
        return false;
    if (path.find(L":") != std::wstring::npos)
        return false;
    if (path.rfind(L"\\\\", 0) == 0)
        return false;
    if (path.find(L"..") != std::wstring::npos)
        return false;

    std::wstring normalized = path;
    std::replace(normalized.begin(), normalized.end(), L'/', L'\\');
    if (!normalized.empty() && (normalized.front() == L'\\' || normalized.front() == L'/'))
        return false;

    size_t dot = normalized.find_last_of(L'.');
    if (dot == std::wstring::npos)
        return false;
    std::wstring ext = normalized.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
    return ext == L".dll";
}
