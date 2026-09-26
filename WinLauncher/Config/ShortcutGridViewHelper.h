#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl.h>
#include <vector>
#include <unordered_map>
#include <string>
#include "../Model/ShortcutInfo.h"
#include "../ShortcutManager.h"
#include "UIStyle.h"

using Microsoft::WRL::ComPtr;

class ShortcutBrushCache
{
public:
    void Clear()
    {
        m_brushes.clear();
        m_bmpBrushes.clear();
    }

    ComPtr<ID2D1SolidColorBrush> GetOrCreateSolidBrush(ID2D1HwndRenderTarget* rt, const D2D1_COLOR_F& color);
    ComPtr<ID2D1BitmapBrush> GetOrCreateBitmapBrush(ID2D1HwndRenderTarget* rt, ID2D1Bitmap* bmp);

private:
    struct BrushCacheEntry
    {
        D2D1_COLOR_F color;
        ComPtr<ID2D1SolidColorBrush> brush;
    };
    std::vector<BrushCacheEntry> m_brushes;
    std::unordered_map<ID2D1Bitmap*, ComPtr<ID2D1BitmapBrush>> m_bmpBrushes;
};

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

    static void RenderInsertionSlot(
        ID2D1HwndRenderTarget* rt,
        ShortcutBrushCache& brushCache,
        float x, float y);

    static void RenderCardItem(
        ID2D1HwndRenderTarget* rt,
        ShortcutBrushCache& brushCache,
        float x, float y,
        bool isSelected,
        bool isHovered,
        bool isDragging,
        ID2D1Bitmap* iconBitmap,
        float revealAlpha,
        const std::wstring& name,
        IDWriteTextFormat* textFormat);

    static void RenderAddCard(
        ID2D1HwndRenderTarget* rt,
        ShortcutBrushCache& brushCache,
        float x, float y,
        bool isHovered,
        IDWriteTextFormat* textFormat);

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
