#pragma once

#include <d2d1.h>
#include <dwrite.h>
#include <string>

// Static drawing kit for the settings page controls (checkboxes, segment
// buttons, stepper cards, action/small buttons and info cards). The page
// supplies the render target, text format, theme base color and cached text
// brushes; the kit owns only the pixel math, moved verbatim from the drawing
// lambdas that used to live inside SettingsPage::OnPaint.
class SettingsControlKit
{
public:
    static void DrawInlineCheckbox(ID2D1HwndRenderTarget* rt, IDWriteTextFormat* textFormat,
        const D2D1_COLOR_F& baseColor, float x, bool checked, bool hovered, const wchar_t* label);

    static void DrawSegmentButton(ID2D1HwndRenderTarget* rt, IDWriteTextFormat* textFormat,
        const D2D1_COLOR_F& baseColor, const D2D1_RECT_F& cardRect, const std::wstring& text,
        bool selected, bool hovered);

    static void DrawStepperCard(ID2D1HwndRenderTarget* rt, IDWriteTextFormat* textFormat,
        const D2D1_COLOR_F& baseColor, float ix, float iy, const std::wstring& label,
        const std::wstring& value, bool hovered, int button);

    static void DrawActionButton(ID2D1HwndRenderTarget* rt, IDWriteTextFormat* textFormat,
        const D2D1_COLOR_F& baseColor, ID2D1SolidColorBrush* textBrush,
        const D2D1_RECT_F& buttonRect, const std::wstring& text, bool hovered, bool danger);

    static void DrawSmallButton(ID2D1HwndRenderTarget* rt, IDWriteTextFormat* textFormat,
        const D2D1_COLOR_F& baseColor, ID2D1SolidColorBrush* textBrush,
        D2D1_RECT_F buttonRect, const wchar_t* text, bool hovered, bool accent);

    static void DrawInfoCard(ID2D1HwndRenderTarget* rt, IDWriteTextFormat* textFormat,
        const D2D1_COLOR_F& baseColor, ID2D1SolidColorBrush* titleBrush, ID2D1SolidColorBrush* bodyBrush,
        const D2D1_RECT_F& cardRect, const wchar_t* title, const wchar_t* body);
};
