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
    virtual void OnLButtonUp(POINT pt, bool& repaint) override;
    virtual void OnLButtonDblClk(POINT pt, bool& repaint) override;
    virtual bool IsAnimating() const override { return m_selectionAnimating; }
    virtual void UpdateAnimation(float dt, bool& repaint) override;

    std::function<void()> OnImportJsonClicked;

private:
    bool HitTestAutoStart(POINT pt);
    int HitTestTrigger(POINT pt);
    int HitTestPopupAlignMode(POINT pt);
    int HitTestPopupAutoClose(POINT pt);
    int HitTestPopupMultiOpenWhenPinned(POINT pt);
    int HitTestSortMode(POINT pt);
    bool HitTestHoverLeaveDelay(POINT pt, int& buttonType);
    int HitTestTheme(POINT pt);
    int HitTestThemeColor(POINT pt);
    int HitTestWindowMode(POINT pt);
    bool HitTestOpenConfigFile(POINT pt);
    bool HitTestOpenLogFile(POINT pt);
    bool HitTestConfigDirText(POINT pt);
    bool HitTestOpenConfigHistoryDir(POINT pt);
    bool HitTestCreateConfigBackup(POINT pt);
    bool HitTestRestoreConfigBackup(POINT pt);
    bool HitTestClearConfig(POINT pt);
    bool HitTestClearConfigHistory(POINT pt);
    bool HitTestImportJson(POINT pt);
    bool HitTestAppearance(POINT pt, int& settingIdx, int& buttonType);
    bool HitTestThemeDetails(POINT pt, int& settingIdx, int& buttonType);
    bool HitTestHardwareAcceleration(POINT pt);
    bool HitTestAnimationToggle(POINT pt);
    bool HitTestAnimationDuration(POINT pt, int& buttonType);
    bool HitTestGlobalScaleSlider(POINT pt);
    bool HitTestGlobalScaleApply(POINT pt);
    int GlobalScaleFromPoint(POINT pt) const;
    int PendingGlobalScalePercent();
    bool HitTestHideTrayIcon(POINT pt);
    bool HitTestCheckUpdate(POINT pt);
    bool HitTestApplyUpdate(POINT pt);

    struct SelectionVisual
    {
        bool initialized = false;
        bool moving = false;
        D2D1_RECT_F current = {};
        D2D1_RECT_F target = {};
    };

    D2D1_RECT_F GetSelectionRect(SelectionVisual& visual, const D2D1_RECT_F& target);
    void ShowTriggerPresetMenu();
    void DrawSelectionHighlight(ID2D1HwndRenderTarget* rt, const D2D1_RECT_F& rect, float radius, float bgAlpha = 0.10f, float borderAlpha = 0.34f);

    IConfigWindow* m_owner;
    int m_categoryIndex = 0; // 0 = 常规设置, 1 = 关于

    // Hover states
    bool m_hoveredAutoStart = false;
    bool m_hoveredHideTrayIcon = false;
    bool m_hoveredOpenConfigFile = false;
    bool m_hoveredOpenLogFile = false;
    bool m_hoveredConfigDirText = false;
    bool m_hoveredOpenConfigHistoryDir = false;
    bool m_hoveredCreateConfigBackup = false;
    bool m_hoveredRestoreConfigBackup = false;
    bool m_hoveredClearConfig = false;
    bool m_hoveredClearConfigHistory = false;
    bool m_hoveredImportJson = false;
    int m_hoveredTrigger = -1; // 0 = middle, 1 = mb4, 2 = mb5
    int m_hoveredPopupAlignMode = -1;
    int m_hoveredPopupAutoClose = -1;
    int m_hoveredPopupMultiOpenWhenPinned = -1;
    int m_hoveredSortMode = -1;
    bool m_hoveredHoverLeaveDelay = false;
    int m_hoveredHoverLeaveDelayButton = 0;
    int m_hoveredTheme = -1;   // 0 = dark, 1 = light
    int m_hoveredThemeColor = -1; // 0 to 9
    int m_hoveredWindowMode = -1; // 0 = glow, 1 = acrylic, 2 = glass

    int m_hoveredAppearanceSetting = -1; // 0 to 5
    int m_hoveredAppearanceButton = 0;   // 1 = minus, 2 = plus, 0 = none

    int m_hoveredThemeDetailSetting = -1; // 0 to 5
    int m_hoveredThemeDetailButton = 0;   // 1 = minus, 2 = plus, 0 = none

    bool m_hoveredAnimationToggle = false;
    bool m_hoveredHardwareAcceleration = false;
    bool m_hoveredAnimationDuration = false;
    int m_hoveredAnimationDurationButton = 0; // 1 = minus, 2 = plus, 0 = none
    bool m_hoveredGlobalScaleSlider = false;
    bool m_hoveredGlobalScaleApply = false;
    bool m_draggingGlobalScaleSlider = false;
    int m_pendingGlobalScalePercent = 0;
    bool m_hoveredApplyUpdate = false;
    bool m_hoveredCheckUpdate = false;

    bool m_selectionAnimating = false;
    SelectionVisual m_themeSelection;
    SelectionVisual m_themeColorSelection;
    SelectionVisual m_windowModeSelection;
    SelectionVisual m_triggerSelection;
    SelectionVisual m_popupAlignSelection;
    SelectionVisual m_popupAutoCloseSelection;
    SelectionVisual m_popupMultiOpenSelection;
    SelectionVisual m_sortModeSelection;
};
