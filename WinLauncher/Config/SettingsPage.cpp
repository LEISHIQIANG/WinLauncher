#include "SettingsPage.h"
#include "SettingsControlKit.h"
#include "../UI/MouseCaptureController.h"
#include "SettingsTabHelper.h"
#include "IConfigWindow.h"
#include "UIStyle.h"
#include "ConfirmWindow.h"
#include "PromptWindow.h"
#include "DropDownMenu.h"
#include "../DpiHelper.h"
#include "../App/PluginManager.h"
#include "../Services/ConfigPath.h"
#include "../Services/UpdateService.h"
#include "..\version.h"
#include <commdlg.h>
#include <cwchar>
#include <cwctype>
#include <cmath>
#include <vector>
#include <algorithm>
#include <shellapi.h>

#include "SettingsPageLayout.h"
#include "SettingsPresetMenus.h"
#include "SettingsPluginView.h"
#include "SettingsPluginActions.h"

using namespace SettingsPageLayout;

SettingsPage::SettingsPage(IConfigWindow* owner)
    : m_owner(owner)
{
}

SettingsPage::~SettingsPage()
{
    MouseCaptureController::ReleaseForContext(this, L"settings_page_destroyed");
}

void SettingsPage::CancelPointerInteractionThunk(void* context)
{
    if (context)
        static_cast<SettingsPage*>(context)->CancelPointerInteraction();
}

void SettingsPage::CancelPointerInteraction()
{
    const bool changed = m_draggingAnimationDurationSlider || m_draggingGlobalScaleSlider;
    m_draggingAnimationDurationSlider = false;
    m_draggingGlobalScaleSlider = false;
    if (changed && m_owner)
    {
        HWND hwnd = m_owner->GetWindowHWND();
        if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
    }
}

static bool SameRectLocal(const D2D1_RECT_F& a, const D2D1_RECT_F& b)
{
    return fabsf(a.left - b.left) < 0.1f &&
        fabsf(a.top - b.top) < 0.1f &&
        fabsf(a.right - b.right) < 0.1f &&
        fabsf(a.bottom - b.bottom) < 0.1f;
}

D2D1_RECT_F SettingsPage::GetSelectionRect(SelectionVisual& visual, const D2D1_RECT_F& target)
{
    if (!visual.initialized || !UIStyle::Animation::IsEnabled())
    {
        visual.initialized = true;
        visual.moving = false;
        visual.current = target;
        visual.target = target;
        return visual.current;
    }

    if (!SameRectLocal(visual.target, target))
    {
        visual.target = target;
        visual.moving = true;
        m_selectionAnimating = true;
        if (m_owner)
            m_owner->StartAnimation();
    }

    return visual.current;
}



void SettingsPage::DrawSelectionHighlight(ID2D1HwndRenderTarget* rt, const D2D1_RECT_F& rect, float radius, float bgAlpha, float borderAlpha)
{
    D2D1_ROUNDED_RECT rounded = D2D1::RoundedRect(rect, radius, radius);

    ID2D1SolidColorBrush* bgBrush = nullptr;
    D2D1_COLOR_F bgClr = UIStyle::ThemeColor::Accent().d2d;
    bgClr.a = bgAlpha;
    rt->CreateSolidColorBrush(bgClr, &bgBrush);
    if (bgBrush)
    {
        rt->FillRoundedRectangle(rounded, bgBrush);
        bgBrush->Release();
    }

    ID2D1SolidColorBrush* borderBrush = nullptr;
    D2D1_COLOR_F borderClr = UIStyle::ThemeColor::Accent().d2d;
    borderClr.a = borderAlpha;
    rt->CreateSolidColorBrush(borderClr, &borderBrush);
    if (borderBrush)
    {
        rt->DrawRoundedRectangle(rounded, borderBrush, UIStyle::Metrics::ControlStroke());
        borderBrush->Release();
    }
}

void SettingsPage::UpdateAnimation(float dt, bool& repaint)
{
    if (!UIStyle::Animation::IsEnabled())
    {
        m_selectionAnimating = false;
        return;
    }

    bool stillMoving = false;
    auto updateVisual = [&](SelectionVisual& visual)
    {
        if (!visual.initialized || !visual.moving)
            return;

        float t = 1.0f - std::exp(-20.0f * dt);
        visual.current.left += (visual.target.left - visual.current.left) * t;
        visual.current.top += (visual.target.top - visual.current.top) * t;
        visual.current.right += (visual.target.right - visual.current.right) * t;
        visual.current.bottom += (visual.target.bottom - visual.current.bottom) * t;

        if (SameRectLocal(visual.current, visual.target))
        {
            visual.current = visual.target;
            visual.moving = false;
        }
        else
        {
            stillMoving = true;
        }
    };

    updateVisual(m_themeSelection);
    updateVisual(m_themeColorSelection);
    updateVisual(m_windowModeSelection);
    updateVisual(m_triggerSelection);
    updateVisual(m_popupAlignSelection);
    updateVisual(m_popupAutoCloseSelection);
    updateVisual(m_popupMultiOpenSelection);
    updateVisual(m_sortModeSelection);

    m_selectionAnimating = stillMoving;
    repaint = true;
}

int SettingsPage::PendingGlobalScalePercent()
{
    if (m_pendingGlobalScalePercent == 0)
    {
        m_pendingGlobalScalePercent = m_owner ? m_owner->GetGlobalScalePercent() : 100;
    }
    return UIStyle::Scaling::ClampPercent(m_pendingGlobalScalePercent);
}

int SettingsPage::PendingAnimationDuration()
{
    if (m_pendingAnimationDuration == 0)
        m_pendingAnimationDuration = m_owner ? m_owner->GetAnimationDuration() : 200;
    if (m_pendingAnimationDuration < ANIMATION_DURATION_MIN_MS) m_pendingAnimationDuration = ANIMATION_DURATION_MIN_MS;
    if (m_pendingAnimationDuration > ANIMATION_DURATION_MAX_MS) m_pendingAnimationDuration = ANIMATION_DURATION_MAX_MS;
    return m_pendingAnimationDuration;
}

