#define NOMINMAX
#include "ConfigWindow.h"
#include "WaitWindow.h"
#include "../DpiHelper.h"
#include "../MouseHook.h"
#include "UIStyle.h"

size_t ConfigWindow::GetCategoryCount()
{
    if (m_showSettings)
    {
        return 6;
    }
    return m_pages.size();
}

std::wstring ConfigWindow::GetCategoryName(size_t index)
{
    if (m_showSettings)
    {
        if (index == 0) return L"系统设置";
        if (index == 1) return L"弹窗外观";
        if (index == 2) return L"弹窗交互";
        if (index == 3) return L"插件管理";
        if (index == 4) return L"配置管理";
        if (index == 5) return L"关于软件";
        return L"";
    }
    if (index >= m_pages.size()) return L"";
    return m_pages[index].name;
}

int ConfigWindow::GetCurrentCategoryIndex()
{
    if (m_showSettings)
    {
        return m_currentSettingsCategory;
    }
    return m_currentCategory;
}

void ConfigWindow::SetCurrentCategoryIndex(int index)
{
    if (m_showSettings)
    {
        if (index >= 0 && index < 6)
        {
            m_currentSettingsCategory = index;
            m_settingsPage.SetCategory(index == 3 ? 4 : (index == 4 ? 3 : index));
        }
    }
    else
    {
        if (index >= 0 && index < (int)m_pages.size())
        {
            m_currentCategory = index;
            m_shortcutPage.SetPageData(&m_pages[m_currentCategory]);
        }
    }
}

int ConfigWindow::GetTriggerType()
{
    if (m_appCtx && m_appCtx->configService)
    {
        return m_appCtx->configService->GetTriggerType();
    }
    return 0;
}

void ConfigWindow::SetTriggerType(int type)
{
    if (m_appCtx && m_appCtx->configService)
    {
        m_appCtx->configService->SetTriggerType(type);
        MouseHook::SetTriggerType(type);
    }
}

std::vector<std::wstring> ConfigWindow::GetTriggerBlacklist()
{
    if (m_appCtx && m_appCtx->configService)
    {
        return m_appCtx->configService->GetTriggerBlacklist();
    }
    return {};
}

void ConfigWindow::SetTriggerBlacklist(const std::vector<std::wstring>& processNames)
{
    if (m_appCtx && m_appCtx->configService)
    {
        m_appCtx->configService->SetTriggerBlacklist(processNames);
        if (m_appCtx->triggerProcessResolver)
            m_appCtx->triggerProcessResolver->SetBlacklist(processNames);
    }
}

bool ConfigWindow::GetAutoStart()
{
    if (m_appCtx && m_appCtx->configService)
    {
        return m_appCtx->configService->GetAutoStart();
    }
    return false;
}

void ConfigWindow::SetAutoStart(bool enable)
{
    if (m_appCtx && m_appCtx->configService)
    {
        HWND parent = GetHWND();
        const wchar_t* prompt = enable ? L"正在开启开机自启，请稍候..." : L"正在关闭开机自启，请稍候...";
        WaitWindow::Show(parent, L"请稍候", prompt, [this, enable]() {
            m_appCtx->configService->SetAutoStart(enable);
        }, m_appCtx);
    }
}

bool ConfigWindow::GetHideTrayIcon()
{
    if (m_appCtx && m_appCtx->configService)
    {
        return m_appCtx->configService->GetHideTrayIcon();
    }
    return false;
}

void ConfigWindow::SetHideTrayIcon(bool hide)
{
    if (m_appCtx && m_appCtx->configService)
    {
        m_appCtx->configService->SetHideTrayIcon(hide);
        NotifyConfigChanged();
    }
}

bool ConfigWindow::GetAutoUpdate()
{
    if (m_appCtx && m_appCtx->configService)
    {
        return m_appCtx->configService->GetAutoUpdate();
    }
    return true;
}

void ConfigWindow::SetAutoUpdate(bool enable)
{
    if (m_appCtx && m_appCtx->configService)
    {
        m_appCtx->configService->SetAutoUpdate(enable);
    }
}

