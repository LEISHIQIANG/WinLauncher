#include "ShortcutGridViewHelper.h"
#include "../UI/Controls/IconRenderer.h"
#include <cmath>

ComPtr<ID2D1SolidColorBrush> ShortcutBrushCache::GetOrCreateSolidBrush(ID2D1HwndRenderTarget* rt, const D2D1_COLOR_F& color)
{
    for (auto& entry : m_brushes)
    {
        if (entry.color.r == color.r && entry.color.g == color.g &&
            entry.color.b == color.b && entry.color.a == color.a)
        {
            return entry.brush;
        }
    }

    ComPtr<ID2D1SolidColorBrush> brush;
    if (rt)
    {
        rt->CreateSolidColorBrush(color, &brush);
        if (brush)
        {
            m_brushes.push_back({ color, brush });
        }
    }
    return brush;
}

ComPtr<ID2D1BitmapBrush> ShortcutBrushCache::GetOrCreateBitmapBrush(ID2D1HwndRenderTarget* rt, ID2D1Bitmap* bmp)
{
    auto it = m_bmpBrushes.find(bmp);
    if (it != m_bmpBrushes.end())
    {
        return it->second;
    }

    ComPtr<ID2D1BitmapBrush> brush;
    if (rt && bmp)
    {
        rt->CreateBitmapBrush(bmp, &brush);
        if (brush)
        {
            m_bmpBrushes[bmp] = brush;
        }
    }
    return brush;
}

void ShortcutGridViewHelper::RenderInsertionSlot(
    ID2D1HwndRenderTarget* rt,
    ShortcutBrushCache& brushCache,
    float x, float y)
{
    if (!rt) return;

    D2D1_COLOR_F phClr = UIStyle::ThemeColor::Accent().d2d;
    phClr.a = 0.18f;
    auto placeholderBrush = brushCache.GetOrCreateSolidBrush(rt, phClr);
    if (placeholderBrush)
    {
        D2D1_RECT_F insertRect = D2D1::RectF(x, y, x + 62.0f, y + 62.0f);
        D2D1_ROUNDED_RECT roundedInsert = D2D1::RoundedRect(insertRect, 8.0f, 8.0f);
        rt->DrawRoundedRectangle(roundedInsert, placeholderBrush.Get(), UIStyle::Metrics::EmphasisStroke());
    }
}

static const float GRID_ICON_SIZE = 24.0f;

