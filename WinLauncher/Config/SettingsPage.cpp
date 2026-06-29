#include "SettingsPage.h"
#include "IConfigWindow.h"
#include "UIStyle.h"
#include "ConfirmWindow.h"
#include "../Services/UpdateService.h"
#include "..\version.h"
#include <cwchar>
#include <cmath>
#include <vector>

namespace
{
    constexpr float GLOBAL_SCALE_CARD_LEFT = 160.0f;
    constexpr float GLOBAL_SCALE_CARD_TOP = 112.0f;
    constexpr float GLOBAL_SCALE_CARD_RIGHT = 510.0f;
    constexpr float GLOBAL_SCALE_CARD_BOTTOM = 148.0f;
    constexpr float GLOBAL_SCALE_TRACK_LEFT = 250.0f;
    constexpr float GLOBAL_SCALE_TRACK_RIGHT = 402.0f;
    constexpr float GLOBAL_SCALE_TRACK_Y = 130.0f;
    constexpr float GLOBAL_SCALE_APPLY_LEFT = 448.0f;
    constexpr float GLOBAL_SCALE_APPLY_TOP = 118.0f;
    constexpr float GLOBAL_SCALE_APPLY_RIGHT = 506.0f;
    constexpr float GLOBAL_SCALE_APPLY_BOTTOM = 142.0f;
}

SettingsPage::SettingsPage(IConfigWindow* owner)
    : m_owner(owner)
{
}

SettingsPage::~SettingsPage()
{
}

int SettingsPage::PendingGlobalScalePercent()
{
    if (m_pendingGlobalScalePercent == 0)
    {
        m_pendingGlobalScalePercent = m_owner ? m_owner->GetGlobalScalePercent() : 100;
    }
    return UIStyle::Scaling::ClampPercent(m_pendingGlobalScalePercent);
}

int SettingsPage::GlobalScaleFromPoint(POINT pt) const
{
    float x = (float)pt.x;
    if (x < GLOBAL_SCALE_TRACK_LEFT) x = GLOBAL_SCALE_TRACK_LEFT;
    if (x > GLOBAL_SCALE_TRACK_RIGHT) x = GLOBAL_SCALE_TRACK_RIGHT;

    float t = (x - GLOBAL_SCALE_TRACK_LEFT) / (GLOBAL_SCALE_TRACK_RIGHT - GLOBAL_SCALE_TRACK_LEFT);
    int steps = (int)std::round(t * ((UIStyle::Scaling::MaxPercent - UIStyle::Scaling::MinPercent) / UIStyle::Scaling::StepPercent));
    return UIStyle::Scaling::MinPercent + steps * UIStyle::Scaling::StepPercent;
}

void SettingsPage::SetCategory(int categoryIndex)
{
    m_categoryIndex = categoryIndex;
    m_hoveredAutoStart = false;
    m_hoveredHideTrayIcon = false;
    m_hoveredOpenConfigFile = false;
    m_hoveredOpenLogFile = false;
    m_hoveredConfigDirText = false;
    m_hoveredTrigger = -1;
    m_hoveredTheme = -1;
    m_hoveredThemeColor = -1;
    m_hoveredWindowMode = -1;
    m_hoveredAppearanceSetting = -1;
    m_hoveredAppearanceButton = 0;
    m_hoveredThemeDetailSetting = -1;
    m_hoveredThemeDetailButton = 0;
    m_hoveredAnimationToggle = false;
    m_hoveredHardwareAcceleration = false;
    m_hoveredAnimationDuration = false;
    m_hoveredAnimationDurationButton = 0;
    m_hoveredGlobalScaleSlider = false;
    m_hoveredGlobalScaleApply = false;
    m_draggingGlobalScaleSlider = false;
    m_hoveredApplyUpdate = false;
    m_hoveredCheckUpdate = false;
    m_pendingGlobalScalePercent = m_owner ? m_owner->GetGlobalScalePercent() : 100;
}