bool ConfigWindow::GetHardwareAccelerationEnabled()
{
    if (m_appCtx && m_appCtx->configService)
    {
        return m_appCtx->configService->GetHardwareAccelerationEnabled();
    }
    return true;
}

void ConfigWindow::SetHardwareAccelerationEnabled(bool enabled)
{
    if (m_appCtx && m_appCtx->configService)
    {
        m_appCtx->configService->SetHardwareAccelerationEnabled(enabled);
        UIStyle::Performance::SetHardwareAccelerationEnabled(enabled);
        UIStyle::Performance::ApplyProcessPolicy();
        if (m_appCtx->eventBus)
        {
            m_appCtx->eventBus->Publish(EventType::ThemeChanged);
        }
    }
}

int ConfigWindow::GetPopupColumns()
{
    if (m_appCtx && m_appCtx->configService) return m_appCtx->configService->GetPopupColumns();
    return 6;
}

void ConfigWindow::SetPopupColumns(int columns)
{
    if (m_appCtx && m_appCtx->configService) m_appCtx->configService->SetPopupColumns(columns);
}

int ConfigWindow::GetPopupRows()
{
    if (m_appCtx && m_appCtx->configService) return m_appCtx->configService->GetPopupRows();
    return 4;
}

void ConfigWindow::SetPopupRows(int rows)
{
    if (m_appCtx && m_appCtx->configService) m_appCtx->configService->SetPopupRows(rows);
}

int ConfigWindow::GetPopupIconSize()
{
    if (m_appCtx && m_appCtx->configService) return m_appCtx->configService->GetPopupIconSize();
    return 24;
}

void ConfigWindow::SetPopupIconSize(int size)
{
    if (m_appCtx && m_appCtx->configService) m_appCtx->configService->SetPopupIconSize(size);
}

int ConfigWindow::GetPopupIconLabelFontSize()
{
    if (m_appCtx && m_appCtx->configService) return m_appCtx->configService->GetPopupIconLabelFontSize();
    return 9;
}

void ConfigWindow::SetPopupIconLabelFontSize(int size)
{
    if (m_appCtx && m_appCtx->configService) m_appCtx->configService->SetPopupIconLabelFontSize(size);
}

int ConfigWindow::GetPopupHeaderSizeLevel()
{
    if (m_appCtx && m_appCtx->configService) return m_appCtx->configService->GetPopupHeaderSizeLevel();
    return 5;
}

void ConfigWindow::SetPopupHeaderSizeLevel(int level)
{
    if (m_appCtx && m_appCtx->configService) m_appCtx->configService->SetPopupHeaderSizeLevel(level);
}

int ConfigWindow::GetPopupIconGap()
{
    if (m_appCtx && m_appCtx->configService) return m_appCtx->configService->GetPopupIconGap();
    return 4;
}

void ConfigWindow::SetPopupIconGap(int gap)
{
    if (m_appCtx && m_appCtx->configService) m_appCtx->configService->SetPopupIconGap(gap);
}

int ConfigWindow::GetPopupIconRadius()
{
    if (m_appCtx && m_appCtx->configService) return m_appCtx->configService->GetPopupIconRadius();
    return 6;
}

void ConfigWindow::SetPopupIconRadius(int radius)
{
    if (m_appCtx && m_appCtx->configService) m_appCtx->configService->SetPopupIconRadius(radius);
}

int ConfigWindow::GetPopupWndPadding()
{
    if (m_appCtx && m_appCtx->configService) return m_appCtx->configService->GetPopupWndPadding();
    return 8;
}

void ConfigWindow::SetPopupWndPadding(int padding)
{
    if (m_appCtx && m_appCtx->configService) m_appCtx->configService->SetPopupWndPadding(padding);
}

int ConfigWindow::GetTheme()
{
    if (m_appCtx && m_appCtx->configService) return m_appCtx->configService->GetTheme();
    return 0;
}

