#include "SettingsPage.h"
#include "IConfigWindow.h"
#include "UIStyle.h"
#include "ConfirmWindow.h"
#include "DropDownMenu.h"
#include "../DpiHelper.h"
#include "../Services/UpdateService.h"
#include "..\version.h"
#include <cwchar>
#include <cmath>
#include <vector>

namespace
{
    constexpr float CONTENT_LEFT = 160.0f;
    constexpr float CONTENT_RIGHT = 510.0f;
    constexpr float TWO_COLUMN_GAP = 10.0f;
    constexpr float TWO_COLUMN_WIDTH = (CONTENT_RIGHT - CONTENT_LEFT - TWO_COLUMN_GAP) / 2.0f;
    constexpr float TWO_COLUMN_STEP = TWO_COLUMN_WIDTH + TWO_COLUMN_GAP;
    constexpr float CARD_HEIGHT = 32.0f;

    constexpr float GLOBAL_SCALE_CARD_LEFT = 160.0f;
    constexpr float GLOBAL_SCALE_CARD_TOP = 112.0f;
    constexpr float GLOBAL_SCALE_CARD_RIGHT = CONTENT_RIGHT;
    constexpr float GLOBAL_SCALE_CARD_BOTTOM = 148.0f;
    constexpr float GLOBAL_SCALE_TRACK_LEFT = 250.0f;
    constexpr float GLOBAL_SCALE_TRACK_RIGHT = 402.0f;
    constexpr float GLOBAL_SCALE_TRACK_Y = 130.0f;
    constexpr float GLOBAL_SCALE_APPLY_LEFT = 448.0f;
    constexpr float GLOBAL_SCALE_APPLY_TOP = 118.0f;
    constexpr float GLOBAL_SCALE_APPLY_RIGHT = 506.0f;
    constexpr float GLOBAL_SCALE_APPLY_BOTTOM = 142.0f;

    constexpr float TRIGGER_TOP = 108.0f;
    constexpr float TRIGGER_BOTTOM = 136.0f;
    constexpr int TRIGGER_PRESET_BUTTON = 3;
    constexpr int FOUR_SEGMENT_COUNT = 4;
    constexpr float FOUR_SEGMENT_GAP = 10.0f;
    constexpr float FOUR_SEGMENT_WIDTH = (CONTENT_RIGHT - CONTENT_LEFT - FOUR_SEGMENT_GAP * (FOUR_SEGMENT_COUNT - 1)) / FOUR_SEGMENT_COUNT;

    D2D1_RECT_F TwoColumnRect(int col, float top, float height = CARD_HEIGHT)
    {
        const float left = CONTENT_LEFT + col * TWO_COLUMN_STEP;
        return D2D1::RectF(left, top, left + TWO_COLUMN_WIDTH, top + height);
    }

    D2D1_RECT_F FourSegmentRect(int index, float top, float bottom)
    {
        const float left = CONTENT_LEFT + index * (FOUR_SEGMENT_WIDTH + FOUR_SEGMENT_GAP);
        return D2D1::RectF(left, top, left + FOUR_SEGMENT_WIDTH, bottom);
    }

    D2D1_RECT_F PopupAlignRect(int index)
    {
        return FourSegmentRect(index, 182.0f, 210.0f);
    }

    D2D1_RECT_F PopupBehaviorRect(int index, float top)
    {
        return TwoColumnRect(index, top, 28.0f);
    }

    bool PointInRect(const D2D1_RECT_F& rect, POINT pt)
    {
        return pt.x >= rect.left && pt.x <= rect.right && pt.y >= rect.top && pt.y <= rect.bottom;
    }

    D2D1_RECT_F TriggerButtonRect(int index)
    {
        return FourSegmentRect(index, TRIGGER_TOP, TRIGGER_BOTTOM);
    }

    std::wstring TriggerPresetLabel(int type)
    {
        switch (type)
        {
        case 0: return L"鼠标中键";
        case 1: return L"鼠标侧键 4";
        case 2: return L"鼠标侧键 5";
        case 3: return L"Ctrl + 中键";
        case 4: return L"Shift + 中键";
        case 5: return L"Alt + 中键";
        case 6: return L"Ctrl + 侧键 4";
        case 7: return L"Ctrl + 侧键 5";
        default: return L"未知预设";
        }
    }
}