void SettingsPage::OnPaint(ID2D1HwndRenderTarget* rt, const D2D1_RECT_F& rect)
{
    IDWriteTextFormat* tfTitle = m_owner->GetTitleFont();
    IDWriteTextFormat* tfDefault = m_owner->GetDefaultFont();
    if (tfDefault)
    {
        tfDefault->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    }

    D2D1_COLOR_F baseClr = UIStyle::ThemeColor::ThemeBase().d2d;

    // 1. Draw Page Title
    if (tfTitle)
    {
        ID2D1SolidColorBrush* textBrush = nullptr;
        rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextNormal().d2d, &textBrush);
        if (textBrush)
        {
            std::wstring title;
            switch (m_categoryIndex)
            {
            case 0: title = L"系统设置"; break;
            case 1: title = L"弹窗外观"; break;
            case 2: title = L"弹窗交互"; break;
            case 3: title = L"配置管理"; break;
            case 4: title = L"关于软件"; break;
            default: title = L"系统设置"; break;
            }
            rt->DrawTextW(title.c_str(), (UINT32)title.size(), tfTitle,
                D2D1::RectF(160, 42, 510, 62), textBrush);
            textBrush->Release();
        }
    }

    if (m_categoryIndex == 0) // 系统设置
    {
        auto drawInlineCheckbox = [&](float x, bool checked, bool hovered, const wchar_t* label)
        {
            D2D1_RECT_F boxRect = D2D1::RectF(x, 85, x + 16.0f, 101);
            D2D1_ROUNDED_RECT roundedBox = D2D1::RoundedRect(boxRect, 3.0f, 3.0f);

            ID2D1SolidColorBrush* bgBrush = nullptr;
            float alphaBg = hovered ? 0.105f : 0.035f;
            rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, alphaBg), &bgBrush);

            ID2D1SolidColorBrush* borderBrush = nullptr;
            float alphaBorder = hovered ? 0.18f : 0.065f;
            rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, alphaBorder), &borderBrush);

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

            if (tfDefault)
            {
                ID2D1SolidColorBrush* tb = nullptr;
                rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextNormal().d2d, &tb);
                if (tb)
                {
                    rt->DrawTextW(label, (UINT32)wcslen(label), tfDefault,
                        D2D1::RectF(x + 26.0f, 83, x + 90.0f, 103), tb);
                    tb->Release();
                }
            }
        };

        drawInlineCheckbox(160.0f, m_owner->GetAutoStart(), m_hoveredAutoStart, L"开机自启");
        drawInlineCheckbox(245.0f, m_owner->GetHideTrayIcon(), m_hoveredHideTrayIcon, L"隐藏托盘");
        drawInlineCheckbox(330.0f, m_owner->GetHardwareAccelerationEnabled(), m_hoveredHardwareAcceleration, L"硬件加速");
        drawInlineCheckbox(415.0f, !m_owner->GetAnimationEnabled(), m_hoveredAnimationToggle, L"关闭动画");

        // Draw Global Scale Slider
        {
            int currentScale = m_owner->GetGlobalScalePercent();
            int pendingScale = PendingGlobalScalePercent();
            bool hasPendingChange = (pendingScale != currentScale);
            bool isRowHovered = m_hoveredGlobalScaleSlider || m_hoveredGlobalScaleApply || m_draggingGlobalScaleSlider;

            D2D1_ROUNDED_RECT roundedCard = D2D1::RoundedRect(
                D2D1::RectF(GLOBAL_SCALE_CARD_LEFT, GLOBAL_SCALE_CARD_TOP, GLOBAL_SCALE_CARD_RIGHT, GLOBAL_SCALE_CARD_BOTTOM),
                6.0f, 6.0f);

            ID2D1SolidColorBrush* cardBg = nullptr;
            rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, isRowHovered ? 0.06f : 0.018f), &cardBg);
            if (cardBg)
            {
                rt->FillRoundedRectangle(roundedCard, cardBg);
                cardBg->Release();
            }

            ID2D1SolidColorBrush* cardBorder = nullptr;
            rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, isRowHovered ? 0.105f : 0.045f), &cardBorder);
            if (cardBorder)
            {
                rt->DrawRoundedRectangle(roundedCard, cardBorder, UIStyle::Metrics::ControlStroke());
                cardBorder->Release();
            }

            if (tfDefault)
            {
                ID2D1SolidColorBrush* textBrush = nullptr;
                rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextNormal().d2d, &textBrush);
                if (textBrush)
                {
                    std::wstring label = L"全局缩放";
                    rt->DrawTextW(label.c_str(), (UINT32)label.size(), tfDefault,
                        D2D1::RectF(170.0f, 120.0f, 240.0f, 140.0f), textBrush);

                    wchar_t valueBuf[32];
                    swprintf_s(valueBuf, L"%d%%", pendingScale);
                    std::wstring valueText = valueBuf;
                    tfDefault->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    rt->DrawTextW(valueText.c_str(), (UINT32)valueText.size(), tfDefault,
                        D2D1::RectF(406.0f, 120.0f, 444.0f, 140.0f), textBrush);
                    tfDefault->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                    textBrush->Release();
                }
            }

            ID2D1SolidColorBrush* trackBrush = nullptr;
            rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, m_hoveredGlobalScaleSlider ? 0.20f : 0.12f), &trackBrush);
            if (trackBrush)
            {
                rt->DrawLine(
                    D2D1::Point2F(GLOBAL_SCALE_TRACK_LEFT, GLOBAL_SCALE_TRACK_Y),
                    D2D1::Point2F(GLOBAL_SCALE_TRACK_RIGHT, GLOBAL_SCALE_TRACK_Y),
                    trackBrush,
                    3.0f);
                trackBrush->Release();
            }

            float sliderT = (pendingScale - UIStyle::Scaling::MinPercent) / (float)(UIStyle::Scaling::MaxPercent - UIStyle::Scaling::MinPercent);
            float thumbX = GLOBAL_SCALE_TRACK_LEFT + sliderT * (GLOBAL_SCALE_TRACK_RIGHT - GLOBAL_SCALE_TRACK_LEFT);

            ID2D1SolidColorBrush* accentBrush = nullptr;
            rt->CreateSolidColorBrush(UIStyle::ThemeColor::Accent().d2d, &accentBrush);
            if (accentBrush)
            {
                rt->DrawLine(
                    D2D1::Point2F(GLOBAL_SCALE_TRACK_LEFT, GLOBAL_SCALE_TRACK_Y),
                    D2D1::Point2F(thumbX, GLOBAL_SCALE_TRACK_Y),
                    accentBrush,
                    3.0f);
                rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(thumbX, GLOBAL_SCALE_TRACK_Y), 6.0f, 6.0f), accentBrush);
                accentBrush->Release();
            }

            D2D1_COLOR_F applyBg = hasPendingChange ? UIStyle::ThemeColor::Accent().d2d : baseClr;
            applyBg.a = hasPendingChange ? (m_hoveredGlobalScaleApply ? 0.28f : 0.20f) : (m_hoveredGlobalScaleApply ? 0.075f : 0.035f);
            ID2D1SolidColorBrush* applyBgBrush = nullptr;
            rt->CreateSolidColorBrush(applyBg, &applyBgBrush);
            D2D1_ROUNDED_RECT applyRect = D2D1::RoundedRect(
                D2D1::RectF(GLOBAL_SCALE_APPLY_LEFT, GLOBAL_SCALE_APPLY_TOP, GLOBAL_SCALE_APPLY_RIGHT, GLOBAL_SCALE_APPLY_BOTTOM),
                5.0f, 5.0f);
            if (applyBgBrush)
            {
                rt->FillRoundedRectangle(applyRect, applyBgBrush);
                applyBgBrush->Release();
            }

            ID2D1SolidColorBrush* applyBorderBrush = nullptr;
            D2D1_COLOR_F applyBorder = hasPendingChange ? UIStyle::ThemeColor::Accent().d2d : baseClr;
            applyBorder.a = hasPendingChange ? 0.62f : 0.12f;
            rt->CreateSolidColorBrush(applyBorder, &applyBorderBrush);
            if (applyBorderBrush)
            {
                rt->DrawRoundedRectangle(applyRect, applyBorderBrush, UIStyle::Metrics::ControlStroke());
                applyBorderBrush->Release();
            }

            if (tfDefault)
            {
                ID2D1SolidColorBrush* applyTextBrush = nullptr;
                rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextNormal().d2d, &applyTextBrush);
                if (applyTextBrush)
                {
                    std::wstring applyText = L"应用";
                    tfDefault->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    rt->DrawTextW(applyText.c_str(), (UINT32)applyText.size(), tfDefault,
                        D2D1::RectF(GLOBAL_SCALE_APPLY_LEFT, GLOBAL_SCALE_APPLY_TOP + 2.0f, GLOBAL_SCALE_APPLY_RIGHT, GLOBAL_SCALE_APPLY_BOTTOM),
                        applyTextBrush);
                    tfDefault->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                    applyTextBrush->Release();
                }
            }
        }

        // Draw Theme Option Header
        if (tfDefault)
        {
            ID2D1SolidColorBrush* tb = nullptr;
            rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextMuted().d2d, &tb);
            if (tb)
            {
                std::wstring label = L"软件主题";
                rt->DrawTextW(label.c_str(), (UINT32)label.size(), tfDefault,
                    D2D1::RectF(160, 158, 510, 178), tb);
                tb->Release();
            }
        }

        // Draw Theme Buttons side-by-side
        int currentTheme = m_owner->GetTheme();
        std::wstring themeLabels[] = { L"深色主题", L"浅色主题" };
        for (int i = 0; i < 2; i++)
        {
            bool isSelected = (i == currentTheme);
            bool isHovered = (i == m_hoveredTheme);
            float xStart = (i == 0) ? 160.0f : 345.0f;
            D2D1_RECT_F cardRect = D2D1::RectF(xStart, 180.0f, xStart + 165.0f, 212.0f);
            D2D1_ROUNDED_RECT roundedCard = D2D1::RoundedRect(cardRect, 6.0f, 6.0f);

            ID2D1SolidColorBrush* bgBrush = nullptr;
            D2D1_COLOR_F bgClr = isSelected ? UIStyle::ThemeColor::Accent().d2d : baseClr;
            float bgAlpha = isSelected ? (isHovered ? 0.16f : 0.10f) : (isHovered ? 0.06f : 0.018f);
            rt->CreateSolidColorBrush(D2D1::ColorF(bgClr.r, bgClr.g, bgClr.b, bgAlpha), &bgBrush);
            if (bgBrush)
            {
                rt->FillRoundedRectangle(roundedCard, bgBrush);
                bgBrush->Release();
            }

            ID2D1SolidColorBrush* borderBrush = nullptr;
            D2D1_COLOR_F borderClr = isSelected ? UIStyle::ThemeColor::Accent().d2d : baseClr;
            float borderAlpha = isSelected ? 0.34f : (isHovered ? 0.105f : 0.045f);
            rt->CreateSolidColorBrush(D2D1::ColorF(borderClr.r, borderClr.g, borderClr.b, borderAlpha), &borderBrush);
            if (borderBrush)
            {
                rt->DrawRoundedRectangle(roundedCard, borderBrush, UIStyle::Metrics::ControlStroke());
                borderBrush->Release();
            }

            // Text
            if (tfDefault)
            {
                ID2D1SolidColorBrush* textBrush = nullptr;
                D2D1_COLOR_F txtClr = isSelected ? UIStyle::ThemeColor::Accent().d2d : UIStyle::ThemeColor::TextNormal().d2d;
                rt->CreateSolidColorBrush(txtClr, &textBrush);
                if (textBrush)
                {
                    tfDefault->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    rt->DrawTextW(themeLabels[i].c_str(), (UINT32)themeLabels[i].size(), tfDefault,
                        D2D1::RectF(xStart, 186.0f, xStart + 165.0f, 212.0f), textBrush);
                    tfDefault->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                    textBrush->Release();
                }
            }
        }

        // Draw Theme Color Option Header
        int currentThemeColor = m_owner->GetThemeColor();
        if (tfDefault)
        {
            ID2D1SolidColorBrush* tb = nullptr;
            rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextMuted().d2d, &tb);
            if (tb)
            {
                std::wstring label = L"主题颜色";
                rt->DrawTextW(label.c_str(), (UINT32)label.size(), tfDefault,
                    D2D1::RectF(160, 218, 260, 238), tb);

                std::wstring currentLabel = UIStyle::GetThemeColorPresetName(currentThemeColor);
                rt->DrawTextW(currentLabel.c_str(), (UINT32)currentLabel.size(), tfDefault,
                    D2D1::RectF(430, 218, 510, 238), tb);
                tb->Release();
            }
        }

        for (int i = 0; i < UIStyle::ThemeColorPresetCount(); i++)
        {
            const float swatchLeft = 160.0f;
            const float swatchRight = 510.0f;
            const float swatchSize = 18.0f;
            const float swatchStep = (swatchRight - swatchLeft - swatchSize) / (float)(UIStyle::ThemeColorPresetCount() - 1);
            bool isSelected = (i == currentThemeColor);
            bool isHovered = (i == m_hoveredThemeColor);
            float x = swatchLeft + i * swatchStep;
            D2D1_RECT_F swatchRect = D2D1::RectF(x, 244.0f, x + swatchSize, 262.0f);
            D2D1_ROUNDED_RECT roundedSwatch = D2D1::RoundedRect(swatchRect, 5.0f, 5.0f);

            ID2D1SolidColorBrush* swatchBrush = nullptr;
            rt->CreateSolidColorBrush(UIStyle::GetThemeColorPresetColor(i).d2d, &swatchBrush);
            if (swatchBrush)
            {
                rt->FillRoundedRectangle(roundedSwatch, swatchBrush);
                swatchBrush->Release();
            }

            ID2D1SolidColorBrush* borderBrush = nullptr;
            D2D1_COLOR_F borderClr = isSelected ? UIStyle::GetThemeColorPresetColor(i).d2d : baseClr;
            float borderAlpha = isSelected ? 0.92f : (isHovered ? 0.42f : 0.18f);
            rt->CreateSolidColorBrush(D2D1::ColorF(borderClr.r, borderClr.g, borderClr.b, borderAlpha), &borderBrush);
            if (borderBrush)
            {
                rt->DrawRoundedRectangle(roundedSwatch, borderBrush, isSelected ? 1.6f : UIStyle::Metrics::ControlStroke());
                borderBrush->Release();
            }

            if (isSelected)
            {
                ID2D1SolidColorBrush* checkBrush = nullptr;
                rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextOnAccent().d2d, &checkBrush);
                if (checkBrush)
                {
                    rt->DrawLine(D2D1::Point2F(x + 5.0f, 253.0f), D2D1::Point2F(x + 8.0f, 256.0f), checkBrush, 1.4f);
                    rt->DrawLine(D2D1::Point2F(x + 8.0f, 256.0f), D2D1::Point2F(x + 14.0f, 249.0f), checkBrush, 1.4f);
                    checkBrush->Release();
                }
            }
        }

        // 3. Draw Window Mode Option Header
        int currentWindowMode = m_owner->GetWindowMode();
        if (tfDefault)
        {
            ID2D1SolidColorBrush* tb = nullptr;
            rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextMuted().d2d, &tb);
            if (tb)
            {
                std::wstring label = L"窗口材质";
                rt->DrawTextW(label.c_str(), (UINT32)label.size(), tfDefault,
                    D2D1::RectF(160, 276, 510, 294), tb);
                tb->Release();
            }
        }

        // Draw Window Mode Buttons side-by-side
        std::wstring modeLabels[] = { L"毛玻璃材质", L"亚克力材质" };
        for (int i = 0; i < 2; i++)
        {
            bool isSelected = (i == currentWindowMode);
            bool isHovered = (i == m_hoveredWindowMode);
            float xStart = (i == 0) ? 160.0f : 345.0f;
            D2D1_RECT_F cardRect = D2D1::RectF(xStart, 298.0f, xStart + 165.0f, 326.0f);
            D2D1_ROUNDED_RECT roundedCard = D2D1::RoundedRect(cardRect, 6.0f, 6.0f);

            ID2D1SolidColorBrush* bgBrush = nullptr;
            D2D1_COLOR_F bgClr = isSelected ? UIStyle::ThemeColor::Accent().d2d : baseClr;
            float bgAlpha = isSelected ? (isHovered ? 0.16f : 0.10f) : (isHovered ? 0.06f : 0.018f);
            rt->CreateSolidColorBrush(D2D1::ColorF(bgClr.r, bgClr.g, bgClr.b, bgAlpha), &bgBrush);
            if (bgBrush)
            {
                rt->FillRoundedRectangle(roundedCard, bgBrush);
                bgBrush->Release();
            }

            ID2D1SolidColorBrush* borderBrush = nullptr;
            D2D1_COLOR_F borderClr = isSelected ? UIStyle::ThemeColor::Accent().d2d : baseClr;
            float borderAlpha = isSelected ? 0.34f : (isHovered ? 0.105f : 0.045f);
            rt->CreateSolidColorBrush(D2D1::ColorF(borderClr.r, borderClr.g, borderClr.b, borderAlpha), &borderBrush);
            if (borderBrush)
            {
                rt->DrawRoundedRectangle(roundedCard, borderBrush, UIStyle::Metrics::ControlStroke());
                borderBrush->Release();
            }

            // Text
            if (tfDefault)
            {
                ID2D1SolidColorBrush* textBrush = nullptr;
                D2D1_COLOR_F txtClr = isSelected ? UIStyle::ThemeColor::Accent().d2d : UIStyle::ThemeColor::TextNormal().d2d;
                rt->CreateSolidColorBrush(txtClr, &textBrush);
                if (textBrush)
                {
                    tfDefault->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    rt->DrawTextW(modeLabels[i].c_str(), (UINT32)modeLabels[i].size(), tfDefault,
                        D2D1::RectF(xStart, 302.0f, xStart + 165.0f, 326.0f), textBrush);
                    tfDefault->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                    textBrush->Release();
                }
            }
        }

        // Draw Theme Details (6 Sliders/Cards in 2 columns) - Glass and Acrylic Modes
        if (currentWindowMode == 0 || currentWindowMode == 1)
        {
            if (tfDefault)
            {
                ID2D1SolidColorBrush* tb = nullptr;
                rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextMuted().d2d, &tb);
                if (tb)
                {
                    std::wstring label = L"背景效果调节";
                    rt->DrawTextW(label.c_str(), (UINT32)label.size(), tfDefault,
                        D2D1::RectF(160, 338, 510, 354), tb);
                    tb->Release();
                }
            }

            auto& cfg = (currentWindowMode == 1) ?
                ((currentTheme == 1) ? UIStyle::g_AcrylicLightConfig : UIStyle::g_AcrylicDarkConfig) :
                ((currentTheme == 1) ? UIStyle::g_LightConfig : UIStyle::g_DarkConfig);

            struct DetailItem {
                int originalIdx;
                std::wstring label;
                float val;
            };

            std::vector<DetailItem> activeItems;
            activeItems.push_back({ 1, L"模糊度", cfg.blur });
            activeItems.push_back({ 2, L"透明度", (1.0f - cfg.opacity) * 100.0f });
            activeItems.push_back({ 3, L"高光", cfg.highlight * 100.0f });
            activeItems.push_back({ 4, L"亮度", cfg.brightness * 100.0f });
            activeItems.push_back({ 5, L"饱和度", cfg.saturation });

            for (int i = 0; i < (int)activeItems.size(); i++)
            {
                int col = i % 2;
                int row = i / 2;
                float ix = 160.0f + col * 175.0f;
                float iy = 360.0f + row * 38.0f;
                float cy = iy + 16.0f;
                bool isRowHovered = (m_hoveredThemeDetailSetting == activeItems[i].originalIdx);

                // 1. Draw subtle card background
                D2D1_RECT_F cardRect = D2D1::RectF(ix, iy, ix + 165.0f, iy + 32.0f);
                D2D1_ROUNDED_RECT roundedCard = D2D1::RoundedRect(cardRect, 6.0f, 6.0f);

                ID2D1SolidColorBrush* cardBg = nullptr;
                float alphaBg = isRowHovered ? 0.06f : 0.018f;
                rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, alphaBg), &cardBg);
                if (cardBg)
                {
                    rt->FillRoundedRectangle(roundedCard, cardBg);
                    cardBg->Release();
                }

                ID2D1SolidColorBrush* cardBorder = nullptr;
                float alphaBorder = isRowHovered ? 0.105f : 0.045f;
                rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, alphaBorder), &cardBorder);
                if (cardBorder)
                {
                    rt->DrawRoundedRectangle(roundedCard, cardBorder, UIStyle::Metrics::ControlStroke());
                    cardBorder->Release();
                }

                // 2. Draw Label Text
                if (tfDefault)
                {
                    ID2D1SolidColorBrush* textBrush = nullptr;
                    rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextNormal().d2d, &textBrush);
                    if (textBrush)
                    {
                        rt->DrawTextW(activeItems[i].label.c_str(), (UINT32)activeItems[i].label.size(), tfDefault,
                            D2D1::RectF(ix + 10, cy - 10, ix + 75, cy + 10), textBrush);
                        textBrush->Release();
                    }
                }

                // 3. Draw Minus Button
                D2D1_ROUNDED_RECT roundedMinus = D2D1::RoundedRect(D2D1::RectF(ix + 85, cy - 8, ix + 101, cy + 8), 3.0f, 3.0f);
                bool isMinusHovered = (isRowHovered && m_hoveredThemeDetailButton == 1);
                ID2D1SolidColorBrush* btnBrush = nullptr;
                rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, isMinusHovered ? 0.105f : 0.04f), &btnBrush);
                if (btnBrush)
                {
                    rt->FillRoundedRectangle(roundedMinus, btnBrush);
                    btnBrush->Release();
                }
                rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, isMinusHovered ? 0.18f : 0.075f), &btnBrush);
                if (btnBrush)
                {
                    rt->DrawRoundedRectangle(roundedMinus, btnBrush, UIStyle::Metrics::ControlStroke());
                    btnBrush->Release();
                }
                // Draw minus sign
                rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextNormal().d2d, &btnBrush);
                if (btnBrush)
                {
                    rt->DrawLine(D2D1::Point2F(ix + 89, cy), D2D1::Point2F(ix + 97, cy), btnBrush, UIStyle::Metrics::ControlStroke());
                    btnBrush->Release();
                }

                // 4. Draw Value Text
                if (tfDefault)
                {
                    ID2D1SolidColorBrush* textBrush = nullptr;
                    rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextNormal().d2d, &textBrush);
                    if (textBrush)
                    {
                        wchar_t valBuf[32];
                        if (activeItems[i].originalIdx == 5)
                        {
                            swprintf_s(valBuf, L"%.1fx", activeItems[i].val);
                        }
                        else
                        {
                            swprintf_s(valBuf, L"%d", (int)activeItems[i].val);
                            if (activeItems[i].originalIdx == 1) wcscat_s(valBuf, L"px");
                            else if (activeItems[i].originalIdx == 2 || activeItems[i].originalIdx == 3 || activeItems[i].originalIdx == 4) wcscat_s(valBuf, L"%");
                        }
                        
                        std::wstring valStr = valBuf;
                        tfDefault->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                        rt->DrawTextW(valStr.c_str(), (UINT32)valStr.size(), tfDefault,
                            D2D1::RectF(ix + 101, cy - 10, ix + 129, cy + 10), textBrush);
                        tfDefault->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                        textBrush->Release();
                    }
                }

                // 5. Draw Plus Button
                D2D1_ROUNDED_RECT roundedPlus = D2D1::RoundedRect(D2D1::RectF(ix + 129, cy - 8, ix + 145, cy + 8), 3.0f, 3.0f);
                bool isPlusHovered = (isRowHovered && m_hoveredThemeDetailButton == 2);
                rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, isPlusHovered ? 0.105f : 0.04f), &btnBrush);
                if (btnBrush)
                {
                    rt->FillRoundedRectangle(roundedPlus, btnBrush);
                    btnBrush->Release();
                }
                rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, isPlusHovered ? 0.18f : 0.075f), &btnBrush);
                if (btnBrush)
                {
                    rt->DrawRoundedRectangle(roundedPlus, btnBrush, UIStyle::Metrics::ControlStroke());
                    btnBrush->Release();
                }
                // Draw plus sign
                rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextNormal().d2d, &btnBrush);
                if (btnBrush)
                {
                    rt->DrawLine(D2D1::Point2F(ix + 133, cy), D2D1::Point2F(ix + 141, cy), btnBrush, UIStyle::Metrics::ControlStroke());
                    rt->DrawLine(D2D1::Point2F(ix + 137, cy - 4), D2D1::Point2F(ix + 137, cy + 4), btnBrush, UIStyle::Metrics::ControlStroke());
                    btnBrush->Release();
                }
            }
        }
    }
    else if (m_categoryIndex == 1) // 弹窗外观
    {
        std::wstring labels[] = {
            L"图标列数",
            L"图标行数",
            L"图标大小",
            L"图标间距",
            L"图标圆角",
            L"窗口边距",
            L"DOCK行数"
        };

        int values[] = {
            m_owner->GetPopupColumns(),
            m_owner->GetPopupRows(),
            m_owner->GetPopupIconSize(),
            m_owner->GetPopupIconGap(),
            m_owner->GetPopupIconRadius(),
            m_owner->GetPopupWndPadding(),
            m_owner->GetDockHeight()
        };

        for (int i = 0; i < 7; i++)
        {
            int col = i % 2;
            int row = i / 2;
            float ix = 160.0f + col * 175.0f;
            float iy = 90.0f + row * 42.0f;
            float cy = iy + 16.0f;
            bool isRowHovered = (m_hoveredAppearanceSetting == i);

            // 1. Draw subtle card background
            D2D1_RECT_F cardRect = D2D1::RectF(ix, iy, ix + 165.0f, iy + 32.0f);
            D2D1_ROUNDED_RECT roundedCard = D2D1::RoundedRect(cardRect, 6.0f, 6.0f);

            ID2D1SolidColorBrush* cardBg = nullptr;
            float alphaBg = isRowHovered ? 0.06f : 0.018f;
            rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, alphaBg), &cardBg);
            if (cardBg)
            {
                rt->FillRoundedRectangle(roundedCard, cardBg);
                cardBg->Release();
            }

            ID2D1SolidColorBrush* cardBorder = nullptr;
            float alphaBorder = isRowHovered ? 0.105f : 0.045f;
            rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, alphaBorder), &cardBorder);
            if (cardBorder)
            {
                rt->DrawRoundedRectangle(roundedCard, cardBorder, UIStyle::Metrics::ControlStroke());
                cardBorder->Release();
            }

            // 2. Draw Label Text
            if (tfDefault)
            {
                ID2D1SolidColorBrush* textBrush = nullptr;
                rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextNormal().d2d, &textBrush);
                if (textBrush)
                {
                    rt->DrawTextW(labels[i].c_str(), (UINT32)labels[i].size(), tfDefault,
                        D2D1::RectF(ix + 10, cy - 10, ix + 75, cy + 10), textBrush);
                    textBrush->Release();
                }
            }

            // 3. Draw Minus Button
            D2D1_ROUNDED_RECT roundedMinus = D2D1::RoundedRect(D2D1::RectF(ix + 85, cy - 8, ix + 101, cy + 8), 3.0f, 3.0f);
            bool isMinusHovered = (isRowHovered && m_hoveredAppearanceButton == 1);
            ID2D1SolidColorBrush* btnBrush = nullptr;
            rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, isMinusHovered ? 0.105f : 0.04f), &btnBrush);
            if (btnBrush)
            {
                rt->FillRoundedRectangle(roundedMinus, btnBrush);
                btnBrush->Release();
            }
            rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, isMinusHovered ? 0.18f : 0.075f), &btnBrush);
            if (btnBrush)
            {
                rt->DrawRoundedRectangle(roundedMinus, btnBrush, UIStyle::Metrics::ControlStroke());
                btnBrush->Release();
            }
            // Draw minus sign
            rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextNormal().d2d, &btnBrush);
            if (btnBrush)
            {
                rt->DrawLine(D2D1::Point2F(ix + 89, cy), D2D1::Point2F(ix + 97, cy), btnBrush, UIStyle::Metrics::ControlStroke());
                btnBrush->Release();
            }

            // 4. Draw Value Text
            if (tfDefault)
            {
                ID2D1SolidColorBrush* textBrush = nullptr;
                rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextNormal().d2d, &textBrush);
                if (textBrush)
                {
                    std::wstring valStr = std::to_wstring(values[i]);
                    tfDefault->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    rt->DrawTextW(valStr.c_str(), (UINT32)valStr.size(), tfDefault,
                        D2D1::RectF(ix + 101, cy - 10, ix + 129, cy + 10), textBrush);
                    tfDefault->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                    textBrush->Release();
                }
            }

            // 5. Draw Plus Button
            D2D1_ROUNDED_RECT roundedPlus = D2D1::RoundedRect(D2D1::RectF(ix + 129, cy - 8, ix + 145, cy + 8), 3.0f, 3.0f);
            bool isPlusHovered = (isRowHovered && m_hoveredAppearanceButton == 2);
            rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, isPlusHovered ? 0.105f : 0.04f), &btnBrush);
            if (btnBrush)
            {
                rt->FillRoundedRectangle(roundedPlus, btnBrush);
                btnBrush->Release();
            }
            rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, isPlusHovered ? 0.18f : 0.075f), &btnBrush);
            if (btnBrush)
            {
                rt->DrawRoundedRectangle(roundedPlus, btnBrush, UIStyle::Metrics::ControlStroke());
                btnBrush->Release();
            }
            // Draw plus sign
            rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextNormal().d2d, &btnBrush);
            if (btnBrush)
            {
                rt->DrawLine(D2D1::Point2F(ix + 133, cy), D2D1::Point2F(ix + 141, cy), btnBrush, UIStyle::Metrics::ControlStroke());
                rt->DrawLine(D2D1::Point2F(ix + 137, cy - 4), D2D1::Point2F(ix + 137, cy + 4), btnBrush, UIStyle::Metrics::ControlStroke());
                btnBrush->Release();
            }
        }
    }
    else if (m_categoryIndex == 2) // 弹窗交互
    {
        // Draw Trigger Option Header
        if (tfDefault)
        {
            ID2D1SolidColorBrush* tb = nullptr;
            rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextMuted().d2d, &tb);
            if (tb)
            {
                std::wstring label = L"唤醒触发方式";
                rt->DrawTextW(label.c_str(), (UINT32)label.size(), tfDefault,
                    D2D1::RectF(160, 85, 510, 105), tb);
                tb->Release();
            }
        }

        // Draw Radio Buttons
        int currentTrigger = m_owner->GetTriggerType();
        std::wstring radioLabels[] = {
            L"鼠标中键 (Middle Click)",
            L"鼠标侧键 4 (MB4 / XBUTTON1)",
            L"鼠标侧键 5 (MB5 / XBUTTON2)"
        };

        for (int i = 0; i < 3; i++)
        {
            float cy = 123.0f + i * 28.0f;
            bool isSelected = (i == currentTrigger);
            bool isHovered = (i == m_hoveredTrigger);

            // Radio Circle
            D2D1_ELLIPSE outerCircle = D2D1::Ellipse(D2D1::Point2F(168.0f, cy), 8.0f, 8.0f);
            
            ID2D1SolidColorBrush* bgC = nullptr;
            float alphaC = isHovered ? 0.105f : 0.035f;
            rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, alphaC), &bgC);

            ID2D1SolidColorBrush* borderC = nullptr;
            float alphaBC = isHovered ? 0.18f : 0.065f;
            rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, alphaBC), &borderC);

            if (bgC) rt->FillEllipse(outerCircle, bgC);
            if (borderC) rt->DrawEllipse(outerCircle, borderC, UIStyle::Metrics::ControlStroke());

            if (bgC) bgC->Release();
            if (borderC) borderC->Release();

            if (isSelected)
            {
                ID2D1SolidColorBrush* accentC = nullptr;
                rt->CreateSolidColorBrush(UIStyle::ThemeColor::Accent().d2d, &accentC);
                if (accentC)
                {
                    D2D1_ELLIPSE innerCircle = D2D1::Ellipse(D2D1::Point2F(168.0f, cy), 4.0f, 4.0f);
                    rt->FillEllipse(innerCircle, accentC);
                    accentC->Release();
                }
            }

            // Radio Text
            if (tfDefault)
            {
                ID2D1SolidColorBrush* tb = nullptr;
                rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextNormal().d2d, &tb);
                if (tb)
                {
                    rt->DrawTextW(radioLabels[i].c_str(), (UINT32)radioLabels[i].size(), tfDefault,
                        D2D1::RectF(186.0f, cy - 10.0f, 450.0f, cy + 10.0f), tb);
                    tb->Release();
                }
            }
        }

        // Draw Animation Options Header
        if (tfDefault)
        {
            ID2D1SolidColorBrush* tb = nullptr;
            rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextMuted().d2d, &tb);
            if (tb)
            {
                std::wstring label = L"界面与过渡动画";
                rt->DrawTextW(label.c_str(), (UINT32)label.size(), tfDefault,
                    D2D1::RectF(160, 225, 510, 245), tb);
                tb->Release();
            }
        }

        // Draw Animation Duration Row
        {
            float ix = 160.0f;
            float iy = 255.0f;
            float cy = iy + 16.0f;
            bool isRowHovered = m_hoveredAnimationDuration;

            D2D1_RECT_F cardRect = D2D1::RectF(ix, iy, ix + 165.0f, iy + 32.0f);
            D2D1_ROUNDED_RECT roundedCard = D2D1::RoundedRect(cardRect, 6.0f, 6.0f);

            ID2D1SolidColorBrush* cardBg = nullptr;
            float alphaBg = isRowHovered ? 0.06f : 0.018f;
            rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, alphaBg), &cardBg);
            if (cardBg)
            {
                rt->FillRoundedRectangle(roundedCard, cardBg);
                cardBg->Release();
            }

            ID2D1SolidColorBrush* cardBorder = nullptr;
            float alphaBorder = isRowHovered ? 0.105f : 0.045f;
            rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, alphaBorder), &cardBorder);
            if (cardBorder)
            {
                rt->DrawRoundedRectangle(roundedCard, cardBorder, UIStyle::Metrics::ControlStroke());
                cardBorder->Release();
            }

            if (tfDefault)
            {
                ID2D1SolidColorBrush* textBrush = nullptr;
                rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextNormal().d2d, &textBrush);
                if (textBrush)
                {
                    std::wstring label = L"时长";
                    rt->DrawTextW(label.c_str(), (UINT32)label.size(), tfDefault,
                        D2D1::RectF(ix + 10, cy - 10, ix + 75, cy + 10), textBrush);
                    textBrush->Release();
                }
            }

            D2D1_ROUNDED_RECT roundedMinus = D2D1::RoundedRect(D2D1::RectF(ix + 85, cy - 8, ix + 101, cy + 8), 3.0f, 3.0f);
            bool isMinusHovered = (isRowHovered && m_hoveredAnimationDurationButton == 1);
            ID2D1SolidColorBrush* btnBrush = nullptr;
            rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, isMinusHovered ? 0.105f : 0.04f), &btnBrush);
            if (btnBrush)
            {
                rt->FillRoundedRectangle(roundedMinus, btnBrush);
                btnBrush->Release();
            }
            rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, isMinusHovered ? 0.18f : 0.075f), &btnBrush);
            if (btnBrush)
            {
                rt->DrawRoundedRectangle(roundedMinus, btnBrush, UIStyle::Metrics::ControlStroke());
                btnBrush->Release();
            }
            rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextNormal().d2d, &btnBrush);
            if (btnBrush)
            {
                rt->DrawLine(D2D1::Point2F(ix + 89, cy), D2D1::Point2F(ix + 97, cy), btnBrush, UIStyle::Metrics::ControlStroke());
                btnBrush->Release();
            }

            if (tfDefault)
            {
                ID2D1SolidColorBrush* textBrush = nullptr;
                rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextNormal().d2d, &textBrush);
                if (textBrush)
                {
                    wchar_t valBuf[32];
                    swprintf_s(valBuf, L"%dms", m_owner->GetAnimationDuration());
                    std::wstring valStr = valBuf;
                    tfDefault->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    rt->DrawTextW(valStr.c_str(), (UINT32)valStr.size(), tfDefault,
                        D2D1::RectF(ix + 101, cy - 10, ix + 129, cy + 10), textBrush);
                    tfDefault->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                    textBrush->Release();
                }
            }

            D2D1_ROUNDED_RECT roundedPlus = D2D1::RoundedRect(D2D1::RectF(ix + 129, cy - 8, ix + 145, cy + 8), 3.0f, 3.0f);
            bool isPlusHovered = (isRowHovered && m_hoveredAnimationDurationButton == 2);
            rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, isPlusHovered ? 0.105f : 0.04f), &btnBrush);
            if (btnBrush)
            {
                rt->FillRoundedRectangle(roundedPlus, btnBrush);
                btnBrush->Release();
            }
            rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, isPlusHovered ? 0.18f : 0.075f), &btnBrush);
            if (btnBrush)
            {
                rt->DrawRoundedRectangle(roundedPlus, btnBrush, UIStyle::Metrics::ControlStroke());
                btnBrush->Release();
            }
            rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextNormal().d2d, &btnBrush);
            if (btnBrush)
            {
                rt->DrawLine(D2D1::Point2F(ix + 133, cy), D2D1::Point2F(ix + 141, cy), btnBrush, UIStyle::Metrics::ControlStroke());
                rt->DrawLine(D2D1::Point2F(ix + 137, cy - 4), D2D1::Point2F(ix + 137, cy + 4), btnBrush, UIStyle::Metrics::ControlStroke());
                btnBrush->Release();
            }
        }
    }
    else if (m_categoryIndex == 3) // 配置管理
    {
        if (tfDefault)
        {
            const D2D1_RECT_F pathCardRect = D2D1::RectF(160.0f, 86.0f, 500.0f, 166.0f);
            const D2D1_RECT_F dirLabelRect = D2D1::RectF(180.0f, 98.0f, 480.0f, 118.0f);
            const D2D1_RECT_F dirValueRect = D2D1::RectF(180.0f, 120.0f, 480.0f, 156.0f);
            const D2D1_RECT_F openConfigFileRect = D2D1::RectF(160.0f, 178.0f, 325.0f, 214.0f);
            const D2D1_RECT_F openLogFileRect = D2D1::RectF(335.0f, 178.0f, 500.0f, 214.0f);
            const D2D1_RECT_F importJsonRect = D2D1::RectF(160.0f, 226.0f, 500.0f, 262.0f);

            ID2D1SolidColorBrush* tbNormal = nullptr;
            rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextNormal().d2d, &tbNormal);
            ID2D1SolidColorBrush* tbMuted = nullptr;
            rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextMuted().d2d, &tbMuted);
            ID2D1SolidColorBrush* cardBg = nullptr;
            rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, 0.026f), &cardBg);
            ID2D1SolidColorBrush* cardBorder = nullptr;
            rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, 0.065f), &cardBorder);

            D2D1_ROUNDED_RECT pathCard = D2D1::RoundedRect(pathCardRect, 6.0f, 6.0f);
            if (cardBg) rt->FillRoundedRectangle(pathCard, cardBg);
            if (cardBorder) rt->DrawRoundedRectangle(pathCard, cardBorder, UIStyle::Metrics::ControlStroke());

            if (tbNormal && tbMuted)
            {
                DWRITE_WORD_WRAPPING oldWrapping = tfDefault->GetWordWrapping();
                DWRITE_TEXT_ALIGNMENT oldAlignment = tfDefault->GetTextAlignment();
                tfDefault->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
                tfDefault->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

                std::wstring dirLabel = L"配置目录";
                rt->DrawTextW(dirLabel.c_str(), (UINT32)dirLabel.size(), tfDefault, dirLabelRect, tbMuted);

                std::wstring configDir = m_owner->GetConfigDir();
                ID2D1SolidColorBrush* textBrush = tbNormal;
                if (m_hoveredConfigDirText)
                {
                    rt->CreateSolidColorBrush(UIStyle::ThemeColor::Accent().d2d, &textBrush);
                }
                rt->DrawTextW(configDir.c_str(), (UINT32)configDir.size(), tfDefault, dirValueRect, textBrush);
                if (m_hoveredConfigDirText && textBrush)
                {
                    textBrush->Release();
                }

                tfDefault->SetWordWrapping(oldWrapping);
                tfDefault->SetTextAlignment(oldAlignment);
            }

            // 1. Draw "打开配置文件" Button
            {
                D2D1_ROUNDED_RECT btnRect = D2D1::RoundedRect(openConfigFileRect, 6.0f, 6.0f);
                ID2D1SolidColorBrush* btnBg = nullptr;
                D2D1_COLOR_F btnClr = m_hoveredOpenConfigFile ? UIStyle::ThemeColor::Accent().d2d : baseClr;
                float btnAlpha = m_hoveredOpenConfigFile ? 0.12f : 0.035f;
                rt->CreateSolidColorBrush(D2D1::ColorF(btnClr.r, btnClr.g, btnClr.b, btnAlpha), &btnBg);
                if (btnBg)
                {
                    rt->FillRoundedRectangle(btnRect, btnBg);
                    btnBg->Release();
                }

                ID2D1SolidColorBrush* btnBorder = nullptr;
                D2D1_COLOR_F borderClr = m_hoveredOpenConfigFile ? UIStyle::ThemeColor::Accent().d2d : baseClr;
                float borderAlpha = m_hoveredOpenConfigFile ? 0.26f : 0.07f;
                rt->CreateSolidColorBrush(D2D1::ColorF(borderClr.r, borderClr.g, borderClr.b, borderAlpha), &btnBorder);
                if (btnBorder)
                {
                    rt->DrawRoundedRectangle(btnRect, btnBorder, UIStyle::Metrics::ControlStroke());
                    btnBorder->Release();
                }

                if (tbNormal)
                {
                    std::wstring btnText = L"打开配置文件";
                    DWRITE_TEXT_ALIGNMENT oldAlignment = tfDefault->GetTextAlignment();
                    tfDefault->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    rt->DrawTextW(btnText.c_str(), (UINT32)btnText.size(), tfDefault, openConfigFileRect, tbNormal);
                    tfDefault->SetTextAlignment(oldAlignment);
                }
            }

            // 2. Draw "打开日志文件" Button
            {
                D2D1_ROUNDED_RECT btnRect = D2D1::RoundedRect(openLogFileRect, 6.0f, 6.0f);
                ID2D1SolidColorBrush* btnBg = nullptr;
                D2D1_COLOR_F btnClr = m_hoveredOpenLogFile ? UIStyle::ThemeColor::Accent().d2d : baseClr;
                float btnAlpha = m_hoveredOpenLogFile ? 0.12f : 0.035f;
                rt->CreateSolidColorBrush(D2D1::ColorF(btnClr.r, btnClr.g, btnClr.b, btnAlpha), &btnBg);
                if (btnBg)
                {
                    rt->FillRoundedRectangle(btnRect, btnBg);
                    btnBg->Release();
                }

                ID2D1SolidColorBrush* btnBorder = nullptr;
                D2D1_COLOR_F borderClr = m_hoveredOpenLogFile ? UIStyle::ThemeColor::Accent().d2d : baseClr;
                float borderAlpha = m_hoveredOpenLogFile ? 0.26f : 0.07f;
                rt->CreateSolidColorBrush(D2D1::ColorF(borderClr.r, borderClr.g, borderClr.b, borderAlpha), &btnBorder);
                if (btnBorder)
                {
                    rt->DrawRoundedRectangle(btnRect, btnBorder, UIStyle::Metrics::ControlStroke());
                    btnBorder->Release();
                }

                if (tbNormal)
                {
                    std::wstring btnText = L"打开日志文件";
                    DWRITE_TEXT_ALIGNMENT oldAlignment = tfDefault->GetTextAlignment();
                    tfDefault->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    rt->DrawTextW(btnText.c_str(), (UINT32)btnText.size(), tfDefault, openLogFileRect, tbNormal);
                    tfDefault->SetTextAlignment(oldAlignment);
                }
            }

            // 3. Draw "导入QuickLauncher配置" Button
            {
                D2D1_ROUNDED_RECT btnRect = D2D1::RoundedRect(importJsonRect, 6.0f, 6.0f);
                ID2D1SolidColorBrush* btnBg = nullptr;
                D2D1_COLOR_F btnClr = m_hoveredImportJson ? UIStyle::ThemeColor::Accent().d2d : baseClr;
                float btnAlpha = m_hoveredImportJson ? 0.12f : 0.035f;
                rt->CreateSolidColorBrush(D2D1::ColorF(btnClr.r, btnClr.g, btnClr.b, btnAlpha), &btnBg);
                if (btnBg)
                {
                    rt->FillRoundedRectangle(btnRect, btnBg);
                    btnBg->Release();
                }

                ID2D1SolidColorBrush* btnBorder = nullptr;
                D2D1_COLOR_F borderClr = m_hoveredImportJson ? UIStyle::ThemeColor::Accent().d2d : baseClr;
                float borderAlpha = m_hoveredImportJson ? 0.26f : 0.07f;
                rt->CreateSolidColorBrush(D2D1::ColorF(borderClr.r, borderClr.g, borderClr.b, borderAlpha), &btnBorder);
                if (btnBorder)
                {
                    rt->DrawRoundedRectangle(btnRect, btnBorder, UIStyle::Metrics::ControlStroke());
                    btnBorder->Release();
                }

                if (tbNormal)
                {
                    std::wstring btnText = L"\u5BFC\u5165 QuickLauncher \u914D\u7F6E";
                    DWRITE_TEXT_ALIGNMENT oldAlignment = tfDefault->GetTextAlignment();
                    tfDefault->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    rt->DrawTextW(btnText.c_str(), (UINT32)btnText.size(), tfDefault, importJsonRect, tbNormal);
                    tfDefault->SetTextAlignment(oldAlignment);
                }
            }

            if (tbNormal) tbNormal->Release();
            if (tbMuted) tbMuted->Release();
            if (cardBg) cardBg->Release();
            if (cardBorder) cardBorder->Release();
        }
    }
    else if (m_categoryIndex == 4) // 关于软件
    {
        if (tfDefault)
        {
            ID2D1SolidColorBrush* tbNormal = nullptr;
            rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextNormal().d2d, &tbNormal);
            ID2D1SolidColorBrush* tbMuted = nullptr;
            rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextMuted().d2d, &tbMuted);

            if (tbNormal && tbMuted)
            {
                // App Name & Version
                rt->DrawTextW(L"WinLauncher", 11, tfTitle, D2D1::RectF(160, 90, 510, 115), tbNormal);
                std::wstring verText = std::wstring(L"版本: v") + WINLAUNCHER_VERSION_WSTR;
                rt->DrawTextW(verText.c_str(), (UINT32)verText.size(), tfDefault, D2D1::RectF(160, 125, 510, 145), tbMuted);

                // Description
                std::wstring desc = L"一个极简、快速、带毛玻璃特效的快捷方式启动工具。\n可以通过鼠标中键或侧键快速唤醒，方便管理并运行常用程序。";
                rt->DrawTextW(desc.c_str(), (UINT32)desc.size(), tfDefault, D2D1::RectF(160, 160, 500, 235), tbNormal);
            }

            if (tbNormal) tbNormal->Release();
            if (tbMuted) tbMuted->Release();
        }
    }
}

