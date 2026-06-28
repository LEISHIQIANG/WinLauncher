#pragma once
#include "../Model/AppearanceSettings.h"
#include "../Model/ShortcutInfo.h"
#include <string>
#include <vector>

class IConfigService
{
public:
    virtual ~IConfigService() = default;

    virtual std::vector<Model::PopupPage> LoadConfig() = 0;
    virtual void SaveConfig(const std::vector<Model::PopupPage>& pages) = 0;
    virtual std::wstring GetConfigDir() const = 0;
    virtual std::wstring GetConfigFilePath() const = 0;
    virtual Model::AppearanceSettings GetAppearanceSettings() const = 0;
    virtual void SetAppearanceSettings(const Model::AppearanceSettings& settings) = 0;

    virtual int GetTriggerType() = 0;
    virtual void SetTriggerType(int type) = 0;
    virtual bool GetAutoStart() = 0;
    virtual void SetAutoStart(bool enable) = 0;

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
    virtual int GetDockHeight() = 0;
    virtual void SetDockHeight(int height) = 0;
    virtual bool GetSearchMode() = 0;
    virtual void SetSearchMode(bool enabled) = 0;

    virtual bool GetAnimationEnabled() = 0;
    virtual void SetAnimationEnabled(bool enabled) = 0;
    virtual int GetAnimationDuration() = 0;
    virtual void SetAnimationDuration(int duration) = 0;
};