SettingsPage::SettingsPage(IConfigWindow* owner)
    : m_owner(owner)
{
}

SettingsPage::~SettingsPage()
{
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

void SettingsPage::ShowTriggerPresetMenu()
{
    if (!m_owner) return;

    HWND hwnd = m_owner->GetWindowHWND();
    if (!hwnd) return;

    std::vector<DropDownMenu::Item> items;
    const int currentTrigger = m_owner->GetTriggerType();
    auto addPreset = [&](int type)
    {
        std::wstring label = TriggerPresetLabel(type);
        if (type == currentTrigger)
            label = L"当前：" + label;

        items.push_back(DropDownMenu::Item{
            label,
            [this, type]()
            {
                if (!m_owner) return;
                if (m_owner->GetTriggerType() != type)
                    m_owner->SetTriggerType(type);
                m_owner->NotifyConfigChanged();
                HWND ownerHwnd = m_owner->GetWindowHWND();
                if (ownerHwnd)
                    InvalidateRect(ownerHwnd, nullptr, FALSE);
            },
            false
        });
    };

    for (int type = 3; type <= 7; ++type)
        addPreset(type);

    D2D1_RECT_F presetRect = TriggerButtonRect(TRIGGER_PRESET_BUTTON);
    POINT menuPt{ (int)presetRect.left, (int)(presetRect.bottom + 6.0f) };
    menuPt = DpiHelper::LogicalClientToScreen(hwnd, menuPt);
    DropDownMenu::Show(hwnd, menuPt, items, m_owner->GetAppContext(), presetRect.right - presetRect.left, true, 10.5f);
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
    m_hoveredOpenConfigHistoryDir = false;
    m_hoveredCreateConfigBackup = false;
    m_hoveredRestoreConfigBackup = false;
    m_hoveredClearConfig = false;
    m_hoveredClearConfigHistory = false;
    m_hoveredImportJson = false;
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
        {
            float selectedX = (currentTheme == 0) ? 160.0f : 345.0f;
            DrawSelectionHighlight(rt, GetSelectionRect(m_themeSelection, D2D1::RectF(selectedX, 180.0f, selectedX + 165.0f, 212.0f)), 6.0f);
        }
        for (int i = 0; i < 2; i++)
        {
            bool isSelected = (i == currentTheme);
            bool isHovered = (i == m_hoveredTheme);
            float xStart = (i == 0) ? 160.0f : 345.0f;
            D2D1_RECT_F cardRect = D2D1::RectF(xStart, 180.0f, xStart + 165.0f, 212.0f);
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
                    rt->DrawLine(D2D1::Point2F(x + 5.0f, 253.0f), D2D1::Point2F(x + 8.0f, 256.0f), checkBrush, 1.4f);
                    rt->DrawLine(D2D1::Point2F(x + 8.0f, 256.0f), D2D1::Point2F(x + 14.0f, 249.0f), checkBrush, 1.4f);
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
            D2D1_RECT_F ringRect = GetSelectionRect(m_themeColorSelection, D2D1::RectF(x - 2.0f, 242.0f, x + swatchSize + 2.0f, 264.0f));
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
                    D2D1::RectF(160, 276, 510, 294), tb);
                tb->Release();
            }
        }

        // Draw Window Mode Buttons side-by-side
        std::wstring modeLabels[] = { L"发光材质", L"亚克力材质", L"玻璃材质" };
        DrawSelectionHighlight(rt, GetSelectionRect(m_windowModeSelection,
            D2D1::RectF(160.0f + currentWindowMode * 120.0f, 298.0f, 270.0f + currentWindowMode * 120.0f, 326.0f)), 6.0f);
        for (int i = 0; i < 3; i++)
        {
            bool isSelected = (i == currentWindowMode);
            bool isHovered = (i == m_hoveredWindowMode);
            float xStart = 160.0f + i * 120.0f;
            D2D1_RECT_F cardRect = D2D1::RectF(xStart, 298.0f, xStart + 110.0f, 326.0f);
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
                        D2D1::RectF(xStart, 302.0f, xStart + 110.0f, 326.0f), textBrush);
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
                        D2D1::RectF(160, 338, 510, 354), tb);
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
                D2D1_RECT_F cardRect = TwoColumnRect(col, 360.0f + row * 38.0f);
                float ix = cardRect.left;
                float iy = 360.0f + row * 38.0f;
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
        auto drawSegmentButton = [&](const D2D1_RECT_F& cardRect, const std::wstring& text, bool selected, bool hovered)
        {
            D2D1_ROUNDED_RECT roundedCard = D2D1::RoundedRect(cardRect, 6.0f, 6.0f);
            ID2D1SolidColorBrush* bgBrush = nullptr;
            D2D1_COLOR_F bgClr = baseClr;
            float bgAlpha = hovered ? 0.06f : 0.018f;
            rt->CreateSolidColorBrush(D2D1::ColorF(bgClr.r, bgClr.g, bgClr.b, bgAlpha), &bgBrush);
            if (bgBrush)
            {
                rt->FillRoundedRectangle(roundedCard, bgBrush);
                bgBrush->Release();
            }

            ID2D1SolidColorBrush* borderBrush = nullptr;
            D2D1_COLOR_F borderClr = baseClr;
            float borderAlpha = hovered ? 0.105f : 0.045f;
            rt->CreateSolidColorBrush(D2D1::ColorF(borderClr.r, borderClr.g, borderClr.b, borderAlpha), &borderBrush);
            if (borderBrush)
            {
                rt->DrawRoundedRectangle(roundedCard, borderBrush, UIStyle::Metrics::ControlStroke());
                borderBrush->Release();
            }

            if (tfDefault)
            {
                ID2D1SolidColorBrush* textBrush = nullptr;
                D2D1_COLOR_F txtClr = selected ? UIStyle::ThemeColor::Accent().d2d : UIStyle::ThemeColor::TextNormal().d2d;
                rt->CreateSolidColorBrush(txtClr, &textBrush);
                if (textBrush)
                {
                    DWRITE_TEXT_ALIGNMENT oldAlignment = tfDefault->GetTextAlignment();
                    tfDefault->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    rt->DrawTextW(text.c_str(), (UINT32)text.size(), tfDefault, cardRect, textBrush);
                    tfDefault->SetTextAlignment(oldAlignment);
                    textBrush->Release();
                }
            }
        };

        auto drawStepperCard = [&](float ix, float iy, const std::wstring& label, const std::wstring& value, bool hovered, int button)
        {
            float cy = iy + 16.0f;
            D2D1_RECT_F cardRect = D2D1::RectF(ix, iy, ix + TWO_COLUMN_WIDTH, iy + CARD_HEIGHT);
            D2D1_ROUNDED_RECT roundedCard = D2D1::RoundedRect(cardRect, 6.0f, 6.0f);

            ID2D1SolidColorBrush* cardBg = nullptr;
            rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, hovered ? 0.06f : 0.018f), &cardBg);
            if (cardBg)
            {
                rt->FillRoundedRectangle(roundedCard, cardBg);
                cardBg->Release();
            }

            ID2D1SolidColorBrush* cardBorder = nullptr;
            rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, hovered ? 0.105f : 0.045f), &cardBorder);
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
                    rt->DrawTextW(label.c_str(), (UINT32)label.size(), tfDefault,
                        D2D1::RectF(ix + 10, cy - 10, ix + 75, cy + 10), textBrush);
                    DWRITE_TEXT_ALIGNMENT oldAlignment = tfDefault->GetTextAlignment();
                    tfDefault->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    rt->DrawTextW(value.c_str(), (UINT32)value.size(), tfDefault,
                        D2D1::RectF(ix + 101, cy - 10, ix + 129, cy + 10), textBrush);
                    tfDefault->SetTextAlignment(oldAlignment);
                    textBrush->Release();
                }
            }

            auto drawStepButton = [&](float left, bool plus, bool isHovered)
            {
                D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(D2D1::RectF(left, cy - 8, left + 16, cy + 8), 3.0f, 3.0f);
                ID2D1SolidColorBrush* btnBrush = nullptr;
                rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, isHovered ? 0.105f : 0.04f), &btnBrush);
                if (btnBrush)
                {
                    rt->FillRoundedRectangle(rr, btnBrush);
                    btnBrush->Release();
                }
                rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, isHovered ? 0.18f : 0.075f), &btnBrush);
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
            };

            drawStepButton(ix + 85, false, hovered && button == 1);
            drawStepButton(ix + 129, true, hovered && button == 2);
        };

        if (tfDefault)
        {
            ID2D1SolidColorBrush* tb = nullptr;
            rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextMuted().d2d, &tb);
            if (tb)
            {
                std::wstring label = L"唤醒触发方式";
                rt->DrawTextW(label.c_str(), (UINT32)label.size(), tfDefault,
                    D2D1::RectF(160, 82, 300, 102), tb);
                std::wstring currentLabel = L"当前：" + TriggerPresetLabel(m_owner->GetTriggerType());
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
            drawSegmentButton(
                TriggerButtonRect(i),
                radioLabels[i],
                i == currentTrigger,
                i == m_hoveredTrigger);
        }
        drawSegmentButton(TriggerButtonRect(TRIGGER_PRESET_BUTTON), L"其他预设", currentTrigger > 2, m_hoveredTrigger == TRIGGER_PRESET_BUTTON);

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
        std::wstring alignLabels[] = { L"鼠标居中", L"鼠标左上", L"屏幕居中", L"右下角" };
        DrawSelectionHighlight(rt, GetSelectionRect(m_popupAlignSelection, PopupAlignRect(alignMode)), 6.0f);
        for (int i = 0; i < 4; i++)
        {
            drawSegmentButton(
                PopupAlignRect(i),
                alignLabels[i],
                i == alignMode,
                i == m_hoveredPopupAlignMode);
        }

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
        drawSegmentButton(PopupBehaviorRect(0, 256.0f), L"自动关闭", autoClose, m_hoveredPopupAutoClose == 0);
        drawSegmentButton(PopupBehaviorRect(1, 256.0f), L"点击关闭", !autoClose, m_hoveredPopupAutoClose == 1);
        DrawSelectionHighlight(rt, GetSelectionRect(m_popupMultiOpenSelection,
            PopupBehaviorRect(!multiOpen ? 0 : 1, 296.0f)), 6.0f);
        drawSegmentButton(PopupBehaviorRect(0, 296.0f), L"固定时复用", !multiOpen, m_hoveredPopupMultiOpenWhenPinned == 0);
        drawSegmentButton(PopupBehaviorRect(1, 296.0f), L"固定时多开", multiOpen, m_hoveredPopupMultiOpenWhenPinned == 1);
        DrawSelectionHighlight(rt, GetSelectionRect(m_sortModeSelection,
            PopupBehaviorRect(sortMode == 0 ? 0 : 1, 336.0f)), 6.0f);
        drawSegmentButton(PopupBehaviorRect(0, 336.0f), L"自定义排序", sortMode == 0, m_hoveredSortMode == 0);
        drawSegmentButton(PopupBehaviorRect(1, 336.0f), L"智能排序", sortMode == 1, m_hoveredSortMode == 1);

        wchar_t delayBuf[32];
        swprintf_s(delayBuf, L"%dms", m_owner->GetHoverLeaveDelay());
        drawStepperCard(TwoColumnRect(0, 386.0f).left, 386.0f, L"消失延迟", delayBuf, m_hoveredHoverLeaveDelay, m_hoveredHoverLeaveDelayButton);

        wchar_t animBuf[32];
        swprintf_s(animBuf, L"%dms", m_owner->GetAnimationDuration());
        drawStepperCard(TwoColumnRect(1, 386.0f).left, 386.0f, L"动画时长", animBuf, m_hoveredAnimationDuration, m_hoveredAnimationDurationButton);
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
            const D2D1_RECT_F openConfigFileRect = TwoColumnRect(0, 226.0f);
            const D2D1_RECT_F openLogFileRect = TwoColumnRect(1, 226.0f);
            const D2D1_RECT_F backupRect = TwoColumnRect(0, 268.0f);
            const D2D1_RECT_F restoreRect = TwoColumnRect(1, 268.0f);
            const D2D1_RECT_F historyDirRect = TwoColumnRect(0, 310.0f);
            const D2D1_RECT_F importJsonRect = TwoColumnRect(1, 310.0f);
            const D2D1_RECT_F clearConfigRect = TwoColumnRect(0, 352.0f);
            const D2D1_RECT_F clearHistoryRect = TwoColumnRect(1, 352.0f);

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

            auto drawActionButton = [&](const D2D1_RECT_F& buttonRect, const std::wstring& text, bool hovered, bool danger)
            {
                D2D1_ROUNDED_RECT btnRect = D2D1::RoundedRect(buttonRect, 6.0f, 6.0f);
                ID2D1SolidColorBrush* btnBg = nullptr;
                D2D1_COLOR_F actionClr = danger ? UIStyle::ThemeColor::DangerRed().d2d : UIStyle::ThemeColor::Accent().d2d;
                D2D1_COLOR_F btnClr = hovered ? actionClr : baseClr;
                float btnAlpha = hovered ? (danger ? 0.16f : 0.12f) : 0.035f;
                rt->CreateSolidColorBrush(D2D1::ColorF(btnClr.r, btnClr.g, btnClr.b, btnAlpha), &btnBg);
                if (btnBg)
                {
                    rt->FillRoundedRectangle(btnRect, btnBg);
                    btnBg->Release();
                }

                ID2D1SolidColorBrush* btnBorder = nullptr;
                D2D1_COLOR_F borderClr = hovered ? actionClr : baseClr;
                float borderAlpha = hovered ? (danger ? 0.34f : 0.26f) : 0.07f;
                rt->CreateSolidColorBrush(D2D1::ColorF(borderClr.r, borderClr.g, borderClr.b, borderAlpha), &btnBorder);
                if (btnBorder)
                {
                    rt->DrawRoundedRectangle(btnRect, btnBorder, UIStyle::Metrics::ControlStroke());
                    btnBorder->Release();
                }

                if (tbNormal)
                {
                    DWRITE_TEXT_ALIGNMENT oldAlignment = tfDefault->GetTextAlignment();
                    tfDefault->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    rt->DrawTextW(text.c_str(), (UINT32)text.size(), tfDefault, buttonRect, tbNormal);
                    tfDefault->SetTextAlignment(oldAlignment);
                }
            };

            drawActionButton(openConfigFileRect, L"打开配置文件", m_hoveredOpenConfigFile, false);
            drawActionButton(openLogFileRect, L"打开日志文件", m_hoveredOpenLogFile, false);
            drawActionButton(backupRect, L"立即备份", m_hoveredCreateConfigBackup, false);
            drawActionButton(restoreRect, L"回滚最近历史", m_hoveredRestoreConfigBackup, false);
            drawActionButton(historyDirRect, L"打开历史目录", m_hoveredOpenConfigHistoryDir, false);
            drawActionButton(importJsonRect, L"导入 QuickLauncher", m_hoveredImportJson, false);
            drawActionButton(clearConfigRect, L"清除配置", m_hoveredClearConfig, true);
            drawActionButton(clearHistoryRect, L"清除历史", m_hoveredClearConfigHistory, true);

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
                rt->DrawTextW(desc.c_str(), (UINT32)desc.size(), tfDefault, D2D1::RectF(160, 160, CONTENT_RIGHT, 235), tbNormal);
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
        bool hHistoryDir = HitTestOpenConfigHistoryDir(pt);
        bool hBackup = HitTestCreateConfigBackup(pt);
        bool hRestore = HitTestRestoreConfigBackup(pt);
        bool hClearConfig = HitTestClearConfig(pt);
        bool hClearHistory = HitTestClearConfigHistory(pt);
        bool hImportJson = HitTestImportJson(pt);
        if (hConfigFile != m_hoveredOpenConfigFile ||
            hLogFile != m_hoveredOpenLogFile ||
            hConfigDirText != m_hoveredConfigDirText ||
            hHistoryDir != m_hoveredOpenConfigHistoryDir ||
            hBackup != m_hoveredCreateConfigBackup ||
            hRestore != m_hoveredRestoreConfigBackup ||
            hClearConfig != m_hoveredClearConfig ||
            hClearHistory != m_hoveredClearConfigHistory ||
            hImportJson != m_hoveredImportJson)
        {
            m_hoveredOpenConfigFile = hConfigFile;
            m_hoveredOpenLogFile = hLogFile;
            m_hoveredConfigDirText = hConfigDirText;
            m_hoveredOpenConfigHistoryDir = hHistoryDir;
            m_hoveredCreateConfigBackup = hBackup;
            m_hoveredRestoreConfigBackup = hRestore;
            m_hoveredClearConfig = hClearConfig;
            m_hoveredClearConfigHistory = hClearHistory;
            m_hoveredImportJson = hImportJson;
            repaint = true;
        }
    }
}