void SettingsPage::OnMouseMove(POINT pt, bool& repaint)
{
    if (m_categoryIndex == 0)
    {
        bool has = HitTestAutoStart(pt);
        if (has != m_hoveredAutoStart)
        {
            m_hoveredAutoStart = has;
            repaint = true;
        }

        bool hht = HitTestHideTrayIcon(pt);
        if (hht != m_hoveredHideTrayIcon)
        {
            m_hoveredHideTrayIcon = hht;
            repaint = true;
        }

        int htheme = HitTestTheme(pt);
        if (htheme != m_hoveredTheme)
        {
            m_hoveredTheme = htheme;
            repaint = true;
        }

        bool hat = HitTestAnimationToggle(pt);
        if (hat != m_hoveredAnimationToggle)
        {
            m_hoveredAnimationToggle = hat;
            repaint = true;
        }

        bool hhw = HitTestHardwareAcceleration(pt);
        if (hhw != m_hoveredHardwareAcceleration)
        {
            m_hoveredHardwareAcceleration = hhw;
            repaint = true;
        }

        bool hgs = HitTestGlobalScaleSlider(pt);
        if (m_draggingGlobalScaleSlider)
        {
            int nextScale = GlobalScaleFromPoint(pt);
            if (nextScale != m_pendingGlobalScalePercent)
            {
                m_pendingGlobalScalePercent = nextScale;
                repaint = true;
            }
            hgs = true;
        }
        if (hgs != m_hoveredGlobalScaleSlider)
        {
            m_hoveredGlobalScaleSlider = hgs;
            repaint = true;
        }

        bool hga = HitTestGlobalScaleApply(pt);
        if (hga != m_hoveredGlobalScaleApply)
        {
            m_hoveredGlobalScaleApply = hga;
            repaint = true;
        }

        int hcolor = HitTestThemeColor(pt);
        if (hcolor != m_hoveredThemeColor)
        {
            m_hoveredThemeColor = hcolor;
            repaint = true;
        }

        int hwmode = HitTestWindowMode(pt);
        if (hwmode != m_hoveredWindowMode)
        {
            m_hoveredWindowMode = hwmode;
            repaint = true;
        }

        int settingIdx = -1;
        int buttonType = 0;
        bool hit = HitTestThemeDetails(pt, settingIdx, buttonType);
        if (hit)
        {
            if (settingIdx != m_hoveredThemeDetailSetting || buttonType != m_hoveredThemeDetailButton)
            {
                m_hoveredThemeDetailSetting = settingIdx;
                m_hoveredThemeDetailButton = buttonType;
                repaint = true;
            }
        }
        else
        {
            if (m_hoveredThemeDetailSetting != -1 || m_hoveredThemeDetailButton != 0)
            {
                m_hoveredThemeDetailSetting = -1;
                m_hoveredThemeDetailButton = 0;
                repaint = true;
            }
        }
    }
    else if (m_categoryIndex == 1)
    {
        int settingIdx = -1;
        int buttonType = 0;
        bool hit = HitTestAppearance(pt, settingIdx, buttonType);
        if (hit)
        {
            if (settingIdx != m_hoveredAppearanceSetting || buttonType != m_hoveredAppearanceButton)
            {
                m_hoveredAppearanceSetting = settingIdx;
                m_hoveredAppearanceButton = buttonType;
                repaint = true;
            }
        }
        else
        {
            if (m_hoveredAppearanceSetting != -1 || m_hoveredAppearanceButton != 0)
            {
                m_hoveredAppearanceSetting = -1;
                m_hoveredAppearanceButton = 0;
                repaint = true;
            }
        }
    }
    else if (m_categoryIndex == 2)
    {
        int htrig = HitTestTrigger(pt);
        if (htrig != m_hoveredTrigger)
        {
            m_hoveredTrigger = htrig;
            repaint = true;
        }

        bool hat = HitTestAnimationToggle(pt);
        if (hat != m_hoveredAnimationToggle)
        {
            m_hoveredAnimationToggle = hat;
            repaint = true;
        }

        int buttonType = 0;
        bool htd = HitTestAnimationDuration(pt, buttonType);
        if (htd != m_hoveredAnimationDuration || buttonType != m_hoveredAnimationDurationButton)
        {
            m_hoveredAnimationDuration = htd;
            m_hoveredAnimationDurationButton = buttonType;
            repaint = true;
        }
    }
    else if (m_categoryIndex == 3)
    {
        bool hConfigFile = HitTestOpenConfigFile(pt);
        bool hLogFile = HitTestOpenLogFile(pt);
        bool hConfigDirText = HitTestConfigDirText(pt);
        bool hImportJson = HitTestImportJson(pt);
        if (hConfigFile != m_hoveredOpenConfigFile || hLogFile != m_hoveredOpenLogFile || hConfigDirText != m_hoveredConfigDirText || hImportJson != m_hoveredImportJson)
        {
            m_hoveredOpenConfigFile = hConfigFile;
            m_hoveredOpenLogFile = hLogFile;
            m_hoveredConfigDirText = hConfigDirText;
            m_hoveredImportJson = hImportJson;
            repaint = true;
        }
    }
}

