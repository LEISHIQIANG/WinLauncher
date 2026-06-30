#pragma once
#include "../Model/AppearanceSettings.h"
#include "../Model/ShortcutInfo.h"
#include <string>
#include <vector>

struct ConfigHistoryEntry
{
    std::wstring filePath;
    std::wstring displayName;
    std::wstring kind;
    unsigned long long lastWriteTime = 0;
    unsigned long long sizeBytes = 0;
};

class IConfigService
{
public:
    virtual ~IConfigService() = default;

    virtual std::vector<Model::PopupPage> LoadConfig() = 0;
    virtual void SaveConfig(const std::vector<Model::PopupPage>& pages) = 0;
    virtual std::wstring GetConfigDir() const = 0;
    virtual std::wstring GetConfigFilePath() const = 0;
    virtual std::wstring GetConfigHistoryDir() const = 0;
    virtual std::vector<ConfigHistoryEntry> GetConfigHistory() = 0;
    virtual bool CreateConfigBackup(const std::wstring& reason) = 0;
    virtual bool RestoreConfigBackup(const std::wstring& backupPath) = 0;
    virtual bool ClearConfig() = 0;
    virtual bool ClearConfigHistory() = 0;
    virtual Model::AppearanceSettings GetAppearanceSettings() const = 0;
    virtual void SetAppearanceSettings(const Model::AppearanceSettings& settings) = 0;

    virtual int GetTriggerType() = 0;
    virtual void SetTriggerType(int type) = 0;
    virtual bool GetAutoStart() = 0;
    virtual void SetAutoStart(bool enable) = 0;
    virtual bool GetHideTrayIcon() = 0;
    virtual void SetHideTrayIcon(bool hide) = 0;
    virtual bool GetAutoUpdate() = 0;
    virtual void SetAutoUpdate(bool enable) = 0;

    virtual int GetPopupColumns() = 0;
    virtual void SetPopupColumns(int columns) = 0;
    virtual int GetPopupRows() = 0;
    virtual void SetPopupRows(int rows) = 0;
    virtual int GetPopupIconSize() = 0;
    virtual void SetPopupIconSize(int size) = 0;
    virtual int GetPopupIconGap() = 0;
    virtual void SetPopupIconGap(int gap) = 0;
    virtual int GetPopupIconRadius() = 0;
    virtual void SetPopupIconRadius(int radius) = 0;
    virtual int GetPopupWndPadding() = 0;
    virtual void SetPopupWndPadding(int padding) = 0;
    virtual int GetTheme() = 0;
    virtual void SetTheme(int theme) = 0;
    virtual int GetThemeColor() = 0;
    virtual void SetThemeColor(int colorIndex) = 0;
    virtual int GetWindowMode() = 0;
    virtual void SetWindowMode(int mode) = 0;
    virtual int GetGlobalScalePercent() = 0;
    virtual bool HasCustomGlobalScalePercent() = 0;
    virtual void SetGlobalScalePercent(int percent) = 0;
    virtual int GetDockHeight() = 0;
    virtual void SetDockHeight(int height) = 0;
    virtual bool GetSearchMode() = 0;
    virtual void SetSearchMode(bool enabled) = 0;
    virtual int GetPopupAlignMode() = 0;
    virtual void SetPopupAlignMode(int mode) = 0;
    virtual bool GetPopupAutoClose() = 0;
    virtual void SetPopupAutoClose(bool enabled) = 0;
    virtual bool GetPopupMultiOpenWhenPinned() = 0;
    virtual void SetPopupMultiOpenWhenPinned(bool enabled) = 0;
    virtual int GetHoverLeaveDelay() = 0;
    virtual void SetHoverLeaveDelay(int delayMs) = 0;
    virtual int GetSortMode() = 0;
    virtual void SetSortMode(int mode) = 0;

    virtual bool GetAnimationEnabled() = 0;
    virtual void SetAnimationEnabled(bool enabled) = 0;
    virtual int GetAnimationDuration() = 0;
    virtual void SetAnimationDuration(int duration) = 0;
    virtual bool GetHardwareAccelerationEnabled() = 0;
    virtual void SetHardwareAccelerationEnabled(bool enabled) = 0;
};