int SettingsPage::AnimationDurationFromPoint(POINT pt) const
{
    float x = (float)pt.x;
    if (x < GLOBAL_SCALE_TRACK_LEFT) x = GLOBAL_SCALE_TRACK_LEFT;
    if (x > GLOBAL_SCALE_TRACK_RIGHT) x = GLOBAL_SCALE_TRACK_RIGHT;
    float t = (x - GLOBAL_SCALE_TRACK_LEFT) / (GLOBAL_SCALE_TRACK_RIGHT - GLOBAL_SCALE_TRACK_LEFT);
    int steps = (int)std::round(t * ((ANIMATION_DURATION_MAX_MS - ANIMATION_DURATION_MIN_MS) / ANIMATION_DURATION_STEP_MS));
    return ANIMATION_DURATION_MIN_MS + steps * ANIMATION_DURATION_STEP_MS;
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
    m_hoveredOpenLogFile = false;
    m_hoveredConfigDirText = false;
    m_hoveredOpenConfigHistoryDir = false;
    m_hoveredCreateConfigBackup = false;
    m_hoveredRestoreConfigBackup = false;
    m_hoveredClearConfig = false;
    m_hoveredClearConfigHistory = false;
    m_hoveredClearCache = false;
    m_hoveredImportJson = false;
    m_hoveredOpenSourceUrl = false;
    m_hoveredTrigger = -1;
    m_hoveredPopupAlignMode = -1;
    m_hoveredPopupAutoClose = -1;
    m_hoveredPopupMultiOpenWhenPinned = -1;
    m_hoveredSortMode = -1;
    m_hoveredHoverLeaveDelay = false;
    m_hoveredHoverLeaveDelayButton = 0;
    m_hoveredTheme = -1;
    m_hoveredThemeColor = -1;
    m_hoveredWindowMode = -1;
    m_hoveredAppearanceSetting = -1;
    m_hoveredAppearanceButton = 0;
    m_hoveredThemeDetailSetting = -1;
    m_hoveredThemeDetailButton = 0;
    m_hoveredAnimationToggle = false;
    m_hoveredHardwareAcceleration = false;
    m_hoveredFileSelectionValidity = false;
    m_hoveredFileSelectionValidityButton = 0;
    m_hoveredAnimationDurationSlider = false;
    m_hoveredAnimationDurationApply = false;
    m_draggingAnimationDurationSlider = false;
    m_hoveredGlobalScaleSlider = false;
    m_hoveredGlobalScaleApply = false;
    m_draggingGlobalScaleSlider = false;
    m_hoveredApplyUpdate = false;
    m_hoveredCheckUpdate = false;
    m_hoveredPluginInstall = false;
    m_hoveredPluginOpenDir = false;
    m_hoveredPluginRefresh = false;
    m_hoveredPluginConfigure = -1;
    m_hoveredPluginToggle = -1;
    m_hoveredPluginUninstall = -1;
    m_pendingGlobalScalePercent = m_owner ? m_owner->GetGlobalScalePercent() : 100;
    m_pendingAnimationDuration = m_owner ? m_owner->GetAnimationDuration() : 200;
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
            case 4: title = L"插件管理"; break;
            case 5: title = L"关于软件"; break;
            default: title = L"系统设置"; break;
            }
            rt->DrawTextW(title.c_str(), (UINT32)title.size(), tfTitle,
                D2D1::RectF(160, 42, 510, 62), textBrush);
            textBrush->Release();
        }
    }

    if (m_categoryIndex == 0) // 系统设置
    {
        SettingsControlKit::DrawInlineCheckbox(rt, tfDefault, baseClr, 160.0f, m_owner->GetAutoStart(), m_hoveredAutoStart, L"开机自启");
        SettingsControlKit::DrawInlineCheckbox(rt, tfDefault, baseClr, 245.0f, m_owner->GetHideTrayIcon(), m_hoveredHideTrayIcon, L"隐藏托盘");
        SettingsControlKit::DrawInlineCheckbox(rt, tfDefault, baseClr, 330.0f, m_owner->GetHardwareAccelerationEnabled(), m_hoveredHardwareAcceleration, L"硬件加速");
        SettingsControlKit::DrawInlineCheckbox(rt, tfDefault, baseClr, 415.0f, !m_owner->GetAnimationEnabled(), m_hoveredAnimationToggle, L"关闭动画");

        // Animation duration uses the same staged slider/apply pattern as global scale.
        {
            const int currentDuration = m_owner->GetAnimationDuration();
            const int pendingDuration = PendingAnimationDuration();
            const bool hasPendingChange = pendingDuration != currentDuration;
            const bool isRowHovered = m_hoveredAnimationDurationSlider || m_hoveredAnimationDurationApply || m_draggingAnimationDurationSlider;
            const D2D1_RECT_F cardRect = D2D1::RectF(
                GLOBAL_SCALE_CARD_LEFT, ANIMATION_DURATION_CARD_TOP, GLOBAL_SCALE_CARD_RIGHT, ANIMATION_DURATION_CARD_TOP + CARD_HEIGHT);
            const D2D1_ROUNDED_RECT roundedCard = D2D1::RoundedRect(cardRect, 6.0f, 6.0f);

            ID2D1SolidColorBrush* cardBrush = nullptr;
            rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, isRowHovered ? 0.06f : 0.018f), &cardBrush);
            if (cardBrush) { rt->FillRoundedRectangle(roundedCard, cardBrush); cardBrush->Release(); }
            rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, isRowHovered ? 0.105f : 0.045f), &cardBrush);
            if (cardBrush) { rt->DrawRoundedRectangle(roundedCard, cardBrush, UIStyle::Metrics::ControlStroke()); cardBrush->Release(); }

            if (tfDefault)
            {
                ID2D1SolidColorBrush* textBrush = nullptr;
                rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextNormal().d2d, &textBrush);
                if (textBrush)
                {
                    rt->DrawTextW(L"动画时长", 4, tfDefault, D2D1::RectF(170.0f, ANIMATION_DURATION_CARD_TOP + 8.0f, 240.0f, ANIMATION_DURATION_CARD_TOP + 28.0f), textBrush);
                    wchar_t valueBuf[32];
                    swprintf_s(valueBuf, L"%dms", pendingDuration);
                    tfDefault->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    rt->DrawTextW(valueBuf, (UINT32)wcslen(valueBuf), tfDefault, D2D1::RectF(406.0f, ANIMATION_DURATION_CARD_TOP + 8.0f, 444.0f, ANIMATION_DURATION_CARD_TOP + 28.0f), textBrush);
                    tfDefault->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                    textBrush->Release();
                }
            }

            ID2D1SolidColorBrush* trackBrush = nullptr;
            rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, m_hoveredAnimationDurationSlider ? 0.20f : 0.12f), &trackBrush);
            if (trackBrush)
            {
                rt->DrawLine(D2D1::Point2F(GLOBAL_SCALE_TRACK_LEFT, ANIMATION_DURATION_TRACK_Y), D2D1::Point2F(GLOBAL_SCALE_TRACK_RIGHT, ANIMATION_DURATION_TRACK_Y), trackBrush, 3.0f);
                trackBrush->Release();
            }
            const float sliderT = (pendingDuration - ANIMATION_DURATION_MIN_MS) / (float)(ANIMATION_DURATION_MAX_MS - ANIMATION_DURATION_MIN_MS);
            const float thumbX = GLOBAL_SCALE_TRACK_LEFT + sliderT * (GLOBAL_SCALE_TRACK_RIGHT - GLOBAL_SCALE_TRACK_LEFT);
            ID2D1SolidColorBrush* accentBrush = nullptr;
            rt->CreateSolidColorBrush(UIStyle::ThemeColor::Accent().d2d, &accentBrush);
            if (accentBrush)
            {
                rt->DrawLine(D2D1::Point2F(GLOBAL_SCALE_TRACK_LEFT, ANIMATION_DURATION_TRACK_Y), D2D1::Point2F(thumbX, ANIMATION_DURATION_TRACK_Y), accentBrush, 3.0f);
                rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(thumbX, ANIMATION_DURATION_TRACK_Y), 6.0f, 6.0f), accentBrush);
                accentBrush->Release();
            }

            D2D1_COLOR_F applyColor = hasPendingChange ? UIStyle::ThemeColor::Accent().d2d : baseClr;
            applyColor.a = hasPendingChange ? (m_hoveredAnimationDurationApply ? 0.28f : 0.20f) : (m_hoveredAnimationDurationApply ? 0.075f : 0.035f);
            ID2D1SolidColorBrush* applyBrush = nullptr;
            const D2D1_ROUNDED_RECT applyRect = D2D1::RoundedRect(D2D1::RectF(GLOBAL_SCALE_APPLY_LEFT, ANIMATION_DURATION_APPLY_TOP, GLOBAL_SCALE_APPLY_RIGHT, ANIMATION_DURATION_APPLY_BOTTOM), 5.0f, 5.0f);
            rt->CreateSolidColorBrush(applyColor, &applyBrush);
            if (applyBrush) { rt->FillRoundedRectangle(applyRect, applyBrush); applyBrush->Release(); }
            D2D1_COLOR_F applyBorder = hasPendingChange ? UIStyle::ThemeColor::Accent().d2d : baseClr;
            applyBorder.a = hasPendingChange ? 0.62f : 0.12f;
            rt->CreateSolidColorBrush(applyBorder, &applyBrush);
            if (applyBrush) { rt->DrawRoundedRectangle(applyRect, applyBrush, UIStyle::Metrics::ControlStroke()); applyBrush->Release(); }
            if (tfDefault)
            {
                rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextNormal().d2d, &applyBrush);
                if (applyBrush)
                {
                    tfDefault->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    rt->DrawTextW(L"应用", 2, tfDefault, D2D1::RectF(GLOBAL_SCALE_APPLY_LEFT, ANIMATION_DURATION_APPLY_TOP + 2.0f, GLOBAL_SCALE_APPLY_RIGHT, ANIMATION_DURATION_APPLY_BOTTOM), applyBrush);
                    tfDefault->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                    applyBrush->Release();
                }
            }
        }

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
                        D2D1::RectF(170.0f, GLOBAL_SCALE_CARD_TOP + 8.0f, 240.0f, GLOBAL_SCALE_CARD_TOP + 28.0f), textBrush);

                    wchar_t valueBuf[32];
                    swprintf_s(valueBuf, L"%d%%", pendingScale);
                    std::wstring valueText = valueBuf;
                    tfDefault->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    rt->DrawTextW(valueText.c_str(), (UINT32)valueText.size(), tfDefault,
                        D2D1::RectF(406.0f, GLOBAL_SCALE_CARD_TOP + 8.0f, 444.0f, GLOBAL_SCALE_CARD_TOP + 28.0f), textBrush);
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
                    D2D1::RectF(160, 158 + SYSTEM_SETTINGS_CONTENT_OFFSET, 510, 178 + SYSTEM_SETTINGS_CONTENT_OFFSET), tb);
                tb->Release();
            }
        }

        // Draw Theme Buttons side-by-side
        int currentTheme = m_owner->GetTheme();
        std::wstring themeLabels[] = { L"深色主题", L"浅色主题" };
        {
            float selectedX = (currentTheme == 0) ? 160.0f : 345.0f;
            DrawSelectionHighlight(rt, GetSelectionRect(m_themeSelection, D2D1::RectF(selectedX, 180.0f + SYSTEM_SETTINGS_CONTENT_OFFSET, selectedX + 165.0f, 212.0f + SYSTEM_SETTINGS_CONTENT_OFFSET)), 6.0f);
        }
        for (int i = 0; i < 2; i++)
        {
            bool isSelected = (i == currentTheme);
            bool isHovered = (i == m_hoveredTheme);
            float xStart = (i == 0) ? 160.0f : 345.0f;
            D2D1_RECT_F cardRect = D2D1::RectF(xStart, 180.0f + SYSTEM_SETTINGS_CONTENT_OFFSET, xStart + 165.0f, 212.0f + SYSTEM_SETTINGS_CONTENT_OFFSET);
            D2D1_ROUNDED_RECT roundedCard = D2D1::RoundedRect(cardRect, 6.0f, 6.0f);

            ID2D1SolidColorBrush* bgBrush = nullptr;
            D2D1_COLOR_F bgClr = baseClr;
            float bgAlpha = isHovered ? 0.06f : 0.018f;
            rt->CreateSolidColorBrush(D2D1::ColorF(bgClr.r, bgClr.g, bgClr.b, bgAlpha), &bgBrush);
            if (bgBrush)
            {
                rt->FillRoundedRectangle(roundedCard, bgBrush);
                bgBrush->Release();
            }

            ID2D1SolidColorBrush* borderBrush = nullptr;
            D2D1_COLOR_F borderClr = baseClr;
            float borderAlpha = isHovered ? 0.105f : 0.045f;
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
                        D2D1::RectF(xStart, 186.0f + SYSTEM_SETTINGS_CONTENT_OFFSET, xStart + 165.0f, 212.0f + SYSTEM_SETTINGS_CONTENT_OFFSET), textBrush);
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
                    D2D1::RectF(160, 218 + SYSTEM_SETTINGS_CONTENT_OFFSET, 260, 238 + SYSTEM_SETTINGS_CONTENT_OFFSET), tb);

                std::wstring currentLabel = UIStyle::GetThemeColorPresetName(currentThemeColor);
                rt->DrawTextW(currentLabel.c_str(), (UINT32)currentLabel.size(), tfDefault,
                    D2D1::RectF(430, 218 + SYSTEM_SETTINGS_CONTENT_OFFSET, 510, 238 + SYSTEM_SETTINGS_CONTENT_OFFSET), tb);
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
            D2D1_RECT_F swatchRect = D2D1::RectF(x, 244.0f + SYSTEM_SETTINGS_CONTENT_OFFSET, x + swatchSize, 262.0f + SYSTEM_SETTINGS_CONTENT_OFFSET);
            D2D1_ROUNDED_RECT roundedSwatch = D2D1::RoundedRect(swatchRect, 5.0f, 5.0f);

            ID2D1SolidColorBrush* swatchBrush = nullptr;
            rt->CreateSolidColorBrush(UIStyle::GetThemeColorPresetColor(i).d2d, &swatchBrush);
            if (swatchBrush)
            {
                rt->FillRoundedRectangle(roundedSwatch, swatchBrush);
                swatchBrush->Release();
            }

            ID2D1SolidColorBrush* borderBrush = nullptr;
            D2D1_COLOR_F borderClr = baseClr;
            float borderAlpha = isHovered ? 0.42f : 0.18f;
            rt->CreateSolidColorBrush(D2D1::ColorF(borderClr.r, borderClr.g, borderClr.b, borderAlpha), &borderBrush);
            if (borderBrush)
            {
                rt->DrawRoundedRectangle(roundedSwatch, borderBrush, UIStyle::Metrics::ControlStroke());
                borderBrush->Release();
            }

            if (isSelected)
            {
                ID2D1SolidColorBrush* checkBrush = nullptr;
                rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextOnAccent().d2d, &checkBrush);
                if (checkBrush)
                {
                    rt->DrawLine(D2D1::Point2F(x + 5.0f, 253.0f + SYSTEM_SETTINGS_CONTENT_OFFSET), D2D1::Point2F(x + 8.0f, 256.0f + SYSTEM_SETTINGS_CONTENT_OFFSET), checkBrush, 1.4f);
                    rt->DrawLine(D2D1::Point2F(x + 8.0f, 256.0f + SYSTEM_SETTINGS_CONTENT_OFFSET), D2D1::Point2F(x + 14.0f, 249.0f + SYSTEM_SETTINGS_CONTENT_OFFSET), checkBrush, 1.4f);
                    checkBrush->Release();
                }
            }
        }
        {
            const float swatchLeft = 160.0f;
            const float swatchRight = 510.0f;
            const float swatchSize = 18.0f;
            const float swatchStep = (swatchRight - swatchLeft - swatchSize) / (float)(UIStyle::ThemeColorPresetCount() - 1);
            float x = swatchLeft + currentThemeColor * swatchStep;
            D2D1_RECT_F ringRect = GetSelectionRect(m_themeColorSelection, D2D1::RectF(x - 2.0f, 242.0f + SYSTEM_SETTINGS_CONTENT_OFFSET, x + swatchSize + 2.0f, 264.0f + SYSTEM_SETTINGS_CONTENT_OFFSET));
            ID2D1SolidColorBrush* ringBrush = nullptr;
            D2D1_COLOR_F ringClr = UIStyle::GetThemeColorPresetColor(currentThemeColor).d2d;
            ringClr.a = 0.92f;
            rt->CreateSolidColorBrush(ringClr, &ringBrush);
            if (ringBrush)
            {
                rt->DrawRoundedRectangle(D2D1::RoundedRect(ringRect, 6.0f, 6.0f), ringBrush, 1.6f);
                ringBrush->Release();
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
                    D2D1::RectF(160, 276 + SYSTEM_SETTINGS_CONTENT_OFFSET, 510, 294 + SYSTEM_SETTINGS_CONTENT_OFFSET), tb);
                tb->Release();
            }
        }

        // Draw Window Mode Buttons side-by-side
        std::wstring modeLabels[] = { L"发光材质", L"亚克力材质", L"玻璃材质" };
        DrawSelectionHighlight(rt, GetSelectionRect(m_windowModeSelection,
            D2D1::RectF(160.0f + currentWindowMode * 120.0f, 298.0f + SYSTEM_SETTINGS_CONTENT_OFFSET, 270.0f + currentWindowMode * 120.0f, 326.0f + SYSTEM_SETTINGS_CONTENT_OFFSET)), 6.0f);
        for (int i = 0; i < 3; i++)
        {
            bool isSelected = (i == currentWindowMode);
            bool isHovered = (i == m_hoveredWindowMode);
            float xStart = 160.0f + i * 120.0f;
            D2D1_RECT_F cardRect = D2D1::RectF(xStart, 298.0f + SYSTEM_SETTINGS_CONTENT_OFFSET, xStart + 110.0f, 326.0f + SYSTEM_SETTINGS_CONTENT_OFFSET);
            D2D1_ROUNDED_RECT roundedCard = D2D1::RoundedRect(cardRect, 6.0f, 6.0f);

            ID2D1SolidColorBrush* bgBrush = nullptr;
            D2D1_COLOR_F bgClr = baseClr;
            float bgAlpha = isHovered ? 0.06f : 0.018f;
            rt->CreateSolidColorBrush(D2D1::ColorF(bgClr.r, bgClr.g, bgClr.b, bgAlpha), &bgBrush);
            if (bgBrush)
            {
                rt->FillRoundedRectangle(roundedCard, bgBrush);
                bgBrush->Release();
            }

            ID2D1SolidColorBrush* borderBrush = nullptr;
            D2D1_COLOR_F borderClr = baseClr;
            float borderAlpha = isHovered ? 0.105f : 0.045f;
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
                        D2D1::RectF(xStart, 302.0f + SYSTEM_SETTINGS_CONTENT_OFFSET, xStart + 110.0f, 326.0f + SYSTEM_SETTINGS_CONTENT_OFFSET), textBrush);
                    tfDefault->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                    textBrush->Release();
                }
            }
        }

        // Draw Theme Details (6 Sliders/Cards in 2 columns) - Glass and Acrylic Modes
        if (currentWindowMode == 0 || currentWindowMode == 1 || currentWindowMode == 2)
        {
            if (tfDefault)
            {
                ID2D1SolidColorBrush* tb = nullptr;
                rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextMuted().d2d, &tb);
                if (tb)
                {
                    std::wstring label = L"背景效果调节";
                    rt->DrawTextW(label.c_str(), (UINT32)label.size(), tfDefault,
                        D2D1::RectF(160, 338 + SYSTEM_SETTINGS_CONTENT_OFFSET, 510, 354 + SYSTEM_SETTINGS_CONTENT_OFFSET), tb);
                    tb->Release();
                }
            }

            auto& cfg = (currentWindowMode == 2) ?
                ((currentTheme == 1) ? UIStyle::g_GlassLightConfig : UIStyle::g_GlassDarkConfig) :
                ((currentWindowMode == 1) ?
                    ((currentTheme == 1) ? UIStyle::g_AcrylicLightConfig : UIStyle::g_AcrylicDarkConfig) :
                    ((currentTheme == 1) ? UIStyle::g_LightConfig : UIStyle::g_DarkConfig));

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
                D2D1_RECT_F cardRect = TwoColumnRect(col, 360.0f + SYSTEM_SETTINGS_CONTENT_OFFSET + row * 38.0f);
                float ix = cardRect.left;
                float iy = 360.0f + SYSTEM_SETTINGS_CONTENT_OFFSET + row * 38.0f;
                float cy = iy + 16.0f;
                bool isRowHovered = (m_hoveredThemeDetailSetting == activeItems[i].originalIdx);

                // 1. Draw subtle card background
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
            L"标题大小",
            L"窗口边距",
            L"图标列数",
            L"图标行数",
            L"图标大小",
            L"图标字号",
            L"图标间距",
            L"图标圆角",
            L"停靠行数"
        };

        int values[] = {
            m_owner->GetPopupHeaderSizeLevel(),
            m_owner->GetPopupWndPadding(),
            m_owner->GetPopupColumns(),
            m_owner->GetPopupRows(),
            m_owner->GetPopupIconSize(),
            m_owner->GetPopupIconLabelFontSize(),
            m_owner->GetPopupIconGap(),
            m_owner->GetPopupIconRadius(),
            m_owner->GetDockHeight()
        };

        for (int i = 0; i < 9; i++)
        {
            int col = i % 2;
            int row = i / 2;
            D2D1_RECT_F cardRect = TwoColumnRect(col, 90.0f + row * 42.0f);
            float ix = cardRect.left;
            float iy = 90.0f + row * 42.0f;
            float cy = iy + 16.0f;
            bool isRowHovered = (m_hoveredAppearanceSetting == i);

            // 1. Draw subtle card background
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
        if (tfDefault)
        {
            ID2D1SolidColorBrush* tb = nullptr;
            rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextMuted().d2d, &tb);
            if (tb)
            {
                std::wstring label = L"唤醒触发方式";
                rt->DrawTextW(label.c_str(), (UINT32)label.size(), tfDefault,
                    D2D1::RectF(160, 82, 300, 102), tb);
                std::wstring currentLabel = L"当前：" + SettingsPresetMenus::TriggerPresetLabel(m_owner->GetTriggerType());
                DWRITE_TEXT_ALIGNMENT oldAlignment = tfDefault->GetTextAlignment();
                tfDefault->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
                rt->DrawTextW(currentLabel.c_str(), (UINT32)currentLabel.size(), tfDefault,
                    D2D1::RectF(300, 82, 510, 102), tb);
                tfDefault->SetTextAlignment(oldAlignment);
                tb->Release();
            }
        }

        int currentTrigger = m_owner->GetTriggerType();
        std::wstring radioLabels[] = { L"鼠标中键", L"侧键 4", L"侧键 5" };
        int selectedTriggerButton = (currentTrigger >= 0 && currentTrigger <= 2) ? currentTrigger : TRIGGER_PRESET_BUTTON;
        DrawSelectionHighlight(rt, GetSelectionRect(m_triggerSelection, TriggerButtonRect(selectedTriggerButton)), 6.0f);
        for (int i = 0; i < 3; i++)
        {
            SettingsControlKit::DrawSegmentButton(rt, tfDefault, baseClr, 
                TriggerButtonRect(i),
                radioLabels[i],
                i == currentTrigger,
                i == m_hoveredTrigger);
        }
        SettingsControlKit::DrawSegmentButton(rt, tfDefault, baseClr, TriggerButtonRect(TRIGGER_PRESET_BUTTON), L"其他预设", currentTrigger > 2, m_hoveredTrigger == TRIGGER_PRESET_BUTTON);

        if (tfDefault)
        {
            ID2D1SolidColorBrush* tb = nullptr;
            rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextMuted().d2d, &tb);
            if (tb)
            {
                std::wstring label = L"弹窗位置";
                rt->DrawTextW(label.c_str(), (UINT32)label.size(), tfDefault,
                    D2D1::RectF(160, 156, 510, 176), tb);
                tb->Release();
            }
        }

        int alignMode = m_owner->GetPopupAlignMode();
        const int selectedPopupAlignButton =
            (alignMode >= 0 && alignMode < POPUP_ALIGN_PRIMARY_COUNT) ? alignMode : POPUP_ALIGN_PRESET_BUTTON;
        std::wstring alignLabels[] = { L"鼠标居中", L"鼠标左上", L"屏幕居中" };
        DrawSelectionHighlight(rt, GetSelectionRect(m_popupAlignSelection, PopupAlignRect(selectedPopupAlignButton)), 6.0f);
        for (int i = 0; i < POPUP_ALIGN_PRIMARY_COUNT; i++)
        {
            SettingsControlKit::DrawSegmentButton(rt, tfDefault, baseClr, 
                PopupAlignRect(i),
                alignLabels[i],
                i == alignMode,
                i == m_hoveredPopupAlignMode);
        }
        SettingsControlKit::DrawSegmentButton(rt, tfDefault, baseClr, 
            PopupAlignRect(POPUP_ALIGN_PRESET_BUTTON),
            L"其他预设",
            alignMode >= POPUP_ALIGN_PRESET_BUTTON,
            m_hoveredPopupAlignMode == POPUP_ALIGN_PRESET_BUTTON);

        if (tfDefault)
        {
            ID2D1SolidColorBrush* tb = nullptr;
            rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextMuted().d2d, &tb);
            if (tb)
            {
                std::wstring label = L"弹窗行为";
                rt->DrawTextW(label.c_str(), (UINT32)label.size(), tfDefault,
                    D2D1::RectF(160, 230, 510, 250), tb);
                tb->Release();
            }
        }

        bool autoClose = m_owner->GetPopupAutoClose();
        bool multiOpen = m_owner->GetPopupMultiOpenWhenPinned();
        int sortMode = m_owner->GetSortMode();
        DrawSelectionHighlight(rt, GetSelectionRect(m_popupAutoCloseSelection,
            PopupBehaviorRect(autoClose ? 0 : 1, 256.0f)), 6.0f);
        SettingsControlKit::DrawSegmentButton(rt, tfDefault, baseClr, PopupBehaviorRect(0, 256.0f), L"自动关闭", autoClose, m_hoveredPopupAutoClose == 0);
        SettingsControlKit::DrawSegmentButton(rt, tfDefault, baseClr, PopupBehaviorRect(1, 256.0f), L"点击关闭", !autoClose, m_hoveredPopupAutoClose == 1);
        DrawSelectionHighlight(rt, GetSelectionRect(m_popupMultiOpenSelection,
            PopupBehaviorRect(!multiOpen ? 0 : 1, 296.0f)), 6.0f);
        SettingsControlKit::DrawSegmentButton(rt, tfDefault, baseClr, PopupBehaviorRect(0, 296.0f), L"固定时复用", !multiOpen, m_hoveredPopupMultiOpenWhenPinned == 0);
        SettingsControlKit::DrawSegmentButton(rt, tfDefault, baseClr, PopupBehaviorRect(1, 296.0f), L"固定时多开", multiOpen, m_hoveredPopupMultiOpenWhenPinned == 1);
        DrawSelectionHighlight(rt, GetSelectionRect(m_sortModeSelection,
            PopupBehaviorRect(sortMode == 0 ? 0 : 1, 336.0f)), 6.0f);
        SettingsControlKit::DrawSegmentButton(rt, tfDefault, baseClr, PopupBehaviorRect(0, 336.0f), L"自定义排序", sortMode == 0, m_hoveredSortMode == 0);
        SettingsControlKit::DrawSegmentButton(rt, tfDefault, baseClr, PopupBehaviorRect(1, 336.0f), L"智能排序", sortMode == 1, m_hoveredSortMode == 1);

        wchar_t delayBuf[32];
        swprintf_s(delayBuf, L"%dms", m_owner->GetHoverLeaveDelay());
        SettingsControlKit::DrawStepperCard(rt, tfDefault, baseClr, TwoColumnRect(0, 386.0f).left, 386.0f, L"消失延迟", delayBuf, m_hoveredHoverLeaveDelay, m_hoveredHoverLeaveDelayButton);

        wchar_t selectionBuf[32];
        const int selectionValiditySeconds = m_owner->GetFileSelectionValiditySeconds();
        if (selectionValiditySeconds < 0)
            wcscpy_s(selectionBuf, L"无限");
        else
            swprintf_s(selectionBuf, L"%d秒", selectionValiditySeconds);
        SettingsControlKit::DrawStepperCard(rt, tfDefault, baseClr, TwoColumnRect(1, 386.0f).left, 386.0f, L"选中时限", selectionBuf, m_hoveredFileSelectionValidity, m_hoveredFileSelectionValidityButton);

        const D2D1_RECT_F blacklistRect = TriggerBlacklistRect();
        const D2D1_RECT_F editRect = TriggerBlacklistEditRect();
        D2D1_ROUNDED_RECT blacklistCard = D2D1::RoundedRect(blacklistRect, 6.0f, 6.0f);
        ID2D1SolidColorBrush* cardBrush = nullptr;
        rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, m_hoveredTriggerBlacklist ? 0.06f : 0.018f), &cardBrush);
        if (cardBrush)
        {
            rt->FillRoundedRectangle(blacklistCard, cardBrush);
            cardBrush->Release();
        }
        rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, m_hoveredTriggerBlacklist ? 0.105f : 0.045f), &cardBrush);
        if (cardBrush)
        {
            rt->DrawRoundedRectangle(blacklistCard, cardBrush, UIStyle::Metrics::ControlStroke());
            cardBrush->Release();
        }

        if (tfDefault)
        {
            ID2D1SolidColorBrush* textBrush = nullptr;
            rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextNormal().d2d, &textBrush);
            if (textBrush)
            {
                std::wstring title = L"触发黑名单";
                rt->DrawTextW(title.c_str(), (UINT32)title.size(), tfDefault,
                    D2D1::RectF(blacklistRect.left + 10.0f, blacklistRect.top + 7.0f, blacklistRect.left + 102.0f, blacklistRect.bottom - 6.0f),
                    textBrush);
                textBrush->Release();
            }

            rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextMuted().d2d, &textBrush);
            if (textBrush)
            {
                std::wstring summary = SettingsPresetMenus::TriggerBlacklistSummary(m_owner->GetTriggerBlacklist());
                DWRITE_TEXT_ALIGNMENT oldAlignment = tfDefault->GetTextAlignment();
                DWRITE_WORD_WRAPPING oldWrapping = tfDefault->GetWordWrapping();
                tfDefault->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                tfDefault->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                rt->DrawTextW(summary.c_str(), (UINT32)summary.size(), tfDefault,
                    D2D1::RectF(blacklistRect.left + 104.0f, blacklistRect.top + 7.0f, editRect.left - 8.0f, blacklistRect.bottom - 6.0f),
                    textBrush);
                tfDefault->SetWordWrapping(oldWrapping);
                tfDefault->SetTextAlignment(oldAlignment);
                textBrush->Release();
            }

            SettingsControlKit::DrawSegmentButton(rt, tfDefault, baseClr, editRect, L"编辑", false, m_hoveredTriggerBlacklist);
        }
    }
    else if (m_categoryIndex == 3) // 配置管理
    {
        if (tfDefault)
        {
            const D2D1_RECT_F pathCardRect = D2D1::RectF(CONTENT_LEFT, 82.0f, CONTENT_RIGHT, 154.0f);
            const D2D1_RECT_F dirLabelRect = D2D1::RectF(CONTENT_LEFT + 20.0f, 92.0f, CONTENT_RIGHT - 20.0f, 110.0f);
            const D2D1_RECT_F dirValueRect = D2D1::RectF(CONTENT_LEFT + 20.0f, 112.0f, CONTENT_RIGHT - 20.0f, 146.0f);
            const D2D1_RECT_F historyCardRect = D2D1::RectF(CONTENT_LEFT, 164.0f, CONTENT_RIGHT, 214.0f);
            const D2D1_RECT_F historyLabelRect = D2D1::RectF(CONTENT_LEFT + 20.0f, 172.0f, CONTENT_RIGHT - 20.0f, 190.0f);
            const D2D1_RECT_F historyValueRect = D2D1::RectF(CONTENT_LEFT + 20.0f, 192.0f, CONTENT_RIGHT - 20.0f, 210.0f);
            const D2D1_RECT_F openLogFileRect = TwoColumnRect(0, 226.0f);
            const D2D1_RECT_F backupRect = TwoColumnRect(1, 226.0f);
            const D2D1_RECT_F restoreRect = TwoColumnRect(0, 268.0f);
            const D2D1_RECT_F historyDirRect = TwoColumnRect(1, 268.0f);
            const D2D1_RECT_F diagnosticRect = TwoColumnRect(0, 310.0f);
            const D2D1_RECT_F exportMigrationRect = TwoColumnRect(1, 310.0f);
            const D2D1_RECT_F importMigrationRect = TwoColumnRect(0, 352.0f);
            const D2D1_RECT_F importJsonRect = TwoColumnRect(1, 352.0f);
            const D2D1_RECT_F clearUsageRect = TwoColumnRect(0, 394.0f);
            const D2D1_RECT_F clearCacheRect = TwoColumnRect(1, 394.0f);
            const D2D1_RECT_F clearConfigRect = TwoColumnRect(0, 436.0f);
            const D2D1_RECT_F clearHistoryRect = TwoColumnRect(1, 436.0f);

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
            D2D1_ROUNDED_RECT historyCard = D2D1::RoundedRect(historyCardRect, 6.0f, 6.0f);
            if (cardBg) rt->FillRoundedRectangle(historyCard, cardBg);
            if (cardBorder) rt->DrawRoundedRectangle(historyCard, cardBorder, UIStyle::Metrics::ControlStroke());

            if (tbNormal && tbMuted)
            {
                DWRITE_WORD_WRAPPING oldWrapping = tfDefault->GetWordWrapping();
                DWRITE_TEXT_ALIGNMENT oldAlignment = tfDefault->GetTextAlignment();
                tfDefault->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
                tfDefault->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

                std::wstring dirLabel = L"配置文件夹";
                rt->DrawTextW(dirLabel.c_str(), (UINT32)dirLabel.size(), tfDefault, dirLabelRect, tbMuted);

                std::wstring configDir = ConfigPath::GetUserDataDirectory();
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

            if (tbNormal && tbMuted)
            {
                DWRITE_TEXT_ALIGNMENT oldAlignment = tfDefault->GetTextAlignment();
                tfDefault->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                std::wstring historyLabel = L"配置历史";
                std::wstring historySummary = m_owner->GetConfigHistorySummary();
                rt->DrawTextW(historyLabel.c_str(), (UINT32)historyLabel.size(), tfDefault, historyLabelRect, tbMuted);
                rt->DrawTextW(historySummary.c_str(), (UINT32)historySummary.size(), tfDefault, historyValueRect, tbNormal);
                tfDefault->SetTextAlignment(oldAlignment);
            }

            SettingsControlKit::DrawActionButton(rt, tfDefault, baseClr, tbNormal, openLogFileRect, L"打开日志文件", m_hoveredOpenLogFile, false);
            SettingsControlKit::DrawActionButton(rt, tfDefault, baseClr, tbNormal, backupRect, L"立即备份", m_hoveredCreateConfigBackup, false);
            SettingsControlKit::DrawActionButton(rt, tfDefault, baseClr, tbNormal, restoreRect, L"回滚最近历史", m_hoveredRestoreConfigBackup, false);
            SettingsControlKit::DrawActionButton(rt, tfDefault, baseClr, tbNormal, historyDirRect, L"打开历史目录", m_hoveredOpenConfigHistoryDir, false);
            SettingsControlKit::DrawActionButton(rt, tfDefault, baseClr, tbNormal, diagnosticRect, L"生成诊断包", m_hoveredDiagnosticPackage, false);
            SettingsControlKit::DrawActionButton(rt, tfDefault, baseClr, tbNormal, exportMigrationRect, L"导出迁移备份", m_hoveredExportMigration, false);
            SettingsControlKit::DrawActionButton(rt, tfDefault, baseClr, tbNormal, importMigrationRect, L"导入迁移备份", m_hoveredImportMigration, false);
            SettingsControlKit::DrawActionButton(rt, tfDefault, baseClr, tbNormal, importJsonRect, L"导入 QuickLauncher", m_hoveredImportJson, false);
            SettingsControlKit::DrawActionButton(rt, tfDefault, baseClr, tbNormal, clearUsageRect, L"清除使用记录", m_hoveredClearUsageHistory, true);
            SettingsControlKit::DrawActionButton(rt, tfDefault, baseClr, tbNormal, clearCacheRect, L"清理缓存", m_hoveredClearCache, true);
            SettingsControlKit::DrawActionButton(rt, tfDefault, baseClr, tbNormal, clearConfigRect, L"清除配置", m_hoveredClearConfig, true);
            SettingsControlKit::DrawActionButton(rt, tfDefault, baseClr, tbNormal, clearHistoryRect, L"清除历史", m_hoveredClearConfigHistory, true);

            if (tbNormal) tbNormal->Release();
            if (tbMuted) tbMuted->Release();
            if (cardBg) cardBg->Release();
            if (cardBorder) cardBorder->Release();
        }
    }
    else if (m_categoryIndex == 4) // 插件管理
    {
        if (tfDefault)
        {
            ID2D1SolidColorBrush* tbNormal = nullptr;
            rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextNormal().d2d, &tbNormal);
            ID2D1SolidColorBrush* tbMuted = nullptr;
            rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextMuted().d2d, &tbMuted);

            SettingsPluginHoverState hover;
            hover.install = m_hoveredPluginInstall;
            hover.openDir = m_hoveredPluginOpenDir;
            hover.refresh = m_hoveredPluginRefresh;
            hover.configure = m_hoveredPluginConfigure;
            hover.toggle = m_hoveredPluginToggle;
            hover.uninstall = m_hoveredPluginUninstall;

            SettingsPluginView::RenderPluginSection(rt, m_owner, tfDefault, tbNormal, tbMuted, baseClr, hover);

            if (tbNormal) tbNormal->Release();
            if (tbMuted) tbMuted->Release();
        }
    }
    else if (m_categoryIndex == 5) // 关于软件
    {
        if (tfDefault)
        {
            ID2D1SolidColorBrush* tbNormal = nullptr;
            rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextNormal().d2d, &tbNormal);
            ID2D1SolidColorBrush* tbMuted = nullptr;
            rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextMuted().d2d, &tbMuted);

            if (tbNormal && tbMuted)
            {
                if (tfTitle)
                    rt->DrawTextW(L"WinLauncher", 11, tfTitle, D2D1::RectF(160, 82, 510, 107), tbNormal);
                std::wstring verText = std::wstring(L"版本: v") + WINLAUNCHER_VERSION_WSTR;
                rt->DrawTextW(verText.c_str(), (UINT32)verText.size(), tfDefault, D2D1::RectF(160, 112, 510, 130), tbMuted);
                const wchar_t* tagline = L"原生 Windows 桌面启动器 · 快速、轻量、本地优先";
                rt->DrawTextW(tagline, (UINT32)wcslen(tagline), tfDefault,
                    D2D1::RectF(160, 132, CONTENT_RIGHT, 150), tbMuted);

                SettingsControlKit::DrawInfoCard(rt, tfDefault, baseClr, tbNormal, tbMuted, TwoColumnRect(0, 164.0f, 68.0f), L"快速启动", L"通过鼠标手势或快捷键\n在光标处唤出快捷方式面板");
                SettingsControlKit::DrawInfoCard(rt, tfDefault, baseClr, tbNormal, tbMuted, TwoColumnRect(1, 164.0f, 68.0f), L"搜索与分类", L"分页管理常用项目，支持\n即时搜索、智能排序与场景筛选");
                SettingsControlKit::DrawInfoCard(rt, tfDefault, baseClr, tbNormal, tbMuted, TwoColumnRect(0, 242.0f, 68.0f), L"命令与自动化", L"运行自定义命令、批量启动与宏；\n可使用已选文件作为命令输入");
                SettingsControlKit::DrawInfoCard(rt, tfDefault, baseClr, tbNormal, tbMuted, TwoColumnRect(1, 242.0f, 68.0f), L"外观与扩展", L"可调主题、材质、布局与动画；\n支持 DLL 插件、/ 命令与搜索源");
                SettingsControlKit::DrawInfoCard(rt, tfDefault, baseClr, tbNormal, tbMuted, D2D1::RectF(CONTENT_LEFT, 320.0f, CONTENT_RIGHT, 400.0f), L"本地优先与诊断", L"配置、使用记录、日志和崩溃诊断均保留在本机，不会自动上传。\n可在“配置管理”中生成脱敏诊断包、创建备份或迁移到新设备。");

                const D2D1_RECT_F sourceLinkRect = AboutOpenSourceLinkRect();
                const D2D1_ROUNDED_RECT roundedSourceLink = D2D1::RoundedRect(sourceLinkRect, 5.0f, 5.0f);
                ID2D1SolidColorBrush* sourceBrush = nullptr;
                const D2D1_COLOR_F accent = UIStyle::ThemeColor::Accent().d2d;
                rt->CreateSolidColorBrush(D2D1::ColorF(accent.r, accent.g, accent.b, m_hoveredOpenSourceUrl ? 0.16f : 0.075f), &sourceBrush);
                if (sourceBrush)
                {
                    rt->FillRoundedRectangle(roundedSourceLink, sourceBrush);
                    sourceBrush->Release();
                }
                rt->CreateSolidColorBrush(D2D1::ColorF(accent.r, accent.g, accent.b, m_hoveredOpenSourceUrl ? 0.62f : 0.36f), &sourceBrush);
                if (sourceBrush)
                {
                    rt->DrawRoundedRectangle(roundedSourceLink, sourceBrush, UIStyle::Metrics::ControlStroke());
                    sourceBrush->Release();
                }
                const wchar_t* sourceLabel = L"开源地址  github.com/LEISHIQIANG/WinLauncher";
                rt->CreateSolidColorBrush(accent, &sourceBrush);
                if (sourceBrush)
                {
                    tfDefault->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    rt->DrawTextW(sourceLabel, (UINT32)wcslen(sourceLabel), tfDefault, sourceLinkRect, sourceBrush);
                    tfDefault->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                    sourceBrush->Release();
                }
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

        bool had = HitTestAnimationDurationSlider(pt);
        if (m_draggingAnimationDurationSlider)
        {
            int nextDuration = AnimationDurationFromPoint(pt);
            if (nextDuration != m_pendingAnimationDuration)
            {
                m_pendingAnimationDuration = nextDuration;
                repaint = true;
            }
            had = true;
        }
        if (had != m_hoveredAnimationDurationSlider)
        {
            m_hoveredAnimationDurationSlider = had;
            repaint = true;
        }

        bool haa = HitTestAnimationDurationApply(pt);
        if (haa != m_hoveredAnimationDurationApply)
        {
            m_hoveredAnimationDurationApply = haa;
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

        int hAlign = HitTestPopupAlignMode(pt);
        if (hAlign != m_hoveredPopupAlignMode)
        {
            m_hoveredPopupAlignMode = hAlign;
            repaint = true;
        }

        int hAutoClose = HitTestPopupAutoClose(pt);
        if (hAutoClose != m_hoveredPopupAutoClose)
        {
            m_hoveredPopupAutoClose = hAutoClose;
            repaint = true;
        }

        int hMultiOpen = HitTestPopupMultiOpenWhenPinned(pt);
        if (hMultiOpen != m_hoveredPopupMultiOpenWhenPinned)
        {
            m_hoveredPopupMultiOpenWhenPinned = hMultiOpen;
            repaint = true;
        }

        int hSort = HitTestSortMode(pt);
        if (hSort != m_hoveredSortMode)
        {
            m_hoveredSortMode = hSort;
            repaint = true;
        }

        int buttonType = 0;
        bool hDelay = HitTestHoverLeaveDelay(pt, buttonType);
        if (hDelay != m_hoveredHoverLeaveDelay || buttonType != m_hoveredHoverLeaveDelayButton)
        {
            m_hoveredHoverLeaveDelay = hDelay;
            m_hoveredHoverLeaveDelayButton = buttonType;
            repaint = true;
        }

        buttonType = 0;
        bool htd = HitTestFileSelectionValidity(pt, buttonType);
        if (htd != m_hoveredFileSelectionValidity || buttonType != m_hoveredFileSelectionValidityButton)
        {
            m_hoveredFileSelectionValidity = htd;
            m_hoveredFileSelectionValidityButton = buttonType;
            repaint = true;
        }

        bool hBlacklist = HitTestTriggerBlacklist(pt);
        if (hBlacklist != m_hoveredTriggerBlacklist)
        {
            m_hoveredTriggerBlacklist = hBlacklist;
            repaint = true;
        }
    }
    else if (m_categoryIndex == 3)
    {
        bool hLogFile = HitTestOpenLogFile(pt);
        bool hConfigDirText = HitTestConfigDirText(pt);
        bool hHistoryDir = HitTestOpenConfigHistoryDir(pt);
        bool hBackup = HitTestCreateConfigBackup(pt);
        bool hRestore = HitTestRestoreConfigBackup(pt);
        bool hClearConfig = HitTestClearConfig(pt);
        bool hClearHistory = HitTestClearConfigHistory(pt);
        bool hImportJson = HitTestImportJson(pt);
        bool hDiagnostic = HitTestDiagnosticPackage(pt);
        bool hExport = HitTestExportMigration(pt);
        bool hImport = HitTestImportMigration(pt);
        bool hClearUsage = HitTestClearUsageHistory(pt);
        bool hClearCache = HitTestClearCache(pt);
        if (hLogFile != m_hoveredOpenLogFile ||
            hConfigDirText != m_hoveredConfigDirText ||
            hHistoryDir != m_hoveredOpenConfigHistoryDir ||
            hBackup != m_hoveredCreateConfigBackup ||
            hRestore != m_hoveredRestoreConfigBackup ||
            hClearConfig != m_hoveredClearConfig ||
            hClearHistory != m_hoveredClearConfigHistory ||
            hImportJson != m_hoveredImportJson || hDiagnostic != m_hoveredDiagnosticPackage || hExport != m_hoveredExportMigration || hImport != m_hoveredImportMigration || hClearUsage != m_hoveredClearUsageHistory || hClearCache != m_hoveredClearCache)
        {
            m_hoveredOpenLogFile = hLogFile;
            m_hoveredConfigDirText = hConfigDirText;
            m_hoveredOpenConfigHistoryDir = hHistoryDir;
            m_hoveredCreateConfigBackup = hBackup;
            m_hoveredRestoreConfigBackup = hRestore;
            m_hoveredClearConfig = hClearConfig;
            m_hoveredClearConfigHistory = hClearHistory;
            m_hoveredImportJson = hImportJson;
            m_hoveredDiagnosticPackage = hDiagnostic;
            m_hoveredExportMigration = hExport;
            m_hoveredImportMigration = hImport;
            m_hoveredClearUsageHistory = hClearUsage;
            m_hoveredClearCache = hClearCache;
            repaint = true;
        }
    }
    else if (m_categoryIndex == 4)
    {
        bool install = HitTestPluginInstall(pt);
        bool openDir = HitTestPluginOpenDir(pt);
        bool refresh = HitTestPluginRefresh(pt);
        int configure = HitTestPluginConfigure(pt);
        int toggle = HitTestPluginToggle(pt);
        int uninstall = HitTestPluginUninstall(pt);
        if (install != m_hoveredPluginInstall ||
            openDir != m_hoveredPluginOpenDir ||
            refresh != m_hoveredPluginRefresh ||
            configure != m_hoveredPluginConfigure ||
            toggle != m_hoveredPluginToggle ||
            uninstall != m_hoveredPluginUninstall)
        {
            m_hoveredPluginInstall = install;
            m_hoveredPluginOpenDir = openDir;
            m_hoveredPluginRefresh = refresh;
            m_hoveredPluginConfigure = configure;
            m_hoveredPluginToggle = toggle;
            m_hoveredPluginUninstall = uninstall;
            repaint = true;
        }
    }
    else if (m_categoryIndex == 5)
    {
        const bool hoverSourceUrl = HitTestOpenSourceUrl(pt);
        if (hoverSourceUrl != m_hoveredOpenSourceUrl)
        {
            m_hoveredOpenSourceUrl = hoverSourceUrl;
            repaint = true;
        }
    }
}

