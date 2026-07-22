#include "SettingsPage.h"
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

namespace
{
    constexpr float CONTENT_LEFT = 160.0f;
    constexpr float CONTENT_RIGHT = 510.0f;
    constexpr float TWO_COLUMN_GAP = 10.0f;
    constexpr float TWO_COLUMN_WIDTH = (CONTENT_RIGHT - CONTENT_LEFT - TWO_COLUMN_GAP) / 2.0f;
    constexpr float TWO_COLUMN_STEP = TWO_COLUMN_WIDTH + TWO_COLUMN_GAP;
    constexpr float CARD_HEIGHT = 32.0f;

    constexpr float GLOBAL_SCALE_CARD_LEFT = 160.0f;
    constexpr float ANIMATION_DURATION_CARD_TOP = 112.0f;
    constexpr float ANIMATION_DURATION_TRACK_Y = 130.0f;
    constexpr float ANIMATION_DURATION_APPLY_TOP = 118.0f;
    constexpr float ANIMATION_DURATION_APPLY_BOTTOM = 142.0f;
    constexpr int ANIMATION_DURATION_MIN_MS = 50;
    constexpr int ANIMATION_DURATION_MAX_MS = 1000;
    constexpr int ANIMATION_DURATION_STEP_MS = 50;

    constexpr float GLOBAL_SCALE_CARD_TOP = 154.0f;
    constexpr float GLOBAL_SCALE_CARD_RIGHT = CONTENT_RIGHT;
    constexpr float GLOBAL_SCALE_CARD_BOTTOM = 190.0f;
    constexpr float GLOBAL_SCALE_TRACK_LEFT = 250.0f;
    constexpr float GLOBAL_SCALE_TRACK_RIGHT = 402.0f;
    constexpr float GLOBAL_SCALE_TRACK_Y = 172.0f;
    constexpr float GLOBAL_SCALE_APPLY_LEFT = 448.0f;
    constexpr float GLOBAL_SCALE_APPLY_TOP = 160.0f;
    constexpr float GLOBAL_SCALE_APPLY_RIGHT = 506.0f;
    constexpr float GLOBAL_SCALE_APPLY_BOTTOM = 184.0f;
    constexpr float SYSTEM_SETTINGS_CONTENT_OFFSET = 42.0f;

    constexpr float TRIGGER_TOP = 108.0f;
    constexpr float TRIGGER_BOTTOM = 136.0f;
    constexpr int TRIGGER_PRESET_BUTTON = 3;
    constexpr int POPUP_ALIGN_PRESET_BUTTON = 3;
    constexpr int POPUP_ALIGN_PRIMARY_COUNT = 3;
    constexpr int POPUP_ALIGN_PRESET_LAST = 10;
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

    D2D1_RECT_F TriggerBlacklistRect()
    {
        return D2D1::RectF(CONTENT_LEFT, 430.0f, CONTENT_RIGHT, 462.0f);
    }

    D2D1_RECT_F TriggerBlacklistEditRect()
    {
        return D2D1::RectF(438.0f, 436.0f, 502.0f, 456.0f);
    }

