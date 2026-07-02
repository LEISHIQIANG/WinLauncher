#pragma once
#include "IConfigService.h"
#include "../App/Logger.h"
#include "../AutoStartHelper.h"
#include <Windows.h>
#include <ole2.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cwctype>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "FolderWatcher.h"
#include "SyncFolderService.h"
#include "ConfigPath.h"
#include <objbase.h>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "advapi32.lib")

class IniConfigRepository : public IConfigService
{
public:
    static constexpr int AutoBackupDebounceSeconds = 180;

    explicit IniConfigRepository(Logger* logger = nullptr, HWND notifyHwnd = nullptr, UINT notifyMessage = 0)
        : m_logger(logger)
        , m_notifyHwnd(notifyHwnd)
        , m_notifyMessage(notifyMessage)
        , m_triggerType(0)
        , m_popupColumns(6)
        , m_popupRows(4)
        , m_popupIconSize(24)
        , m_popupIconGap(4)
        , m_popupIconRadius(6)
        , m_popupWndPadding(8)
        , m_themeMode(0)
        , m_themeColorIndex(0)
        , m_windowMode(0)
        , m_globalScalePercent(100)
        , m_dockHeight(1)
        , m_searchMode(false)
        , m_popupAlignMode(0)
        , m_popupAutoClose(true)
        , m_popupMultiOpenWhenPinned(false)
        , m_hoverLeaveDelay(200)
        , m_sortMode(0)
        , m_animationEnabled(true)
        , m_animationDuration(200)
        , m_hardwareAccelerationEnabled(true)
        , m_hideTrayIcon(false)
        , m_autoUpdate(true)
    {
        m_configDir = ConfigPath::PrepareUserConfigDirectory();
        m_configFilePath = m_configDir + L"\\launcher_config.ini";
        m_historyDir = m_configDir + L"\\history";
    }

private:
    static std::wstring GenerateGUID()
    {
        GUID guid;
        if (SUCCEEDED(CoCreateGuid(&guid)))
        {
            wchar_t buf[40]{};
            if (StringFromGUID2(guid, buf, 40) > 0)
            {
                return buf;
            }
        }
        return L"{" + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(rand()) + L"}";
    }

public:

    virtual std::vector<Model::PopupPage> LoadConfig() override
    {
        std::vector<Model::PopupPage> pages;
        m_hasGlobalScalePercent = false;
        m_globalScalePercent = 100;

        if (GetFileAttributesW(m_configFilePath.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            return CreateDefaultConfig();
        }

        std::wstring content = ReadFile(m_configFilePath);
        if (content.empty())
        {
            Model::PopupPage dockPage;
            dockPage.name = L"DOCK";
            pages.push_back(std::move(dockPage));

            Model::PopupPage commonPage;
            commonPage.name = L"常用";
            pages.push_back(std::move(commonPage));
            return pages;
        }

        std::wstringstream wss(content);
        std::wstring line;
        Model::PopupPage* currentPage = nullptr;
        Model::ShortcutInfo* currentItem = nullptr;
        bool inSettings = false;
        int currentPageIndex = -1;
        int currentItemIndex = -1;

        while (std::getline(wss, line))
        {
            Trim(line);
            if (line.empty()) continue;

            if (line.front() == L'[' && line.back() == L']')
            {
                std::wstring sec = line.substr(1, line.size() - 2);
                if (sec == L"Settings")
                {
                    inSettings = true;
                    currentPage = nullptr;
                    currentItem = nullptr;
                    currentPageIndex = -1;
                    currentItemIndex = -1;
                }
                else if (sec.rfind(L"Page:", 0) == 0)
                {
                    inSettings = false;
                    currentItem = nullptr;
                    currentItemIndex = -1;
                    currentPage = nullptr;
                    currentPageIndex = -1;

                    try
                    {
                        currentPageIndex = std::stoi(sec.substr(5));
                    }
                    catch (...)
                    {
                        currentPageIndex = -1;
                    }

                    if (currentPageIndex >= 0)
                    {
                        if ((int)pages.size() <= currentPageIndex)
                            pages.resize(currentPageIndex + 1);
                        currentPage = &pages[currentPageIndex];
                    }
                }
                else if (sec.rfind(L"Item:", 0) == 0)
                {
                    inSettings = false;
                    currentPage = nullptr;
                    currentItem = nullptr;
                    currentPageIndex = -1;
                    currentItemIndex = -1;

                    std::wstring rest = sec.substr(5);
                    size_t colon = rest.find(L':');
                    if (colon != std::wstring::npos)
                    {
                        try
                        {
                            currentPageIndex = std::stoi(rest.substr(0, colon));
                            currentItemIndex = std::stoi(rest.substr(colon + 1));
                        }
                        catch (...)
                        {
                            currentPageIndex = -1;
                            currentItemIndex = -1;
                        }
                    }

                    if (currentPageIndex >= 0 && currentItemIndex >= 0)
                    {
                        if ((int)pages.size() <= currentPageIndex)
                            pages.resize(currentPageIndex + 1);
                        currentPage = &pages[currentPageIndex];
                        if ((int)currentPage->shortcuts.size() <= currentItemIndex)
                            currentPage->shortcuts.resize(currentItemIndex + 1);
                        currentItem = &currentPage->shortcuts[currentItemIndex];
                    }
                }
                else
                {
                    inSettings = false;
                    currentPage = nullptr;
                    currentItem = nullptr;
                    currentPageIndex = -1;
                    currentItemIndex = -1;
                }
            }
            else if (inSettings)
            {
                size_t eq = line.find(L'=');
                if (eq != std::wstring::npos)
                {
                    std::wstring key = line.substr(0, eq);
                    std::wstring val = UnescapeValue(line.substr(eq + 1));
                    Trim(key); Trim(val);
                    if (key == L"TriggerType")
                    {
                        try { m_triggerType = std::stoi(val); } catch (...) { m_triggerType = 0; }
                    }
                    else if (key == L"PopupColumns")
                    {
                        try { m_popupColumns = std::stoi(val); } catch (...) { m_popupColumns = 6; }
                    }
                    else if (key == L"PopupRows")
                    {
                        try { m_popupRows = std::stoi(val); } catch (...) { m_popupRows = 4; }
                    }
                    else if (key == L"PopupIconSize")
                    {
                        try { m_popupIconSize = std::stoi(val); } catch (...) { m_popupIconSize = 24; }
                    }
                    else if (key == L"PopupIconGap")
                    {
                        try { m_popupIconGap = std::stoi(val); } catch (...) { m_popupIconGap = 4; }
                    }
                    else if (key == L"PopupIconRadius")
                    {
                        try { m_popupIconRadius = std::stoi(val); } catch (...) { m_popupIconRadius = 6; }
                    }
                    else if (key == L"PopupWndPadding")
                    {
                        try { m_popupWndPadding = std::stoi(val); } catch (...) { m_popupWndPadding = 8; }
                    }
                    else if (key == L"ThemeMode")
                    {
                        try { m_themeMode = std::stoi(val); } catch (...) { m_themeMode = 0; }
                    }
                    else if (key == L"ThemeColorIndex")
                    {
                        try { SetThemeColor(std::stoi(val)); } catch (...) { m_themeColorIndex = 0; }
                    }
                    else if (key == L"WindowMode")
                    {
                        try { m_windowMode = std::stoi(val); } catch (...) { m_windowMode = 0; }
                    }
                    else if (key == L"GlobalScalePercent")
                    {
                        try { SetGlobalScalePercent(std::stoi(val)); } catch (...) { m_globalScalePercent = 100; m_hasGlobalScalePercent = false; }
                    }
                    else if (key == L"DockHeight")
                    {
                        try { m_dockHeight = std::stoi(val); if (m_dockHeight < 1) m_dockHeight = 1; if (m_dockHeight > 5) m_dockHeight = 5; } catch (...) { m_dockHeight = 1; }
                    }
                    else if (key == L"SearchMode")
                    {
                        try { m_searchMode = (std::stoi(val) != 0); } catch (...) { m_searchMode = false; }
                    }
                    else if (key == L"PopupAlignMode")
                    {
                        try { SetPopupAlignMode(std::stoi(val)); } catch (...) { m_popupAlignMode = 0; }
                    }
                    else if (key == L"PopupAutoClose")
                    {
                        try { m_popupAutoClose = (std::stoi(val) != 0); } catch (...) { m_popupAutoClose = true; }
                    }
                    else if (key == L"PopupMultiOpenWhenPinned")
                    {
                        try { m_popupMultiOpenWhenPinned = (std::stoi(val) != 0); } catch (...) { m_popupMultiOpenWhenPinned = false; }
                    }
                    else if (key == L"HoverLeaveDelay")
                    {
                        try { SetHoverLeaveDelay(std::stoi(val)); } catch (...) { m_hoverLeaveDelay = 200; }
                    }
                    else if (key == L"SortMode")
                    {
                        try { SetSortMode(std::stoi(val)); } catch (...) { m_sortMode = 0; }
                    }
                    else if (key == L"AnimationEnabled")
                    {
                        try { m_animationEnabled = (std::stoi(val) != 0); } catch (...) { m_animationEnabled = true; }
                    }
                    else if (key == L"AnimationDuration")
                    {
                        try { m_animationDuration = std::stoi(val); } catch (...) { m_animationDuration = 200; }
                    }
                    else if (key == L"HardwareAccelerationEnabled")
                    {
                        try { m_hardwareAccelerationEnabled = (std::stoi(val) != 0); } catch (...) { m_hardwareAccelerationEnabled = true; }
                    }
                    else if (key == L"HideTrayIcon")
                    {
                        try { m_hideTrayIcon = (std::stoi(val) != 0); } catch (...) { m_hideTrayIcon = false; }
                    }
                    else if (key == L"AutoUpdate")
                    {
                        try { m_autoUpdate = (std::stoi(val) != 0); } catch (...) { m_autoUpdate = true; }
                    }
                    else if (key == L"DarkHue")
                    {
                        try { m_appearance.dark.hue = std::stof(val); } catch (...) {}
                    }
                    else if (key == L"DarkBlur")
                    {
                        try { m_appearance.dark.blur = std::stof(val); } catch (...) {}
                    }
                    else if (key == L"DarkOpacity")
                    {
                        try { m_appearance.dark.opacity = std::stof(val); } catch (...) {}
                    }
                    else if (key == L"DarkHighlight")
                    {
                        try { m_appearance.dark.highlight = std::stof(val); } catch (...) {}
                    }
                    else if (key == L"DarkBrightness")
                    {
                        try {
                            m_appearance.dark.brightness = std::stof(val);
                            if (m_appearance.dark.brightness > 1.0f) m_appearance.dark.brightness = 1.0f;
                        } catch (...) {}
                    }
                    else if (key == L"DarkSaturation")
                    {
                        try { m_appearance.dark.saturation = std::stof(val); } catch (...) {}
                    }
                    else if (key == L"LightHue")
                    {
                        try { m_appearance.light.hue = std::stof(val); } catch (...) {}
                    }
                    else if (key == L"LightBlur")
                    {
                        try { m_appearance.light.blur = std::stof(val); } catch (...) {}
                    }
                    else if (key == L"LightOpacity")
                    {
                        try { m_appearance.light.opacity = std::stof(val); } catch (...) {}
                    }
                    else if (key == L"LightHighlight")
                    {
                        try { m_appearance.light.highlight = std::stof(val); } catch (...) {}
                    }
                    else if (key == L"LightBrightness")
                    {
                        try {
                            m_appearance.light.brightness = std::stof(val);
                            if (m_appearance.light.brightness > 1.0f) m_appearance.light.brightness = 1.0f;
                        } catch (...) {}
                    }
                    else if (key == L"LightSaturation")
                    {
                        try { m_appearance.light.saturation = std::stof(val); } catch (...) {}
                    }
                    else if (key == L"AcrylicDarkHue")
                    {
                        try { m_appearance.acrylicDark.hue = std::stof(val); } catch (...) {}
                    }
                    else if (key == L"AcrylicDarkBlur")
                    {
                        try { m_appearance.acrylicDark.blur = std::stof(val); } catch (...) {}
                    }
                    else if (key == L"AcrylicDarkOpacity")
                    {
                        try { m_appearance.acrylicDark.opacity = std::stof(val); } catch (...) {}
                    }
                    else if (key == L"AcrylicDarkHighlight")
                    {
                        try { m_appearance.acrylicDark.highlight = std::stof(val); } catch (...) {}
                    }
                    else if (key == L"AcrylicDarkBrightness")
                    {
                        try {
                            m_appearance.acrylicDark.brightness = std::stof(val);
                            if (m_appearance.acrylicDark.brightness > 1.0f) m_appearance.acrylicDark.brightness = 1.0f;
                        } catch (...) {}
                    }
                    else if (key == L"AcrylicDarkSaturation")
                    {
                        try { m_appearance.acrylicDark.saturation = std::stof(val); } catch (...) {}
                    }
                    else if (key == L"AcrylicLightHue")
                    {
                        try { m_appearance.acrylicLight.hue = std::stof(val); } catch (...) {}
                    }
                    else if (key == L"AcrylicLightBlur")
                    {
                        try { m_appearance.acrylicLight.blur = std::stof(val); } catch (...) {}
                    }
                    else if (key == L"AcrylicLightOpacity")
                    {
                        try { m_appearance.acrylicLight.opacity = std::stof(val); } catch (...) {}
                    }
                    else if (key == L"AcrylicLightHighlight")
                    {
                        try { m_appearance.acrylicLight.highlight = std::stof(val); } catch (...) {}
                    }
                    else if (key == L"AcrylicLightBrightness")
                    {
                        try {
                            m_appearance.acrylicLight.brightness = std::stof(val);
                            if (m_appearance.acrylicLight.brightness > 1.0f) m_appearance.acrylicLight.brightness = 1.0f;
                        } catch (...) {}
                    }
                    else if (key == L"AcrylicLightSaturation")
                    {
                        try { m_appearance.acrylicLight.saturation = std::stof(val); } catch (...) {}
                    }
                    else if (key == L"GlassDarkHue")
                    {
                        try { m_appearance.glassDark.hue = std::stof(val); } catch (...) {}
                    }
                    else if (key == L"GlassDarkBlur")
                    {
                        try { m_appearance.glassDark.blur = std::stof(val); } catch (...) {}
                    }
                    else if (key == L"GlassDarkOpacity")
                    {
                        try { m_appearance.glassDark.opacity = std::stof(val); } catch (...) {}
                    }
                    else if (key == L"GlassDarkHighlight")
                    {
                        try { m_appearance.glassDark.highlight = std::stof(val); } catch (...) {}
                    }
                    else if (key == L"GlassDarkBrightness")
                    {
                        try {
                            m_appearance.glassDark.brightness = std::stof(val);
                            if (m_appearance.glassDark.brightness > 1.0f) m_appearance.glassDark.brightness = 1.0f;
                        } catch (...) {}
                    }
                    else if (key == L"GlassDarkSaturation")
                    {
                        try { m_appearance.glassDark.saturation = std::stof(val); } catch (...) {}
                    }
                    else if (key == L"GlassLightHue")
                    {
                        try { m_appearance.glassLight.hue = std::stof(val); } catch (...) {}
                    }
                    else if (key == L"GlassLightBlur")
                    {
                        try { m_appearance.glassLight.blur = std::stof(val); } catch (...) {}
                    }
                    else if (key == L"GlassLightOpacity")
                    {
                        try { m_appearance.glassLight.opacity = std::stof(val); } catch (...) {}
                    }
                    else if (key == L"GlassLightHighlight")
                    {
                        try { m_appearance.glassLight.highlight = std::stof(val); } catch (...) {}
                    }
                    else if (key == L"GlassLightBrightness")
                    {
                        try {
                            m_appearance.glassLight.brightness = std::stof(val);
                            if (m_appearance.glassLight.brightness > 1.0f) m_appearance.glassLight.brightness = 1.0f;
                        } catch (...) {}
                    }
                    else if (key == L"GlassLightSaturation")
                    {
                        try { m_appearance.glassLight.saturation = std::stof(val); } catch (...) {}
                    }
                }
            }
            else if (currentItem)
            {
                size_t eq = line.find(L'=');
                if (eq != std::wstring::npos)
                {
                    std::wstring key = line.substr(0, eq);
                    std::wstring val = UnescapeValue(line.substr(eq + 1));
                    Trim(key); Trim(val);

                    if (key == L"Name") currentItem->name = val;
                    else if (key == L"Id") currentItem->id = val;
                    else if (key == L"Type") currentItem->type = Model::ShortcutTypeFromKey(val);
                    else if (key == L"TargetKind") currentItem->targetKind = Model::ShortcutTargetKindFromKey(val);
                    else if (key == L"TargetPath") currentItem->targetPath = val;
                    else if (key == L"Arguments") currentItem->arguments = val;
                    else if (key == L"RunAsAdmin")
                    {
                        try { currentItem->runAsAdmin = (std::stoi(val) != 0); } catch (...) { currentItem->runAsAdmin = false; }
                    }
                    else if (key == L"IconSource") currentItem->iconSource = Model::IconSourceFromKey(val);
                    else if (key == L"IconPath") currentItem->iconPath = val;
                    else if (key == L"BuiltinIconId") currentItem->builtinIconId = val;
                    else if (key == L"IconInvertLight")
                    {
                        try { currentItem->iconInvertLight = (std::stoi(val) != 0); } catch (...) { currentItem->iconInvertLight = false; }
                    }
                    else if (key == L"IconInvertDark")
                    {
                        try { currentItem->iconInvertDark = (std::stoi(val) != 0); } catch (...) { currentItem->iconInvertDark = false; }
                    }
                }
            }
            else if (currentPage)
            {
                size_t eq = line.find(L'=');
                if (eq != std::wstring::npos)
                {
                    std::wstring key = line.substr(0, eq);
                    std::wstring val = UnescapeValue(line.substr(eq + 1));
                    Trim(key); Trim(val);

                    if (key == L"Name") currentPage->name = val;
                    else if (key == L"IsSyncFolder")
                    {
                        try { currentPage->isSyncFolder = (std::stoi(val) != 0); } catch (...) { currentPage->isSyncFolder = false; }
                    }
                    else if (key == L"FolderPath") currentPage->folderPath = val;
                    else if (key == L"SceneMode") currentPage->sceneMode = Model::PageSceneModeFromKey(val);
                    else if (key.rfind(L"SceneAvailableApp", 0) == 0 && key != L"SceneAvailableAppCount")
                    {
                        if (!val.empty())
                            currentPage->sceneAvailableApps.push_back(val);
                    }
                    else if (key.rfind(L"SceneApp", 0) == 0 && key != L"SceneAppCount")
                    {
                        if (!val.empty())
                            currentPage->sceneApps.push_back(val);
                    }
                }
            }
        }

        if (pages.empty())
        {
            return CreateDefaultConfig();
        }

        for (auto& page : pages)
        {
            for (auto& sc : page.shortcuts)
            {
                if (sc.id.empty())
                {
                    sc.id = GenerateGUID();
                }
            }
        }

        // Load and reconcile shortcuts for synchronized folders
        for (auto& page : pages)
        {
            if (page.isSyncFolder && !page.folderPath.empty())
            {
                std::vector<Model::ShortcutInfo> diskShortcuts = SyncFolderService::LoadShortcuts(page.folderPath);
                std::vector<Model::ShortcutInfo> reconciled;
                std::vector<bool> matched(diskShortcuts.size(), false);

                for (const auto& saved : page.shortcuts)
                {
                    auto it = std::find_if(diskShortcuts.begin(), diskShortcuts.end(), [&](const Model::ShortcutInfo& disk) {
                        return _wcsicmp(disk.targetPath.c_str(), saved.targetPath.c_str()) == 0;
                    });
                    if (it != diskShortcuts.end())
                    {
                        Model::ShortcutInfo disk = *it;
                        disk.id = saved.id;
                        reconciled.push_back(std::move(disk));
                        matched[std::distance(diskShortcuts.begin(), it)] = true;
                    }
                }

                for (size_t i = 0; i < diskShortcuts.size(); ++i)
                {
                    if (!matched[i])
                    {
                        reconciled.push_back(diskShortcuts[i]);
                    }
                }

                page.shortcuts = std::move(reconciled);
            }
        }

        // Update folder watcher
        std::vector<std::wstring> syncFolders;
        for (const auto& page : pages)
        {
            if (page.isSyncFolder && !page.folderPath.empty())
            {
                syncFolders.push_back(page.folderPath);
            }
        }
        m_folderWatcher.UpdateFolders(syncFolders, m_notifyHwnd, m_notifyMessage);

        bool hasDock = false;
        for (const auto& page : pages)
        {
            if (page.name == L"DOCK")
            {
                hasDock = true;
                break;
            }
        }
        if (!hasDock)
        {
            Model::PopupPage dockPage;
            dockPage.name = L"DOCK";
            pages.insert(pages.begin(), std::move(dockPage));
        }

        LOG_INFO(m_logger, L"Loaded %zu pages from config", pages.size());
        return pages;
    }

    virtual void SaveConfig(const std::vector<Model::PopupPage>& pages) override
    {
        std::wstring content;
        content += L"[Settings]\r\n";
        content += L"TriggerType=" + std::to_wstring(m_triggerType) + L"\r\n";
        content += L"PopupColumns=" + std::to_wstring(m_popupColumns) + L"\r\n";
        content += L"PopupRows=" + std::to_wstring(m_popupRows) + L"\r\n";
        content += L"PopupIconSize=" + std::to_wstring(m_popupIconSize) + L"\r\n";
        content += L"PopupIconGap=" + std::to_wstring(m_popupIconGap) + L"\r\n";
        content += L"PopupIconRadius=" + std::to_wstring(m_popupIconRadius) + L"\r\n";
        content += L"PopupWndPadding=" + std::to_wstring(m_popupWndPadding) + L"\r\n";
        content += L"ThemeMode=" + std::to_wstring(m_themeMode) + L"\r\n";
        content += L"ThemeColorIndex=" + std::to_wstring(m_themeColorIndex) + L"\r\n";
        content += L"WindowMode=" + std::to_wstring(m_windowMode) + L"\r\n";
        if (m_hasGlobalScalePercent)
        {
            content += L"GlobalScalePercent=" + std::to_wstring(m_globalScalePercent) + L"\r\n";
        }
        content += L"DockHeight=" + std::to_wstring(m_dockHeight) + L"\r\n";
        content += L"SearchMode=" + std::to_wstring(m_searchMode ? 1 : 0) + L"\r\n";
        content += L"PopupAlignMode=" + std::to_wstring(m_popupAlignMode) + L"\r\n";
        content += L"PopupAutoClose=" + std::to_wstring(m_popupAutoClose ? 1 : 0) + L"\r\n";
        content += L"PopupMultiOpenWhenPinned=" + std::to_wstring(m_popupMultiOpenWhenPinned ? 1 : 0) + L"\r\n";
        content += L"HoverLeaveDelay=" + std::to_wstring(m_hoverLeaveDelay) + L"\r\n";
        content += L"SortMode=" + std::to_wstring(m_sortMode) + L"\r\n";
        content += L"AnimationEnabled=" + std::to_wstring(m_animationEnabled ? 1 : 0) + L"\r\n";
        content += L"AnimationDuration=" + std::to_wstring(m_animationDuration) + L"\r\n";
        content += L"HardwareAccelerationEnabled=" + std::to_wstring(m_hardwareAccelerationEnabled ? 1 : 0) + L"\r\n";
        content += L"HideTrayIcon=" + std::to_wstring(m_hideTrayIcon ? 1 : 0) + L"\r\n";
        content += L"AutoUpdate=" + std::to_wstring(m_autoUpdate ? 1 : 0) + L"\r\n";
        content += L"DarkHue=" + std::to_wstring(m_appearance.dark.hue) + L"\r\n";
        content += L"DarkBlur=" + std::to_wstring(m_appearance.dark.blur) + L"\r\n";
        content += L"DarkOpacity=" + std::to_wstring(m_appearance.dark.opacity) + L"\r\n";
        content += L"DarkHighlight=" + std::to_wstring(m_appearance.dark.highlight) + L"\r\n";
        content += L"DarkBrightness=" + std::to_wstring(m_appearance.dark.brightness) + L"\r\n";
        content += L"DarkSaturation=" + std::to_wstring(m_appearance.dark.saturation) + L"\r\n";
        content += L"LightHue=" + std::to_wstring(m_appearance.light.hue) + L"\r\n";
        content += L"LightBlur=" + std::to_wstring(m_appearance.light.blur) + L"\r\n";
        content += L"LightOpacity=" + std::to_wstring(m_appearance.light.opacity) + L"\r\n";
        content += L"LightHighlight=" + std::to_wstring(m_appearance.light.highlight) + L"\r\n";
        content += L"LightBrightness=" + std::to_wstring(m_appearance.light.brightness) + L"\r\n";
        content += L"LightSaturation=" + std::to_wstring(m_appearance.light.saturation) + L"\r\n";
        content += L"AcrylicDarkHue=" + std::to_wstring(m_appearance.acrylicDark.hue) + L"\r\n";
        content += L"AcrylicDarkBlur=" + std::to_wstring(m_appearance.acrylicDark.blur) + L"\r\n";
        content += L"AcrylicDarkOpacity=" + std::to_wstring(m_appearance.acrylicDark.opacity) + L"\r\n";
        content += L"AcrylicDarkHighlight=" + std::to_wstring(m_appearance.acrylicDark.highlight) + L"\r\n";
        content += L"AcrylicDarkBrightness=" + std::to_wstring(m_appearance.acrylicDark.brightness) + L"\r\n";
        content += L"AcrylicDarkSaturation=" + std::to_wstring(m_appearance.acrylicDark.saturation) + L"\r\n";
        content += L"AcrylicLightHue=" + std::to_wstring(m_appearance.acrylicLight.hue) + L"\r\n";
        content += L"AcrylicLightBlur=" + std::to_wstring(m_appearance.acrylicLight.blur) + L"\r\n";
        content += L"AcrylicLightOpacity=" + std::to_wstring(m_appearance.acrylicLight.opacity) + L"\r\n";
        content += L"AcrylicLightHighlight=" + std::to_wstring(m_appearance.acrylicLight.highlight) + L"\r\n";
        content += L"AcrylicLightBrightness=" + std::to_wstring(m_appearance.acrylicLight.brightness) + L"\r\n";
        content += L"AcrylicLightSaturation=" + std::to_wstring(m_appearance.acrylicLight.saturation) + L"\r\n";
        content += L"GlassDarkHue=" + std::to_wstring(m_appearance.glassDark.hue) + L"\r\n";
        content += L"GlassDarkBlur=" + std::to_wstring(m_appearance.glassDark.blur) + L"\r\n";
        content += L"GlassDarkOpacity=" + std::to_wstring(m_appearance.glassDark.opacity) + L"\r\n";
        content += L"GlassDarkHighlight=" + std::to_wstring(m_appearance.glassDark.highlight) + L"\r\n";
        content += L"GlassDarkBrightness=" + std::to_wstring(m_appearance.glassDark.brightness) + L"\r\n";
        content += L"GlassDarkSaturation=" + std::to_wstring(m_appearance.glassDark.saturation) + L"\r\n";
        content += L"GlassLightHue=" + std::to_wstring(m_appearance.glassLight.hue) + L"\r\n";
        content += L"GlassLightBlur=" + std::to_wstring(m_appearance.glassLight.blur) + L"\r\n";
        content += L"GlassLightOpacity=" + std::to_wstring(m_appearance.glassLight.opacity) + L"\r\n";
        content += L"GlassLightHighlight=" + std::to_wstring(m_appearance.glassLight.highlight) + L"\r\n";
        content += L"GlassLightBrightness=" + std::to_wstring(m_appearance.glassLight.brightness) + L"\r\n";
        content += L"GlassLightSaturation=" + std::to_wstring(m_appearance.glassLight.saturation) + L"\r\n\r\n";

        int pageIndex = 0;
        for (const auto& page : pages)
        {
            content += L"[Page:" + std::to_wstring(pageIndex) + L"]\r\n";
            content += L"Name=" + EscapeValue(page.name) + L"\r\n";
            content += L"ShortcutCount=" + std::to_wstring(page.shortcuts.size()) + L"\r\n";
            if (!page.folderPath.empty())
            {
                // Write IsSyncFolder=1 for active sync, IsSyncFolder=0 for paused
                content += L"IsSyncFolder=" + std::to_wstring(page.isSyncFolder ? 1 : 0) + L"\r\n";
                content += L"FolderPath=" + EscapeValue(page.folderPath) + L"\r\n";
            }
            if (!page.sceneApps.empty())
            {
                content += L"SceneMode=" + std::wstring(Model::PageSceneModeKey(page.sceneMode)) + L"\r\n";
                content += L"SceneAppCount=" + std::to_wstring(page.sceneApps.size()) + L"\r\n";
                for (size_t appIndex = 0; appIndex < page.sceneApps.size(); ++appIndex)
                {
                    content += L"SceneApp" + std::to_wstring(appIndex) + L"=" + EscapeValue(page.sceneApps[appIndex]) + L"\r\n";
                }
            }
            if (!page.sceneAvailableApps.empty())
            {
                content += L"SceneAvailableAppCount=" + std::to_wstring(page.sceneAvailableApps.size()) + L"\r\n";
                for (size_t appIndex = 0; appIndex < page.sceneAvailableApps.size(); ++appIndex)
                {
                    content += L"SceneAvailableApp" + std::to_wstring(appIndex) + L"=" + EscapeValue(page.sceneAvailableApps[appIndex]) + L"\r\n";
                }
            }
            content += L"\r\n";

            int shortcutIndex = 0;
            for (const auto& sc : page.shortcuts)
            {
                content += L"[Item:" + std::to_wstring(pageIndex) + L":" + std::to_wstring(shortcutIndex) + L"]\r\n";
                content += L"Id=" + EscapeValue(sc.id.empty() ? GenerateGUID() : sc.id) + L"\r\n";
                content += L"Name=" + EscapeValue(sc.name) + L"\r\n";
                content += L"Type=" + std::wstring(Model::ShortcutTypeKey(sc.type)) + L"\r\n";
                content += L"TargetKind=" + std::wstring(Model::ShortcutTargetKindKey(sc.targetKind)) + L"\r\n";
                content += L"TargetPath=" + EscapeValue(sc.targetPath) + L"\r\n";
                content += L"Arguments=" + EscapeValue(sc.arguments) + L"\r\n";
                content += L"RunAsAdmin=" + std::to_wstring(sc.runAsAdmin ? 1 : 0) + L"\r\n";
                content += L"IconSource=" + std::wstring(Model::IconSourceKey(sc.iconSource)) + L"\r\n";
                content += L"IconPath=" + EscapeValue(sc.iconPath) + L"\r\n";
                content += L"BuiltinIconId=" + EscapeValue(sc.builtinIconId) + L"\r\n";
                content += L"IconInvertLight=" + std::to_wstring(sc.iconInvertLight ? 1 : 0) + L"\r\n";
                content += L"IconInvertDark=" + std::to_wstring(sc.iconInvertDark ? 1 : 0) + L"\r\n\r\n";
                shortcutIndex++;
            }
            pageIndex++;
        }
        ConfigPath::EnsureDirectoryExists(m_configDir);
        if (ProtectedWriteFile(m_configFilePath, content))
        {
            LOG_INFO(m_logger, L"Saved %zu pages to config", pages.size());
        }
        else
        {
            LOG_ERROR(m_logger, L"Failed to save config to %s", m_configFilePath.c_str());
        }
    }

    virtual std::wstring GetConfigDir() const override { return m_configDir; }
    virtual std::wstring GetConfigFilePath() const override { return m_configFilePath; }
    virtual std::wstring GetConfigHistoryDir() const override { return m_historyDir; }

    virtual std::vector<ConfigHistoryEntry> GetConfigHistory() override
    {
        std::vector<ConfigHistoryEntry> entries;
        ConfigPath::EnsureDirectoryExists(m_historyDir);

        WIN32_FIND_DATAW fd{};
        std::wstring pattern = m_historyDir + L"\\*.ini";
        HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
        if (hFind == INVALID_HANDLE_VALUE)
            return entries;

        do
        {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;

            ConfigHistoryEntry entry;
            entry.displayName = fd.cFileName;
            entry.filePath = m_historyDir + L"\\" + entry.displayName;
            entry.lastWriteTime = FileTimeToUInt64(fd.ftLastWriteTime);
            ULARGE_INTEGER size{};
            size.HighPart = fd.nFileSizeHigh;
            size.LowPart = fd.nFileSizeLow;
            entry.sizeBytes = size.QuadPart;
            entry.kind = ParseHistoryKind(entry.displayName);
            entries.push_back(std::move(entry));
        } while (FindNextFileW(hFind, &fd));

        FindClose(hFind);
        std::sort(entries.begin(), entries.end(), [](const ConfigHistoryEntry& a, const ConfigHistoryEntry& b) {
            return a.lastWriteTime > b.lastWriteTime;
        });
        return entries;
    }

    virtual bool CreateConfigBackup(const std::wstring& reason) override
    {
        bool ok = BackupCurrentConfig(reason);
        if (ok)
            CancelPendingAutoBackup();
        return ok;
    }

    virtual bool RestoreConfigBackup(const std::wstring& backupPath) override
    {
        if (!IsPathUnderDirectory(m_historyDir, backupPath))
        {
            LOG_ERROR(m_logger, L"Refused config restore outside history dir: %s", backupPath.c_str());
            return false;
        }
        if (GetFileAttributesW(backupPath.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            LOG_ERROR(m_logger, L"Config restore backup not found: %s", backupPath.c_str());
            return false;
        }

        CancelPendingAutoBackup();
        BackupCurrentConfig(L"before-restore");
        ConfigPath::EnsureDirectoryExists(m_configDir);
        std::wstring tempPath = m_configFilePath + L".restore.tmp";
        DeleteFileW(tempPath.c_str());
        if (!CopyFileW(backupPath.c_str(), tempPath.c_str(), FALSE))
        {
            LOG_ERROR(m_logger, L"Failed to copy config restore temp from %s", backupPath.c_str());
            return false;
        }
        if (!MoveFileExW(tempPath.c_str(), m_configFilePath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            DeleteFileW(tempPath.c_str());
            LOG_ERROR(m_logger, L"Failed to replace config from backup: %s", backupPath.c_str());
            return false;
        }

        LoadConfig();
        LOG_INFO(m_logger, L"Restored config from backup: %s", backupPath.c_str());
        return true;
    }

    virtual bool ClearConfig() override
    {
        CancelPendingAutoBackup();
        bool hasCurrentConfig = GetFileAttributesW(m_configFilePath.c_str()) != INVALID_FILE_ATTRIBUTES;
        if (hasCurrentConfig && !BackupCurrentConfig(L"auto"))
        {
            LOG_ERROR(m_logger, L"Refused to clear config because immediate auto backup failed: %s", m_configFilePath.c_str());
            return false;
        }
        if (!DeleteFileW(m_configFilePath.c_str()) && GetFileAttributesW(m_configFilePath.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            LOG_ERROR(m_logger, L"Failed to delete config before clearing: %s", m_configFilePath.c_str());
            return false;
        }
        ResetRuntimeSettingsToDefaults();
        CreateDefaultConfig();
        if (GetFileAttributesW(m_configFilePath.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            LOG_ERROR(m_logger, L"Failed to recreate default config after clearing");
            return false;
        }
        LOG_INFO(m_logger, L"Cleared config and recreated defaults");
        return true;
    }

    virtual bool ClearConfigHistory() override
    {
        auto entries = GetConfigHistory();
        bool ok = true;
        for (const auto& entry : entries)
        {
            if (!DeleteFileW(entry.filePath.c_str()))
            {
                ok = false;
                LOG_ERROR(m_logger, L"Failed to delete config history item: %s", entry.filePath.c_str());
            }
        }
        return ok;
    }

    virtual Model::AppearanceSettings GetAppearanceSettings() const override { return m_appearance; }
    virtual void SetAppearanceSettings(const Model::AppearanceSettings& settings) override { m_appearance = settings; }

    virtual int GetTriggerType() override { return m_triggerType; }
    virtual void SetTriggerType(int type) override { m_triggerType = type; }

    virtual int GetPopupColumns() override { return m_popupColumns; }
    virtual void SetPopupColumns(int columns) override { m_popupColumns = columns; }
    virtual int GetPopupRows() override { return m_popupRows; }
    virtual void SetPopupRows(int rows) override { m_popupRows = rows; }
    virtual int GetPopupIconSize() override { return m_popupIconSize; }
    virtual void SetPopupIconSize(int size) override { m_popupIconSize = size; }
    virtual int GetPopupIconGap() override { return m_popupIconGap; }
    virtual void SetPopupIconGap(int gap) override { m_popupIconGap = gap; }
    virtual int GetPopupIconRadius() override { return m_popupIconRadius; }
    virtual void SetPopupIconRadius(int radius) override { m_popupIconRadius = radius; }
    virtual int GetPopupWndPadding() override { return m_popupWndPadding; }
    virtual void SetPopupWndPadding(int padding) override { m_popupWndPadding = padding; }
    virtual int GetTheme() override { return m_themeMode; }
    virtual void SetTheme(int theme) override { m_themeMode = theme; }
    virtual int GetThemeColor() override { return m_themeColorIndex; }
    virtual void SetThemeColor(int colorIndex) override
    {
        if (colorIndex < 0) colorIndex = 0;
        m_themeColorIndex = colorIndex;
    }
    virtual int GetWindowMode() override { return m_windowMode; }
    virtual void SetWindowMode(int mode) override { m_windowMode = mode; }
    virtual int GetGlobalScalePercent() override { return m_globalScalePercent; }
    virtual bool HasCustomGlobalScalePercent() override { return m_hasGlobalScalePercent; }
    virtual void SetGlobalScalePercent(int percent) override
    {
        if (percent < 80) percent = 80;
        if (percent > 250) percent = 250;
        int remainder = percent % 10;
        if (remainder != 0)
            percent += (remainder >= 5) ? (10 - remainder) : -remainder;
        if (percent < 80) percent = 80;
        if (percent > 250) percent = 250;
        m_globalScalePercent = percent;
        m_hasGlobalScalePercent = true;
    }
    virtual int GetDockHeight() override { return m_dockHeight; }
    virtual void SetDockHeight(int height) override { m_dockHeight = height; }
    virtual bool GetSearchMode() override { return m_searchMode; }
    virtual void SetSearchMode(bool enabled) override { m_searchMode = enabled; }
    virtual int GetPopupAlignMode() override { return m_popupAlignMode; }
    virtual void SetPopupAlignMode(int mode) override
    {
        if (mode < 0) mode = 0;
        if (mode > 3) mode = 3;
        m_popupAlignMode = mode;
    }
    virtual bool GetPopupAutoClose() override { return m_popupAutoClose; }
    virtual void SetPopupAutoClose(bool enabled) override { m_popupAutoClose = enabled; }
    virtual bool GetPopupMultiOpenWhenPinned() override { return m_popupMultiOpenWhenPinned; }
    virtual void SetPopupMultiOpenWhenPinned(bool enabled) override { m_popupMultiOpenWhenPinned = enabled; }
    virtual int GetHoverLeaveDelay() override { return m_hoverLeaveDelay; }
    virtual void SetHoverLeaveDelay(int delayMs) override
    {
        if (delayMs < 0) delayMs = 0;
        if (delayMs > 5000) delayMs = 5000;
        m_hoverLeaveDelay = delayMs;
    }
    virtual int GetSortMode() override { return m_sortMode; }
    virtual void SetSortMode(int mode) override
    {
        if (mode < 0) mode = 0;
        if (mode > 1) mode = 1;
        m_sortMode = mode;
    }
    virtual bool GetAnimationEnabled() override { return m_animationEnabled; }
    virtual void SetAnimationEnabled(bool enabled) override { m_animationEnabled = enabled; }
    virtual int GetAnimationDuration() override { return m_animationDuration; }
    virtual void SetAnimationDuration(int duration) override { m_animationDuration = duration; }
    virtual bool GetHardwareAccelerationEnabled() override { return m_hardwareAccelerationEnabled; }
    virtual void SetHardwareAccelerationEnabled(bool enabled) override { m_hardwareAccelerationEnabled = enabled; }

    virtual bool GetAutoStart() override
    {
        AutoStartHelper::CleanupRegistryRunValue();
        return AutoStartHelper::TaskExists();
    }

    virtual void SetAutoStart(bool enable) override
    {
        AutoStartHelper::SetEnabled(enable);
    }

    virtual bool GetHideTrayIcon() override { return m_hideTrayIcon; }
    virtual void SetHideTrayIcon(bool hide) override { m_hideTrayIcon = hide; }
    virtual bool GetAutoUpdate() override { return m_autoUpdate; }
    virtual void SetAutoUpdate(bool enable) override { m_autoUpdate = enable; }

    virtual ~IniConfigRepository() override
    {
        StopAutoBackupWorker();
        m_folderWatcher.Stop();
    }

private:
    static std::vector<std::wstring> Split(const std::wstring& s, wchar_t delim)
    {
        std::vector<std::wstring> result;
        std::wstring current;
        for (wchar_t ch : s)
        {
            if (ch == delim)
            {
                result.push_back(current);
                current.clear();
            }
            else
            {
                current.push_back(ch);
            }
        }
        result.push_back(current);
        return result;
    }

    static void Trim(std::wstring& s)
    {
        while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n' || s.back() == L' ' || s.back() == L'\t'))
            s.pop_back();
        size_t start = 0;
        while (start < s.size() && (s[start] == L' ' || s[start] == L'\t'))
            start++;
        if (start > 0) s = s.substr(start);
    }

    static std::wstring EscapeValue(const std::wstring& value)
    {
        std::wstring result;
        result.reserve(value.size());
        for (wchar_t ch : value)
        {
            switch (ch)
            {
            case L'\\': result += L"\\\\"; break;
            case L'\r': result += L"\\r"; break;
            case L'\n': result += L"\\n"; break;
            case L'\t': result += L"\\t"; break;
            default: result.push_back(ch); break;
            }
        }
        return result;
    }

    static std::wstring UnescapeValue(const std::wstring& value)
    {
        std::wstring result;
        result.reserve(value.size());
        bool escaping = false;
        for (wchar_t ch : value)
        {
            if (!escaping)
            {
                if (ch == L'\\')
                {
                    escaping = true;
                }
                else
                {
                    result.push_back(ch);
                }
                continue;
            }

            switch (ch)
            {
            case L'\\': result.push_back(L'\\'); break;
            case L'r': result.push_back(L'\r'); break;
            case L'n': result.push_back(L'\n'); break;
            case L't': result.push_back(L'\t'); break;
            default:
                result.push_back(L'\\');
                result.push_back(ch);
                break;
            }
            escaping = false;
        }
        if (escaping)
            result.push_back(L'\\');
        return result;
    }

    static void ParseShortcutLine(const std::wstring& val, Model::ShortcutInfo& info)
    {
        auto parts = Split(val, L'|');
        if (parts.size() < 2) return;

        info.name = parts[0];
        info.targetPath = parts[1];
        if (parts.size() > 2) info.arguments = parts[2];
        if (parts.size() > 3) info.iconPath = parts[3];
        if (parts.size() > 4) info.runAsAdmin = (parts[4] == L"1");
        if (parts.size() > 5)
        {
            try { info.type = (Model::ShortcutType)std::stoi(parts[5]); } catch (...) {}
        }
        if (parts.size() > 6) info.iconInvertLight = (parts[6] == L"1");
        if (parts.size() > 7) info.iconInvertDark = (parts[7] == L"1");
    }

    std::vector<Model::PopupPage> CreateDefaultConfig()
    {
        std::vector<Model::PopupPage> pages;
        Model::PopupPage dockPage;
        dockPage.name = L"DOCK";
        pages.push_back(std::move(dockPage));

        Model::PopupPage commonPage;
        commonPage.name = L"常用";
        pages.push_back(std::move(commonPage));

        SaveConfig(pages);
        LOG_INFO(m_logger, L"Created default config at %s", m_configFilePath.c_str());
        return pages;
    }

    void ResetRuntimeSettingsToDefaults()
    {
        m_appearance = Model::AppearanceSettings{};
        m_triggerType = 0;
        m_popupColumns = 6;
        m_popupRows = 4;
        m_popupIconSize = 24;
        m_popupIconGap = 4;
        m_popupIconRadius = 6;
        m_popupWndPadding = 8;
        m_themeMode = 0;
        m_themeColorIndex = 0;
        m_windowMode = 0;
        m_globalScalePercent = 100;
        m_hasGlobalScalePercent = false;
        m_dockHeight = 1;
        m_searchMode = false;
        m_popupAlignMode = 0;
        m_popupAutoClose = true;
        m_popupMultiOpenWhenPinned = false;
        m_hoverLeaveDelay = 200;
        m_sortMode = 0;
        m_animationEnabled = true;
        m_animationDuration = 200;
        m_hardwareAccelerationEnabled = true;
        m_hideTrayIcon = false;
        m_autoUpdate = true;
    }

    static std::wstring ReadFile(const std::wstring& path)
    {
        std::ifstream fs(path, std::ios::binary);
        if (!fs) return L"";
        std::string bytes((std::istreambuf_iterator<char>(fs)), std::istreambuf_iterator<char>());
        if (bytes.empty()) return L"";
        int len = MultiByteToWideChar(CP_UTF8, 0, bytes.c_str(), (int)bytes.size(), nullptr, 0);
        std::wstring wstr(len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, bytes.c_str(), (int)bytes.size(), &wstr[0], len);
        return wstr;
    }

    static bool WriteFile(const std::wstring& path, const std::wstring& wstr)
    {
        std::ofstream fs(path, std::ios::binary | std::ios::trunc);
        if (!fs) return false;
        int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
        std::string bytes(len, L'\0');
        WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &bytes[0], len, nullptr, nullptr);
        fs.write(bytes.data(), bytes.size());
        return true;
    }

    bool ProtectedWriteFile(const std::wstring& path, const std::wstring& wstr)
    {
        std::wstring existing = ReadFile(path);
        bool contentChanged = !existing.empty() && existing != wstr;

        std::wstring tempPath = path + L".tmp";
        DeleteFileW(tempPath.c_str());
        if (!WriteFile(tempPath, wstr))
            return false;

        if (MoveFileExW(tempPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            if (contentChanged)
                ScheduleAutoBackup();
            PruneConfigHistory(80);
            return true;
        }

        DeleteFileW(tempPath.c_str());
        return false;
    }

    void ScheduleAutoBackup()
    {
        {
            std::lock_guard<std::mutex> lock(m_autoBackupMutex);
            m_autoBackupPending = true;
            m_autoBackupDue = std::chrono::steady_clock::now() + std::chrono::seconds(AutoBackupDebounceSeconds);
            if (!m_autoBackupWorkerStarted)
            {
                m_autoBackupWorkerStarted = true;
                m_autoBackupThread = std::thread([this]() { AutoBackupWorkerLoop(); });
            }
        }
        m_autoBackupCv.notify_one();
        LOG_INFO(m_logger, L"Scheduled auto config backup in %d seconds", AutoBackupDebounceSeconds);
    }

    void CancelPendingAutoBackup()
    {
        {
            std::lock_guard<std::mutex> lock(m_autoBackupMutex);
            m_autoBackupPending = false;
        }
        m_autoBackupCv.notify_one();
    }

    void StopAutoBackupWorker()
    {
        {
            std::lock_guard<std::mutex> lock(m_autoBackupMutex);
            m_autoBackupStopping = true;
            m_autoBackupPending = false;
        }
        m_autoBackupCv.notify_one();
        if (m_autoBackupThread.joinable())
            m_autoBackupThread.join();
    }

    void AutoBackupWorkerLoop()
    {
        std::unique_lock<std::mutex> lock(m_autoBackupMutex);
        while (!m_autoBackupStopping)
        {
            if (!m_autoBackupPending)
            {
                m_autoBackupCv.wait(lock, [this]() {
                    return m_autoBackupStopping || m_autoBackupPending;
                });
                continue;
            }

            auto due = m_autoBackupDue;
            bool interrupted = m_autoBackupCv.wait_until(lock, due, [this, due]() {
                return m_autoBackupStopping || !m_autoBackupPending || m_autoBackupDue != due;
            });
            if (interrupted)
                continue;

            m_autoBackupPending = false;
            lock.unlock();
            BackupCurrentConfig(L"auto");
            lock.lock();
        }
    }

    bool BackupCurrentConfig(const std::wstring& reason)
    {
        if (GetFileAttributesW(m_configFilePath.c_str()) == INVALID_FILE_ATTRIBUTES)
            return false;

        ConfigPath::EnsureDirectoryExists(m_historyDir);
        std::wstring backupPath = MakeHistoryPath(reason);
        if (!CopyFileW(m_configFilePath.c_str(), backupPath.c_str(), FALSE))
        {
            LOG_ERROR(m_logger, L"Failed to create config backup: %s", backupPath.c_str());
            return false;
        }
        PruneConfigHistory(80);
        LOG_INFO(m_logger, L"Created config backup: %s", backupPath.c_str());
        return true;
    }

    std::wstring MakeHistoryPath(const std::wstring& reason)
    {
        std::wstring safeReason = SanitizeHistoryReason(reason);
        if (safeReason.empty())
            safeReason = L"manual";

        std::wstring base = m_historyDir + L"\\" + MakeTimestamp() + L"-" + safeReason;
        std::wstring path = base + L".ini";
        int suffix = 1;
        while (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            path = base + L"-" + std::to_wstring(suffix++) + L".ini";
        }
        return path;
    }

    static std::wstring MakeTimestamp()
    {
        SYSTEMTIME st{};
        GetLocalTime(&st);
        wchar_t buf[32]{};
        swprintf_s(buf, L"%04u%02u%02u-%02u%02u%02u-%03u",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        return buf;
    }

    static std::wstring SanitizeHistoryReason(const std::wstring& reason)
    {
        std::wstring result;
        result.reserve(reason.size());
        for (wchar_t ch : reason)
        {
            if ((ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') ||
                (ch >= L'0' && ch <= L'9') || ch == L'-' || ch == L'_')
            {
                result.push_back(ch);
            }
        }
        return result;
    }

    static std::wstring ParseHistoryKind(const std::wstring& fileName)
    {
        size_t firstDash = fileName.find(L'-');
        if (firstDash == std::wstring::npos) return L"unknown";
        size_t secondDash = fileName.find(L'-', firstDash + 1);
        if (secondDash == std::wstring::npos) return L"unknown";
        size_t dot = fileName.rfind(L'.');
        if (dot == std::wstring::npos || dot <= secondDash + 1) return L"unknown";
        return fileName.substr(secondDash + 1, dot - secondDash - 1);
    }

    static unsigned long long FileTimeToUInt64(const FILETIME& ft)
    {
        ULARGE_INTEGER value{};
        value.LowPart = ft.dwLowDateTime;
        value.HighPart = ft.dwHighDateTime;
        return value.QuadPart;
    }

    static std::wstring FullPath(const std::wstring& path)
    {
        DWORD len = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
        if (len == 0) return path;
        std::wstring result(len, L'\0');
        DWORD written = GetFullPathNameW(path.c_str(), len, &result[0], nullptr);
        if (written == 0) return path;
        result.resize(written);
        return result;
    }

    static bool StartsWithNoCase(const std::wstring& value, const std::wstring& prefix)
    {
        if (value.size() < prefix.size()) return false;
        for (size_t i = 0; i < prefix.size(); ++i)
        {
            if (towlower(value[i]) != towlower(prefix[i]))
                return false;
        }
        return true;
    }

    static bool IsPathUnderDirectory(const std::wstring& dir, const std::wstring& path)
    {
        std::wstring fullDir = FullPath(dir);
        std::wstring fullPath = FullPath(path);
        while (!fullDir.empty() && (fullDir.back() == L'\\' || fullDir.back() == L'/'))
            fullDir.pop_back();
        fullDir += L"\\";
        return StartsWithNoCase(fullPath, fullDir);
    }

    void PruneConfigHistory(size_t keepCount)
    {
        auto entries = GetConfigHistory();
        if (entries.size() <= keepCount)
            return;

        for (size_t i = keepCount; i < entries.size(); ++i)
        {
            DeleteFileW(entries[i].filePath.c_str());
        }
    }

    std::wstring m_configDir;
    std::wstring m_configFilePath;
    std::wstring m_historyDir;
    Logger* m_logger = nullptr;
    HWND m_notifyHwnd = nullptr;
    UINT m_notifyMessage = 0;
    Model::AppearanceSettings m_appearance;
    int m_triggerType = 0;
    int m_popupColumns = 6;
    int m_popupRows = 4;
    int m_popupIconSize = 24;
    int m_popupIconGap = 4;
    int m_popupIconRadius = 6;
    int m_popupWndPadding = 8;
    int m_themeMode = 0;
    int m_themeColorIndex = 0;
    int m_windowMode = 0;
    int m_globalScalePercent = 100;
    bool m_hasGlobalScalePercent = false;
    int m_dockHeight = 50;
    bool m_searchMode = false;
    int m_popupAlignMode = 0;
    bool m_popupAutoClose = true;
    bool m_popupMultiOpenWhenPinned = false;
    int m_hoverLeaveDelay = 200;
    int m_sortMode = 0;
    bool m_animationEnabled = true;
    int m_animationDuration = 200;
    bool m_hardwareAccelerationEnabled = true;
    bool m_hideTrayIcon;
    bool m_autoUpdate;
    std::mutex m_autoBackupMutex;
    std::condition_variable m_autoBackupCv;
    std::thread m_autoBackupThread;
    bool m_autoBackupWorkerStarted = false;
    bool m_autoBackupStopping = false;
    bool m_autoBackupPending = false;
    std::chrono::steady_clock::time_point m_autoBackupDue{};
    FolderWatcher m_folderWatcher;
};