void SettingsPage::OnMouseLeave(bool& repaint)
{
    if (m_hoveredAutoStart || m_hoveredHideTrayIcon || m_hoveredOpenConfigFile || m_hoveredOpenLogFile || m_hoveredConfigDirText || m_hoveredImportJson || m_hoveredTrigger != -1 || m_hoveredTheme != -1 || m_hoveredThemeColor != -1 || m_hoveredWindowMode != -1 || m_hoveredAppearanceSetting != -1 || m_hoveredAppearanceButton != 0 || m_hoveredThemeDetailSetting != -1 || m_hoveredThemeDetailButton != 0 || m_hoveredAnimationToggle || m_hoveredHardwareAcceleration || m_hoveredAnimationDuration || m_hoveredAnimationDurationButton != 0 || m_hoveredGlobalScaleSlider || m_hoveredGlobalScaleApply || m_draggingGlobalScaleSlider)
    {
        m_hoveredAutoStart = false;
        m_hoveredHideTrayIcon = false;
        m_hoveredOpenConfigFile = false;
        m_hoveredOpenLogFile = false;
        m_hoveredConfigDirText = false;
        m_hoveredImportJson = false;
        m_hoveredTrigger = -1;
        m_hoveredTheme = -1;
        m_hoveredThemeColor = -1;
        m_hoveredWindowMode = -1;
        m_hoveredAppearanceSetting = -1;
        m_hoveredAppearanceButton = 0;
        m_hoveredThemeDetailSetting = -1;
        m_hoveredThemeDetailButton = 0;
        m_hoveredAnimationToggle = false;
        m_hoveredHardwareAcceleration = false;
        m_hoveredAnimationDuration = false;
        m_hoveredAnimationDurationButton = 0;
        m_hoveredGlobalScaleSlider = false;
        m_hoveredGlobalScaleApply = false;
        m_draggingGlobalScaleSlider = false;
        repaint = true;
    }
}