    D2D1_RECT_F AboutOpenSourceLinkRect()
    {
        return D2D1::RectF(CONTENT_LEFT, 412.0f, CONTENT_RIGHT, 442.0f);
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

    std::wstring PopupAlignPresetLabel(int mode)
    {
        switch (mode)
        {
        case 0: return L"鼠标居中";
        case 1: return L"鼠标左上";
        case 2: return L"屏幕居中";
        case 3: return L"屏幕右下";
        case 4: return L"屏幕中下";
        case 5: return L"屏幕左下";
        case 6: return L"屏幕左上";
        case 7: return L"屏幕中上";
        case 8: return L"屏幕右上";
        case 9: return L"屏幕中左";
        case 10: return L"屏幕中右";
        default: return L"未知预设";
        }
    }

    std::wstring ToLowerCopy(std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
            return (wchar_t)towlower(ch);
        });
        return value;
    }

    void TrimInPlace(std::wstring& value)
    {
        while (!value.empty() && iswspace(value.back()))
            value.pop_back();
        size_t start = 0;
        while (start < value.size() && iswspace(value[start]))
            ++start;
        if (start > 0)
            value.erase(0, start);
    }

    std::vector<std::wstring> ParseTriggerBlacklistInput(const std::wstring& input)
    {
        std::vector<std::wstring> result;
        std::wstring current;
        auto pushCurrent = [&]()
        {
            TrimInPlace(current);
            if (!current.empty())
            {
                std::wstring lower = ToLowerCopy(current);
                bool exists = false;
                for (const auto& item : result)
                {
                    if (ToLowerCopy(item) == lower)
                    {
                        exists = true;
                        break;
                    }
                }
                if (!exists)
                    result.push_back(current);
            }
            current.clear();
        };

        for (wchar_t ch : input)
        {
            if (ch == L';' || ch == L',' || ch == L'\xFF1B' || ch == L'\xFF0C' || ch == L'\x3001' || ch == L'\r' || ch == L'\n')
                pushCurrent();
            else
                current.push_back(ch);
        }
        pushCurrent();
        return result;
    }

    std::wstring JoinTriggerBlacklistInput(const std::vector<std::wstring>& items)
    {
        std::wstring result;
        for (const auto& item : items)
        {
            if (!result.empty())
                result += L"\r\n";
            result += item;
        }
        return result;
    }

    std::wstring TriggerBlacklistSummary(const std::vector<std::wstring>& items)
    {
        if (items.empty())
            return L"未设置";

        return L"已设置 " + std::to_wstring(items.size()) + L" 项";
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

void SettingsPage::ShowPopupAlignPresetMenu()
{
    if (!m_owner) return;

    HWND hwnd = m_owner->GetWindowHWND();
    if (!hwnd) return;

    std::vector<DropDownMenu::Item> items;
    const int currentMode = m_owner->GetPopupAlignMode();
    for (int mode = POPUP_ALIGN_PRESET_BUTTON; mode <= POPUP_ALIGN_PRESET_LAST; ++mode)
    {
        std::wstring label = PopupAlignPresetLabel(mode);
        if (mode == currentMode)
            label = L"当前：" + label;

        items.push_back(DropDownMenu::Item{
            label,
            [this, mode]()
            {
                if (!m_owner) return;
                if (m_owner->GetPopupAlignMode() != mode)
                    m_owner->SetPopupAlignMode(mode);
                m_owner->NotifyConfigChanged();
                HWND ownerHwnd = m_owner->GetWindowHWND();
                if (ownerHwnd)
                    InvalidateRect(ownerHwnd, nullptr, FALSE);
            },
            false
        });
    }

    D2D1_RECT_F presetRect = PopupAlignRect(POPUP_ALIGN_PRESET_BUTTON);
    POINT menuPt{ (int)presetRect.left, (int)(presetRect.bottom + 6.0f) };
    menuPt = DpiHelper::LogicalClientToScreen(hwnd, menuPt);
    DropDownMenu::Show(hwnd, menuPt, items, m_owner->GetAppContext(), presetRect.right - presetRect.left, true, 10.5f);
}

void SettingsPage::ShowTriggerBlacklistEditor()
{
    if (!m_owner) return;

    std::wstring input = JoinTriggerBlacklistInput(m_owner->GetTriggerBlacklist());
    const wchar_t* prompt = L"忽略大小写，支持模糊匹配。\n每行一个，也可用中英文逗号/分号分隔。";
    if (PromptWindow::ShowMultiline(
        m_owner->GetWindowHWND(),
        L"触发黑名单",
        prompt,
        input,
        input.c_str(),
        m_owner->GetAppContext()))
    {
        m_owner->SetTriggerBlacklist(ParseTriggerBlacklistInput(input));
        m_owner->NotifyConfigChanged();
        if (HWND hwnd = m_owner->GetWindowHWND())
            InvalidateRect(hwnd, nullptr, FALSE);
    }
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
        const int selectedPopupAlignButton =
            (alignMode >= 0 && alignMode < POPUP_ALIGN_PRIMARY_COUNT) ? alignMode : POPUP_ALIGN_PRESET_BUTTON;
        std::wstring alignLabels[] = { L"鼠标居中", L"鼠标左上", L"屏幕居中" };
        DrawSelectionHighlight(rt, GetSelectionRect(m_popupAlignSelection, PopupAlignRect(selectedPopupAlignButton)), 6.0f);
        for (int i = 0; i < POPUP_ALIGN_PRIMARY_COUNT; i++)
        {
            drawSegmentButton(
                PopupAlignRect(i),
                alignLabels[i],
                i == alignMode,
                i == m_hoveredPopupAlignMode);
        }
        drawSegmentButton(
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

        wchar_t selectionBuf[32];
        const int selectionValiditySeconds = m_owner->GetFileSelectionValiditySeconds();
        if (selectionValiditySeconds < 0)
            wcscpy_s(selectionBuf, L"无限");
        else
            swprintf_s(selectionBuf, L"%d秒", selectionValiditySeconds);
        drawStepperCard(TwoColumnRect(1, 386.0f).left, 386.0f, L"选中时限", selectionBuf, m_hoveredFileSelectionValidity, m_hoveredFileSelectionValidityButton);

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
                std::wstring summary = TriggerBlacklistSummary(m_owner->GetTriggerBlacklist());
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

            drawSegmentButton(editRect, L"编辑", false, m_hoveredTriggerBlacklist);
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

            drawActionButton(openLogFileRect, L"打开日志文件", m_hoveredOpenLogFile, false);
            drawActionButton(backupRect, L"立即备份", m_hoveredCreateConfigBackup, false);
            drawActionButton(restoreRect, L"回滚最近历史", m_hoveredRestoreConfigBackup, false);
            drawActionButton(historyDirRect, L"打开历史目录", m_hoveredOpenConfigHistoryDir, false);
            drawActionButton(diagnosticRect, L"生成诊断包", m_hoveredDiagnosticPackage, false);
            drawActionButton(exportMigrationRect, L"导出迁移备份", m_hoveredExportMigration, false);
            drawActionButton(importMigrationRect, L"导入迁移备份", m_hoveredImportMigration, false);
            drawActionButton(importJsonRect, L"导入 QuickLauncher", m_hoveredImportJson, false);
            drawActionButton(clearUsageRect, L"清除使用记录", m_hoveredClearUsageHistory, true);
            drawActionButton(clearCacheRect, L"清理缓存", m_hoveredClearCache, true);
            drawActionButton(clearConfigRect, L"清除配置", m_hoveredClearConfig, true);
            drawActionButton(clearHistoryRect, L"清除历史", m_hoveredClearConfigHistory, true);

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
            ID2D1SolidColorBrush* cardBg = nullptr;
            rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, 0.026f), &cardBg);
            ID2D1SolidColorBrush* cardBorder = nullptr;
            rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, 0.08f), &cardBorder);

            auto appCtx = m_owner ? m_owner->GetAppContext() : nullptr;
            auto plugins = (appCtx && appCtx->pluginManager) ? appCtx->pluginManager->GetPlugins() : std::vector<PluginInfo>{};

            D2D1_RECT_F rootRect = D2D1::RectF(160.0f, 82.0f, CONTENT_RIGHT, 132.0f);
            D2D1_ROUNDED_RECT rootRounded = D2D1::RoundedRect(rootRect, 6.0f, 6.0f);
            if (cardBg) rt->FillRoundedRectangle(rootRounded, cardBg);
            if (cardBorder) rt->DrawRoundedRectangle(rootRounded, cardBorder, UIStyle::Metrics::ControlStroke());

            if (tbNormal && tbMuted)
            {
                rt->DrawTextW(L"安装目录", 4, tfDefault, D2D1::RectF(172, 92, 250, 112), tbMuted);
                std::wstring dirText = ConfigPath::GetUserPluginInstalledDirectory();
                rt->DrawTextW(dirText.c_str(), (UINT32)dirText.size(), tfDefault, D2D1::RectF(172, 112, 340, 130), tbNormal);
                std::wstring dropHint = L"可将 .wlplugin 文件直接拖入此页面安装";
                rt->DrawTextW(dropHint.c_str(), (UINT32)dropHint.size(), tfDefault, D2D1::RectF(172, 134, CONTENT_RIGHT, 150), tbMuted);
            }

            auto drawSmallButton = [&](D2D1_RECT_F buttonRect, const wchar_t* text, bool hovered, bool accent)
            {
                D2D1_COLOR_F bg = accent ? UIStyle::ThemeColor::Accent().d2d : baseClr;
                bg.a = accent ? (hovered ? 0.28f : 0.18f) : (hovered ? 0.08f : 0.04f);
                ID2D1SolidColorBrush* bgBrush = nullptr;
                rt->CreateSolidColorBrush(bg, &bgBrush);
                if (bgBrush)
                {
                    rt->FillRoundedRectangle(D2D1::RoundedRect(buttonRect, 5.0f, 5.0f), bgBrush);
                    bgBrush->Release();
                }
                if (tbNormal)
                {
                    DWRITE_TEXT_ALIGNMENT old = tfDefault->GetTextAlignment();
                    tfDefault->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    rt->DrawTextW(text, (UINT32)wcslen(text), tfDefault, buttonRect, tbNormal);
                    tfDefault->SetTextAlignment(old);
                }
            };

            drawSmallButton(D2D1::RectF(348.0f, 96.0f, 396.0f, 122.0f), L"安装", m_hoveredPluginInstall, true);
            drawSmallButton(D2D1::RectF(402.0f, 96.0f, 450.0f, 122.0f), L"打开", m_hoveredPluginOpenDir, false);
            drawSmallButton(D2D1::RectF(456.0f, 96.0f, 504.0f, 122.0f), L"刷新", m_hoveredPluginRefresh, false);

            if (plugins.empty())
            {
                if (tbMuted)
                {
                    std::wstring emptyText = L"暂无已安装插件。将包含 plugin.json 和 DLL 的插件目录放入 installed 后刷新即可显示。";
                    rt->DrawTextW(emptyText.c_str(), (UINT32)emptyText.size(),
                        tfDefault, D2D1::RectF(160, 158, CONTENT_RIGHT, 210), tbMuted);
                }
            }
            else
            {
                size_t visibleCount = (std::min)(plugins.size(), (size_t)6);
                for (size_t i = 0; i < visibleCount; ++i)
                {
                    const auto& plugin = plugins[i];
                    float top = 152.0f + (float)i * 48.0f;
                    D2D1_RECT_F rowRect = D2D1::RectF(160.0f, top, CONTENT_RIGHT, top + 38.0f);
                    D2D1_ROUNDED_RECT rowRounded = D2D1::RoundedRect(rowRect, 6.0f, 6.0f);
                    bool hovered = ((int)i == m_hoveredPluginConfigure) || ((int)i == m_hoveredPluginToggle) || ((int)i == m_hoveredPluginUninstall);

                    ID2D1SolidColorBrush* rowBg = nullptr;
                    rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, hovered ? 0.06f : 0.022f), &rowBg);
                    if (rowBg)
                    {
                        rt->FillRoundedRectangle(rowRounded, rowBg);
                        rowBg->Release();
                    }
                    if (cardBorder) rt->DrawRoundedRectangle(rowRounded, cardBorder, UIStyle::Metrics::ControlStroke());

                    if (tbNormal && tbMuted)
                    {
                        std::wstring title = plugin.name + (plugin.version.empty() ? L"" : (L"  v" + plugin.version));
                        rt->DrawTextW(title.c_str(), (UINT32)title.size(), tfDefault, D2D1::RectF(172, top + 4, 340, top + 22), tbNormal);

                        std::wstring status = plugin.statusText;
                        if (!plugin.lastError.empty())
                            status += L" - " + plugin.lastError;
                        else if (!plugin.permissionSummary.empty())
                            status += L" - " + plugin.permissionSummary;
                        if (plugin.settingCount > 0)
                            status += L" - 配置项 " + std::to_wstring(plugin.settingCount);
                        if (status.empty())
                            status = plugin.enabled ? L"已启用" : L"已禁用";
                        rt->DrawTextW(status.c_str(), (UINT32)status.size(), tfDefault, D2D1::RectF(172, top + 22, 340, top + 39), tbMuted);
                    }

                    if (plugin.settingCount > 0)
                    {
                        D2D1_RECT_F configRect = D2D1::RectF(346.0f, top + 8.0f, 392.0f, top + 30.0f);
                        D2D1_COLOR_F configBg = baseClr;
                        configBg.a = ((int)i == m_hoveredPluginConfigure) ? 0.08f : 0.04f;
                        ID2D1SolidColorBrush* configBgBrush = nullptr;
                        rt->CreateSolidColorBrush(configBg, &configBgBrush);
                        if (configBgBrush)
                        {
                            rt->FillRoundedRectangle(D2D1::RoundedRect(configRect, 5.0f, 5.0f), configBgBrush);
                            configBgBrush->Release();
                        }
                        if (tbNormal)
                        {
                            DWRITE_TEXT_ALIGNMENT old = tfDefault->GetTextAlignment();
                            tfDefault->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                            rt->DrawTextW(L"配置", 2, tfDefault, configRect, tbNormal);
                            tfDefault->SetTextAlignment(old);
                        }
                    }

                    D2D1_RECT_F toggleRect = D2D1::RectF(398.0f, top + 8.0f, 450.0f, top + 30.0f);
                    D2D1_COLOR_F toggleBg = plugin.enabled ? UIStyle::ThemeColor::Accent().d2d : baseClr;
                    toggleBg.a = plugin.enabled ? (((int)i == m_hoveredPluginToggle) ? 0.32f : 0.22f) : (((int)i == m_hoveredPluginToggle) ? 0.08f : 0.04f);
                    ID2D1SolidColorBrush* toggleBgBrush = nullptr;
                    rt->CreateSolidColorBrush(toggleBg, &toggleBgBrush);
                    if (toggleBgBrush)
                    {
                        rt->FillRoundedRectangle(D2D1::RoundedRect(toggleRect, 5.0f, 5.0f), toggleBgBrush);
                        toggleBgBrush->Release();
                    }
                    if (tbNormal)
                    {
                        const wchar_t* label = plugin.enabled ? L"禁用" : L"启用";
                        DWRITE_TEXT_ALIGNMENT old = tfDefault->GetTextAlignment();
                        tfDefault->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                        rt->DrawTextW(label, 2, tfDefault, toggleRect, tbNormal);
                        tfDefault->SetTextAlignment(old);
                    }

                    D2D1_RECT_F uninstallRect = D2D1::RectF(456.0f, top + 8.0f, 502.0f, top + 30.0f);
                    D2D1_COLOR_F removeBg = UIStyle::ThemeColor::DangerRed().d2d;
                    removeBg.a = ((int)i == m_hoveredPluginUninstall) ? 0.24f : 0.12f;
                    ID2D1SolidColorBrush* removeBgBrush = nullptr;
                    rt->CreateSolidColorBrush(removeBg, &removeBgBrush);
                    if (removeBgBrush)
                    {
                        rt->FillRoundedRectangle(D2D1::RoundedRect(uninstallRect, 5.0f, 5.0f), removeBgBrush);
                        removeBgBrush->Release();
                    }
                    if (tbNormal)
                    {
                        DWRITE_TEXT_ALIGNMENT old = tfDefault->GetTextAlignment();
                        tfDefault->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                        rt->DrawTextW(L"卸载", 2, tfDefault, uninstallRect, tbNormal);
                        tfDefault->SetTextAlignment(old);
                    }
                }
            }

            if (tbNormal) tbNormal->Release();
            if (tbMuted) tbMuted->Release();
            if (cardBg) cardBg->Release();
            if (cardBorder) cardBorder->Release();
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

                auto drawInfoCard = [&](const D2D1_RECT_F& cardRect, const wchar_t* title, const wchar_t* body)
                {
                    D2D1_ROUNDED_RECT roundedCard = D2D1::RoundedRect(cardRect, 6.0f, 6.0f);
                    ID2D1SolidColorBrush* cardBrush = nullptr;
                    rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, 0.025f), &cardBrush);
                    if (cardBrush)
                    {
                        rt->FillRoundedRectangle(roundedCard, cardBrush);
                        cardBrush->Release();
                    }
                    rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, 0.065f), &cardBrush);
                    if (cardBrush)
                    {
                        rt->DrawRoundedRectangle(roundedCard, cardBrush, UIStyle::Metrics::ControlStroke());
                        cardBrush->Release();
                    }
                    rt->DrawTextW(title, (UINT32)wcslen(title), tfDefault,
                        D2D1::RectF(cardRect.left + 10.0f, cardRect.top + 6.0f, cardRect.right - 10.0f, cardRect.top + 24.0f), tbNormal);
                    rt->DrawTextW(body, (UINT32)wcslen(body), tfDefault,
                        D2D1::RectF(cardRect.left + 10.0f, cardRect.top + 24.0f, cardRect.right - 10.0f, cardRect.bottom - 5.0f), tbMuted);
                };

                drawInfoCard(TwoColumnRect(0, 164.0f, 68.0f), L"快速启动", L"通过鼠标手势或快捷键\n在光标处唤出快捷方式面板");
                drawInfoCard(TwoColumnRect(1, 164.0f, 68.0f), L"搜索与分类", L"分页管理常用项目，支持\n即时搜索、智能排序与场景筛选");
                drawInfoCard(TwoColumnRect(0, 242.0f, 68.0f), L"命令与自动化", L"运行自定义命令、批量启动与宏；\n可使用已选文件作为命令输入");
                drawInfoCard(TwoColumnRect(1, 242.0f, 68.0f), L"外观与扩展", L"可调主题、材质、布局与动画；\n支持 DLL 插件、/ 命令与搜索源");
                drawInfoCard(D2D1::RectF(CONTENT_LEFT, 320.0f, CONTENT_RIGHT, 400.0f), L"本地优先与诊断", L"配置、使用记录、日志和崩溃诊断均保留在本机，不会自动上传。\n可在“配置管理”中生成脱敏诊断包、创建备份或迁移到新设备。");

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
                SetCapture(hwnd);
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
        auto appCtx = m_owner ? m_owner->GetAppContext() : nullptr;
        if (!appCtx || !appCtx->pluginManager)
            return;

        HWND hwnd = m_owner ? m_owner->GetWindowHWND() : nullptr;
        if (HitTestPluginInstall(pt))
        {
            wchar_t filePath[MAX_PATH]{};
            OPENFILENAMEW ofn{};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hwnd;
            ofn.lpstrFilter = L"WinLauncher 插件包 (*.wlplugin)\0*.wlplugin\0ZIP 包 (*.zip)\0*.zip\0所有文件\0*.*\0";
            ofn.lpstrFile = filePath;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            ofn.lpstrTitle = L"安装插件包";
            if (GetOpenFileNameW(&ofn))
            {
                InstallPluginPackageFromPath(filePath, false);
                repaint = true;
            }
        }
        else if (HitTestPluginOpenDir(pt))
        {
            ConfigPath::PrepareUserPluginInstalledDirectory();
            ShellExecuteW(hwnd, L"open", ConfigPath::GetUserPluginInstalledDirectory().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            repaint = true;
        }
        else if (HitTestPluginRefresh(pt))
        {
            appCtx->pluginManager->Rescan();
            repaint = true;
        }
        else
        {
            auto plugins = appCtx->pluginManager->GetPlugins();
            int configIdx = HitTestPluginConfigure(pt);
            if (configIdx >= 0 && configIdx < (int)plugins.size())
            {
                const auto& plugin = plugins[configIdx];
                auto settings = appCtx->pluginManager->GetPluginSettings(plugin.id);
                if (settings.empty())
                {
                    ConfirmWindow::Show(hwnd, L"插件配置", L"该插件没有声明可编辑配置项。", appCtx, false);
                    return;
                }

                PluginSettingInfo setting = settings.front();
                if (settings.size() > 1)
                {
                    std::vector<std::wstring> options;
                    options.reserve(settings.size());
                    for (const auto& item : settings)
                        options.push_back(item.title + L" (" + item.key + L")");

                    std::wstring selected;
                    if (!PromptWindow::ShowChoose(hwnd, L"插件配置", L"选择要编辑的配置项:", options, selected, appCtx))
                        return;

                    for (size_t i = 0; i < options.size(); ++i)
                    {
                        if (options[i] == selected)
                        {
                            setting = settings[i];
                            break;
                        }
                    }
                }

                std::wstring newValue = setting.currentValue.empty() ? setting.defaultValue : setting.currentValue;
                bool accepted = false;
                if (setting.type == L"boolean")
                {
                    std::wstring selected;
                    std::vector<std::wstring> booleanOptions = { L"true", L"false" };
                    accepted = PromptWindow::ShowChoose(hwnd, setting.title.c_str(), L"选择配置值:", booleanOptions, selected, appCtx);
                    if (accepted)
                        newValue = selected;
                }
                else
                {
                    std::wstring prompt = setting.title + L"\r\nKey: " + setting.key;
                    if (setting.type == L"integer")
                    {
                        if (setting.hasMin)
                            prompt += L"\r\nMin: " + std::to_wstring(setting.minValue);
                        if (setting.hasMax)
                            prompt += L"\r\nMax: " + std::to_wstring(setting.maxValue);
                    }
                    accepted = PromptWindow::Show(hwnd, L"插件配置", prompt.c_str(), newValue, newValue.c_str(), appCtx);
                }

                if (accepted)
                {
                    if (!appCtx->pluginManager->SetPluginSettingValue(plugin.id, setting.key, newValue))
                    {
                        ConfirmWindow::Show(hwnd, L"插件配置失败", L"配置值无效或无法写入插件私有配置。", appCtx, false);
                    }
                    repaint = true;
                }
            }
            else
            {
                int uninstallIdx = HitTestPluginUninstall(pt);
                if (uninstallIdx >= 0 && uninstallIdx < (int)plugins.size())
                {
                    const auto& plugin = plugins[uninstallIdx];
                    std::wstring prompt = L"确定要卸载插件 \"" + plugin.name + L"\" 吗？";
                    if (ConfirmWindow::Show(hwnd, L"卸载插件", prompt.c_str(), appCtx, true))
                    {
                        std::wstring message;
                        if (!appCtx->pluginManager->UninstallPlugin(plugin.id, message))
                        {
                            ConfirmWindow::Show(hwnd, L"插件卸载失败", message.c_str(), appCtx, false);
                        }
                        repaint = true;
                    }
                }
                else
                {
                    int idx = HitTestPluginToggle(pt);
                    if (idx >= 0 && idx < (int)plugins.size())
                    {
                        std::wstring message;
                        bool ok = appCtx->pluginManager->SetPluginEnabled(plugins[idx].id, !plugins[idx].enabled);
                        if (!ok)
                        {
                            message = L"插件状态切换失败，请查看插件错误状态和日志。";
                            ConfirmWindow::Show(hwnd, L"插件操作失败", message.c_str(), appCtx, false);
                        }
                        repaint = true;
                    }
                }
            }
        }
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
        ReleaseCapture();
        repaint = true;
    }
    else if (m_draggingGlobalScaleSlider)
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