void ConfigWindow::SetTheme(int theme, POINT clickPt)
{
    if (m_appCtx && m_appCtx->configService)
    {
        UIStyle::ThemeMode oldThemeMode = UIStyle::GetThemeMode();
        int oldThemeColor = UIStyle::GetThemeColorIndex();
        int oldWindowMode = UIStyle::GetWindowMode();
        if (UIStyle::Animation::IsEnabled())
        {
            UIStyle::ThemeTransition::Begin(oldThemeMode, oldThemeColor, oldWindowMode);
            StartThemeTransition(clickPt);
        }

        m_appCtx->configService->SetTheme(theme);
        UIStyle::SetThemeMode((UIStyle::ThemeMode)theme);
        PersistAppearanceConfig();
        if (m_appCtx->eventBus)
        {
            m_appCtx->eventBus->Publish(EventType::ThemeChanged);
        }
    }
}

int ConfigWindow::GetThemeColor()
{
    if (m_appCtx && m_appCtx->configService) return m_appCtx->configService->GetThemeColor();
    return 0;
}

void ConfigWindow::SetThemeColor(int colorIndex, POINT clickPt)
{
    if (m_appCtx && m_appCtx->configService)
    {
        UIStyle::ThemeMode oldThemeMode = UIStyle::GetThemeMode();
        int oldThemeColor = UIStyle::GetThemeColorIndex();
        int oldWindowMode = UIStyle::GetWindowMode();
        if (UIStyle::Animation::IsEnabled())
        {
            UIStyle::ThemeTransition::Begin(oldThemeMode, oldThemeColor, oldWindowMode);
            StartThemeTransition(clickPt);
        }

        m_appCtx->configService->SetThemeColor(colorIndex);
        UIStyle::SetThemeColorIndex(m_appCtx->configService->GetThemeColor());
        PersistAppearanceConfig();
        if (m_appCtx->eventBus)
        {
            m_appCtx->eventBus->Publish(EventType::ThemeChanged);
        }
    }
}

int ConfigWindow::GetWindowMode()
{
    if (m_appCtx && m_appCtx->configService) return m_appCtx->configService->GetWindowMode();
    return 0;
}

void ConfigWindow::SetWindowMode(int mode, POINT clickPt)
{
    if (m_appCtx && m_appCtx->configService)
    {
        UIStyle::ThemeMode oldThemeMode = UIStyle::GetThemeMode();
        int oldThemeColor = UIStyle::GetThemeColorIndex();
        int oldWindowMode = UIStyle::GetWindowMode();
        if (UIStyle::Animation::IsEnabled())
        {
            UIStyle::ThemeTransition::Begin(oldThemeMode, oldThemeColor, oldWindowMode);
            StartThemeTransition(clickPt);
        }

        m_appCtx->configService->SetWindowMode(mode);
        UIStyle::SetWindowMode(mode);
        RefreshAllWinLauncherDisplayAffinity();
        PersistAppearanceConfig();
        if (m_appCtx->eventBus)
        {
            m_appCtx->eventBus->Publish(EventType::ThemeChanged);
        }
    }
}

int ConfigWindow::GetGlobalScalePercent()
{
    if (m_appCtx && m_appCtx->configService && m_appCtx->configService->HasCustomGlobalScalePercent())
        return m_appCtx->configService->GetGlobalScalePercent();
    return UIStyle::Scaling::GetGlobalScalePercent();
}

void ConfigWindow::SetGlobalScalePercent(int percent)
{
    int normalized = UIStyle::Scaling::NormalizePercent(percent);
    if (normalized == GetGlobalScalePercent())
        return;

    auto applyScale = [this, normalized]() {
        if (m_appCtx && m_appCtx->configService)
            m_appCtx->configService->SetGlobalScalePercent(normalized);

        UIStyle::Scaling::SetGlobalScalePercent(normalized);
        ResizeToCurrentScale();

        if (m_appCtx && m_appCtx->eventBus)
            m_appCtx->eventBus->Publish(EventType::UiScaleChanged);

        NotifyConfigChanged();
    };

    HWND hwnd = GetHWND();
    if (hwnd && IsWindowVisible(hwnd) && UIStyle::Animation::IsEnabled())
    {
        StartCloseTransition([this, hwnd, applyScale]() {
            applyScale();
            ShowWindow(hwnd, SW_SHOW);
            StartOpenTransition(true);
        }, true);
    }
    else
    {
        applyScale();
    }
}

