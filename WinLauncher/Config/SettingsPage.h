#pragma once
#include "ConfigPage.h"
#include <string>
#include <functional>

class IConfigWindow;

class SettingsPage : public ConfigPage
{
public:
    SettingsPage(IConfigWindow* owner);
    virtual ~SettingsPage() override;

    void SetCategory(int categoryIndex);

    virtual void OnPaint(ID2D1HwndRenderTarget* rt, const D2D1_RECT_F& rect) override;
    virtual void OnMouseMove(POINT pt, bool& repaint) override;
    virtual void OnMouseLeave(bool& repaint) override;
    virtual void OnLButtonDown(POINT pt, bool& repaint) override;

    std::function<void()> OnImportJsonClicked;

private:
    bool HitTestAutoStart(POINT pt);
    int HitTestTrigger(POINT pt);
    int HitTestTheme(POINT pt);
    int HitTestThemeColor(POINT pt);
    int HitTestWindowMode(POINT pt);
    bool HitTestOpenConfigFile(POINT pt);
    bool HitTestOpenLogFile(POINT pt);
    bool HitTestConfigDirText(POINT pt);
    bool HitTestImportJson(POINT pt);
    bool HitTestAppearance(POINT pt, int& settingIdx, int& buttonType);
    bool HitTestThemeDetails(POINT pt, int& settingIdx, int& buttonType);

    IConfigWindow* m_owner;
    int m_categoryIndex = 0; // 0 = 常规设置, 1 = 关于

    // Hover states
    bool m_hoveredAutoStart = false;
    bool m_hoveredOpenConfigFile = false;
    bool m_hoveredOpenLogFile = false;
    bool m_hoveredConfigDirText = false;
    bool m_hoveredImportJson = false;
    int m_hoveredTrigger = -1; // 0 = middle, 1 = mb4, 2 = mb5
    int m_hoveredTheme = -1;   // 0 = dark, 1 = light
    int m_hoveredThemeColor = -1; // 0 to 9
    int m_hoveredWindowMode = -1; // 0 = glass, 1 = acrylic

    int m_hoveredAppearanceSetting = -1; // 0 to 5
    int m_hoveredAppearanceButton = 0;   // 1 = minus, 2 = plus, 0 = none

    int m_hoveredThemeDetailSetting = -1; // 0 to 5
    int m_hoveredThemeDetailButton = 0;   // 1 = minus, 2 = plus, 0 = none
};
