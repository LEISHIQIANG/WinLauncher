#include "PopupRenderHelper.h"

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
