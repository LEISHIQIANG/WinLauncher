#include "PopupRenderHelper.h"
#include "../UI/Controls/IconRenderer.h"

void PopupRenderHelper::DrawSearchPlaceholder(
    ID2D1HwndRenderTarget* rt,
    IDWriteTextFormat* format,
    const D2D1_RECT_F& rect,
    const wchar_t* text,
    ID2D1SolidColorBrush* brush)
{
    if (!rt || !format || !text || !brush) return;
    rt->DrawTextW(text, (UINT32)wcslen(text), format, rect, brush);
}

void PopupRenderHelper::RenderSelectionHighlight(
    ID2D1HwndRenderTarget* rt,
    const D2D1_RECT_F& itemRect,
    ID2D1SolidColorBrush* bgBrush,
    ID2D1SolidColorBrush* borderBrush,
    float cornerRadius)
{
    if (!rt) return;
    D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(itemRect, cornerRadius, cornerRadius);
    if (bgBrush)
    {
        rt->FillRoundedRectangle(roundedRect, bgBrush);
    }
    if (borderBrush)
    {
        rt->DrawRoundedRectangle(roundedRect, borderBrush, 1.0f);
    }
}

void PopupRenderHelper::RenderDockSeparator(
    ID2D1HwndRenderTarget* rt,
    float totalWidth,
    float lineY,
    ID2D1SolidColorBrush* lineBrush)
{
    if (!rt || !lineBrush) return;
    rt->DrawLine(
        D2D1::Point2F(0.0f, lineY),
        D2D1::Point2F(totalWidth, lineY),
        lineBrush, 0.3f);
}

void PopupRenderHelper::RenderFileSelectionTimeline(
    ID2D1HwndRenderTarget* rt,
    float totalWidth,
    float lineY,
    float progress)
{
    if (!rt || progress <= 0.0f) return;
    if (progress > 1.0f) progress = 1.0f;

    float midX = totalWidth / 2.0f;
    float halfLength = (totalWidth / 2.0f) * progress;
    float left = midX - halfLength;
    float right = midX + halfLength;

    if (right <= left + 0.1f) return;

    D2D1_POINT_2F startPt = D2D1::Point2F(left, lineY);
    D2D1_POINT_2F endPt = D2D1::Point2F(right, lineY);

    D2D1_GRADIENT_STOP stops[3];
    stops[0].position = 0.0f;
    stops[0].color = UIStyle::ThemeColor::AccentSubtle().d2d;
    stops[1].position = 0.5f;
    stops[1].color = UIStyle::ThemeColor::Accent().d2d;
    stops[2].position = 1.0f;
    stops[2].color = UIStyle::ThemeColor::AccentSubtle().d2d;

    ComPtr<ID2D1GradientStopCollection> stopCollection;
    HRESULT hr = rt->CreateGradientStopCollection(stops, 3, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &stopCollection);
    if (SUCCEEDED(hr) && stopCollection)
    {
        ComPtr<ID2D1LinearGradientBrush> gradientBrush;
        hr = rt->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(startPt, endPt),
            stopCollection.Get(),
            &gradientBrush
        );
        if (SUCCEEDED(hr) && gradientBrush)
        {
            rt->DrawLine(startPt, endPt, gradientBrush.Get(), 1.5f);
        }
    }
}

void PopupRenderHelper::RenderShortcutCards(
    ID2D1HwndRenderTarget* rt,
    const ShortcutCellMetrics& metrics,
    const ShortcutCellBrushes& brushes,
    const std::function<bool(int)>& hovered,
    const std::function<bool(int)>& selected)
{
    if (!rt) return;
    for (int i = 0; i < metrics.count; ++i)
    {
        const float ix = (float)(metrics.padding + (i % metrics.columns) * metrics.cellWidth);
        const float iy = (float)(metrics.gridTop + (i / metrics.columns) * metrics.cellHeight);
        const bool isSelected = selected && selected(i);
        const bool isHovered = hovered && hovered(i);

        const D2D1_RECT_F cardRect = D2D1::RectF(ix, iy,
            ix + metrics.cellWidth - metrics.gap, iy + metrics.cellHeight - metrics.gap);
        const D2D1_ROUNDED_RECT roundedCard = D2D1::RoundedRect(cardRect, metrics.cardCornerRadius, metrics.cardCornerRadius);

        ID2D1SolidColorBrush* bg = isSelected ? brushes.bgSelected
            : (isHovered ? brushes.bgHover : brushes.bgNormal);
        if (bg) rt->FillRoundedRectangle(roundedCard, bg);

        ID2D1SolidColorBrush* border = isSelected ? brushes.borderSelected
            : (isHovered ? brushes.borderHover : brushes.borderNormal);
        if (border) rt->DrawRoundedRectangle(roundedCard, border, brushes.stroke);
    }
}

void PopupRenderHelper::RenderCellIcons(
    ID2D1HwndRenderTarget* rt,
    const ShortcutCellMetrics& metrics,
    int cellMarginX,
    int cellMarginY,
    int iconSize,
    const std::function<ID2D1Bitmap*(int)>& bitmapFor,
    const std::function<void(int, const D2D1_RECT_F&)>& paintIcon)
{
    if (!rt || !bitmapFor || !paintIcon) return;
    for (int i = 0; i < metrics.count; ++i)
    {
        ID2D1Bitmap* bmp = bitmapFor(i);
        if (!bmp) continue;

        const float ix = (float)(metrics.padding + (i % metrics.columns) * metrics.cellWidth);
        const float iy = (float)(metrics.gridTop + (i / metrics.columns) * metrics.cellHeight);
        const D2D1_RECT_F iconRect = IconRenderer::AlignToPixels(rt,
            ix + cellMarginX, iy + cellMarginY, (float)iconSize, (float)iconSize);
        paintIcon(i, iconRect);
    }
}

void PopupRenderHelper::RenderShortcutLabels(
    ID2D1HwndRenderTarget* rt,
    const ShortcutCellMetrics& metrics,
    IDWriteTextFormat* textFormat,
    ID2D1SolidColorBrush* textBrush,
    int cellMarginY,
    int iconSize,
    int labelHeight,
    const std::function<const std::wstring&(int)>& textFor)
{
    if (!rt || !textFormat || !textBrush || !textFor) return;
    for (int i = 0; i < metrics.count; ++i)
    {
        const int col = i % metrics.columns;
        const int row = i / metrics.columns;
        const float lx = (float)(metrics.padding + col * metrics.cellWidth);
        const float ly = (float)(metrics.gridTop + row * metrics.cellHeight + cellMarginY + iconSize + 2);
        const std::wstring& text = textFor(i);
        rt->DrawTextW(text.c_str(), (UINT32)text.size(), textFormat,
            D2D1::RectF(lx + 2, ly, lx + metrics.cellWidth - metrics.gap - 2, ly + labelHeight),
            textBrush);
    }
}
