#include "SettingsControlKit.h"
#include "SettingsPageLayout.h"
#include "UIStyle.h"
#include <cwchar>

using namespace SettingsPageLayout;

void SettingsControlKit::DrawInlineCheckbox(ID2D1HwndRenderTarget* rt, IDWriteTextFormat* textFormat,
    const D2D1_COLOR_F& baseColor, float x, bool checked, bool hovered, const wchar_t* label)
{
    if (!rt) return;
    D2D1_RECT_F boxRect = D2D1::RectF(x, 85, x + 16.0f, 101);
    D2D1_ROUNDED_RECT roundedBox = D2D1::RoundedRect(boxRect, 3.0f, 3.0f);

    ID2D1SolidColorBrush* bgBrush = nullptr;
    float alphaBg = hovered ? 0.105f : 0.035f;
    rt->CreateSolidColorBrush(D2D1::ColorF(baseColor.r, baseColor.g, baseColor.b, alphaBg), &bgBrush);

    ID2D1SolidColorBrush* borderBrush = nullptr;
    float alphaBorder = hovered ? 0.18f : 0.065f;
    rt->CreateSolidColorBrush(D2D1::ColorF(baseColor.r, baseColor.g, baseColor.b, alphaBorder), &borderBrush);

    if (bgBrush) rt->FillRoundedRectangle(roundedBox, bgBrush);
    if (borderBrush) rt->DrawRoundedRectangle(roundedBox, borderBrush, UIStyle::Metrics::ControlStroke());

    if (bgBrush) bgBrush->Release();
    if (borderBrush) borderBrush->Release();

    if (checked)
    {
        ID2D1SolidColorBrush* accentBrush = nullptr;
        rt->CreateSolidColorBrush(UIStyle::ThemeColor::Accent().d2d, &accentBrush);
        if (accentBrush)
        {
            D2D1_ROUNDED_RECT checkRect = D2D1::RoundedRect(D2D1::RectF(x + 3.0f, 88, x + 13.0f, 98), 2.0f, 2.0f);
            rt->FillRoundedRectangle(checkRect, accentBrush);
            accentBrush->Release();
        }
    }

    if (textFormat)
    {
        ID2D1SolidColorBrush* tb = nullptr;
        rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextNormal().d2d, &tb);
        if (tb)
        {
            rt->DrawTextW(label, (UINT32)wcslen(label), textFormat,
                D2D1::RectF(x + 26.0f, 83, x + 90.0f, 103), tb);
            tb->Release();
        }
    }
}

void SettingsControlKit::DrawSegmentButton(ID2D1HwndRenderTarget* rt, IDWriteTextFormat* textFormat,
    const D2D1_COLOR_F& baseColor, const D2D1_RECT_F& cardRect, const std::wstring& text,
    bool selected, bool hovered)
{
    if (!rt) return;
    D2D1_ROUNDED_RECT roundedCard = D2D1::RoundedRect(cardRect, 6.0f, 6.0f);
    ID2D1SolidColorBrush* bgBrush = nullptr;
    D2D1_COLOR_F bgClr = baseColor;
    float bgAlpha = hovered ? 0.06f : 0.018f;
    rt->CreateSolidColorBrush(D2D1::ColorF(bgClr.r, bgClr.g, bgClr.b, bgAlpha), &bgBrush);
    if (bgBrush)
    {
        rt->FillRoundedRectangle(roundedCard, bgBrush);
        bgBrush->Release();
    }

    ID2D1SolidColorBrush* borderBrush = nullptr;
    D2D1_COLOR_F borderClr = baseColor;
    float borderAlpha = hovered ? 0.105f : 0.045f;
    rt->CreateSolidColorBrush(D2D1::ColorF(borderClr.r, borderClr.g, borderClr.b, borderAlpha), &borderBrush);
    if (borderBrush)
    {
        rt->DrawRoundedRectangle(roundedCard, borderBrush, UIStyle::Metrics::ControlStroke());
        borderBrush->Release();
    }

    if (textFormat)
    {
        ID2D1SolidColorBrush* textBrush = nullptr;
        D2D1_COLOR_F txtClr = selected ? UIStyle::ThemeColor::Accent().d2d : UIStyle::ThemeColor::TextNormal().d2d;
        rt->CreateSolidColorBrush(txtClr, &textBrush);
        if (textBrush)
        {
            DWRITE_TEXT_ALIGNMENT oldAlignment = textFormat->GetTextAlignment();
            textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            rt->DrawTextW(text.c_str(), (UINT32)text.size(), textFormat, cardRect, textBrush);
            textFormat->SetTextAlignment(oldAlignment);
            textBrush->Release();
        }
    }
}

