#pragma once
#include <d2d1.h>

class IConfigWindow;

class SettingsSelectionAnimator
{
public:
    struct SelectionVisual
    {
        bool initialized = false;
        bool moving = false;
        D2D1_RECT_F current = {};
        D2D1_RECT_F target = {};
    };

    D2D1_RECT_F GetSelectionRect(SelectionVisual& visual, const D2D1_RECT_F& target, IConfigWindow* owner);
    void UpdateAnimation(float dt, bool& repaint);
    bool IsAnimating() const { return m_selectionAnimating; }

    static void DrawSelectionHighlight(ID2D1HwndRenderTarget* rt, const D2D1_RECT_F& rect, float radius, float bgAlpha = 0.10f, float borderAlpha = 0.34f);

    SelectionVisual m_themeSelection;
    SelectionVisual m_themeColorSelection;
    SelectionVisual m_windowModeSelection;
    SelectionVisual m_triggerSelection;
    SelectionVisual m_popupAlignSelection;
    SelectionVisual m_popupAutoCloseSelection;
    SelectionVisual m_popupMultiOpenSelection;
    SelectionVisual m_sortModeSelection;

private:
    bool m_selectionAnimating = false;
};
