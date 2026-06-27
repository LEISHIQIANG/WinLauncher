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

#include "FolderWatcher.h"
#include "SyncFolderService.h"
#include "ConfigPath.h"

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "advapi32.lib")

class IniConfigRepository : public IConfigService
{
public:
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
        , m_dockHeight(1)
        , m_searchMode(false)
    {
        m_configDir = ConfigPath::PrepareUserConfigDirectory();
        m_configFilePath = m_configDir + L"\\launcher_config.ini";
    }

    virtual std::vector<Model::PopupPage> LoadConfig() override
    {
        std::vector<Model::PopupPage> pages;

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
        bool inSettings = false;

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
                }
                else if (sec.rfind(L"Page:", 0) == 0)
                {
                    inSettings = false;
                    Model::PopupPage page;
                    page.name = sec.substr(5);
                    pages.push_back(std::move(page));
                    currentPage = &pages.back();
                }
                else
                {
                    inSettings = false;
                    currentPage = nullptr;
                }
            }
            else if (inSettings)
            {
                size_t eq = line.find(L'=');
                if (eq != std::wstring::npos)
                {
                    std::wstring key = line.substr(0, eq);
                    std::wstring val = line.substr(eq + 1);
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
                    else if (key == L"DockHeight")
                    {
                        try { m_dockHeight = std::stoi(val); if (m_dockHeight < 1) m_dockHeight = 1; if (m_dockHeight > 5) m_dockHeight = 5; } catch (...) { m_dockHeight = 1; }
                    }
                    else if (key == L"SearchMode")
                    {
                        try { m_searchMode = (std::stoi(val) != 0); } catch (...) { m_searchMode = false; }
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
                        try { m_appearance.dark.brightness = std::stof(val); } catch (...) {}
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
                        try { m_appearance.light.brightness = std::stof(val); } catch (...) {}
                    }
                    else if (key == L"LightSaturation")
                    {
                        try { m_appearance.light.saturation = std::stof(val); } catch (...) {}
                    }
                    else if (key == L"AcrylicDarkHue")
                    {
                        try { m_appearance.acrylicDark.hue = std::stof(val); } catch (...) {}
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
                        try { m_appearance.acrylicDark.brightness = std::stof(val); } catch (...) {}
                    }
                    else if (key == L"AcrylicLightHue")
                    {
                        try { m_appearance.acrylicLight.hue = std::stof(val); } catch (...) {}
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
                        try { m_appearance.acrylicLight.brightness = std::stof(val); } catch (...) {}
                    }
                }
            }
            else if (currentPage)
            {
                if (line.rfind(L"IsSyncFolder=", 0) == 0)
                {
                    try { currentPage->isSyncFolder = (std::stoi(line.substr(13)) != 0); } catch (...) {}
                }
                else if (line.rfind(L"FolderPath=", 0) == 0)
                {
                    currentPage->folderPath = line.substr(11);
                }
                else if (line.rfind(L"Shortcut=", 0) == 0)
                {
                    Model::ShortcutInfo info;
                    ParseShortcutLine(line.substr(9), info);
                    currentPage->shortcuts.push_back(std::move(info));
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
                        reconciled.push_back(*it);
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
        content += L"DockHeight=" + std::to_wstring(m_dockHeight) + L"\r\n";
        content += L"SearchMode=" + std::to_wstring(m_searchMode ? 1 : 0) + L"\r\n";
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
        content += L"AcrylicDarkOpacity=" + std::to_wstring(m_appearance.acrylicDark.opacity) + L"\r\n";
        content += L"AcrylicDarkHighlight=" + std::to_wstring(m_appearance.acrylicDark.highlight) + L"\r\n";
        content += L"AcrylicDarkBrightness=" + std::to_wstring(m_appearance.acrylicDark.brightness) + L"\r\n";
        content += L"AcrylicLightHue=" + std::to_wstring(m_appearance.acrylicLight.hue) + L"\r\n";
        content += L"AcrylicLightOpacity=" + std::to_wstring(m_appearance.acrylicLight.opacity) + L"\r\n";
        content += L"AcrylicLightHighlight=" + std::to_wstring(m_appearance.acrylicLight.highlight) + L"\r\n";
        content += L"AcrylicLightBrightness=" + std::to_wstring(m_appearance.acrylicLight.brightness) + L"\r\n\r\n";

        for (const auto& page : pages)
        {
            content += L"[Page:" + page.name + L"]\r\n";
            if (!page.folderPath.empty())
            {
                // Write IsSyncFolder=1 for active sync, IsSyncFolder=0 for paused
                content += L"IsSyncFolder=" + std::to_wstring(page.isSyncFolder ? 1 : 0) + L"\r\n";
                content += L"FolderPath=" + page.folderPath + L"\r\n";
            }
            for (const auto& sc : page.shortcuts)
            {
                content += L"Shortcut=" + sc.name + L"|" + sc.targetPath + L"|" + sc.arguments + L"\r\n";
            }
            content += L"\r\n";
        }
        ConfigPath::EnsureDirectoryExists(m_configDir);
        if (WriteFile(m_configFilePath, content))
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
    virtual int GetDockHeight() override { return m_dockHeight; }
    virtual void SetDockHeight(int height) override { m_dockHeight = height; }
    virtual bool GetSearchMode() override { return m_searchMode; }
    virtual void SetSearchMode(bool enabled) override { m_searchMode = enabled; }

    virtual bool GetAutoStart() override
    {
        AutoStartHelper::CleanupRegistryRunValue();
        return AutoStartHelper::TaskExists();
    }

    virtual void SetAutoStart(bool enable) override
    {
        AutoStartHelper::SetEnabled(enable);
    }

    virtual ~IniConfigRepository() override
    {
        m_folderWatcher.Stop();
    }

private:
    static void Trim(std::wstring& s)
    {
        while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n' || s.back() == L' ' || s.back() == L'\t'))
            s.pop_back();
        size_t start = 0;
        while (start < s.size() && (s[start] == L' ' || s[start] == L'\t'))
            start++;
        if (start > 0) s = s.substr(start);
    }

    static void ParseShortcutLine(const std::wstring& val, Model::ShortcutInfo& info)
    {
        size_t p1 = val.find(L'|');
        if (p1 == std::wstring::npos) return;

        info.name = val.substr(0, p1);
        std::wstring rest = val.substr(p1 + 1);
        size_t p2 = rest.find(L'|');
        if (p2 != std::wstring::npos)
        {
            info.targetPath = rest.substr(0, p2);
            info.arguments = rest.substr(p2 + 1);
        }
        else
        {
            info.targetPath = rest;
        }
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

    std::wstring m_configDir;
    std::wstring m_configFilePath;
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
    int m_dockHeight = 50;
    bool m_searchMode = false;
    FolderWatcher m_folderWatcher;
};