void ShortcutGridViewHelper::RenderCardItem(
    ID2D1HwndRenderTarget* rt,
    ShortcutBrushCache& brushCache,
    float x, float y,
    bool isSelected,
    bool isHovered,
    bool isDragging,
    ID2D1Bitmap* iconBitmap,
    float revealAlpha,
    const std::wstring& name,
    IDWriteTextFormat* textFormat)
{
    if (!rt) return;

    D2D1_RECT_F cardRect = D2D1::RectF(x, y, x + 62.0f, y + 62.0f);
    D2D1_ROUNDED_RECT roundedCard = D2D1::RoundedRect(cardRect, 8.0f, 8.0f);

    if (isDragging)
    {
        D2D1_COLOR_F dragBg = UIStyle::ThemeColor::Accent().d2d;
        dragBg.a = 0.20f;
        auto bgBrush = brushCache.GetOrCreateSolidBrush(rt, dragBg);
        if (bgBrush)
        {
            rt->FillRoundedRectangle(roundedCard, bgBrush.Get());
        }

        D2D1_COLOR_F dragBorder = UIStyle::ThemeColor::Accent().d2d;
        dragBorder.a = 0.42f;
        auto borderBrush = brushCache.GetOrCreateSolidBrush(rt, dragBorder);
        if (borderBrush)
        {
            rt->DrawRoundedRectangle(roundedCard, borderBrush.Get(), UIStyle::Metrics::EmphasisStroke());
        }
    }
    else
    {
        D2D1_COLOR_F baseClr = UIStyle::ThemeColor::ThemeBase().d2d;
        float alphaBg = isHovered ? 0.105f : 0.035f;
        float alphaBorder = isHovered ? 0.18f : 0.065f;

        D2D1_COLOR_F selBg = UIStyle::ThemeColor::Accent().d2d;
        selBg.a = isHovered ? 0.20f : 0.13f;
        D2D1_COLOR_F normBg = baseClr;
        normBg.a = alphaBg;

        auto bgBrush = brushCache.GetOrCreateSolidBrush(rt, isSelected ? selBg : normBg);
        if (bgBrush)
        {
            rt->FillRoundedRectangle(roundedCard, bgBrush.Get());
        }

        D2D1_COLOR_F selBorder = UIStyle::ThemeColor::Accent().d2d;
        selBorder.a = isHovered ? 0.42f : 0.30f;
        D2D1_COLOR_F normBorder = baseClr;
        normBorder.a = alphaBorder;

        auto borderBrush = brushCache.GetOrCreateSolidBrush(rt, isSelected ? selBorder : normBorder);
        if (borderBrush)
        {
            rt->DrawRoundedRectangle(roundedCard, borderBrush.Get(), UIStyle::Metrics::ControlStroke());
        }
    }

    // Draw Icon (Preview)
    if (iconBitmap)
    {
        float iconX = x + 19.0f;
        float iconY = y + 8.0f;
        D2D1_RECT_F iconRect = IconRenderer::AlignToPixels(rt, iconX, iconY, GRID_ICON_SIZE, GRID_ICON_SIZE);
        rt->DrawBitmap(iconBitmap, iconRect, revealAlpha, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }

    // Label
    if (textFormat)
    {
        D2D1_COLOR_F tbClr = UIStyle::ThemeColor::TextNormal().d2d;
        if (!isDragging) tbClr.a = 0.9f;
        auto tb = brushCache.GetOrCreateSolidBrush(rt, tbClr);
        if (tb)
        {
            std::wstring dispName = name;
            if (dispName.length() > 6) dispName = dispName.substr(0, 6) + L"…";
            rt->DrawTextW(dispName.c_str(), (UINT32)dispName.size(), textFormat,
                D2D1::RectF(x + 1.0f, y + 36.0f, x + 61.0f, y + 58.0f), tb.Get());
        }
    }
}

void ShortcutGridViewHelper::RenderAddCard(
    ID2D1HwndRenderTarget* rt,
    ShortcutBrushCache& brushCache,
    float x, float y,
    bool isHovered,
    IDWriteTextFormat* textFormat)
{
    if (!rt) return;

    D2D1_RECT_F addCardRect = D2D1::RectF(x, y, x + 62.0f, y + 62.0f);
    D2D1_ROUNDED_RECT roundedAdd = D2D1::RoundedRect(addCardRect, 8.0f, 8.0f);

    D2D1_COLOR_F baseClr = UIStyle::ThemeColor::ThemeBase().d2d;
    float alphaBg = isHovered ? 0.09f : 0.02f;
    float alphaBorder = isHovered ? 0.16f : 0.065f;

    auto bgBrush = brushCache.GetOrCreateSolidBrush(rt, D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, alphaBg));
    if (bgBrush)
    {
        rt->FillRoundedRectangle(roundedAdd, bgBrush.Get());
    }

    auto borderBrush = brushCache.GetOrCreateSolidBrush(rt, D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, alphaBorder));
    if (borderBrush)
    {
        rt->DrawRoundedRectangle(roundedAdd, borderBrush.Get(), UIStyle::Metrics::ControlStroke());
    }

    // Draw Plus sign
    auto plBrush = brushCache.GetOrCreateSolidBrush(rt, UIStyle::ThemeColor::TextMuted().d2d);
    if (plBrush)
    {
        rt->DrawLine(D2D1::Point2F(x + 31.0f, y + 14.0f), D2D1::Point2F(x + 31.0f, y + 26.0f), plBrush.Get(), UIStyle::Metrics::IconStroke());
        rt->DrawLine(D2D1::Point2F(x + 25.0f, y + 20.0f), D2D1::Point2F(x + 37.0f, y + 20.0f), plBrush.Get(), UIStyle::Metrics::IconStroke());
    }

    // Label
    if (textFormat)
    {
        auto tb = brushCache.GetOrCreateSolidBrush(rt, UIStyle::ThemeColor::TextMuted().d2d);
        if (tb)
        {
            rt->DrawTextW(L"添加", 2, textFormat, D2D1::RectF(x + 1.0f, y + 36.0f, x + 61.0f, y + 58.0f), tb.Get());
        }
    }
}

void ShortcutGridViewHelper::RenderShortcutCard(
    ID2D1HwndRenderTarget* rt,
    const D2D1_RECT_F& cardRect,
    bool isSelected,
    bool isHovered,
    ID2D1SolidColorBrush* bgBrush,
    ID2D1SolidColorBrush* borderBrush)
{
    if (!rt) return;

    D2D1_ROUNDED_RECT roundedCard = D2D1::RoundedRect(cardRect, 8.0f, 8.0f);
    if (bgBrush)
    {
        rt->FillRoundedRectangle(roundedCard, bgBrush);
    }
    if (borderBrush)
    {
        rt->DrawRoundedRectangle(roundedCard, borderBrush, 1.0f);
    }
}

void ShortcutGridViewHelper::RenderDragInsertionLine(
    ID2D1HwndRenderTarget* rt,
    const D2D1_POINT_2F& startPt,
    const D2D1_POINT_2F& endPt,
    ID2D1SolidColorBrush* lineBrush)
{
    if (!rt || !lineBrush) return;
    rt->DrawLine(startPt, endPt, lineBrush, 2.0f);
}