void SettingsPage::OnMouseLeave(bool& repaint)
{
    if (m_hoveredAutoStart || m_hoveredHideTrayIcon || m_hoveredOpenConfigFile || m_hoveredOpenLogFile || m_hoveredConfigDirText || m_hoveredOpenConfigHistoryDir || m_hoveredCreateConfigBackup || m_hoveredRestoreConfigBackup || m_hoveredClearConfig || m_hoveredClearConfigHistory || m_hoveredImportJson || m_hoveredTrigger != -1 || m_hoveredPopupAlignMode != -1 || m_hoveredPopupAutoClose != -1 || m_hoveredPopupMultiOpenWhenPinned != -1 || m_hoveredSortMode != -1 || m_hoveredHoverLeaveDelay || m_hoveredHoverLeaveDelayButton != 0 || m_hoveredTheme != -1 || m_hoveredThemeColor != -1 || m_hoveredWindowMode != -1 || m_hoveredAppearanceSetting != -1 || m_hoveredAppearanceButton != 0 || m_hoveredThemeDetailSetting != -1 || m_hoveredThemeDetailButton != 0 || m_hoveredAnimationToggle || m_hoveredHardwareAcceleration || m_hoveredAnimationDuration || m_hoveredAnimationDurationButton != 0 || m_hoveredGlobalScaleSlider || m_hoveredGlobalScaleApply || m_draggingGlobalScaleSlider)
    {
        m_hoveredAutoStart = false;
        m_hoveredHideTrayIcon = false;
        m_hoveredOpenConfigFile = false;
        m_hoveredOpenLogFile = false;
        m_hoveredConfigDirText = false;
        m_hoveredOpenConfigHistoryDir = false;
        m_hoveredCreateConfigBackup = false;
        m_hoveredRestoreConfigBackup = false;
        m_hoveredClearConfig = false;
        m_hoveredClearConfigHistory = false;
        m_hoveredImportJson = false;
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
            if (alignMode >= 0)
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
                        else if (m_hoveredAnimationDuration)
                        {
                            if (m_hoveredAnimationDurationButton == 1)
                            {
                                int current = m_owner->GetAnimationDuration();
                                if (current > 50) m_owner->SetAnimationDuration(current - 50);
                                repaint = true;
                            }
                            else if (m_hoveredAnimationDurationButton == 2)
                            {
                                int current = m_owner->GetAnimationDuration();
                                if (current < 1000) m_owner->SetAnimationDuration(current + 50);
                                repaint = true;
                            }
                        }
                    }
                }
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
        D2D1_RECT_F cardRect = TwoColumnRect(col, 90.0f + row * 42.0f);
        float ix = cardRect.left;
        float iy = 90.0f + row * 42.0f;
        float cy = iy + 16.0f;

        if (PointInRect(cardRect, pt))
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
    for (int i = 0; i <= TRIGGER_PRESET_BUTTON; i++)
    {
        D2D1_RECT_F rect = TriggerButtonRect(i);
        if (pt.x >= rect.left && pt.x <= rect.right && pt.y >= rect.top && pt.y <= rect.bottom)
        {
            return i;
        }
    }
    return -1;
}

