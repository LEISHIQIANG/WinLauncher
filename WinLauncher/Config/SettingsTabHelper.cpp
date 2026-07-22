#include "SettingsTabHelper.h"

void SettingsTabHelper::RenderSectionHeader(
    ID2D1HwndRenderTarget* rt,
    const D2D1_RECT_F& rect,
    const wchar_t* title,
    IDWriteTextFormat* format,
    ID2D1SolidColorBrush* brush)
{
    if (!rt || !title || !format || !brush) return;
    rt->DrawTextW(title, (UINT32)wcslen(title), format, rect, brush);
}

void SettingsTabHelper::RenderSettingCard(const CardItemParams& params)
{
    if (!params.rt || !params.title || !params.titleFormat) return;

    D2D1_ROUNDED_RECT cardRect = D2D1::RoundedRect(params.rect, 8.0f, 8.0f);
    ID2D1SolidColorBrush* bg = params.isHovered ? params.hoverBrush : params.bgBrush;
    if (bg)
    {
        params.rt->FillRoundedRectangle(cardRect, bg);
    }

    float textPadding = 12.0f;
    D2D1_RECT_F titleRect = D2D1::RectF(
        params.rect.left + textPadding,
        params.rect.top + textPadding,
        params.rect.right - textPadding,
        params.rect.top + textPadding + 20.0f
    );

    if (params.textBrush)
    {
        params.rt->DrawTextW(params.title, (UINT32)wcslen(params.title), params.titleFormat, titleRect, params.textBrush);
    }

    if (params.description && params.descFormat && params.descBrush)
    {
        D2D1_RECT_F descRect = D2D1::RectF(
            params.rect.left + textPadding,
            titleRect.bottom + 4.0f,
            params.rect.right - textPadding,
            params.rect.bottom - textPadding
        );
        params.rt->DrawTextW(params.description, (UINT32)wcslen(params.description), params.descFormat, descRect, params.descBrush);
    }
}
