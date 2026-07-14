#pragma once

#include <d2d1.h>
#include <wrl.h>
#include <vector>

// Shared, deliberately small shell infrastructure for the custom-drawn
// editor dialogs.  Forms remain independent; shared paint/hit-test behavior
// no longer drifts between macro, batch, and system-icon windows.
namespace DialogShell
{
    class BrushCache
    {
    public:
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> Get(ID2D1HwndRenderTarget* target, const D2D1_COLOR_F& color)
        {
            for (const auto& entry : m_entries)
                if (entry.color.r == color.r && entry.color.g == color.g && entry.color.b == color.b && entry.color.a == color.a)
                    return entry.brush;
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
            if (target) target->CreateSolidColorBrush(color, &brush);
            if (brush) m_entries.push_back({ color, brush });
            return brush;
        }

        void Clear() { m_entries.clear(); }

    private:
        struct Entry { D2D1_COLOR_F color{}; Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush; };
        std::vector<Entry> m_entries;
    };

    inline bool HitTestRect(POINT point, const D2D1_RECT_F& rect)
    {
        return point.x >= rect.left && point.x <= rect.right && point.y >= rect.top && point.y <= rect.bottom;
    }
}
