#define NOMINMAX
#include "ConfigWindow.h"
#include "PromptWindow.h"
#include "WaitWindow.h"
#include "HotkeyDialog.h"
#include "UrlDialog.h"
#include "CommandDialog.h"
#include "../DpiHelper.h"
#include "../MouseHook.h"
#include "../UI/Controls/IconRenderer.h"
#include "../Services/SyncFolderService.h"
#include <windowsx.h>
#include <shlobj.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <commdlg.h>
#include <algorithm>
#include <cmath>

static const int ICON_SIZE = 24;

ConfigWindow* ConfigWindow::s_instance = nullptr;
AppContext* ConfigWindow::s_ctx = nullptr;

ConfigWindow::ConfigWindow(AppContext* ctx)
    : m_currentCategory(0)
    , m_categoryList(this)
    , m_shortcutPage(this)
    , m_settingsPage(this)
    , m_currentPage(&m_shortcutPage)
    , m_showSettings(false)
    , m_currentSettingsCategory(0)
    , m_hoveredClose(false)
    , m_hoveredSettingsBtn(false)
    , m_hoveredAddBtn(false)
    , m_trackMouse(false)
    , m_lastRt(nullptr)
{
    m_appCtx = ctx;
    if (ctx)
    {
        m_viewModel = std::make_unique<ConfigViewModel>(ctx);
        m_themeChangedToken = ctx->eventBus->Subscribe(EventType::ThemeChanged, [this]() {
            m_shortcutPage.UpdateTheme();
            UpdateTheme();
        });
        m_configChangedToken = ctx->eventBus->Subscribe(EventType::ConfigChanged, [this]() {
            if (m_ignoreConfigChangedCount > 0)
            {
                m_ignoreConfigChangedCount--;
                return;
            }
            LoadConfig();
            InvalidateRect(GetHWND(), nullptr, FALSE);
        });
    }
    else
    {
        m_viewModel = std::make_unique<ConfigViewModel>(nullptr);
    }
}

ConfigWindow::~ConfigWindow()
{
    if (m_appCtx && m_appCtx->eventBus)
    {
        if (m_themeChangedToken)
            m_appCtx->eventBus->Unsubscribe(EventType::ThemeChanged, m_themeChangedToken);
        if (m_configChangedToken)
            m_appCtx->eventBus->Unsubscribe(EventType::ConfigChanged, m_configChangedToken);
    }
    ClearPages();
}

void ConfigWindow::ClearPages()
{
    for (auto& page : m_pages)
    {
        for (auto* bmp : page.iconBitmaps)
            if (bmp) bmp->Release();
        page.iconBitmaps.clear();
        ShortcutManager::FreeShortcuts(page.shortcuts);
    }
    m_pages.clear();
}

void ConfigWindow::LoadConfig()
{
    m_categoryList.Reset();
    ClearPages();

    if (m_viewModel)
    {
        m_viewModel->ReloadConfig();
        // Convert ViewModel pages to legacy format
        for (const auto& vp : m_viewModel->GetPages())
        {
            RendPopupPage pp;
            pp.name = vp.name;
            pp.isSyncFolder = vp.isSyncFolder;
            pp.folderPath = vp.folderPath;
            for (const auto& vs : vp.shortcuts)
            {
                RendShortcutInfo si;
                si.name = vs.name;
                si.targetPath = vs.targetPath;
                si.arguments = vs.arguments;
                si.iconPath = vs.iconPath;
                si.runAsAdmin = vs.runAsAdmin;
                si.type = vs.type;
                si.targetKind = vs.targetKind;
                si.iconSource = vs.iconSource;
                si.builtinIconId = vs.builtinIconId;
                si.iconInvertLight = vs.iconInvertLight;
                si.iconInvertDark = vs.iconInvertDark;
                si.hIcon = ShortcutManager::GetShortcutIcon(si);
                pp.shortcuts.push_back(std::move(si));
            }
            m_pages.push_back(std::move(pp));
        }
    }
    else
    {
        m_pages = ShortcutManager::LoadConfig(m_configDir);
    }

    if (m_currentCategory >= (int)m_pages.size())
        m_currentCategory = 0;
    if (m_currentCategory < 0 && !m_pages.empty())
        m_currentCategory = 0;

    // Ensure DOCK is always at the first position
    for (size_t i = 0; i < m_pages.size(); i++)
    {
        if (m_pages[i].name == L"DOCK" && i != 0)
        {
            RendPopupPage dockPage = std::move(m_pages[i]);
            m_pages.erase(m_pages.begin() + i);
            m_pages.insert(m_pages.begin(), std::move(dockPage));
            break;
        }
    }

    if (!m_pages.empty())
        m_shortcutPage.SetPageData(&m_pages[m_currentCategory], true);
    else
        m_shortcutPage.SetPageData(nullptr);
}

