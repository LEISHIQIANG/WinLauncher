#include "ConfigChromeRenderer.h"
#include "../GlassWindow.h"
#include "UIStyle.h"

namespace ConfigChromeRenderer
{
    bool HitTestCloseButton(POINT pt)
    {
        return (pt.x >= 490 && pt.x <= 510 && pt.y >= 10 && pt.y <= 30);
    }

    bool HitTestSettingsButton(POINT pt)
    {
        return (pt.x >= 430 && pt.x <= 475 && pt.y >= 10 && pt.y <= 30);
    }

    bool HitTestAddButton(POINT pt)
    {
        return (pt.x >= 430 && pt.x <= 510 && pt.y >= 36 && pt.y <= 56);
    }

    bool HitTestUpdatePillText(POINT pt, bool isDownloading)
    {
        float pillLeft = UpdatePillGeometry::kPillLeft;
        float textRight = UpdatePillGeometry::TextRight(isDownloading);
        return (pt.x >= (int)pillLeft && pt.x <= (int)textRight && pt.y >= 10 && pt.y <= 30);
    }

    bool HitTestUpdatePillClose(POINT pt, bool isDownloading)
    {
        float pillRight = UpdatePillGeometry::PillRight(isDownloading);
        float textRight = UpdatePillGeometry::TextRight(isDownloading);
        return (pt.x > (int)textRight && pt.x <= (int)pillRight && pt.y >= 10 && pt.y <= 30);
    }

    void DrawHeaderTitle(ID2D1HwndRenderTarget* rt, IDWriteTextFormat* tfHeader, GlassWindow* glassWnd, bool showSettings)
    {
        if (!rt || !tfHeader || !glassWnd)
            return;

        auto textBrush = glassWnd->GetOrCreateBrush(UIStyle::ThemeColor::TextNormal().d2d);
        if (textBrush)
        {
            std::wstring headerText = showSettings ? L"设置面板" : L"配置面板";
            rt->DrawTextW(headerText.c_str(), (UINT32)headerText.size(), tfHeader, D2D1::RectF(10, 10, 150, 30), textBrush.Get());
        }
    }

    void DrawUpdatePill(ID2D1HwndRenderTarget* rt, IDWriteTextFormat* tfLeft, GlassWindow* glassWnd,
                        bool isDownloading, int downloadProgress, bool hoveredText, bool hoveredClose)
    {
        if (!rt || !glassWnd)
            return;

        const float pillLeft = UpdatePillGeometry::kPillLeft;
        const float pillRight = UpdatePillGeometry::PillRight(isDownloading);
        const float textRight = UpdatePillGeometry::TextRight(isDownloading);

        D2D1_RECT_F pillRect = D2D1::RectF(pillLeft, 10.0f, pillRight, 30.0f);
        D2D1_ROUNDED_RECT roundedPill = D2D1::RoundedRect(pillRect, 4.0f, 4.0f);

        // Background
        float bgAlpha = hoveredText ? 0.22f : 0.10f;
        auto bgBrush = glassWnd->GetOrCreateBrush(D2D1::ColorF(UIStyle::ThemeColor::Accent().d2d.r, UIStyle::ThemeColor::Accent().d2d.g, UIStyle::ThemeColor::Accent().d2d.b, bgAlpha));
        if (bgBrush) rt->FillRoundedRectangle(roundedPill, bgBrush.Get());

        // Border
        float borderAlpha = (hoveredText || hoveredClose) ? 0.35f : 0.15f;
        auto borderBrush = glassWnd->GetOrCreateBrush(D2D1::ColorF(UIStyle::ThemeColor::Accent().d2d.r, UIStyle::ThemeColor::Accent().d2d.g, UIStyle::ThemeColor::Accent().d2d.b, borderAlpha));
        if (borderBrush) rt->DrawRoundedRectangle(roundedPill, borderBrush.Get(), 1.0f);

        // Text: "更新" or progress
        auto accentBrush = glassWnd->GetOrCreateBrush(UIStyle::ThemeColor::Accent().d2d);
        if (accentBrush && tfLeft)
        {
            std::wstring btnText = L"更新";
            if (isDownloading)
            {
                btnText = L"更新 " + std::to_wstring(downloadProgress) + L"%";
            }

            D2D1_RECT_F textRect = D2D1::RectF(pillLeft, 10.0f, textRight, 30.0f);
            tfLeft->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            rt->DrawTextW(btnText.c_str(), (UINT32)btnText.size(), tfLeft, textRect, accentBrush.Get());
            tfLeft->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        }

        // Close button "x"
        auto xBrush = glassWnd->GetOrCreateBrush(hoveredClose ? UIStyle::ThemeColor::Accent().d2d : UIStyle::ThemeColor::TextMuted().d2d);
        if (xBrush)
        {
            float xCenter = pillRight - 6.0f;
            rt->DrawLine(D2D1::Point2F(xCenter - 2.5f, 17.5f), D2D1::Point2F(xCenter + 2.5f, 22.5f), xBrush.Get(), 1.0f);
            rt->DrawLine(D2D1::Point2F(xCenter + 2.5f, 17.5f), D2D1::Point2F(xCenter - 2.5f, 22.5f), xBrush.Get(), 1.0f);
        }
    }