void SettingsPage::OnMouseLeave(bool& repaint)
{
    if (m_hoveredAutoStart || m_hoveredHideTrayIcon || m_hoveredOpenLogFile || m_hoveredConfigDirText || m_hoveredOpenConfigHistoryDir || m_hoveredCreateConfigBackup || m_hoveredRestoreConfigBackup || m_hoveredClearConfig || m_hoveredClearConfigHistory || m_hoveredClearCache || m_hoveredImportJson || m_hoveredOpenSourceUrl || m_hoveredTrigger != -1 || m_hoveredPopupAlignMode != -1 || m_hoveredPopupAutoClose != -1 || m_hoveredPopupMultiOpenWhenPinned != -1 || m_hoveredSortMode != -1 || m_hoveredTriggerBlacklist || m_hoveredHoverLeaveDelay || m_hoveredHoverLeaveDelayButton != 0 || m_hoveredTheme != -1 || m_hoveredThemeColor != -1 || m_hoveredWindowMode != -1 || m_hoveredAppearanceSetting != -1 || m_hoveredAppearanceButton != 0 || m_hoveredThemeDetailSetting != -1 || m_hoveredThemeDetailButton != 0 || m_hoveredAnimationToggle || m_hoveredHardwareAcceleration || m_hoveredFileSelectionValidity || m_hoveredFileSelectionValidityButton != 0 || m_hoveredAnimationDurationSlider || m_hoveredAnimationDurationApply || m_draggingAnimationDurationSlider || m_hoveredGlobalScaleSlider || m_hoveredGlobalScaleApply || m_draggingGlobalScaleSlider || m_hoveredPluginInstall || m_hoveredPluginOpenDir || m_hoveredPluginRefresh || m_hoveredPluginConfigure != -1 || m_hoveredPluginToggle != -1 || m_hoveredPluginUninstall != -1)
    {
        m_hoveredAutoStart = false;
        m_hoveredHideTrayIcon = false;
        m_hoveredOpenLogFile = false;
        m_hoveredConfigDirText = false;
        m_hoveredOpenConfigHistoryDir = false;
        m_hoveredCreateConfigBackup = false;
        m_hoveredRestoreConfigBackup = false;
        m_hoveredClearConfig = false;
        m_hoveredClearConfigHistory = false;
        m_hoveredClearCache = false;
        m_hoveredImportJson = false;
        m_hoveredOpenSourceUrl = false;
        m_hoveredTrigger = -1;
        m_hoveredPopupAlignMode = -1;
        m_hoveredPopupAutoClose = -1;
        m_hoveredPopupMultiOpenWhenPinned = -1;
        m_hoveredSortMode = -1;
        m_hoveredTriggerBlacklist = false;
        m_hoveredHoverLeaveDelay = false;
        m_hoveredHoverLeaveDelayButton = 0;
        m_hoveredTheme = -1;
        m_hoveredThemeColor = -1;
        m_hoveredWindowMode = -1;
        m_hoveredAppearanceSetting = -1;
        m_hoveredAppearanceButton = 0;
        m_hoveredThemeDetailSetting = -1;
        m_hoveredThemeDetailButton = 0;
        m_hoveredAnimationToggle = false;
        m_hoveredHardwareAcceleration = false;
        m_hoveredFileSelectionValidity = false;
        m_hoveredFileSelectionValidityButton = 0;
        m_hoveredAnimationDurationSlider = false;
        m_hoveredAnimationDurationApply = false;
        m_draggingAnimationDurationSlider = false;
        m_hoveredGlobalScaleSlider = false;
        m_hoveredGlobalScaleApply = false;
        m_draggingGlobalScaleSlider = false;
        m_hoveredPluginInstall = false;
        m_hoveredPluginOpenDir = false;
        m_hoveredPluginRefresh = false;
        m_hoveredPluginConfigure = -1;
        m_hoveredPluginToggle = -1;
        m_hoveredPluginUninstall = -1;
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
        else if (HitTestAnimationDurationSlider(pt))
        {
            m_draggingAnimationDurationSlider = true;
            m_pendingAnimationDuration = AnimationDurationFromPoint(pt);
            if (HWND hwnd = m_owner->GetWindowHWND())
            {
                if (!MouseCaptureController::CaptureGesture(
                        hwnd, VK_LBUTTON, this, &SettingsPage::CancelPointerInteractionThunk))
                    m_draggingAnimationDurationSlider = false;
            }
            repaint = true;
        }
        else if (HitTestAnimationDurationApply(pt))
        {
            const int pendingDuration = PendingAnimationDuration();
            if (pendingDuration != m_owner->GetAnimationDuration())
            {
                m_owner->SetAnimationDuration(pendingDuration);
                m_pendingAnimationDuration = pendingDuration;
            }
            repaint = true;
        }
        else if (HitTestGlobalScaleSlider(pt))
        {
            m_draggingGlobalScaleSlider = true;
            m_pendingGlobalScalePercent = GlobalScaleFromPoint(pt);
            if (HWND hwnd = m_owner->GetWindowHWND())
            {
                if (!MouseCaptureController::CaptureGesture(
                        hwnd, VK_LBUTTON, this, &SettingsPage::CancelPointerInteractionThunk))
                    m_draggingGlobalScaleSlider = false;
            }
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
                    if (hwmode >= 0 && hwmode <= 2)
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
                                auto& cfg = (currentWindowMode == 2) ?
                                    ((currentTheme == 1) ? UIStyle::g_GlassLightConfig : UIStyle::g_GlassDarkConfig) :
                                    ((currentWindowMode == 1) ?
                                        ((currentTheme == 1) ? UIStyle::g_AcrylicLightConfig : UIStyle::g_AcrylicDarkConfig) :
                                        ((currentTheme == 1) ? UIStyle::g_LightConfig : UIStyle::g_DarkConfig));

                                bool changed = false;
                                if (settingIdx == 0) // Hue
                                {
                                    float val = cfg.hue + step * 5.0f;
                                    if (val < 0.0f) val += 360.0f;
                                    if (val >= 360.0f) val -= 360.0f;
                                    if (fabsf(val - cfg.hue) > 0.001f)
                                    {
                                        cfg.hue = val;
                                        changed = true;
                                    }
                                }
                                else if (settingIdx == 1) // Blur
                                {
                                    float val = cfg.blur + step;
                                    if (val < 0.0f) val = 0.0f;
                                    if (val > 30.0f) val = 30.0f;
                                    if (fabsf(val - cfg.blur) > 0.001f)
                                    {
                                        cfg.blur = val;
                                        changed = true;
                                    }
                                }
                                else if (settingIdx == 2) // Opacity
                                {
                                     float val = UIStyle::ClampMaterialOpacity(currentWindowMode, cfg.opacity - step * 0.05f);
                                     if (fabsf(val - cfg.opacity) > 0.001f)
                                     {
                                         cfg.opacity = val;
                                         changed = true;
                                     }
                                }
                                else if (settingIdx == 3) // Highlight
                                {
                                    float val = cfg.highlight + step * 0.05f;
                                    if (val < 0.0f) val = 0.0f;
                                    if (val > 1.0f) val = 1.0f;
                                    if (fabsf(val - cfg.highlight) > 0.001f)
                                    {
                                        cfg.highlight = val;
                                        changed = true;
                                    }
                                }
                                else if (settingIdx == 4) // Brightness
                                {
                                    float val = cfg.brightness + step * 0.05f;
                                    if (val < 0.0f) val = 0.0f;
                                    if (val > 1.0f) val = 1.0f;
                                    if (fabsf(val - cfg.brightness) > 0.001f)
                                    {
                                        cfg.brightness = val;
                                        changed = true;
                                    }
                                }
                                else if (settingIdx == 5) // Saturation
                                {
                                    float val = cfg.saturation + step * 0.1f;
                                    if (val < 0.5f) val = 0.5f;
                                    if (val > 3.0f) val = 3.0f;
                                    if (fabsf(val - cfg.saturation) > 0.001f)
                                    {
                                        cfg.saturation = val;
                                        changed = true;
                                    }
                                }

                                if (changed)
                                {
                                    m_owner->NotifyConfigChanged(true);
                                    repaint = true;
                                }
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
                
                if (settingIdx == 0) // Header Size Level
                {
                    int val = m_owner->GetPopupHeaderSizeLevel() + step;
                    if (val >= 1 && val <= 9) m_owner->SetPopupHeaderSizeLevel(val);
                }
                else if (settingIdx == 1) // Window Padding
                {
                    int val = m_owner->GetPopupWndPadding() + step;
                    if (val >= 0 && val <= 50) m_owner->SetPopupWndPadding(val);
                }
                else if (settingIdx == 2) // Columns
                {
                    int val = m_owner->GetPopupColumns() + step;
                    if (val >= 1 && val <= 20) m_owner->SetPopupColumns(val);
                }
                else if (settingIdx == 3) // Rows
                {
                    int val = m_owner->GetPopupRows() + step;
                    if (val >= 1 && val <= 20) m_owner->SetPopupRows(val);
                }
                else if (settingIdx == 4) // Icon Size
                {
                    int val = m_owner->GetPopupIconSize() + step * 2;
                    if (val >= 16 && val <= 64) m_owner->SetPopupIconSize(val);
                }
                else if (settingIdx == 5) // Icon Label Font Size
                {
                    int val = m_owner->GetPopupIconLabelFontSize() + step;
                    if (val >= 8 && val <= 32) m_owner->SetPopupIconLabelFontSize(val);
                }
                else if (settingIdx == 6) // Icon Gap
                {
                    int val = m_owner->GetPopupIconGap() + step;
                    if (val >= 0 && val <= 30) m_owner->SetPopupIconGap(val);
                }
                else if (settingIdx == 7) // Icon Radius
                {
                    int val = m_owner->GetPopupIconRadius() + step;
                    if (val >= 0 && val <= 30) m_owner->SetPopupIconRadius(val);
                }
                else if (settingIdx == 8) // DOCK Rows
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
        if (htrig == TRIGGER_PRESET_BUTTON)
        {
            ShowTriggerPresetMenu();
            repaint = true;
        }
        else if (htrig >= 0 && htrig <= 2)
        {
            m_owner->SetTriggerType(htrig);
            m_owner->NotifyConfigChanged();
            repaint = true;
        }
        else
        {
            int alignMode = HitTestPopupAlignMode(pt);
            if (alignMode == POPUP_ALIGN_PRESET_BUTTON)
            {
                ShowPopupAlignPresetMenu();
                repaint = true;
            }
            else if (alignMode >= 0 && alignMode < POPUP_ALIGN_PRIMARY_COUNT)
            {
                m_owner->SetPopupAlignMode(alignMode);
                m_owner->NotifyConfigChanged();
                repaint = true;
            }
            else
            {
                int autoClose = HitTestPopupAutoClose(pt);
                if (autoClose >= 0)
                {
                    m_owner->SetPopupAutoClose(autoClose == 0);
                    m_owner->NotifyConfigChanged();
                    repaint = true;
                }
                else
                {
                    int multiOpen = HitTestPopupMultiOpenWhenPinned(pt);
                    if (multiOpen >= 0)
                    {
                        m_owner->SetPopupMultiOpenWhenPinned(multiOpen == 1);
                        m_owner->NotifyConfigChanged();
                        repaint = true;
                    }
                    else
                    {
                        int sortMode = HitTestSortMode(pt);
                        if (sortMode >= 0)
                        {
                            m_owner->SetSortMode(sortMode);
                            m_owner->NotifyConfigChanged();
                            repaint = true;
                        }
                        else if (m_hoveredHoverLeaveDelay)
                        {
                            if (m_hoveredHoverLeaveDelayButton == 1)
                            {
                                int current = m_owner->GetHoverLeaveDelay();
                                if (current > 0) m_owner->SetHoverLeaveDelay(current - 50);
                                m_owner->NotifyConfigChanged();
                                repaint = true;
                            }
                            else if (m_hoveredHoverLeaveDelayButton == 2)
                            {
                                int current = m_owner->GetHoverLeaveDelay();
                                if (current < 5000) m_owner->SetHoverLeaveDelay(current + 50);
                                m_owner->NotifyConfigChanged();
                                repaint = true;
                            }
                        }
                        else if (m_hoveredFileSelectionValidity)
                        {
                            if (m_hoveredFileSelectionValidityButton == 1)
                            {
                                int current = m_owner->GetFileSelectionValiditySeconds();
                                if (current < 0) m_owner->SetFileSelectionValiditySeconds(20);
                                else if (current > 0) m_owner->SetFileSelectionValiditySeconds(current - 1);
                                m_owner->NotifyConfigChanged();
                                repaint = true;
                            }
                            else if (m_hoveredFileSelectionValidityButton == 2)
                            {
                                int current = m_owner->GetFileSelectionValiditySeconds();
                                if (current < 0)
                                {
                                    // Infinite is the final selectable state.
                                }
                                else if (current >= 20) m_owner->SetFileSelectionValiditySeconds(-1);
                                else m_owner->SetFileSelectionValiditySeconds(current + 1);
                                m_owner->NotifyConfigChanged();
                                repaint = true;
                            }
                        }
                        else if (HitTestTriggerBlacklist(pt))
                        {
                            ShowTriggerBlacklistEditor();
                            repaint = true;
                        }
                    }
                }
            }
        }
    }
    else if (m_categoryIndex == 3)
    {
        if (HitTestOpenLogFile(pt))
        {
            m_owner->OpenLogFile();
            repaint = true;
        }
        else if (HitTestConfigDirText(pt))
        {
            m_owner->OpenConfigDir();
            repaint = true;
        }
        else if (HitTestCreateConfigBackup(pt))
        {
            m_owner->CreateConfigBackupNow();
            repaint = true;
        }
        else if (HitTestRestoreConfigBackup(pt))
        {
            m_owner->RestoreLatestConfigBackup();
            repaint = true;
        }
        else if (HitTestOpenConfigHistoryDir(pt))
        {
            m_owner->OpenConfigHistoryDir();
            repaint = true;
        }
        else if (HitTestImportJson(pt))
        {
            if (OnImportJsonClicked)
                OnImportJsonClicked();
            repaint = true;
        }
        else if (HitTestDiagnosticPackage(pt)) { m_owner->CreateDiagnosticPackage(); repaint = true; }
        else if (HitTestExportMigration(pt)) { m_owner->ExportMigrationBackup(); repaint = true; }
        else if (HitTestImportMigration(pt)) { m_owner->ImportMigrationBackup(); repaint = true; }
        else if (HitTestClearUsageHistory(pt)) { m_owner->ClearUsageHistory(); repaint = true; }
        else if (HitTestClearCache(pt)) { m_owner->ClearCache(); repaint = true; }
        else if (HitTestClearConfig(pt))
        {
            m_owner->ClearConfigData();
            repaint = true;
        }
        else if (HitTestClearConfigHistory(pt))
        {
            m_owner->ClearConfigHistoryData();
            repaint = true;
        }
    }
    else if (m_categoryIndex == 4)
    {
        SettingsPluginActions::HandleLButtonDown(this, m_owner, pt, repaint);
    }
    else if (m_categoryIndex == 5 && HitTestOpenSourceUrl(pt))
    {
        HWND hwnd = m_owner ? m_owner->GetWindowHWND() : nullptr;
        ShellExecuteW(hwnd, L"open", L"https://github.com/LEISHIQIANG/WinLauncher", nullptr, nullptr, SW_SHOWNORMAL);
        repaint = true;
    }
}

void SettingsPage::OnLButtonUp(POINT pt, bool& repaint)
{
    if (m_draggingAnimationDurationSlider)
    {
        m_draggingAnimationDurationSlider = false;
        m_pendingAnimationDuration = AnimationDurationFromPoint(pt);
        MouseCaptureController::Complete(m_owner ? m_owner->GetWindowHWND() : nullptr);
        repaint = true;
    }
    else if (m_draggingGlobalScaleSlider)
    {
        m_draggingGlobalScaleSlider = false;
        m_pendingGlobalScalePercent = GlobalScaleFromPoint(pt);
        MouseCaptureController::Complete(m_owner ? m_owner->GetWindowHWND() : nullptr);
        repaint = true;
    }
}

void SettingsPage::OnLButtonDblClk(POINT pt, bool& repaint)
{
    OnLButtonDown(pt, repaint);
}

void SettingsPage::OnDropFiles(HDROP hDrop, bool& repaint)
{
    if (m_categoryIndex != 4 || !hDrop)
        return;

    SettingsPluginActions::HandleDropFiles(this, m_owner, hDrop, repaint);
}

bool SettingsPage::InstallPluginPackageFromPath(const std::wstring& filePath, bool showSuccessMessage, std::wstring* errorMessage)
{
    return SettingsPluginActions::InstallPluginPackageFromPath(m_owner, filePath, showSuccessMessage, errorMessage);
}

bool SettingsPage::IsPluginPackagePath(const std::wstring& filePath) const
{
    return SettingsPluginActions::IsPluginPackagePath(filePath);
}
