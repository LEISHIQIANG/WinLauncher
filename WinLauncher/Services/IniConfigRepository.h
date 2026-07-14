#pragma once
#include "../Contracts/IConfigService.h"
#include <Windows.h>
#include <memory>

class Logger;

// Public config contract. Parsing, recovery, history and file watching live in
// the implementation unit so ordinary callers do not recompile for INI changes.
class IniConfigRepository final : public IConfigService
{
public:
    explicit IniConfigRepository(Logger* logger = nullptr, HWND notifyHwnd = nullptr, UINT notifyMessage = 0);
    // Test-only construction seam: callers can isolate config I/O in a
    // temporary directory without changing the production AppData location.
    IniConfigRepository(const std::wstring& configDirectory, Logger* logger, HWND notifyHwnd = nullptr, UINT notifyMessage = 0);
    ~IniConfigRepository() override;

    std::vector<Model::PopupPage> LoadConfig() override;
    void SaveConfig(const std::vector<Model::PopupPage>& pages) override;
    bool FlushPendingConfig() override;
    std::wstring GetConfigDir() const override;
    std::wstring GetConfigFilePath() const override;
    std::wstring GetConfigHistoryDir() const override;
    std::vector<ConfigHistoryEntry> GetConfigHistory() override;
    bool CreateConfigBackup(const std::wstring& reason) override;
    bool RestoreConfigBackup(const std::wstring& backupPath) override;
    bool ClearConfig() override;
    bool ClearConfigHistory() override;
    Model::AppearanceSettings GetAppearanceSettings() const override;
    void SetAppearanceSettings(const Model::AppearanceSettings& settings) override;

    int GetTriggerType() override;
    void SetTriggerType(int type) override;
    std::vector<std::wstring> GetTriggerBlacklist() override;
    void SetTriggerBlacklist(const std::vector<std::wstring>& processNames) override;
    bool GetAutoStart() override;
    void SetAutoStart(bool enable) override;
    bool GetHideTrayIcon() override;
    void SetHideTrayIcon(bool hide) override;
    bool GetAutoUpdate() override;
    void SetAutoUpdate(bool enable) override;

    int GetPopupColumns() override;
    void SetPopupColumns(int columns) override;
    int GetPopupRows() override;
    void SetPopupRows(int rows) override;
    int GetPopupIconSize() override;
    void SetPopupIconSize(int size) override;
    int GetPopupIconLabelFontSize() override;
    void SetPopupIconLabelFontSize(int size) override;
    int GetPopupHeaderSizeLevel() override;
    void SetPopupHeaderSizeLevel(int level) override;
    int GetPopupIconGap() override;
    void SetPopupIconGap(int gap) override;
    int GetPopupIconRadius() override;
    void SetPopupIconRadius(int radius) override;
    int GetPopupWndPadding() override;
    void SetPopupWndPadding(int padding) override;
    int GetTheme() override;
    void SetTheme(int theme) override;
    int GetThemeColor() override;
    void SetThemeColor(int colorIndex) override;
    int GetWindowMode() override;
    void SetWindowMode(int mode) override;
    int GetGlobalScalePercent() override;
    bool HasCustomGlobalScalePercent() override;
    void SetGlobalScalePercent(int percent) override;
    int GetDockHeight() override;
    void SetDockHeight(int height) override;
    bool GetSearchMode() override;
    void SetSearchMode(bool enabled) override;
    int GetPopupAlignMode() override;
    void SetPopupAlignMode(int mode) override;
    bool GetPopupAutoClose() override;
    void SetPopupAutoClose(bool enabled) override;
    bool GetPopupMultiOpenWhenPinned() override;
    void SetPopupMultiOpenWhenPinned(bool enabled) override;
    int GetHoverLeaveDelay() override;
    void SetHoverLeaveDelay(int delayMs) override;
    int GetFileSelectionValiditySeconds() override;
    void SetFileSelectionValiditySeconds(int seconds) override;
    int GetSortMode() override;
    void SetSortMode(int mode) override;
    bool GetAnimationEnabled() override;
    void SetAnimationEnabled(bool enabled) override;
    int GetAnimationDuration() override;
    void SetAnimationDuration(int duration) override;
    bool GetHardwareAccelerationEnabled() override;
    void SetHardwareAccelerationEnabled(bool enabled) override;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