    void DrawCloseButton(ID2D1HwndRenderTarget* rt, GlassWindow* glassWnd, bool hoveredClose)
    {
        if (!rt || !glassWnd)
            return;

        D2D1_RECT_F closeRect = D2D1::RectF(490, 10, 510, 30);
        D2D1_ROUNDED_RECT roundedClose = D2D1::RoundedRect(closeRect, 4.0f, 4.0f);
        if (hoveredClose)
        {
            auto closeBg = glassWnd->GetOrCreateBrush(D2D1::ColorF(UIStyle::ThemeColor::DangerRed().d2d.r, UIStyle::ThemeColor::DangerRed().d2d.g, UIStyle::ThemeColor::DangerRed().d2d.b, 0.4f));
            if (closeBg) rt->FillRoundedRectangle(roundedClose, closeBg.Get());
        }

        auto xBrush = glassWnd->GetOrCreateBrush(UIStyle::ThemeColor::TextMuted().d2d);
        if (xBrush)
        {
            rt->DrawLine(D2D1::Point2F(495, 15), D2D1::Point2F(505, 25), xBrush.Get(), UIStyle::Metrics::IconStroke());
            rt->DrawLine(D2D1::Point2F(505, 15), D2D1::Point2F(495, 25), xBrush.Get(), UIStyle::Metrics::IconStroke());
        }
    }

    void DrawSettingsButton(ID2D1HwndRenderTarget* rt, IDWriteTextFormat* tfLeft, GlassWindow* glassWnd, bool showSettings, bool hoveredSettingsBtn)
    {
        if (!rt || !glassWnd)
            return;

        D2D1_RECT_F settingsRect = D2D1::RectF(430, 10, 475, 30);
        D2D1_ROUNDED_RECT roundedSettings = D2D1::RoundedRect(settingsRect, 4.0f, 4.0f);
        if (hoveredSettingsBtn)
        {
            auto btnBg = glassWnd->GetOrCreateBrush(UIStyle::ThemeColor::ButtonBgHover().d2d);
            if (btnBg) rt->FillRoundedRectangle(roundedSettings, btnBg.Get());
        }
        auto btnBorder = glassWnd->GetOrCreateBrush(hoveredSettingsBtn ?
            UIStyle::ThemeColor::ButtonBorderHover().d2d : UIStyle::ThemeColor::ButtonBorderNormal().d2d);
        if (btnBorder) rt->DrawRoundedRectangle(roundedSettings, btnBorder.Get(), UIStyle::Metrics::ControlStroke());

        if (tfLeft)
        {
            auto textBrush = glassWnd->GetOrCreateBrush(UIStyle::ThemeColor::TextNormal().d2d);
            if (textBrush)
            {
                std::wstring btnText = showSettings ? L"返回" : L"设置";
                tfLeft->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                rt->DrawTextW(btnText.c_str(), (UINT32)btnText.size(), tfLeft, settingsRect, textBrush.Get());
                tfLeft->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            }
        }
    }

    void DrawAddButton(ID2D1HwndRenderTarget* rt, IDWriteTextFormat* tfLeft, GlassWindow* glassWnd,
                       bool hoveredAddBtn, bool dropDownMenuVisible)
    {
        if (!rt || !glassWnd)
            return;

        D2D1_RECT_F addRect = D2D1::RectF(430, 36, 510, 56);
        D2D1_ROUNDED_RECT roundedAdd = D2D1::RoundedRect(addRect, 4.0f, 4.0f);

        bool isActive = hoveredAddBtn || dropDownMenuVisible;

        D2D1_COLOR_F accentClr = UIStyle::ThemeColor::Accent().d2d;
        D2D1_COLOR_F bgClr = accentClr;
        bgClr.a = isActive ? 0.22f : 0.18f;
        auto btnBg = glassWnd->GetOrCreateBrush(bgClr);
        if (btnBg) rt->FillRoundedRectangle(roundedAdd, btnBg.Get());

        D2D1_COLOR_F borderClr = accentClr;
        borderClr.a = 0.55f;
        auto btnBorder = glassWnd->GetOrCreateBrush(borderClr);
        if (btnBorder) rt->DrawRoundedRectangle(roundedAdd, btnBorder.Get(), UIStyle::Metrics::HairlineStroke());

        if (tfLeft)
        {
            auto textBrush = glassWnd->GetOrCreateBrush(UIStyle::ThemeColor::TextNormal().d2d);
            if (textBrush)
            {
                const wchar_t* btnText = L"+\u6dfb\u52a0\u56fe\u6807";  // "+添加图标"
                tfLeft->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                rt->DrawTextW(btnText, (UINT32)wcslen(btnText), tfLeft, addRect, textBrush.Get());
                tfLeft->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            }
        }
    }
}
