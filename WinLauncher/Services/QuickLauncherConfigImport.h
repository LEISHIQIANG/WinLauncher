#pragma once
#include "IConfigImportService.h"
#include "JsonImportHelper.h"
#include <algorithm>
#include <cwctype>
#include <set>
#include <sstream>

// Imports the current QuickLauncher data.json schema.  This deliberately keeps
// the conversion here (instead of accepting an untyped JSON blob in the UI) so
// imported data is normalized before it reaches WinLauncher's config store.
class QuickLauncherConfigImport : public IConfigImportService
{
public:
    ImportResult Import(const std::wstring& filePath, const std::wstring& configDir) override
    {
        ImportResult result;
        const auto root = JsonImport::ParseJsonFile(filePath);
        if (root.type != JsonImport::JsonValue::Object)
        {
            result.errorMsg = L"无法解析 JSON 文件，请确认选择的是 QuickLauncher 的 data.json。";
            return result;
        }

        const auto* folders = root.Get(L"folders");
        if (!folders || folders->type != JsonImport::JsonValue::Array)
        {
            result.errorMsg = L"文件中找不到 QuickLauncher 配置所需的 folders 数组。";
            return result;
        }

        ImportSettings(root.Get(L"settings"), result);

        std::vector<const JsonImport::JsonValue*> orderedFolders;
        for (size_t index = 0; index < folders->Size(); ++index)
        {
            const auto* folder = (*folders)[index];
            if (folder && folder->type == JsonImport::JsonValue::Object && !folder->GetBool(L"is_icon_repo", false))
                orderedFolders.push_back(folder);
        }
        std::stable_sort(orderedFolders.begin(), orderedFolders.end(), [](const auto* left, const auto* right) {
            return left->GetInt(L"order", 0) < right->GetInt(L"order", 0);
        });

        std::set<std::wstring> importedIds;
        for (const auto* folder : orderedFolders)
        {
            std::wstring folderName = folder->GetString(L"name");
            const bool isDock = folder->GetBool(L"is_dock", false);
            if (isDock) folderName = L"DOCK";
            if (folderName.empty())
            {
                ++result.skippedItems;
                continue;
            }

            Model::PopupPage page;
            page.name = folderName;
            std::wstring linkedPath = folder->GetString(L"linked_path");
            JsonImport::NormalizePath(linkedPath);
            if (!linkedPath.empty())
            {
                page.isSyncFolder = true;
                page.folderPath = linkedPath;
            }

            const auto* items = folder->Get(L"items");
            if (items && items->type == JsonImport::JsonValue::Array)
            {
                std::vector<const JsonImport::JsonValue*> orderedItems;
                for (size_t index = 0; index < items->Size(); ++index)
                {
                    const auto* item = (*items)[index];
                    if (item && item->type == JsonImport::JsonValue::Object)
                        orderedItems.push_back(item);
                }
                std::stable_sort(orderedItems.begin(), orderedItems.end(), [](const auto* left, const auto* right) {
                    return left->GetInt(L"order", 0) < right->GetInt(L"order", 0);
                });

                for (const auto* item : orderedItems)
                {
                    if (!item->GetBool(L"enabled", true))
                    {
                        ++result.skippedItems;
                        continue;
                    }

                    Model::ShortcutInfo shortcut;
                    if (!ConvertItem(*item, filePath, configDir, shortcut, result))
                    {
                        ++result.skippedItems;
                        continue;
                    }

                    // Batch-launch entries refer to QuickLauncher shortcut IDs.
                    // Preserve valid IDs so their references still work after import.
                    if (!shortcut.id.empty() && importedIds.insert(shortcut.id).second)
                    {
                        page.shortcuts.push_back(std::move(shortcut));
                        ++result.importedItems;
                    }
                    else
                    {
                        ++result.skippedItems;
                    }
                }
            }
            result.pages.push_back(std::move(page));
        }

        result.success = true;
        return result;
    }

private:
    static std::wstring Lower(std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });
        return value;
    }

    static std::wstring JoinHotkey(const JsonImport::JsonValue& item)
    {
        std::vector<std::wstring> values;
        const auto* modifiers = item.Get(L"hotkey_modifiers");
        if (modifiers && modifiers->type == JsonImport::JsonValue::Array)
        {
            for (size_t index = 0; index < modifiers->Size(); ++index)
            {
                const auto* modifier = (*modifiers)[index];
                if (modifier && modifier->type == JsonImport::JsonValue::String && !modifier->stringValue.empty())
                    values.push_back(modifier->stringValue);
            }
        }
        const auto* keys = item.Get(L"hotkey_keys");
        if (keys && keys->type == JsonImport::JsonValue::Array)
        {
            for (size_t index = 0; index < keys->Size(); ++index)
            {
                const auto* key = (*keys)[index];
                if (key && key->type == JsonImport::JsonValue::String && !key->stringValue.empty())
                    values.push_back(key->stringValue);
            }
        }
        if (values.empty())
        {
            const std::wstring key = item.GetString(L"hotkey_key");
            if (!key.empty()) values.push_back(key);
        }
        if (values.empty()) return item.GetString(L"hotkey");

        std::wstring combined;
        for (auto value : values)
        {
            if (!value.empty()) value[0] = static_cast<wchar_t>(towupper(value[0]));
            if (!combined.empty()) combined += L"+";
            combined += value;
        }
        return combined;
    }

    static std::wstring CommandArguments(const JsonImport::JsonValue& item)
    {
        std::wstring type = Lower(item.GetString(L"command_type", L"cmd"));
        if (type == L"bash") type = L"gitbash";
        if (type != L"cmd" && type != L"powershell" && type != L"python" && type != L"gitbash")
            type = L"cmd";
        const bool capture = item.GetBool(L"capture_output", false);
        const bool showWindow = item.GetBool(L"show_window", false) && !capture;
        const int timeout = std::max(1, item.GetInt(L"command_timeout_seconds", 10));
        const int maxChars = std::max(100, item.GetInt(L"command_output_max_chars", 2000));
        return type + L"||||||" + std::to_wstring(showWindow ? 1 : 0) + L"|||" +
               std::to_wstring(capture ? 1 : 0) + L"|||" + std::to_wstring(timeout) + L"|||" + std::to_wstring(maxChars);
    }

    static std::wstring SerializeBatch(const JsonImport::JsonValue& item)
    {
        const auto* steps = item.Get(L"batch_launch_steps");
        if (!steps || steps->type != JsonImport::JsonValue::Array) return L"";

        std::wstring serialized;
        for (size_t index = 0; index < steps->Size(); ++index)
        {
            const auto* step = (*steps)[index];
            if (!step || step->type != JsonImport::JsonValue::Object) continue;
            const std::wstring id = step->GetString(L"shortcut_id");
            if (id.empty()) continue;
            if (!serialized.empty()) serialized += L"|||";
            serialized += id + L"," + std::to_wstring(std::max(0, step->GetInt(L"delay_ms", 0))) + L"," +
                          (step->GetBool(L"stop_on_error", true) ? L"1" : L"0") + L"," +
                          (step->GetBool(L"enabled", true) ? L"1" : L"0");
        }
        return serialized;
    }

    static std::wstring SerializeMacro(const JsonImport::JsonValue& item)
    {
        const auto* events = item.Get(L"macro_events");
        if (!events || events->type != JsonImport::JsonValue::Array || events->Size() == 0) return L"";

        const double speed = item.GetNumber(L"macro_speed", 1.0) > 0.0 ? item.GetNumber(L"macro_speed", 1.0) : 1.0;
        std::wstring serialized = std::to_wstring(speed) + L"|" + item.GetString(L"trigger_mode", L"immediate") + L"|";
        bool hasEvent = false;
        for (size_t index = 0; index < events->Size(); ++index)
        {
            const auto* event = (*events)[index];
            if (!event || event->type != JsonImport::JsonValue::Object) continue;
            if (hasEvent) serialized += L";";
            serialized += std::to_wstring(event->GetInt(L"type", 0)) + L"," +
                          std::to_wstring(event->GetInt(L"flags", 0)) + L"," +
                          std::to_wstring(std::max(0, event->GetInt(L"delay_us", 0))) + L"," +
                          std::to_wstring(event->GetInt(L"x", 0)) + L"," +
                          std::to_wstring(event->GetInt(L"y", 0)) + L"," +
                          std::to_wstring(event->GetInt(L"data", 0)) + L"," +
                          std::to_wstring(event->GetInt(L"vk_code", 0)) + L"," +
                          std::to_wstring(event->GetInt(L"scan_code", 0));
            hasEvent = true;
        }
        return hasEvent ? serialized : L"";
    }

    static void ImportIcon(const JsonImport::JsonValue& item, const std::wstring& sourceFile,
                           const std::wstring& configDir, Model::ShortcutInfo& shortcut, ImportResult& result)
    {
        std::wstring iconPath = item.GetString(L"icon_path");
        JsonImport::NormalizePath(iconPath);
        if (!iconPath.empty() && iconPath.rfind(L"builtin-command:", 0) != 0)
        {
            const std::wstring resolved = JsonImport::ResolveRelativePath(iconPath, sourceFile);
            if (JsonImport::IsImageFile(resolved) && GetFileAttributesW(resolved.c_str()) != INVALID_FILE_ATTRIBUTES)
            {
                shortcut.iconPath = JsonImport::CopyIconToConfigDir(resolved, configDir, shortcut.name);
                if (!shortcut.iconPath.empty())
                {
                    shortcut.iconSource = Model::IconSource::CustomPath;
                    ++result.copiedIcons;
                    return;
                }
            }
        }

        const std::wstring embedded = item.GetString(L"icon_data");
        if (!embedded.empty())
        {
            shortcut.iconPath = JsonImport::CopyEmbeddedIconToConfigDir(embedded, configDir, shortcut.name);
            if (!shortcut.iconPath.empty())
            {
                shortcut.iconSource = Model::IconSource::CustomPath;
                ++result.copiedIcons;
            }
        }
    }

    static bool ConvertItem(const JsonImport::JsonValue& item, const std::wstring& sourceFile,
                            const std::wstring& configDir, Model::ShortcutInfo& shortcut, ImportResult& result)
    {
        const std::wstring type = Lower(item.GetString(L"type"));
        shortcut.id = item.GetString(L"id");
        shortcut.name = item.GetString(L"name");
        if (shortcut.id.empty() || shortcut.name.empty()) return false;
        shortcut.runAsAdmin = item.GetBool(L"run_as_admin", false);
        shortcut.iconInvertLight = item.GetBool(L"icon_invert_light", false);
        shortcut.iconInvertDark = item.GetBool(L"icon_invert_dark", false);

        if (type == L"file" || type == L"folder")
        {
            shortcut.type = Model::ShortcutType::File;
            shortcut.targetPath = item.GetString(L"target_path");
            shortcut.arguments = item.GetString(L"target_args");
            JsonImport::NormalizePath(shortcut.targetPath);
            JsonImport::NormalizePath(shortcut.arguments);
            if (shortcut.targetPath.empty()) return false;
            const std::wstring extension = Lower(shortcut.targetPath.substr(shortcut.targetPath.find_last_of(L'.') == std::wstring::npos ? shortcut.targetPath.size() : shortcut.targetPath.find_last_of(L'.')));
            if (extension == L".exe") shortcut.targetKind = Model::ShortcutTargetKind::Exe;
            else if (extension == L".lnk") shortcut.targetKind = Model::ShortcutTargetKind::Link;
            else
            {
                const DWORD attributes = GetFileAttributesW(shortcut.targetPath.c_str());
                shortcut.targetKind = (type == L"folder" || (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY)))
                    ? Model::ShortcutTargetKind::Folder
                    : Model::ShortcutTargetKind::File;
            }
        }
        else if (type == L"url")
        {
            shortcut.type = Model::ShortcutType::Url;
            shortcut.targetPath = item.GetString(L"url");
            if (shortcut.targetPath.empty()) shortcut.targetPath = item.GetString(L"target_path");
            if (shortcut.targetPath.empty()) return false;
            const std::wstring browser = item.GetString(L"preferred_browser_path");
            if (!browser.empty()) shortcut.arguments = browser + L"|||" + item.GetString(L"preferred_browser_args");
        }
        else if (type == L"hotkey")
        {
            shortcut.type = Model::ShortcutType::Hotkey;
            shortcut.targetPath = JoinHotkey(item);
            if (shortcut.targetPath.empty()) return false;
        }
        else if (type == L"command")
        {
            const std::wstring command = item.GetString(L"command");
            const std::wstring commandType = Lower(item.GetString(L"command_type", L"cmd"));
            if (commandType == L"builtin")
            {
                const std::wstring builtin = Lower(command);
                if (builtin == L"show_config_window" || builtin == L"open_config_window")
                {
                    shortcut.type = Model::ShortcutType::System;
                    shortcut.targetPath = L":config_window";
                }
                else if (builtin == L"toggle_topmost" || builtin == L"topmost")
                {
                    shortcut.type = Model::ShortcutType::System;
                    shortcut.targetPath = L":topmost_toggle";
                }
                else return false;
            }
            else
            {
                if (command.empty()) return false;
                shortcut.type = Model::ShortcutType::Command;
                shortcut.targetPath = command;
                shortcut.arguments = CommandArguments(item);
            }
        }
        else if (type == L"batch_launch")
        {
            shortcut.type = Model::ShortcutType::Batch;
            shortcut.arguments = SerializeBatch(item);
            if (shortcut.arguments.empty()) return false;
        }
        else if (type == L"macro")
        {
            shortcut.type = Model::ShortcutType::Macro;
            shortcut.arguments = SerializeMacro(item);
            if (shortcut.arguments.empty()) return false;
        }
        else return false;

        ImportIcon(item, sourceFile, configDir, shortcut, result);
        return true;
    }

    static void ImportSettings(const JsonImport::JsonValue* settings, ImportResult& result)
    {
        if (!settings || settings->type != JsonImport::JsonValue::Object) return;
        if (settings->Get(L"auto_start"))
        {
            result.hasAutoStartSetting = true;
            result.autoStart = settings->GetBool(L"auto_start", false);
        }
        if (settings->Get(L"cols")) result.popupColumns = settings->GetInt(L"cols", 0);
        if (settings->Get(L"popup_max_rows")) result.popupRows = settings->GetInt(L"popup_max_rows", 0);
        if (settings->Get(L"dock_height_mode")) result.dockHeight = settings->GetBool(L"dock_enabled", true) ? settings->GetInt(L"dock_height_mode", 0) : 0;
        if (settings->Get(L"icon_size")) result.popupIconSize = settings->GetInt(L"icon_size", 0);
        if (settings->Get(L"ui_scale_percent")) result.globalScalePercent = settings->GetInt(L"ui_scale_percent", 0);
        if (settings->Get(L"theme") && !settings->GetBool(L"theme_follow_system", false))
        {
            const std::wstring theme = Lower(settings->GetString(L"theme"));
            if (theme == L"dark") result.theme = 0;
            else if (theme == L"light") result.theme = 1;
        }
        if (settings->Get(L"sort_mode")) result.sortMode = Lower(settings->GetString(L"sort_mode")) == L"smart" ? 1 : 0;
        if (settings->Get(L"hide_tray_icon")) { result.hasHideTrayIcon = true; result.hideTrayIcon = settings->GetBool(L"hide_tray_icon", false); }
        if (settings->Get(L"hardware_acceleration")) { result.hasHardwareAcceleration = true; result.hardwareAcceleration = settings->GetBool(L"hardware_acceleration", false); }
        if (settings->Get(L"search_default_active")) { result.hasSearchMode = true; result.searchMode = settings->GetBool(L"search_default_active", false); }
        if (settings->Get(L"popup_auto_close")) { result.hasPopupAutoClose = true; result.popupAutoClose = settings->GetBool(L"popup_auto_close", true); }
        if (settings->Get(L"popup_multi_open_when_pinned")) { result.hasPopupMultiOpenWhenPinned = true; result.popupMultiOpenWhenPinned = settings->GetBool(L"popup_multi_open_when_pinned", false); }
        if (settings->Get(L"hover_leave_delay")) result.hoverLeaveDelay = settings->GetInt(L"hover_leave_delay", -1);
        const std::wstring align = Lower(settings->GetString(L"popup_align_mode"));
        if (align == L"mouse_center") result.popupAlignMode = 0;
        else if (align == L"mouse_top_left") result.popupAlignMode = 1;
        else if (align == L"screen_center") result.popupAlignMode = 2;
        else if (align == L"bottom_right") result.popupAlignMode = 3;
    }
};