void ConfigWindow::SaveConfig()
{
    m_ignoreConfigChangedCount++;
    if (m_viewModel)
    {
        // Sync pages back to ViewModel
        auto& viewPages = m_viewModel->GetPages();
        viewPages.clear();
        for (const auto& pp : m_pages)
        {
            Model::PopupPage vp;
            vp.name = pp.name;
            vp.isSyncFolder = pp.isSyncFolder;
            vp.folderPath = pp.folderPath;
            for (const auto& si : pp.shortcuts)
            {
                Model::ShortcutInfo vs;
                vs.name = si.name;
                vs.targetPath = si.targetPath;
                vs.arguments = si.arguments;
                vs.iconPath = si.iconPath;
                vs.runAsAdmin = si.runAsAdmin;
                vs.type = si.type;
                vs.targetKind = si.targetKind;
                vs.iconSource = si.iconSource;
                vs.builtinIconId = si.builtinIconId;
                vs.iconInvertLight = si.iconInvertLight;
                vs.iconInvertDark = si.iconInvertDark;
                vp.shortcuts.push_back(std::move(vs));
            }
            viewPages.push_back(std::move(vp));
        }
        m_viewModel->SaveConfig();
    }
    else
    {
        ShortcutManager::SaveConfig(m_configDir, m_pages);
    }
}

void ConfigWindow::Show(HWND parent, AppContext* ctx)
{
    ShowConfig(parent, ctx);
}

void ConfigWindow::ShowConfig(HWND parent, AppContext* ctx)
{
    ShowMode(parent, ctx, false);
}

void ConfigWindow::ShowSettings(HWND parent, AppContext* ctx)
{
    ShowMode(parent, ctx, true);
}