static void DrawStepButton(ID2D1HwndRenderTarget* rt, IDWriteTextFormat* textFormat,
    const D2D1_COLOR_F& baseColor, float left, float cy, bool plus, bool isHovered)
{
    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(D2D1::RectF(left, cy - 8, left + 16, cy + 8), 3.0f, 3.0f);
    ID2D1SolidColorBrush* btnBrush = nullptr;
    rt->CreateSolidColorBrush(D2D1::ColorF(baseColor.r, baseColor.g, baseColor.b, isHovered ? 0.105f : 0.04f), &btnBrush);
    if (btnBrush)
    {
        rt->FillRoundedRectangle(rr, btnBrush);
        btnBrush->Release();
    }
    rt->CreateSolidColorBrush(D2D1::ColorF(baseColor.r, baseColor.g, baseColor.b, isHovered ? 0.18f : 0.075f), &btnBrush);
    if (btnBrush)
    {
        rt->DrawRoundedRectangle(rr, btnBrush, UIStyle::Metrics::ControlStroke());
        btnBrush->Release();
    }
    rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextNormal().d2d, &btnBrush);
    if (btnBrush)
    {
        rt->DrawLine(D2D1::Point2F(left + 4, cy), D2D1::Point2F(left + 12, cy), btnBrush, UIStyle::Metrics::ControlStroke());
        if (plus)
            rt->DrawLine(D2D1::Point2F(left + 8, cy - 4), D2D1::Point2F(left + 8, cy + 4), btnBrush, UIStyle::Metrics::ControlStroke());
        btnBrush->Release();
    }
}

void SettingsControlKit::DrawStepperCard(ID2D1HwndRenderTarget* rt, IDWriteTextFormat* textFormat,
    const D2D1_COLOR_F& baseColor, float ix, float iy, const std::wstring& label,
    const std::wstring& value, bool hovered, int button)
{
    if (!rt) return;
    float cy = iy + 16.0f;
    D2D1_RECT_F cardRect = D2D1::RectF(ix, iy, ix + TWO_COLUMN_WIDTH, iy + CARD_HEIGHT);
    D2D1_ROUNDED_RECT roundedCard = D2D1::RoundedRect(cardRect, 6.0f, 6.0f);

    ID2D1SolidColorBrush* cardBg = nullptr;
    rt->CreateSolidColorBrush(D2D1::ColorF(baseColor.r, baseColor.g, baseColor.b, hovered ? 0.06f : 0.018f), &cardBg);
    if (cardBg)
    {
        rt->FillRoundedRectangle(roundedCard, cardBg);
        cardBg->Release();
    }

    ID2D1SolidColorBrush* cardBorder = nullptr;
    rt->CreateSolidColorBrush(D2D1::ColorF(baseColor.r, baseColor.g, baseColor.b, hovered ? 0.105f : 0.045f), &cardBorder);
    if (cardBorder)
    {
        rt->DrawRoundedRectangle(roundedCard, cardBorder, UIStyle::Metrics::ControlStroke());
        cardBorder->Release();
    }

    if (textFormat)
    {
        ID2D1SolidColorBrush* textBrush = nullptr;
        rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextNormal().d2d, &textBrush);
        if (textBrush)
        {
            rt->DrawTextW(label.c_str(), (UINT32)label.size(), textFormat,
                D2D1::RectF(ix + 10, cy - 10, ix + 75, cy + 10), textBrush);
            DWRITE_TEXT_ALIGNMENT oldAlignment = textFormat->GetTextAlignment();
            textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            rt->DrawTextW(value.c_str(), (UINT32)value.size(), textFormat,
                D2D1::RectF(ix + 101, cy - 10, ix + 129, cy + 10), textBrush);
            textFormat->SetTextAlignment(oldAlignment);
            textBrush->Release();
        }
    }

    DrawStepButton(rt, textFormat, baseColor, ix + 85, cy, false, hovered && button == 1);
    DrawStepButton(rt, textFormat, baseColor, ix + 129, cy, true, hovered && button == 2);
}

