#pragma once
#include <d2d1.h>
#include <dwrite.h>
#include <wrl.h>
#include <vector>
#include "../Model/ShortcutInfo.h"
#include "../ShortcutManager.h"
#include "UIStyle.h"

using Microsoft::WRL::ComPtr;

class ShortcutGridViewHelper
{
public:
    struct GridRenderParams
    {
        ID2D1HwndRenderTarget* rt;
        const RendPopupPage* pageData;
        int hoveredIndex;
        bool hoveredAdd;
        float scrollY;
        IDWriteTextFormat* textFormat;
        ID2D1SolidColorBrush* textBrush;
        ID2D1SolidColorBrush* mutedBrush;
    };

    static void RenderShortcutCard(
        ID2D1HwndRenderTarget* rt,
        const D2D1_RECT_F& cardRect,
        bool isSelected,
        bool isHovered,
        ID2D1SolidColorBrush* bgBrush,
        ID2D1SolidColorBrush* borderBrush);

    static void RenderDragInsertionLine(
        ID2D1HwndRenderTarget* rt,
        const D2D1_POINT_2F& startPt,
        const D2D1_POINT_2F& endPt,
        ID2D1SolidColorBrush* lineBrush);
};