int SettingsPage::HitTestPopupAlignMode(POINT pt)
{
    if (m_categoryIndex != 2) return -1;
    for (int i = 0; i < 4; i++)
    {
        if (PointInRect(PopupAlignRect(i), pt))
            return i;
    }
    return -1;
}

int SettingsPage::HitTestPopupAutoClose(POINT pt)
{
    if (m_categoryIndex != 2) return -1;
    if (pt.y >= 256.0f && pt.y <= 284.0f)
    {
        if (PointInRect(PopupBehaviorRect(0, 256.0f), pt)) return 0;
        if (PointInRect(PopupBehaviorRect(1, 256.0f), pt)) return 1;
    }
    return -1;
}

int SettingsPage::HitTestPopupMultiOpenWhenPinned(POINT pt)
{
    if (m_categoryIndex != 2) return -1;
    if (pt.y >= 296.0f && pt.y <= 324.0f)
    {
        if (PointInRect(PopupBehaviorRect(0, 296.0f), pt)) return 0;
        if (PointInRect(PopupBehaviorRect(1, 296.0f), pt)) return 1;
    }
    return -1;
}

int SettingsPage::HitTestSortMode(POINT pt)
{
    if (m_categoryIndex != 2) return -1;
    if (pt.y >= 336.0f && pt.y <= 364.0f)
    {
        if (PointInRect(PopupBehaviorRect(0, 336.0f), pt)) return 0;
        if (PointInRect(PopupBehaviorRect(1, 336.0f), pt)) return 1;
    }
    return -1;
}