int ConfigWindow::GetDockHeight()
{
    if (m_appCtx && m_appCtx->configService) return m_appCtx->configService->GetDockHeight();
    return 50;
}

void ConfigWindow::SetDockHeight(int height)
{
    if (m_appCtx && m_appCtx->configService) m_appCtx->configService->SetDockHeight(height);
}

int ConfigWindow::GetPopupAlignMode()
{
    if (m_appCtx && m_appCtx->configService) return m_appCtx->configService->GetPopupAlignMode();
    return 0;
}

void ConfigWindow::SetPopupAlignMode(int mode)
{
    if (m_appCtx && m_appCtx->configService) m_appCtx->configService->SetPopupAlignMode(mode);
}

bool ConfigWindow::GetPopupAutoClose()
{
    if (m_appCtx && m_appCtx->configService) return m_appCtx->configService->GetPopupAutoClose();
    return true;
}

void ConfigWindow::SetPopupAutoClose(bool enabled)
{
    if (m_appCtx && m_appCtx->configService) m_appCtx->configService->SetPopupAutoClose(enabled);
}

bool ConfigWindow::GetPopupMultiOpenWhenPinned()
{
    if (m_appCtx && m_appCtx->configService) return m_appCtx->configService->GetPopupMultiOpenWhenPinned();
    return false;
}

void ConfigWindow::SetPopupMultiOpenWhenPinned(bool enabled)
{
    if (m_appCtx && m_appCtx->configService) m_appCtx->configService->SetPopupMultiOpenWhenPinned(enabled);
}

int ConfigWindow::GetHoverLeaveDelay()
{
    if (m_appCtx && m_appCtx->configService) return m_appCtx->configService->GetHoverLeaveDelay();
    return 200;
}

void ConfigWindow::SetHoverLeaveDelay(int delayMs)
{
    if (m_appCtx && m_appCtx->configService) m_appCtx->configService->SetHoverLeaveDelay(delayMs);
}

int ConfigWindow::GetFileSelectionValiditySeconds()
{
    if (m_appCtx && m_appCtx->configService) return m_appCtx->configService->GetFileSelectionValiditySeconds();
    return 15;
}

void ConfigWindow::SetFileSelectionValiditySeconds(int seconds)
{
    if (m_appCtx && m_appCtx->configService) m_appCtx->configService->SetFileSelectionValiditySeconds(seconds);
}

int ConfigWindow::GetSortMode()
{
    if (m_appCtx && m_appCtx->configService) return m_appCtx->configService->GetSortMode();
    return 0;
}

void ConfigWindow::SetSortMode(int mode)
{
    if (m_appCtx && m_appCtx->configService) m_appCtx->configService->SetSortMode(mode);
}

bool ConfigWindow::GetAnimationEnabled()
{
    if (m_appCtx && m_appCtx->configService) return m_appCtx->configService->GetAnimationEnabled();
    return true;
}

void ConfigWindow::SetAnimationEnabled(bool enabled)
{
    if (m_appCtx && m_appCtx->configService)
    {
        m_appCtx->configService->SetAnimationEnabled(enabled);
        UIStyle::Animation::SetEnabled(enabled);
        NotifyConfigChanged();
        StartAnimation();
    }
}

int ConfigWindow::GetAnimationDuration()
{
    if (m_appCtx && m_appCtx->configService) return m_appCtx->configService->GetAnimationDuration();
    return 200;
}

void ConfigWindow::SetAnimationDuration(int duration)
{
    if (m_appCtx && m_appCtx->configService)
    {
        m_appCtx->configService->SetAnimationDuration(duration);
        UIStyle::Animation::SetDurationMs((float)duration);
        NotifyConfigChanged();
        StartAnimation();
    }
}