void ConfigWindow::ShowMode(HWND parent, AppContext* ctx, bool settingsMode)
{
    if (s_instance)
    {
        if (ctx)
        {
            s_ctx = ctx;
            s_instance->m_appCtx = ctx;
        }
        s_instance->SetSettingsMode(settingsMode);
        ShowWindow(s_instance->GetHWND(), SW_SHOW);
        SetActiveWindow(s_instance->GetHWND());
        SetForegroundWindow(s_instance->GetHWND());
        InvalidateRect(s_instance->GetHWND(), nullptr, FALSE);
        return;
    }

    if (ctx) s_ctx = ctx;

    s_instance = new ConfigWindow(s_ctx);
    s_instance->m_configDir = ShortcutManager::FindConfigDir();
    s_instance->LoadConfig();
    s_instance->SetSettingsMode(settingsMode);

    int w = 520;
    int h = 480;

    POINT cursorPt;
    GetCursorPos(&cursorPt);
    HMONITOR hm = MonitorFromPoint(cursorPt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(hm, &mi);
    RECT wa = mi.rcWork;

    float scale = DpiHelper::GetDpiScaleForMonitor(hm);
    int w_px = (int)(w * scale);
    int h_px = (int)(h * scale);

    int x = wa.left + (wa.right - wa.left - w_px) / 2;
    int y = wa.top + (wa.bottom - wa.top - h_px) / 2;

    s_instance->Create(L"", WS_POPUP, WS_EX_TOOLWINDOW | WS_EX_TOPMOST, x, y, w_px, h_px, parent);
    if (s_instance->GetHWND())
    {
        SetWindowDisplayAffinity(s_instance->GetHWND(), WDA_MONITOR | 0x10);
        s_instance->ApplySystemBackdrop();
        s_instance->EnsureD2D();
        s_instance->EnsureIcons();
        ShowWindow(s_instance->GetHWND(), SW_SHOW);
        SetForegroundWindow(s_instance->GetHWND());
    }
}

void ConfigWindow::SetSettingsMode(bool settingsMode)
{
    if (m_showSettings == settingsMode)
    {
        if (m_showSettings)
            m_settingsPage.SetCategory(m_currentSettingsCategory);
        return;
    }

    m_showSettings = settingsMode;
    m_categoryList.Reset();
    if (m_showSettings)
    {
        m_currentPage = &m_settingsPage;
        m_settingsPage.SetCategory(m_currentSettingsCategory);
    }
    else
    {
        m_currentPage = &m_shortcutPage;
    }
}

void ConfigWindow::Hide()
{
    if (s_instance)
    {
        ShowWindow(s_instance->GetHWND(), SW_HIDE);
    }
}

void ConfigWindow::Release()
{
    if (s_instance)
    {
        s_instance->SaveConfig(); // flush any unsaved edits before destroy
        ConfigWindow* inst = s_instance;
        s_instance = nullptr;
        HWND h = inst->GetHWND();
        if (h) DestroyWindow(h);
        delete inst;
    }
}

bool ConfigWindow::IsVisible()
{
    return s_instance && s_instance->GetHWND() && IsWindowVisible(s_instance->GetHWND());
}

void ConfigWindow::NotifyConfigChanged()
{
    if (m_appCtx && m_appCtx->configService)
    {
        m_appCtx->configService->SetAppearanceSettings(UIStyle::CaptureAppearanceSettings());
    }
    SaveConfig();
    if (m_appCtx && m_appCtx->eventBus)
    {
        m_appCtx->eventBus->Publish(EventType::ThemeChanged);
    }
}

HWND ConfigWindow::GetWindowHWND()
{
    return GetHWND();
}

std::wstring ConfigWindow::GetConfigDir()
{
    if (m_appCtx && m_appCtx->configService)
        return m_appCtx->configService->GetConfigDir();
    return m_configDir;
}

std::wstring ConfigWindow::GetConfigFilePath()
{
    if (m_appCtx && m_appCtx->configService)
        return m_appCtx->configService->GetConfigFilePath();
    return m_configDir + L"\\launcher_config.ini";
}

void ConfigWindow::OpenConfigFile()
{
    std::wstring path = GetConfigFilePath();
    if (!path.empty())
        ShellExecuteW(GetHWND(), L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void ConfigWindow::OpenLogFile()
{
    std::wstring path = GetConfigDir() + L"\\winlauncher.log";
    if (!path.empty())
        ShellExecuteW(GetHWND(), L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void ConfigWindow::OpenConfigDir()
{
    std::wstring dir = GetConfigDir();
    if (!dir.empty())
        ShellExecuteW(GetHWND(), L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void ConfigWindow::StartAnimation()
{
    if (!m_animating)
    {
        m_animating = true;
        m_animLastTime = GetTimeInSeconds();
        SetTimer(GetHWND(), 2, 1, nullptr);
    }
}

double ConfigWindow::GetTimeInSeconds()
{
    static LARGE_INTEGER freq;
    static bool init = false;
    if (!init)
    {
        QueryPerformanceFrequency(&freq);
        init = true;
    }
    LARGE_INTEGER count;
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart / (double)freq.QuadPart;
}

ID2D1Bitmap* ConfigWindow::CreateD2DBitmapFromHicon(HICON hIcon, const std::wstring& name)
{
    if (hIcon == nullptr)
    {
        auto bmp = IconRenderer::CreateDefaultIcon(m_rt.Get(), GetDWFactory(), name, ICON_SIZE * 2);
        return bmp.Detach();
    }
    auto bmp = IconRenderer::HicontoD2D(m_rt.Get(), hIcon, ICON_SIZE * 2);
    return bmp.Detach();
}

size_t ConfigWindow::GetCategoryCount()
{
    if (m_showSettings)
    {
        return 5;
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
        if (index == 3) return L"配置管理";
        if (index == 4) return L"关于软件";
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
        if (index >= 0 && index < 5)
        {
            m_currentSettingsCategory = index;
            m_settingsPage.SetCategory(index);
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

void ConfigWindow::SetTheme(int theme)
{
    if (m_appCtx && m_appCtx->configService)
    {
        m_appCtx->configService->SetTheme(theme);
        NotifyConfigChanged();
        UIStyle::SetThemeMode((UIStyle::ThemeMode)theme);
        m_appCtx->eventBus->Publish(EventType::ThemeChanged);
    }
}

int ConfigWindow::GetThemeColor()
{
    if (m_appCtx && m_appCtx->configService) return m_appCtx->configService->GetThemeColor();
    return 0;
}

void ConfigWindow::SetThemeColor(int colorIndex)
{
    if (m_appCtx && m_appCtx->configService)
    {
        m_appCtx->configService->SetThemeColor(colorIndex);
        UIStyle::SetThemeColorIndex(m_appCtx->configService->GetThemeColor());
        NotifyConfigChanged();
    }
}

int ConfigWindow::GetWindowMode()
{
    if (m_appCtx && m_appCtx->configService) return m_appCtx->configService->GetWindowMode();
    return 0;
}

void ConfigWindow::SetWindowMode(int mode)
{
    if (m_appCtx && m_appCtx->configService)
    {
        m_appCtx->configService->SetWindowMode(mode);
        NotifyConfigChanged();
        UIStyle::SetWindowMode(mode);
        m_appCtx->eventBus->Publish(EventType::ThemeChanged);
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

void ConfigWindow::AddCategory(const std::wstring&)
{
    std::wstring name;
    if (PromptWindow::Show(GetHWND(), L"新建分类", L"输入新分类的名称:", name, L"", m_appCtx))
    {
        while (!name.empty() && (name.back() == L' ' || name.back() == L'\t'))
            name.pop_back();
        size_t start = 0;
        while (start < name.size() && (name[start] == L' ' || name[start] == L'\t'))
            start++;
        if (start > 0) name = name.substr(start);

        if (!name.empty())
        {
            // Disallow creating a category named "DOCK" (reserved system category)
            std::wstring nameLower = name;
            for (auto& c : nameLower) c = (wchar_t)towlower(c);
            if (nameLower == L"dock")
                return;

            RendPopupPage newPage;
            newPage.name = name;
            m_pages.push_back(std::move(newPage));
            m_currentCategory = (int)m_pages.size() - 1;
            m_shortcutPage.SetPageData(&m_pages[m_currentCategory]);
            SaveConfig();
        }
    }
}

RendPopupPage* ConfigWindow::GetPageByIndex(int index)
{
    if (index >= 0 && index < (int)m_pages.size())
        return &m_pages[index];
    return nullptr;
}

void ConfigWindow::DeleteCategory(int index)
{
    if (index >= 0 && index < (int)m_pages.size())
    {
        // Disallow deleting the reserved DOCK category
        if (m_pages[index].name == L"DOCK")
            return;

        for (auto* bmp : m_pages[index].iconBitmaps)
            if (bmp) bmp->Release();
        ShortcutManager::FreeShortcuts(m_pages[index].shortcuts);
        m_pages.erase(m_pages.begin() + index);

        if (m_currentCategory >= (int)m_pages.size())
            m_currentCategory = (int)m_pages.size() - 1;
        if (m_currentCategory < 0 && !m_pages.empty())
            m_currentCategory = 0;

        if (!m_pages.empty())
            m_shortcutPage.SetPageData(&m_pages[m_currentCategory]);
        else
            m_shortcutPage.SetPageData(nullptr);

        SaveConfig();
    }
}

void ConfigWindow::ReorderCategories(int fromIndex, int toIndex)
{
    if (fromIndex == toIndex) return;
    if (fromIndex < 0 || fromIndex >= (int)m_pages.size()) return;
    if (toIndex < 0 || toIndex >= (int)m_pages.size()) return;

    // Safety check: DOCK is at index 0 and cannot be moved or replaced
    if (fromIndex == 0 || toIndex == 0) return;

    RendPopupPage page = std::move(m_pages[fromIndex]);
    m_pages.erase(m_pages.begin() + fromIndex);
    m_pages.insert(m_pages.begin() + toIndex, std::move(page));

    // Update current category index
    if (m_currentCategory == fromIndex)
    {
        m_currentCategory = toIndex;
    }
    else if (fromIndex < m_currentCategory && toIndex >= m_currentCategory)
    {
        m_currentCategory--;
    }
    else if (fromIndex > m_currentCategory && toIndex <= m_currentCategory)
    {
        m_currentCategory++;
    }

    // Refresh page data pointer
    m_shortcutPage.SetPageData(&m_pages[m_currentCategory]);

    SaveConfig();
}

void ConfigWindow::RenameCategory(int index, const std::wstring& nameInput)
{
    if (index >= 0 && index < (int)m_pages.size())
    {
        // Disallow renaming DOCK
        if (m_pages[index].name == L"DOCK")
            return;

        std::wstring name = nameInput;
        while (!name.empty() && (name.back() == L' ' || name.back() == L'\t'))
            name.pop_back();
        size_t start = 0;
        while (start < name.size() && (name[start] == L' ' || name[start] == L'\t'))
            start++;
        if (start > 0) name = name.substr(start);

        if (!name.empty())
        {
            std::wstring nameLower = name;
            for (auto& c : nameLower) c = (wchar_t)towlower(c);
            if (nameLower == L"dock")
                return;

            m_pages[index].name = name;
            SaveConfig();
            
            if (m_appCtx && m_appCtx->eventBus)
            {
                m_appCtx->eventBus->Publish(EventType::ThemeChanged);
            }
        }
    }
}

void ConfigWindow::EnsureIcons()
{
    bool rtChanged = (m_rt.Get() != m_lastRt);
    if (rtChanged)
        m_lastRt = m_rt.Get();

    if (m_rt && m_dw)
    {
        if (!m_tfLeft)
        {
            UIStyle::Typography::CreateTextFormat(
                m_dw.Get(),
                &m_tfLeft,
                12.0f,
                DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_TEXT_ALIGNMENT_LEADING,
                DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        if (!m_tfTitle)
        {
            UIStyle::Typography::CreateTextFormat(
                m_dw.Get(),
                &m_tfTitle,
                15.0f,
                DWRITE_FONT_WEIGHT_SEMI_BOLD,
                DWRITE_TEXT_ALIGNMENT_LEADING,
                DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        if (!m_tfHeader)
        {
            UIStyle::Typography::CreateTextFormat(
                m_dw.Get(),
                &m_tfHeader,
                13.0f,
                DWRITE_FONT_WEIGHT_SEMI_BOLD,
                DWRITE_TEXT_ALIGNMENT_LEADING,
                DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }
}

void ConfigWindow::OnPaintContent(ID2D1HwndRenderTarget* rt)
{
    EnsureIcons();

    RECT cr; GetClientRect(GetHWND(), &cr);
    float scale = GetWindowScale(GetHWND());
    float w = (float)cr.right / scale;
    float h = (float)cr.bottom / scale;

    // Header Title
    if (m_tfHeader)
    {
        auto textBrush = GetOrCreateBrush(UIStyle::ThemeColor::TextNormal().d2d);
        if (textBrush)
        {
            std::wstring headerText = m_showSettings ? L"设置面板" : L"配置面板";
            rt->DrawTextW(headerText.c_str(), (UINT32)headerText.size(), m_tfHeader.Get(), D2D1::RectF(10, 10, 150, 30), textBrush.Get());
        }
    }

    // Close Button "X"
    {
        D2D1_RECT_F closeRect = D2D1::RectF(490, 10, 510, 30);
        D2D1_ROUNDED_RECT roundedClose = D2D1::RoundedRect(closeRect, 4.0f, 4.0f);
        if (m_hoveredClose)
        {
            auto closeBg = GetOrCreateBrush(D2D1::ColorF(UIStyle::ThemeColor::DangerRed().d2d.r, UIStyle::ThemeColor::DangerRed().d2d.g, UIStyle::ThemeColor::DangerRed().d2d.b, 0.4f));
            if (closeBg) rt->FillRoundedRectangle(roundedClose, closeBg.Get());
        }

        auto xBrush = GetOrCreateBrush(UIStyle::ThemeColor::TextMuted().d2d);
        if (xBrush)
        {
            rt->DrawLine(D2D1::Point2F(495, 15), D2D1::Point2F(505, 25), xBrush.Get(), UIStyle::Metrics::IconStroke());
            rt->DrawLine(D2D1::Point2F(505, 15), D2D1::Point2F(495, 25), xBrush.Get(), UIStyle::Metrics::IconStroke());
        }
    }

    // Settings Button
    {
        D2D1_RECT_F settingsRect = D2D1::RectF(430, 10, 475, 30);
        D2D1_ROUNDED_RECT roundedSettings = D2D1::RoundedRect(settingsRect, 4.0f, 4.0f);
        if (m_hoveredSettingsBtn)
        {
            auto btnBg = GetOrCreateBrush(UIStyle::ThemeColor::ButtonBgHover().d2d);
            if (btnBg) rt->FillRoundedRectangle(roundedSettings, btnBg.Get());
        }
        auto btnBorder = GetOrCreateBrush(m_hoveredSettingsBtn ?
            UIStyle::ThemeColor::ButtonBorderHover().d2d : UIStyle::ThemeColor::ButtonBorderNormal().d2d);
        if (btnBorder) rt->DrawRoundedRectangle(roundedSettings, btnBorder.Get(), UIStyle::Metrics::ControlStroke());

        if (m_tfLeft)
        {
            auto textBrush = GetOrCreateBrush(UIStyle::ThemeColor::TextNormal().d2d);
            if (textBrush)
            {
                std::wstring btnText = m_showSettings ? L"返回" : L"设置";
                m_tfLeft->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                rt->DrawTextW(btnText.c_str(), (UINT32)btnText.size(), m_tfLeft.Get(), settingsRect, textBrush.Get());
                m_tfLeft->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            }
        }
    }

    // Left Column Separator
    {
        auto lineBrush = GetOrCreateBrush(UIStyle::ThemeColor::CardBorder().d2d);
        if (lineBrush) rt->DrawLine(D2D1::Point2F(150, 0), D2D1::Point2F(150, h), lineBrush.Get(), 1.0f);
    }

    // Category List Left Background
    {
        auto bgLeft = GetOrCreateBrush(UIStyle::ThemeColor::CardBg().d2d);
        if (bgLeft) rt->FillRectangle(D2D1::RectF(0, 0, 150, h), bgLeft.Get());
    }

    m_categoryList.OnPaint(rt, D2D1::RectF(0, 0, 150, h));

    if (m_currentPage)
    {
        m_currentPage->OnPaint(rt, D2D1::RectF(150, 0, w, h));
    }

    // Draw Add button last so it appears on top of all content
    if (!m_showSettings)
    {
        DrawAddButton(rt);
    }
}

LRESULT ConfigWindow::HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CREATE:
        DragAcceptFiles(hWnd, TRUE);
        return 0;

    case WM_SHOWWINDOW:
    {
        return GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
    }

    case WM_DROPFILES:
    {
        HDROP hDrop = (HDROP)wParam;
        POINT dropPt{};
        DragQueryPoint(hDrop, &dropPt);

        float scale = GetWindowScale(hWnd);
        POINT clientPt = dropPt;
        clientPt.x = (int)(clientPt.x / scale);
        clientPt.y = (int)(clientPt.y / scale);

        bool repaint = false;
        if (clientPt.x < 150)
        {
            HandleCategoryDrop(hDrop, repaint);
        }
        else
        {
            if (m_currentPage)
            {
                m_currentPage->OnDropFiles(hDrop, repaint);
            }
        }
        if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        if (!m_trackMouse)
        {
            TRACKMOUSEEVENT tme{ sizeof(tme) };
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hWnd;
            TrackMouseEvent(&tme);
            m_trackMouse = true;
        }

        float scale = GetWindowScale(hWnd);
        POINT pt{ (int)(GET_X_LPARAM(lParam) / scale), (int)(GET_Y_LPARAM(lParam) / scale) };
        bool repaint = false;

        bool hcl = HitTestCloseButton(pt);
        if (hcl != m_hoveredClose) { m_hoveredClose = hcl; repaint = true; }

        bool hsb = HitTestSettingsButton(pt);
        if (hsb != m_hoveredSettingsBtn) { m_hoveredSettingsBtn = hsb; repaint = true; }

        bool hab = !m_showSettings && (HitTestAddButton(pt) || DropDownMenu::IsVisible());
        if (hab != m_hoveredAddBtn) { m_hoveredAddBtn = hab; repaint = true; }

        m_categoryList.OnMouseMove(pt, repaint);
        if (m_currentPage) m_currentPage->OnMouseMove(pt, repaint);

        if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }

    case WM_MOUSELEAVE:
    {
        bool repaint = false;
        m_hoveredClose = false;
        m_hoveredSettingsBtn = false;
        m_hoveredAddBtn = false;
        m_trackMouse = false;
        repaint = true;
        m_categoryList.OnMouseLeave(repaint);
        if (m_currentPage) m_currentPage->OnMouseLeave(repaint);
        if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }

    case WM_LBUTTONUP:
    {
        float scale = GetWindowScale(hWnd);
        POINT pt{ (int)(GET_X_LPARAM(lParam) / scale), (int)(GET_Y_LPARAM(lParam) / scale) };
        bool repaint = false;
        m_categoryList.OnLButtonUp(pt, repaint);
        if (m_currentPage) m_currentPage->OnLButtonUp(pt, repaint);
        if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }

    case WM_RBUTTONDOWN:
    {
        float scale = GetWindowScale(hWnd);
        POINT pt{ (int)(GET_X_LPARAM(lParam) / scale), (int)(GET_Y_LPARAM(lParam) / scale) };
        bool repaint = false;
        m_categoryList.OnRButtonDown(pt, repaint);
        if (m_currentPage) m_currentPage->OnRButtonDown(pt, repaint);
        if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }

    case WM_LBUTTONDBLCLK:
    {
        float scale = GetWindowScale(hWnd);
        POINT pt{ (int)(GET_X_LPARAM(lParam) / scale), (int)(GET_Y_LPARAM(lParam) / scale) };
        bool repaint = false;
        if (m_currentPage) m_currentPage->OnLButtonDblClk(pt, repaint);
        if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        float scale = GetWindowScale(hWnd);
        POINT pt{ (int)(GET_X_LPARAM(lParam) / scale), (int)(GET_Y_LPARAM(lParam) / scale) };
        bool repaint = false;

        if (HitTestCloseButton(pt)) { Release(); return 0; }
        if (HitTestSettingsButton(pt))
        {
            SetSettingsMode(!m_showSettings);
            repaint = true;
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        }
        if (!m_showSettings && HitTestAddButton(pt))
        {
            if (DropDownMenu::IsVisible())
            {
                DropDownMenu::Hide();
            }
            else
            {
                POINT screenPt = { 430, 62 };
                ClientToScreen(hWnd, &screenPt);

                std::vector<DropDownMenu::Item> items = {
                    { L"\u5feb\u6377\u65b9\u5f0f",   [this]() { m_shortcutPage.ShowAddShortcutDialog(); } },
                    { L"\u5feb\u6377\u952e",         [this]() { m_shortcutPage.ShowAddHotkeyDialog(); } },
                    { L"\u6253\u5f00\u7f51\u7ad9",   [this]() { m_shortcutPage.ShowAddUrlDialog(); } },
                    { L"\u8fd0\u884c\u547d\u4ee4",   [this]() { m_shortcutPage.ShowAddCommandDialog(); } },
                    { L"\u6dfb\u52a0\u5b8f",         nullptr },
                    { L"\u6279\u91cf\u542f\u52a8",   nullptr },
                    { L"\u5185\u7f6e\u56fe\u6807",   nullptr },
                };
                DropDownMenu::Show(hWnd, screenPt, items, m_appCtx);
            }
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        }

        m_categoryList.OnLButtonDown(pt, repaint);
        if (m_currentPage) m_currentPage->OnLButtonDown(pt, repaint);

        if (pt.y < 38 && pt.x < 490 && pt.x > 150)
        {
            ReleaseCapture();
            SendMessageW(hWnd, WM_SYSCOMMAND, SC_MOVE | HTCAPTION, 0);
        }

        if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }

    case WM_MOUSEWHEEL:
    {
        POINT pt; GetCursorPos(&pt); ScreenToClient(hWnd, &pt);
        float scale = GetWindowScale(hWnd);
        pt.x = (int)(pt.x / scale);
        pt.y = (int)(pt.y / scale);
        bool repaint = false;
        if (m_currentPage)
        {
            int zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
            m_currentPage->OnMouseWheel((short)zDelta, pt, repaint);
        }
        if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }

    case WM_TIMER:
        if (wParam == 2)
        {
            if (m_animating)
            {
                double now = GetTimeInSeconds();
                float dt = (float)(now - m_animLastTime);
                m_animLastTime = now;

                if (dt > 0.1f) dt = 0.1f;
                if (dt <= 0.0f) dt = 0.001f;

                bool repaint = false;

                if (m_currentPage)
                {
                    m_currentPage->UpdateAnimation(dt, repaint);
                }

                m_categoryList.UpdateAnimation(dt, repaint);

                bool stillAnimating = false;
                if (m_currentPage && m_currentPage->IsAnimating()) stillAnimating = true;
                if (m_categoryList.IsAnimating()) stillAnimating = true;

                m_animating = stillAnimating;

                if (repaint)
                {
                    InvalidateRect(hWnd, nullptr, FALSE);
                    UpdateWindow(hWnd);
                }

                if (!m_animating)
                {
                    KillTimer(hWnd, 2);
                }
            }
            return 0;
        }
        break;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) Release();
        return 0;

    case WM_DESTROY:
        GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
        // s_instance is managed by Release()
        return 0;
    }

    return GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
}

bool ConfigWindow::HitTestCloseButton(POINT pt)
{
    return (pt.x >= 490 && pt.x <= 510 && pt.y >= 10 && pt.y <= 30);
}

bool ConfigWindow::HitTestSettingsButton(POINT pt)
{
    return (pt.x >= 430 && pt.x <= 475 && pt.y >= 10 && pt.y <= 30);
}

bool ConfigWindow::HitTestAddButton(POINT pt)
{
    // "添加" button: left = settings-button-left (430), right = close-button-right (510)
    return (pt.x >= 430 && pt.x <= 510 && pt.y >= 36 && pt.y <= 56);
}

void ConfigWindow::DrawAddButton(ID2D1HwndRenderTarget* rt)
{
    // Left aligns with settings button (430), right aligns with close button right edge (510)
    D2D1_RECT_F addRect = D2D1::RectF(430, 36, 510, 56);
    D2D1_ROUNDED_RECT roundedAdd = D2D1::RoundedRect(addRect, 4.0f, 4.0f);

    bool isActive = m_hoveredAddBtn || DropDownMenu::IsVisible();

    D2D1_COLOR_F accentClr = UIStyle::ThemeColor::Accent().d2d;
    D2D1_COLOR_F bgClr = accentClr;
    bgClr.a = isActive ? 0.22f : 0.18f;
    auto btnBg = GetOrCreateBrush(bgClr);
    if (btnBg) rt->FillRoundedRectangle(roundedAdd, btnBg.Get());

    D2D1_COLOR_F borderClr = accentClr;
    borderClr.a = 0.55f;
    auto btnBorder = GetOrCreateBrush(borderClr);
    if (btnBorder) rt->DrawRoundedRectangle(roundedAdd, btnBorder.Get(), UIStyle::Metrics::HairlineStroke());

    if (m_tfLeft)
    {
        auto textBrush = GetOrCreateBrush(UIStyle::ThemeColor::TextNormal().d2d);
        if (textBrush)
        {
            const wchar_t* btnText = L"+\u6dfb\u52a0\u56fe\u6807";  // "+添加图标"
            m_tfLeft->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            rt->DrawTextW(btnText, (UINT32)wcslen(btnText), m_tfLeft.Get(), addRect, textBrush.Get());
            m_tfLeft->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        }
    }
}



void ConfigWindow::HandleCategoryDrop(HDROP hDrop, bool& repaint)
{
    UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
    bool addedAny = false;
    for (UINT i = 0; i < fileCount; ++i)
    {
        wchar_t filePath[MAX_PATH]{};
        if (DragQueryFileW(hDrop, i, filePath, MAX_PATH))
        {
            DWORD attr = GetFileAttributesW(filePath);
            if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
            {
                std::wstring dirPath = filePath;
                wchar_t nameBuf[MAX_PATH]{};
                wcscpy_s(nameBuf, PathFindFileNameW(dirPath.c_str()));
                std::wstring folderName = nameBuf;
                if (folderName.empty())
                {
                    folderName = L"未命名分类";
                }

                AddSyncCategory(folderName, dirPath);
                addedAny = true;
            }
        }
    }
    if (addedAny)
    {
        NotifyConfigChanged();
        repaint = true;
    }
}

void ConfigWindow::AddSyncCategory(const std::wstring& name, const std::wstring& folderPath)
{
    std::wstring nameLower = name;
    for (auto& c : nameLower) c = (wchar_t)towlower(c);
    if (nameLower == L"dock")
        return;

    RendPopupPage newPage;
    newPage.name = name;
    newPage.isSyncFolder = true;
    newPage.folderPath = folderPath;

    // Load shortcuts and icons from the folder
    auto rendShortcuts = SyncFolderService::LoadRendShortcuts(folderPath);
    for (auto& rs : rendShortcuts)
    {
        if (!rs.hIcon)
            rs.hIcon = ShortcutManager::GetShortcutIcon(rs);
        newPage.shortcuts.push_back(rs);
    }

    m_pages.push_back(std::move(newPage));
    m_currentCategory = (int)m_pages.size() - 1;
    m_shortcutPage.SetPageData(&m_pages[m_currentCategory]);
}