void SettingsPage::OnLButtonDown(POINT pt, bool& repaint)
{
    if (m_categoryIndex == 0)
    {
        if (HitTestAutoStart(pt))
        {
            bool current = m_owner->GetAutoStart();
            m_owner->SetAutoStart(!current);
            m_owner->NotifyConfigChanged();
            repaint = true;
        }
        else if (HitTestHideTrayIcon(pt))
        {
            bool current = m_owner->GetHideTrayIcon();
            m_owner->SetHideTrayIcon(!current);
            m_owner->NotifyConfigChanged();
            repaint = true;
        }
        else if (HitTestAnimationToggle(pt))
        {
            bool current = m_owner->GetAnimationEnabled();
            m_owner->SetAnimationEnabled(!current);
            repaint = true;
        }
        else if (HitTestHardwareAcceleration(pt))
        {
            bool current = m_owner->GetHardwareAccelerationEnabled();
            m_owner->SetHardwareAccelerationEnabled(!current);
            m_owner->NotifyConfigChanged();
            repaint = true;
        }
        else if (HitTestGlobalScaleSlider(pt))
        {
            m_draggingGlobalScaleSlider = true;
            m_pendingGlobalScalePercent = GlobalScaleFromPoint(pt);
            if (HWND hwnd = m_owner->GetWindowHWND())
                SetCapture(hwnd);
            repaint = true;
        }
        else if (HitTestGlobalScaleApply(pt))
        {
            int pendingScale = PendingGlobalScalePercent();
            if (pendingScale != m_owner->GetGlobalScalePercent())
            {
                m_owner->SetGlobalScalePercent(pendingScale);
                m_pendingGlobalScalePercent = pendingScale;
            }
            repaint = true;
        }
        else
        {
            int htheme = HitTestTheme(pt);
            if (htheme >= 0 && htheme <= 1)
            {
                m_owner->SetTheme(htheme, pt);
                repaint = true;
            }
            else
            {
                int hcolor = HitTestThemeColor(pt);
                if (hcolor >= 0 && hcolor < UIStyle::ThemeColorPresetCount())
                {
                    m_owner->SetThemeColor(hcolor, pt);
                    repaint = true;
                }
                else
                {
                    int hwmode = HitTestWindowMode(pt);
                    if (hwmode >= 0 && hwmode <= 1)
                    {
                        m_owner->SetWindowMode(hwmode, pt);
                        repaint = true;
                    }
                    else
                    {
                        int settingIdx = -1;
                        int buttonType = 0;
                        if (HitTestThemeDetails(pt, settingIdx, buttonType))
                        {
                            if (buttonType == 1 || buttonType == 2)
                            {
                                int step = (buttonType == 2) ? 1 : -1;
                                int currentTheme = m_owner->GetTheme();
                                int currentWindowMode = m_owner->GetWindowMode();
                                auto& cfg = (currentWindowMode == 1) ?
                                    ((currentTheme == 1) ? UIStyle::g_AcrylicLightConfig : UIStyle::g_AcrylicDarkConfig) :
                                    ((currentTheme == 1) ? UIStyle::g_LightConfig : UIStyle::g_DarkConfig);

                                if (settingIdx == 0) // Hue
                                {
                                    float val = cfg.hue + step * 5.0f;
                                    if (val < 0.0f) val += 360.0f;
                                    if (val >= 360.0f) val -= 360.0f;
                                    cfg.hue = val;
                                }
                                else if (settingIdx == 1) // Blur
                                {
                                    float val = cfg.blur + step;
                                    if (val >= 0.0f && val <= 30.0f) cfg.blur = val;
                                }
                                else if (settingIdx == 2) // Opacity
                                {
                                     float val = cfg.opacity - step * 0.05f;
                                     if (val < 0.0f) val = 0.0f;
                                     if (val > 1.0f) val = 1.0f;
                                     cfg.opacity = val;
                                }
                                else if (settingIdx == 3) // Highlight
                                {
                                    float val = cfg.highlight + step * 0.05f;
                                    if (val >= 0.0f && val <= 1.0f) cfg.highlight = val;
                                }
                                else if (settingIdx == 4) // Brightness
                                {
                                    float val = cfg.brightness + step * 0.05f;
                                    if (val < 0.0f) val = 0.0f;
                                    if (val > 1.0f) val = 1.0f;
                                    cfg.brightness = val;
                                }
                                else if (settingIdx == 5) // Saturation
                                {
                                    float val = cfg.saturation + step * 0.1f;
                                    if (val >= 0.5f && val <= 3.0f) cfg.saturation = val;
                                }

                                m_owner->NotifyConfigChanged(true);
                                repaint = true;
                            }
                        }
                    }
                }
            }
        }
    }
    else if (m_categoryIndex == 1)
    {
        int settingIdx = -1;
        int buttonType = 0;
        if (HitTestAppearance(pt, settingIdx, buttonType))
        {
            if (buttonType == 1 || buttonType == 2)
            {
                int step = (buttonType == 2) ? 1 : -1;
                
                if (settingIdx == 0) // Columns
                {
                    int val = m_owner->GetPopupColumns() + step;
                    if (val >= 1 && val <= 20) m_owner->SetPopupColumns(val);
                }
                else if (settingIdx == 1) // Rows
                {
                    int val = m_owner->GetPopupRows() + step;
                    if (val >= 1 && val <= 20) m_owner->SetPopupRows(val);
                }
                else if (settingIdx == 2) // Icon Size
                {
                    int val = m_owner->GetPopupIconSize() + step * 2;
                    if (val >= 16 && val <= 64) m_owner->SetPopupIconSize(val);
                }
                else if (settingIdx == 3) // Icon Gap
                {
                    int val = m_owner->GetPopupIconGap() + step;
                    if (val >= 0 && val <= 30) m_owner->SetPopupIconGap(val);
                }
                else if (settingIdx == 4) // Icon Radius
                {
                    int val = m_owner->GetPopupIconRadius() + step;
                    if (val >= 0 && val <= 30) m_owner->SetPopupIconRadius(val);
                }
                else if (settingIdx == 5) // Window Padding
                {
                    int val = m_owner->GetPopupWndPadding() + step;
                    if (val >= 0 && val <= 50) m_owner->SetPopupWndPadding(val);
                }
                else if (settingIdx == 6) // DOCK Rows
                {
                    int val = m_owner->GetDockHeight() + step;
                    if (val >= 1 && val <= 5) m_owner->SetDockHeight(val);
                }
                
                m_owner->NotifyConfigChanged();
                repaint = true;
            }
        }
    }
    else if (m_categoryIndex == 2)
    {
        int htrig = HitTestTrigger(pt);
        if (htrig >= 0 && htrig <= 2)
        {
            m_owner->SetTriggerType(htrig);
            m_owner->NotifyConfigChanged();
            repaint = true;
        }
        else if (HitTestAnimationToggle(pt))
        {
            bool current = m_owner->GetAnimationEnabled();
            m_owner->SetAnimationEnabled(!current);
            repaint = true;
        }
        else if (m_hoveredAnimationDuration)
        {
            if (m_hoveredAnimationDurationButton == 1) // minus
            {
                int current = m_owner->GetAnimationDuration();
                if (current > 50) m_owner->SetAnimationDuration(current - 50);
                repaint = true;
            }
            else if (m_hoveredAnimationDurationButton == 2) // plus
            {
                int current = m_owner->GetAnimationDuration();
                if (current < 1000) m_owner->SetAnimationDuration(current + 50);
                repaint = true;
            }
        }
    }
    else if (m_categoryIndex == 3)
    {
        if (HitTestOpenConfigFile(pt))
        {
            m_owner->OpenConfigFile();
            repaint = true;
        }
        else if (HitTestOpenLogFile(pt))
        {
            m_owner->OpenLogFile();
            repaint = true;
        }
        else if (HitTestConfigDirText(pt))
        {
            m_owner->OpenConfigDir();
            repaint = true;
        }
        else if (HitTestImportJson(pt))
        {
            if (OnImportJsonClicked)
                OnImportJsonClicked();
            repaint = true;
        }
    }
}

