#pragma once
#include <d2d1.h>
#include <dwrite.h>
#include <wrl.h>
#include <functional>
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

    // Geometry of a shortcut cell grid (cards + icons + labels) in DIPs.
    struct ShortcutCellMetrics
    {
        int count = 0;          // number of cells to paint
        int columns = 1;
        int cellWidth = 1;
        int cellHeight = 1;
        int padding = 0;        // left inset of the first column
        int gridTop = 0;        // y of the first row
        int gap = 0;            // card inset inside the cell
        float cardCornerRadius = 0.0f;
    };

    // Pre-made interaction brushes for one grid. The selected brushes may be
    // left null for grids without selection state (page and dock grids); a
    // null brush skips that fill/stroke exactly like the previous inline code.
    struct ShortcutCellBrushes
    {
        ID2D1SolidColorBrush* bgSelected = nullptr;
        ID2D1SolidColorBrush* bgHover = nullptr;
        ID2D1SolidColorBrush* bgNormal = nullptr;
        ID2D1SolidColorBrush* borderSelected = nullptr;
        ID2D1SolidColorBrush* borderHover = nullptr;
        ID2D1SolidColorBrush* borderNormal = nullptr;
        float stroke = 1.0f;
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

    // Paints the card backgrounds of a shortcut grid. The hovered/selected
    // per-index predicates keep each grid's interaction state at the call
    // site; brushes stay owned by the window's cached brush factory.
    static void RenderShortcutCards(
        ID2D1HwndRenderTarget* rt,
        const ShortcutCellMetrics& metrics,
        const ShortcutCellBrushes& brushes,
        const std::function<bool(int)>& hovered,
        const std::function<bool(int)>& selected = nullptr);

    // Paints cell icons through caller callbacks: bitmapFor returns the
    // bitmap for an index (null skips the cell) and paintIcon receives the
    // pixel-aligned icon rect, so flash overlays and other window state stay
    // in PopupWindow.
    static void RenderCellIcons(
        ID2D1HwndRenderTarget* rt,
        const ShortcutCellMetrics& metrics,
        int cellMarginX,
        int cellMarginY,
        int iconSize,
        const std::function<ID2D1Bitmap*(int)>& bitmapFor,
        const std::function<void(int, const D2D1_RECT_F&)>& paintIcon);

    // Paints the single-line name label under each cell.
    static void RenderShortcutLabels(
        ID2D1HwndRenderTarget* rt,
        const ShortcutCellMetrics& metrics,
        IDWriteTextFormat* textFormat,
        ID2D1SolidColorBrush* textBrush,
        int cellMarginY,
        int iconSize,
        int labelHeight,
        const std::function<const std::wstring&(int)>& textFor);
};