void SettingsControlKit::DrawActionButton(ID2D1HwndRenderTarget* rt, IDWriteTextFormat* textFormat,
    const D2D1_COLOR_F& baseColor, ID2D1SolidColorBrush* textBrush,
    const D2D1_RECT_F& buttonRect, const std::wstring& text, bool hovered, bool danger)
{
    if (!rt) return;
    D2D1_ROUNDED_RECT btnRect = D2D1::RoundedRect(buttonRect, 6.0f, 6.0f);
    ID2D1SolidColorBrush* btnBg = nullptr;
    D2D1_COLOR_F actionClr = danger ? UIStyle::ThemeColor::DangerRed().d2d : UIStyle::ThemeColor::Accent().d2d;
    D2D1_COLOR_F btnClr = hovered ? actionClr : baseColor;
    float btnAlpha = hovered ? (danger ? 0.16f : 0.12f) : 0.035f;
    rt->CreateSolidColorBrush(D2D1::ColorF(btnClr.r, btnClr.g, btnClr.b, btnAlpha), &btnBg);
    if (btnBg)
    {
        rt->FillRoundedRectangle(btnRect, btnBg);
        btnBg->Release();
    }

    ID2D1SolidColorBrush* btnBorder = nullptr;
    D2D1_COLOR_F borderClr = hovered ? actionClr : baseColor;
    float borderAlpha = hovered ? (danger ? 0.34f : 0.26f) : 0.07f;
    rt->CreateSolidColorBrush(D2D1::ColorF(borderClr.r, borderClr.g, borderClr.b, borderAlpha), &btnBorder);
    if (btnBorder)
    {
        rt->DrawRoundedRectangle(btnRect, btnBorder, UIStyle::Metrics::ControlStroke());
        btnBorder->Release();
    }

    if (textBrush)
    {
        DWRITE_TEXT_ALIGNMENT oldAlignment = textFormat->GetTextAlignment();
        textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        rt->DrawTextW(text.c_str(), (UINT32)text.size(), textFormat, buttonRect, textBrush);
        textFormat->SetTextAlignment(oldAlignment);
    }
}

void SettingsControlKit::DrawSmallButton(ID2D1HwndRenderTarget* rt, IDWriteTextFormat* textFormat,
    const D2D1_COLOR_F& baseColor, ID2D1SolidColorBrush* textBrush,
    D2D1_RECT_F buttonRect, const wchar_t* text, bool hovered, bool accent)
{
    if (!rt) return;
    D2D1_COLOR_F bg = accent ? UIStyle::ThemeColor::Accent().d2d : baseColor;
    bg.a = accent ? (hovered ? 0.28f : 0.18f) : (hovered ? 0.08f : 0.04f);
    ID2D1SolidColorBrush* bgBrush = nullptr;
    rt->CreateSolidColorBrush(bg, &bgBrush);
    if (bgBrush)
    {
        rt->FillRoundedRectangle(D2D1::RoundedRect(buttonRect, 5.0f, 5.0f), bgBrush);
        bgBrush->Release();
    }
    if (textBrush)
    {
        DWRITE_TEXT_ALIGNMENT old = textFormat->GetTextAlignment();
        textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        rt->DrawTextW(text, (UINT32)wcslen(text), textFormat, buttonRect, textBrush);
        textFormat->SetTextAlignment(old);
    }
}

void SettingsControlKit::DrawInfoCard(ID2D1HwndRenderTarget* rt, IDWriteTextFormat* textFormat,
    const D2D1_COLOR_F& baseColor, ID2D1SolidColorBrush* titleBrush, ID2D1SolidColorBrush* bodyBrush,
    const D2D1_RECT_F& cardRect, const wchar_t* title, const wchar_t* body)
{
    if (!rt) return;
    D2D1_ROUNDED_RECT roundedCard = D2D1::RoundedRect(cardRect, 6.0f, 6.0f);
    ID2D1SolidColorBrush* cardBrush = nullptr;
    rt->CreateSolidColorBrush(D2D1::ColorF(baseColor.r, baseColor.g, baseColor.b, 0.025f), &cardBrush);
    if (cardBrush)
    {
        rt->FillRoundedRectangle(roundedCard, cardBrush);
        cardBrush->Release();
    }
    rt->CreateSolidColorBrush(D2D1::ColorF(baseColor.r, baseColor.g, baseColor.b, 0.065f), &cardBrush);
    if (cardBrush)
    {
        rt->DrawRoundedRectangle(roundedCard, cardBrush, UIStyle::Metrics::ControlStroke());
        cardBrush->Release();
    }
    rt->DrawTextW(title, (UINT32)wcslen(title), textFormat,
        D2D1::RectF(cardRect.left + 10.0f, cardRect.top + 6.0f, cardRect.right - 10.0f, cardRect.top + 24.0f), titleBrush);
    rt->DrawTextW(body, (UINT32)wcslen(body), textFormat,
        D2D1::RectF(cardRect.left + 10.0f, cardRect.top + 24.0f, cardRect.right - 10.0f, cardRect.bottom - 5.0f), bodyBrush);
}
