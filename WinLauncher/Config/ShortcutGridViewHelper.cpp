#include "ShortcutGridViewHelper.h"

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
