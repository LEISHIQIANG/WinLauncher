#pragma once
#include <Windows.h>
#include <d2d1.h>

class ConfigPage
{
public:
    virtual ~ConfigPage() = default;

    virtual void OnPaint(ID2D1HwndRenderTarget* rt, const D2D1_RECT_F& rect) = 0;
    virtual void OnMouseMove(POINT pt, bool& repaint) {}
    virtual void OnMouseLeave(bool& repaint) {}
    virtual void OnLButtonDown(POINT pt, bool& repaint) {}
    virtual void OnLButtonDblClk(POINT pt, bool& repaint) {}
    virtual void OnLButtonUp(POINT pt, bool& repaint) {}
    virtual void OnRButtonDown(POINT pt, bool& repaint) {}
    virtual void OnMouseWheel(short zDelta, POINT pt, bool& repaint) {}
    virtual void OnKeyDown(WPARAM wParam, bool& repaint) {}
    virtual void OnDropFiles(HDROP hDrop, bool& repaint) {}
    virtual void OnTimer(UINT_PTR timerId, bool& repaint) {}

    virtual bool IsAnimating() const { return false; }
    virtual void UpdateAnimation(float dt, bool& repaint) {}
};
