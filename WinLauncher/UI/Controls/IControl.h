#pragma once
#include <Windows.h>
#include <d2d1.h>

class IControl
{
public:
    virtual ~IControl() = default;

    virtual void OnPaint(ID2D1HwndRenderTarget* rt, float scale) = 0;
    virtual bool OnMouseMove(POINT pt, float scale) = 0;
    virtual bool OnLButtonDown(POINT pt, float scale) = 0;
    virtual bool OnLButtonUp(POINT pt, float scale) = 0;
    virtual void OnResize(const D2D1_RECT_F& bounds) = 0;

    virtual bool HitTest(POINT pt, float scale) const
    {
        return pt.x >= m_bounds.left && pt.x <= m_bounds.right &&
               pt.y >= m_bounds.top && pt.y <= m_bounds.bottom;
    }

    void SetBounds(const D2D1_RECT_F& bounds) { m_bounds = bounds; }
    D2D1_RECT_F GetBounds() const { return m_bounds; }

protected:
    D2D1_RECT_F m_bounds = {};
};