void SettingsPage::OnLButtonUp(POINT pt, bool& repaint)
{
    if (m_draggingGlobalScaleSlider)
    {
        m_draggingGlobalScaleSlider = false;
        m_pendingGlobalScalePercent = GlobalScaleFromPoint(pt);
        ReleaseCapture();
        repaint = true;
    }
}

void SettingsPage::OnLButtonDblClk(POINT pt, bool& repaint)
{
    OnLButtonDown(pt, repaint);
}

bool SettingsPage::HitTestAppearance(POINT pt, int& settingIdx, int& buttonType)
{
    if (m_categoryIndex != 1) return false;
    for (int i = 0; i < 7; i++)
    {
        int col = i % 2;
        int row = i / 2;
        float ix = 160.0f + col * 175.0f;
        float iy = 90.0f + row * 42.0f;
        float cy = iy + 16.0f;

        if (pt.x >= ix && pt.x <= ix + 165.0f && pt.y >= iy && pt.y <= iy + 32.0f)
        {
            settingIdx = i;
            if (pt.x >= ix + 83 && pt.x <= ix + 103 && pt.y >= cy - 10 && pt.y <= cy + 10)
            {
                buttonType = 1; // minus
            }
            else if (pt.x >= ix + 127 && pt.x <= ix + 147 && pt.y >= cy - 10 && pt.y <= cy + 10)
            {
                buttonType = 2; // plus
            }
            else
            {
                buttonType = 0; // card body
            }
            return true;
        }
    }
    return false;
}