void SettingsPage::OnDropFiles(HDROP hDrop, bool& repaint)
{
    if (m_categoryIndex != 4 || !hDrop)
        return;

    auto appCtx = m_owner ? m_owner->GetAppContext() : nullptr;
    if (!appCtx || !appCtx->pluginManager)
        return;

    UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
    int installedCount = 0;
    int skippedCount = 0;
    std::wstring failedMessages;

    for (UINT i = 0; i < fileCount; ++i)
    {
        wchar_t filePath[MAX_PATH]{};
        if (!DragQueryFileW(hDrop, i, filePath, MAX_PATH))
            continue;

        if (!IsPluginPackagePath(filePath))
        {
            skippedCount++;
            continue;
        }

        std::wstring errorMessage;
        if (InstallPluginPackageFromPath(filePath, false, &errorMessage))
        {
            installedCount++;
        }
        else
        {
            if (!failedMessages.empty())
                failedMessages += L"\r\n";
            failedMessages += filePath;
            if (!errorMessage.empty())
                failedMessages += L": " + errorMessage;
        }
    }

    HWND hwnd = m_owner ? m_owner->GetWindowHWND() : nullptr;
    if (!failedMessages.empty())
    {
        std::wstring message = L"以下插件包安装失败，请检查插件包格式或错误日志:\r\n" + failedMessages;
        ConfirmWindow::Show(hwnd, L"插件安装失败", message.c_str(), appCtx, false);
    }
    else if (installedCount > 0)
    {
        std::wstring message = installedCount == 1
            ? L"插件已安装。"
            : (L"已安装 " + std::to_wstring(installedCount) + L" 个插件。");
        if (skippedCount > 0)
            message += L"\r\n已忽略非插件包文件。";
        ConfirmWindow::Show(hwnd, L"插件安装完成", message.c_str(), appCtx, false);
    }
    else if (skippedCount > 0)
    {
        ConfirmWindow::Show(hwnd, L"未找到插件包", L"请拖入 .wlplugin 插件包文件。", appCtx, false);
    }

    if (installedCount > 0)
        repaint = true;
}

