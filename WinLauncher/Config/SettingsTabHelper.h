#pragma once
#include <d2d1.h>
#include <dwrite.h>
#include <wrl.h>
#include <string>
#include <vector>
#include "UIStyle.h"

using Microsoft::WRL::ComPtr;

class SettingsTabHelper
{
public:
    struct CardItemParams
    {
        ID2D1HwndRenderTarget* rt;
        D2D1_RECT_F rect;
        const wchar_t* title;
        const wchar_t* description;
        IDWriteTextFormat* titleFormat;
        IDWriteTextFormat* descFormat;
        bool isHovered;
        ID2D1SolidColorBrush* bgBrush;
        ID2D1SolidColorBrush* hoverBrush;
        ID2D1SolidColorBrush* textBrush;
        ID2D1SolidColorBrush* descBrush;
    };

    static void RenderSettingCard(const CardItemParams& params);

    static void RenderSectionHeader(
        ID2D1HwndRenderTarget* rt,
        const D2D1_RECT_F& rect,
        const wchar_t* title,
        IDWriteTextFormat* format,
        ID2D1SolidColorBrush* brush);
};