bool SettingsPage::HitTestAutoStart(POINT pt)
{
    if (m_categoryIndex != 0) return false;
    return (pt.x >= 160 && pt.x <= 240 && pt.y >= 80 && pt.y <= 110);
}

bool SettingsPage::HitTestHideTrayIcon(POINT pt)
{
    if (m_categoryIndex != 0) return false;
    return (pt.x >= 245 && pt.x <= 325 && pt.y >= 80 && pt.y <= 110);
}

bool SettingsPage::HitTestHardwareAcceleration(POINT pt)
{
    if (m_categoryIndex != 0) return false;
    return (pt.x >= 330 && pt.x <= 410 && pt.y >= 80 && pt.y <= 110);
}

int SettingsPage::HitTestTrigger(POINT pt)
{
    if (m_categoryIndex != 2) return -1;
    for (int i = 0; i < 3; i++)
    {
        float cy = 123.0f + i * 28.0f;
        if (pt.x >= 160 && pt.x <= 400 && pt.y >= cy - 12.0f && pt.y <= cy + 12.0f)
        {
            return i;
        }
    }
    return -1;
}

int SettingsPage::HitTestTheme(POINT pt)
{
    if (m_categoryIndex != 0) return -1;
    if (pt.y >= 180.0f && pt.y <= 212.0f)
    {
        if (pt.x >= 160.0f && pt.x <= 325.0f) return 0; // Dark
        if (pt.x >= 345.0f && pt.x <= 510.0f) return 1; // Light
    }
    return -1;
}

