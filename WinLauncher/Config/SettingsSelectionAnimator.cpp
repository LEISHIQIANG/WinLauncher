#include "SettingsSelectionAnimator.h"
#include "IConfigWindow.h"
#include "UIStyle.h"
#include <cmath>

namespace
{
inline bool SameRect(const D2D1_RECT_F& a, const D2D1_RECT_F& b)
{
    return fabsf(a.left - b.left) < 0.1f &&
        fabsf(a.top - b.top) < 0.1f &&
        fabsf(a.right - b.right) < 0.1f &&
        fabsf(a.bottom - b.bottom) < 0.1f;
}
}

D2D1_RECT_F SettingsSelectionAnimator::GetSelectionRect(SelectionVisual& visual, const D2D1_RECT_F& target, IConfigWindow* owner)
{
    if (!visual.initialized || !UIStyle::Animation::IsEnabled())
    {
        visual.initialized = true;
        visual.moving = false;
        visual.current = target;
        visual.target = target;
        return visual.current;
    }

    if (!SameRect(visual.target, target))
    {
        visual.target = target;
        visual.moving = true;
        m_selectionAnimating = true;
        if (owner)
            owner->StartAnimation();
    }

    return visual.current;
}

void SettingsSelectionAnimator::DrawSelectionHighlight(ID2D1HwndRenderTarget* rt, const D2D1_RECT_F& rect, float radius, float bgAlpha, float borderAlpha)
{
    if (!rt) return;

    D2D1_ROUNDED_RECT rounded = D2D1::RoundedRect(rect, radius, radius);

    ID2D1SolidColorBrush* bgBrush = nullptr;
    D2D1_COLOR_F bgClr = UIStyle::ThemeColor::Accent().d2d;
    bgClr.a = bgAlpha;
    rt->CreateSolidColorBrush(bgClr, &bgBrush);
    if (bgBrush)
    {
        rt->FillRoundedRectangle(rounded, bgBrush);
        bgBrush->Release();
    }

    ID2D1SolidColorBrush* borderBrush = nullptr;
    D2D1_COLOR_F borderClr = UIStyle::ThemeColor::Accent().d2d;
    borderClr.a = borderAlpha;
    rt->CreateSolidColorBrush(borderClr, &borderBrush);
    if (borderBrush)
    {
        rt->DrawRoundedRectangle(rounded, borderBrush, UIStyle::Metrics::ControlStroke());
        borderBrush->Release();
    }
}

void SettingsSelectionAnimator::UpdateAnimation(float dt, bool& repaint)
{
    if (!UIStyle::Animation::IsEnabled())
    {
        m_selectionAnimating = false;
        return;
    }

    bool stillMoving = false;
    auto updateVisual = [&](SelectionVisual& visual)
    {
        if (!visual.initialized || !visual.moving)
            return;

        float t = 1.0f - std::exp(-20.0f * dt);
        visual.current.left += (visual.target.left - visual.current.left) * t;
        visual.current.top += (visual.target.top - visual.current.top) * t;
        visual.current.right += (visual.target.right - visual.current.right) * t;
        visual.current.bottom += (visual.target.bottom - visual.current.bottom) * t;

        if (SameRect(visual.current, visual.target))
        {
            visual.current = visual.target;
            visual.moving = false;
        }
        else
        {
            stillMoving = true;
        }
    };

    updateVisual(m_themeSelection);
    updateVisual(m_themeColorSelection);
    updateVisual(m_windowModeSelection);
    updateVisual(m_triggerSelection);
    updateVisual(m_popupAlignSelection);
    updateVisual(m_popupAutoCloseSelection);
    updateVisual(m_popupMultiOpenSelection);
    updateVisual(m_sortModeSelection);

    m_selectionAnimating = stillMoving;
    repaint = true;
}