bool SettingsPage::HitTestHoverLeaveDelay(POINT pt, int& buttonType)
{
    if (m_categoryIndex != 2) return false;
    D2D1_RECT_F cardRect = TwoColumnRect(0, 386.0f);
    float ix = cardRect.left;
    float iy = 386.0f;
    float cy = iy + 16.0f;

    if (PointInRect(cardRect, pt))
    {
        if (pt.x >= ix + 83 && pt.x <= ix + 103 && pt.y >= cy - 10 && pt.y <= cy + 10)
            buttonType = 1;
        else if (pt.x >= ix + 127 && pt.x <= ix + 147 && pt.y >= cy - 10 && pt.y <= cy + 10)
            buttonType = 2;
        else
            buttonType = 0;
        return true;
    }
    return false;
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
        for (int i = 0; i < 3; i++)
        {
            float xStart = 160.0f + i * 120.0f;
            if (pt.x >= xStart && pt.x <= xStart + 110.0f)
            {
                return i;
            }
        }
    }
    return -1;
}

bool SettingsPage::HitTestOpenConfigFile(POINT pt)
{
    if (m_categoryIndex != 3) return false;
    return PointInRect(TwoColumnRect(0, 226.0f), pt);
}

bool SettingsPage::HitTestOpenLogFile(POINT pt)
{
    if (m_categoryIndex != 3) return false;
    return PointInRect(TwoColumnRect(1, 226.0f), pt);
}