int SettingsPage::HitTestThemeColor(POINT pt)
{
    if (m_categoryIndex != 0) return -1;
    if (pt.y < 241.0f || pt.y > 265.0f) return -1;

    const float swatchLeft = 160.0f;
    const float swatchRight = 510.0f;
    const float swatchSize = 18.0f;
    const float swatchStep = (swatchRight - swatchLeft - swatchSize) / (float)(UIStyle::ThemeColorPresetCount() - 1);

    for (int i = 0; i < UIStyle::ThemeColorPresetCount(); i++)
    {
        float x = swatchLeft + i * swatchStep;
        if (pt.x >= x - 3.0f && pt.x <= x + swatchSize + 3.0f)
        {
            return i;
        }
    }

    return -1;
}

int SettingsPage::HitTestWindowMode(POINT pt)
{
    if (m_categoryIndex != 0) return -1;
    if (pt.y >= 298.0f && pt.y <= 326.0f)
    {
        if (pt.x >= 160.0f && pt.x <= 325.0f) return 0; // Glass
        if (pt.x >= 345.0f && pt.x <= 510.0f) return 1; // Acrylic
    }
    return -1;
}

bool SettingsPage::HitTestOpenConfigFile(POINT pt)
{
    if (m_categoryIndex != 3) return false;
    return (pt.x >= 160 && pt.x <= 325 && pt.y >= 178 && pt.y <= 214);
}

bool SettingsPage::HitTestOpenLogFile(POINT pt)
{
    if (m_categoryIndex != 3) return false;
    return (pt.x >= 335 && pt.x <= 500 && pt.y >= 178 && pt.y <= 214);
}

bool SettingsPage::HitTestConfigDirText(POINT pt)
{
    if (m_categoryIndex != 3) return false;
    return (pt.x >= 180 && pt.x <= 480 && pt.y >= 120 && pt.y <= 156);
}

bool SettingsPage::HitTestImportJson(POINT pt)
{
    if (m_categoryIndex != 3) return false;
    return (pt.x >= 160 && pt.x <= 500 && pt.y >= 226 && pt.y <= 262);
}

bool SettingsPage::HitTestThemeDetails(POINT pt, int& settingIdx, int& buttonType)
{
    if (m_categoryIndex != 0) return false;
    int currentWindowMode = m_owner->GetWindowMode();
    if (currentWindowMode != 0 && currentWindowMode != 1) return false;

    std::vector<int> activeIndices = { 1, 2, 3, 4, 5 };
    for (int i = 0; i < (int)activeIndices.size(); i++)
    {
        int col = i % 2;
        int row = i / 2;
        float ix = 160.0f + col * 175.0f;
        float iy = 360.0f + row * 38.0f;
        float cy = iy + 16.0f;

        if (pt.x >= ix && pt.x <= ix + 165.0f && pt.y >= iy && pt.y <= iy + 32.0f)
        {
            settingIdx = activeIndices[i];
            if (pt.x >= ix + 83 && pt.x <= ix + 103 && pt.y >= cy - 10 && pt.y <= cy + 10)
            {
                buttonType = 1; // minus
            }
            else if (pt.x >= ix + 127 && pt.x <= ix + 147 && pt.y >= cy - 10 && pt.y <= cy + 10)
            {
                buttonType = 2; // plus
            }
            else
            {
                buttonType = 0; // card body
            }
            return true;
        }
    }
    return false;
}

bool SettingsPage::HitTestAnimationToggle(POINT pt)
{
    if (m_categoryIndex != 0) return false;
    return (pt.x >= 415 && pt.x <= 505 && pt.y >= 80 && pt.y <= 110);
}

bool SettingsPage::HitTestAnimationDuration(POINT pt, int& buttonType)
{
    if (m_categoryIndex != 2) return false;
    float ix = 160.0f;
    float iy = 255.0f;
    float cy = iy + 16.0f;

    if (pt.x >= ix && pt.x <= ix + 165.0f && pt.y >= iy && pt.y <= iy + 32.0f)
    {
        if (pt.x >= ix + 83 && pt.x <= ix + 103 && pt.y >= cy - 10 && pt.y <= cy + 10)
        {
            buttonType = 1; // minus
        }
        else if (pt.x >= ix + 127 && pt.x <= ix + 147 && pt.y >= cy - 10 && pt.y <= cy + 10)
        {
            buttonType = 2; // plus
        }
        else
        {
            buttonType = 0; // body
        }
        return true;
    }
    return false;
}

bool SettingsPage::HitTestGlobalScaleSlider(POINT pt)
{
    if (m_categoryIndex != 0) return false;
    return pt.x >= (int)(GLOBAL_SCALE_TRACK_LEFT - 8.0f) &&
        pt.x <= (int)(GLOBAL_SCALE_TRACK_RIGHT + 8.0f) &&
        pt.y >= (int)(GLOBAL_SCALE_TRACK_Y - 12.0f) &&
        pt.y <= (int)(GLOBAL_SCALE_TRACK_Y + 12.0f);
}

bool SettingsPage::HitTestGlobalScaleApply(POINT pt)
{
    if (m_categoryIndex != 0) return false;
    return pt.x >= (int)GLOBAL_SCALE_APPLY_LEFT &&
        pt.x <= (int)GLOBAL_SCALE_APPLY_RIGHT &&
        pt.y >= (int)GLOBAL_SCALE_APPLY_TOP &&
        pt.y <= (int)GLOBAL_SCALE_APPLY_BOTTOM;
}
