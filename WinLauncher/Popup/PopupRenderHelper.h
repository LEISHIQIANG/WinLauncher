#pragma once
#include <d2d1.h>
#include <dwrite.h>
#include <wrl.h>
#include <string>
#include <vector>
#include "../Model/ShortcutInfo.h"
#include "PopupSearchService.h"
#include "../Config/UIStyle.h"

using Microsoft::WRL::ComPtr;

class PopupRenderHelper
{
public:
    struct SearchRenderParams
    {
        ID2D1HwndRenderTarget* rt;
        IDWriteTextFormat* textFormat;
        IDWriteTextFormat* searchFormat;
        const std::vector<PopupSearchService::SearchResult>& searchResults;
        int selectedIndex;
        int wndPadding;
        float headerHeight;
        float scale;
        float itemHeight;
        float contentWidth;
    };

    static void DrawSearchPlaceholder(
        ID2D1HwndRenderTarget* rt,
        IDWriteTextFormat* format,
        const D2D1_RECT_F& rect,
        const wchar_t* text,
        ID2D1SolidColorBrush* brush);

    static void RenderSelectionHighlight(
        ID2D1HwndRenderTarget* rt,
        const D2D1_RECT_F& itemRect,
        ID2D1SolidColorBrush* bgBrush,
        ID2D1SolidColorBrush* borderBrush,
        float cornerRadius);

    static void RenderDockSeparator(
        ID2D1HwndRenderTarget* rt,
        float totalWidth,
        float lineY,
        ID2D1SolidColorBrush* lineBrush);

    static void RenderFileSelectionTimeline(
        ID2D1HwndRenderTarget* rt,
        float totalWidth,
        float lineY,
        float progress);
};