bool SettingsPage::HitTestConfigDirText(POINT pt)
{
    if (m_categoryIndex != 3) return false;
    return (pt.x >= CONTENT_LEFT + 20.0f && pt.x <= CONTENT_RIGHT - 20.0f && pt.y >= 112 && pt.y <= 146);
}

bool SettingsPage::HitTestOpenConfigHistoryDir(POINT pt)
{
    if (m_categoryIndex != 3) return false;
    return PointInRect(TwoColumnRect(0, 310.0f), pt);
}

bool SettingsPage::HitTestCreateConfigBackup(POINT pt)
{
    if (m_categoryIndex != 3) return false;
    return PointInRect(TwoColumnRect(0, 268.0f), pt);
}

bool SettingsPage::HitTestRestoreConfigBackup(POINT pt)
{
    if (m_categoryIndex != 3) return false;
    return PointInRect(TwoColumnRect(1, 268.0f), pt);
}

bool SettingsPage::HitTestClearConfig(POINT pt)
{
    if (m_categoryIndex != 3) return false;
    return PointInRect(TwoColumnRect(0, 352.0f), pt);
}

bool SettingsPage::HitTestClearConfigHistory(POINT pt)
{
    if (m_categoryIndex != 3) return false;
    return PointInRect(TwoColumnRect(1, 352.0f), pt);
}

bool SettingsPage::HitTestImportJson(POINT pt)
{
    if (m_categoryIndex != 3) return false;
    return PointInRect(TwoColumnRect(1, 310.0f), pt);
}

bool SettingsPage::HitTestThemeDetails(POINT pt, int& settingIdx, int& buttonType)
{
    if (m_categoryIndex != 0) return false;
    int currentWindowMode = m_owner->GetWindowMode();
    if (currentWindowMode != 0 && currentWindowMode != 1 && currentWindowMode != 2) return false;

    std::vector<int> activeIndices = { 1, 2, 3, 4, 5 };
    for (int i = 0; i < (int)activeIndices.size(); i++)
    {
        int col = i % 2;
        int row = i / 2;
        D2D1_RECT_F cardRect = TwoColumnRect(col, 360.0f + row * 38.0f);
        float ix = cardRect.left;
        float iy = 360.0f + row * 38.0f;
        float cy = iy + 16.0f;

        if (PointInRect(cardRect, pt))
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
    D2D1_RECT_F cardRect = TwoColumnRect(1, 386.0f);
    float ix = cardRect.left;
    float iy = 386.0f;
    float cy = iy + 16.0f;

    if (PointInRect(cardRect, pt))
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