bool SettingsPage::InstallPluginPackageFromPath(const std::wstring& filePath, bool showSuccessMessage, std::wstring* errorMessage)
{
    auto appCtx = m_owner ? m_owner->GetAppContext() : nullptr;
    if (!appCtx || !appCtx->pluginManager)
        return false;

    HWND hwnd = m_owner ? m_owner->GetWindowHWND() : nullptr;
    std::wstring message;
    if (!appCtx->pluginManager->InstallPackage(filePath, message))
    {
        if (errorMessage)
        {
            *errorMessage = message;
        }
        else
        {
            ConfirmWindow::Show(hwnd, L"插件安装失败", message.c_str(), appCtx, false);
        }
        return false;
    }

    if (showSuccessMessage)
        ConfirmWindow::Show(hwnd, L"插件安装完成", message.c_str(), appCtx, false);
    return true;
}

bool SettingsPage::IsPluginPackagePath(const std::wstring& filePath) const
{
    size_t dot = filePath.find_last_of(L'.');
    if (dot == std::wstring::npos)
        return false;
    std::wstring ext = ToLowerCopy(filePath.substr(dot));
    return ext == L".wlplugin" || ext == L".zip";
}

bool SettingsPage::HitTestAppearance(POINT pt, int& settingIdx, int& buttonType)
{
    if (m_categoryIndex != 1) return false;
    for (int i = 0; i < 9; i++)
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
    for (int i = 0; i <= POPUP_ALIGN_PRESET_BUTTON; i++)
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

bool SettingsPage::HitTestTriggerBlacklist(POINT pt)
{
    if (m_categoryIndex != 2) return false;
    return PointInRect(TriggerBlacklistRect(), pt);
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
    if (pt.y >= 180.0f + SYSTEM_SETTINGS_CONTENT_OFFSET && pt.y <= 212.0f + SYSTEM_SETTINGS_CONTENT_OFFSET)
    {
        if (pt.x >= 160.0f && pt.x <= 325.0f) return 0; // Dark
        if (pt.x >= 345.0f && pt.x <= 510.0f) return 1; // Light
    }
    return -1;
}

int SettingsPage::HitTestThemeColor(POINT pt)
{
    if (m_categoryIndex != 0) return -1;
    if (pt.y < 241.0f + SYSTEM_SETTINGS_CONTENT_OFFSET || pt.y > 265.0f + SYSTEM_SETTINGS_CONTENT_OFFSET) return -1;

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
    if (pt.y >= 298.0f + SYSTEM_SETTINGS_CONTENT_OFFSET && pt.y <= 326.0f + SYSTEM_SETTINGS_CONTENT_OFFSET)
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

bool SettingsPage::HitTestOpenLogFile(POINT pt)
{
    if (m_categoryIndex != 3) return false;
    return PointInRect(TwoColumnRect(0, 226.0f), pt);
}

bool SettingsPage::HitTestConfigDirText(POINT pt)
{
    if (m_categoryIndex != 3) return false;
    return (pt.x >= CONTENT_LEFT + 20.0f && pt.x <= CONTENT_RIGHT - 20.0f && pt.y >= 112 && pt.y <= 146);
}

bool SettingsPage::HitTestOpenConfigHistoryDir(POINT pt)
{
    if (m_categoryIndex != 3) return false;
    return PointInRect(TwoColumnRect(1, 268.0f), pt);
}

bool SettingsPage::HitTestCreateConfigBackup(POINT pt)
{
    if (m_categoryIndex != 3) return false;
    return PointInRect(TwoColumnRect(1, 226.0f), pt);
}

bool SettingsPage::HitTestRestoreConfigBackup(POINT pt)
{
    if (m_categoryIndex != 3) return false;
    return PointInRect(TwoColumnRect(0, 268.0f), pt);
}

bool SettingsPage::HitTestClearConfig(POINT pt)
{
    if (m_categoryIndex != 3) return false;
    return PointInRect(TwoColumnRect(0, 436.0f), pt);
}

bool SettingsPage::HitTestClearConfigHistory(POINT pt)
{
    if (m_categoryIndex != 3) return false;
    return PointInRect(TwoColumnRect(1, 436.0f), pt);
}

bool SettingsPage::HitTestImportJson(POINT pt)
{
    if (m_categoryIndex != 3) return false;
    return PointInRect(TwoColumnRect(1, 352.0f), pt);
}

bool SettingsPage::HitTestDiagnosticPackage(POINT pt) { return m_categoryIndex == 3 && PointInRect(TwoColumnRect(0, 310.0f), pt); }
bool SettingsPage::HitTestExportMigration(POINT pt) { return m_categoryIndex == 3 && PointInRect(TwoColumnRect(1, 310.0f), pt); }
bool SettingsPage::HitTestImportMigration(POINT pt) { return m_categoryIndex == 3 && PointInRect(TwoColumnRect(0, 352.0f), pt); }
bool SettingsPage::HitTestClearUsageHistory(POINT pt) { return m_categoryIndex == 3 && PointInRect(TwoColumnRect(0, 394.0f), pt); }
bool SettingsPage::HitTestClearCache(POINT pt) { return m_categoryIndex == 3 && PointInRect(TwoColumnRect(1, 394.0f), pt); }

bool SettingsPage::HitTestOpenSourceUrl(POINT pt)
{
    return m_categoryIndex == 5 && PointInRect(AboutOpenSourceLinkRect(), pt);
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
        D2D1_RECT_F cardRect = TwoColumnRect(col, 360.0f + SYSTEM_SETTINGS_CONTENT_OFFSET + row * 38.0f);
        float ix = cardRect.left;
        float iy = 360.0f + SYSTEM_SETTINGS_CONTENT_OFFSET + row * 38.0f;
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

bool SettingsPage::HitTestFileSelectionValidity(POINT pt, int& buttonType)
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

bool SettingsPage::HitTestAnimationDurationSlider(POINT pt)
{
    if (m_categoryIndex != 0) return false;
    return pt.x >= (int)(GLOBAL_SCALE_TRACK_LEFT - 8.0f) &&
        pt.x <= (int)(GLOBAL_SCALE_TRACK_RIGHT + 8.0f) &&
        pt.y >= (int)(ANIMATION_DURATION_TRACK_Y - 12.0f) &&
        pt.y <= (int)(ANIMATION_DURATION_TRACK_Y + 12.0f);
}

bool SettingsPage::HitTestAnimationDurationApply(POINT pt)
{
    if (m_categoryIndex != 0) return false;
    return pt.x >= (int)GLOBAL_SCALE_APPLY_LEFT &&
        pt.x <= (int)GLOBAL_SCALE_APPLY_RIGHT &&
        pt.y >= (int)ANIMATION_DURATION_APPLY_TOP &&
        pt.y <= (int)ANIMATION_DURATION_APPLY_BOTTOM;
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

bool SettingsPage::HitTestPluginInstall(POINT pt)
{
    if (m_categoryIndex != 4) return false;
    return pt.x >= 348 && pt.x <= 396 && pt.y >= 96 && pt.y <= 122;
}

bool SettingsPage::HitTestPluginOpenDir(POINT pt)
{
    if (m_categoryIndex != 4) return false;
    return pt.x >= 402 && pt.x <= 450 && pt.y >= 96 && pt.y <= 122;
}

bool SettingsPage::HitTestPluginRefresh(POINT pt)
{
    if (m_categoryIndex != 4) return false;
    return pt.x >= 456 && pt.x <= 504 && pt.y >= 96 && pt.y <= 122;
}

int SettingsPage::HitTestPluginConfigure(POINT pt)
{
    if (m_categoryIndex != 4) return -1;
    if (pt.x < 346 || pt.x > 392) return -1;
    for (int i = 0; i < 6; ++i)
    {
        float top = 152.0f + (float)i * 48.0f;
        if (pt.y >= (int)(top + 8.0f) && pt.y <= (int)(top + 30.0f))
            return i;
    }
    return -1;
}

int SettingsPage::HitTestPluginToggle(POINT pt)
{
    if (m_categoryIndex != 4) return -1;
    if (pt.x < 398 || pt.x > 450) return -1;
    for (int i = 0; i < 6; ++i)
    {
        float top = 152.0f + (float)i * 48.0f;
        if (pt.y >= (int)(top + 8.0f) && pt.y <= (int)(top + 30.0f))
            return i;
    }
    return -1;
}

int SettingsPage::HitTestPluginUninstall(POINT pt)
{
    if (m_categoryIndex != 4) return -1;
    if (pt.x < 456 || pt.x > 502) return -1;
    for (int i = 0; i < 6; ++i)
    {
        float top = 152.0f + (float)i * 48.0f;
        if (pt.y >= (int)(top + 8.0f) && pt.y <= (int)(top + 30.0f))
            return i;
    }
    return -1;
}
