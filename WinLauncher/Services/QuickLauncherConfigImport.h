#pragma once
#include "IConfigImportService.h"
#include "JsonImportHelper.h"

class QuickLauncherConfigImport : public IConfigImportService
{
public:
    virtual ImportResult Import(const std::wstring& filePath, const std::wstring& configDir) override
    {
        ImportResult result;

        auto root = JsonImport::ParseJsonFile(filePath);
        if (root.type != JsonImport::JsonValue::Object)
        {
            result.errorMsg = L"\u65E0\u6CD5\u89E3\u6790 JSON \u6587\u4EF6\uFF0C\u8BF7\u786E\u8BA4\u6587\u4EF6\u683C\u5F0F\u6B63\u786E\u3002";
            return result;
        }

        auto* folders = root.Get(L"folders");
        if (!folders || folders->type != JsonImport::JsonValue::Array)
        {
            result.errorMsg = L"JSON \u6587\u4EF6\u4E2D\u627E\u4E0D\u5230\u6709\u6548\u7684 folders \u6570\u7EC4\u3002";
            return result;
        }

        // Convert settings
        auto* settings = root.Get(L"settings");
        if (settings && settings->type == JsonImport::JsonValue::Object)
        {
            result.hasAutoStart = settings->GetBool(L"auto_start", false);
            result.popupColumns = settings->GetInt(L"cols", 0);
            result.popupRows = settings->GetInt(L"popup_max_rows", 0);
            result.dockHeight = settings->GetInt(L"dock_height_mode", 0);
        }

        // Convert folders to pages
        for (size_t fi = 0; fi < folders->Size(); fi++)
        {
            auto* folder = (*folders)[fi];
            if (!folder || folder->type != JsonImport::JsonValue::Object) continue;

            std::wstring folderName = folder->GetString(L"name");
            if (folderName.empty()) continue;

            bool isDock = folder->GetBool(L"is_dock", false);
            if (isDock)
                folderName = L"DOCK";

            Model::PopupPage page;
            page.name = folderName;

            std::wstring linkedPath = folder->GetString(L"linked_path");
            JsonImport::NormalizePath(linkedPath);
            if (!linkedPath.empty())
            {
                page.isSyncFolder = true;
                page.folderPath = linkedPath;
            }

            auto* items = folder->Get(L"items");
            if (items && items->type == JsonImport::JsonValue::Array)
            {
                for (size_t ii = 0; ii < items->Size(); ii++)
                {
                    auto* item = (*items)[ii];
                    if (!item || item->type != JsonImport::JsonValue::Object) continue;

                    std::wstring itemType = item->GetString(L"type");
                    std::wstring itemName = item->GetString(L"name");
                    bool enabled = item->GetBool(L"enabled", true);

                    if (!enabled) continue;

                    if (itemType != L"file" && itemType != L"folder" && itemType != L"url" && itemType != L"hotkey")
                        continue;

                    Model::ShortcutInfo sc;
                    sc.name = itemName;

                    if (itemType == L"file" || itemType == L"folder")
                    {
                        sc.type = Model::ShortcutType::File;
                        sc.targetPath = item->GetString(L"target_path");
                        sc.arguments = item->GetString(L"target_args");
                        JsonImport::NormalizePath(sc.targetPath);
                        JsonImport::NormalizePath(sc.arguments);
                        sc.runAsAdmin = item->GetBool(L"run_as_admin", false);

                        std::wstring path = sc.targetPath;
                        if (path.size() >= 4 && _wcsicmp(path.substr(path.size() - 4).c_str(), L".exe") == 0)
                            sc.targetKind = Model::ShortcutTargetKind::Exe;
                        else if (path.size() >= 4 && _wcsicmp(path.substr(path.size() - 4).c_str(), L".lnk") == 0)
                            sc.targetKind = Model::ShortcutTargetKind::Link;
                        else
                        {
                            DWORD attrs = GetFileAttributesW(path.c_str());
                            if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY))
                                sc.targetKind = Model::ShortcutTargetKind::Folder;
                            else
                                sc.targetKind = Model::ShortcutTargetKind::File;
                        }
                    }
                    else if (itemType == L"url")
                    {
                        sc.type = Model::ShortcutType::Url;
                        std::wstring url = item->GetString(L"url");
                        std::wstring targetPath = item->GetString(L"target_path");
                        sc.targetPath = url.empty() ? targetPath : url;
                        sc.targetKind = Model::ShortcutTargetKind::Unknown;
                    }
                    else if (itemType == L"hotkey")
                    {
                        sc.type = Model::ShortcutType::Hotkey;
                        sc.runAsAdmin = item->GetBool(L"run_as_admin", false);

                        // Build hotkey display string from modifiers + key
                        std::wstring hotkeyStr;
                        auto* modifiers = item->Get(L"hotkey_modifiers");
                        if (modifiers && modifiers->type == JsonImport::JsonValue::Array)
                        {
                            for (size_t mi = 0; mi < modifiers->Size(); mi++)
                            {
                                auto* mod = (*modifiers)[mi];
                                if (mod && mod->type == JsonImport::JsonValue::String)
                                {
                                    std::wstring m = mod->stringValue;
                                    if (!m.empty())
                                    {
                                        m[0] = (wchar_t)towupper(m[0]);
                                        if (!hotkeyStr.empty()) hotkeyStr += L"+";
                                        hotkeyStr += m;
                                    }
                                }
                            }
                        }
                        std::wstring key = item->GetString(L"hotkey_key");
                        if (!key.empty())
                        {
                            key[0] = (wchar_t)towupper(key[0]);
                            if (!hotkeyStr.empty()) hotkeyStr += L"+";
                            hotkeyStr += key;
                        }

                        // Fallback: use combined hotkey field if available
                        if (hotkeyStr.empty())
                            hotkeyStr = item->GetString(L"hotkey");

                        sc.targetPath = hotkeyStr;
                        sc.targetKind = Model::ShortcutTargetKind::Unknown;
                    }

                    // Handle icon copy — only copy actual image files
                    std::wstring iconPath = item->GetString(L"icon_path");
                    JsonImport::NormalizePath(iconPath);
                    if (!iconPath.empty() && !configDir.empty() && JsonImport::IsImageFile(iconPath))
                    {
                        if (GetFileAttributesW(iconPath.c_str()) != INVALID_FILE_ATTRIBUTES)
                        {
                            std::wstring copiedPath = JsonImport::CopyIconToConfigDir(iconPath, configDir, itemName);
                            if (!copiedPath.empty())
                            {
                                sc.iconPath = copiedPath;
                                sc.iconSource = Model::IconSource::CustomPath;
                            }
                        }
                    }

                    page.shortcuts.push_back(std::move(sc));
                }
            }

            result.pages.push_back(std::move(page));
        }

        result.success = true;
        return result;
    }
};
