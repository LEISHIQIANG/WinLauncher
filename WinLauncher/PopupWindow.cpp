#define NOMINMAX
#include "PopupWindow.h"
#include "DpiHelper.h"
#include "UI/Controls/IconRenderer.h"
#include "Services/SystemIconService.h"
#include "Services/PrivilegeLaunchService.h"
#include "Config/UIStyle.h"
#include "Config/PromptWindow.h"
#include "Config/ConfirmWindow.h"
#include "App/Logger.h"
#include "App/AppMessages.h"
#include <windowsx.h>
#include <shellapi.h>
#include <algorithm>
#include <imm.h>
#include <string>
#pragma comment(lib, "imm32.lib")
#include "Services/MacroService.h"
#include "Services/BatchLaunchService.h"

PopupWindow* PopupWindow::s_instance = nullptr;

static const int ICON_SIZE      = 24;
static const int CELL_MARGIN_X  = 6;
static const int CELL_MARGIN_Y  = 6;
static const int GAP_H          = 4;
static const int GAP_V          = 4;
static const int LABEL_HEIGHT   = 15;
static const int COLUMNS        = 6;
static const int WND_PAD        = 8;

static const UINT_PTR AUTO_HIDE_TIMER_ID = 1;
static const UINT_PTR POPUP_ANIMATION_TIMER_ID = 2;
static const UINT POPUP_ANIMATION_FRAME_MS = 8;
static const UINT_PTR TIMELINE_ANIMATION_TIMER_ID = 3;
static const UINT TIMELINE_ANIMATION_FRAME_MS = 16;
static const UINT WM_USER_ANIMATE = WM_USER + 100;
static const UINT WM_USER_REFRESH_ICONS = WM_USER + 101;
static const UINT WM_USER_SELECTION_UPDATED = WM_USER + 102;
static const double POPUP_SLOW_SHOW_MS = 24.0;
static const double POPUP_SLOW_FRAME_MS = 16.0;
static const double POPUP_SLOW_ICON_REFRESH_MS = 40.0;
static const double FILE_SELECTION_VALIDITY_DURATION = 5.0;

static bool ShouldRenderGeneratedDefaultIcon(const RendPopupPage& page, const RendShortcutInfo& shortcut)
{
    return !page.isSyncFolder && ShortcutManager::UsesGeneratedDefaultIcon(shortcut);
}

static bool HasLaunchAction(const RendShortcutInfo& shortcut)
{
    switch (shortcut.type)
    {
    case Model::ShortcutType::Macro:
    case Model::ShortcutType::Batch:
        return !shortcut.arguments.empty();
    default:
        return !shortcut.targetPath.empty();
    }
}

static double GetTimeInSeconds()
{
    static double freq = 0.0;
    if (freq == 0.0)
    {
        LARGE_INTEGER li;
        QueryPerformanceFrequency(&li);
        freq = (double)li.QuadPart;
    }
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    return (double)li.QuadPart / freq;
}

int PopupWindow::CellWidth() const  { return GetIconSize() + GetCellMarginX() * 2 + GetIconGap(); }
int PopupWindow::CellHeight() const { return GetIconSize() + GetCellMarginY() * 2 + GetLabelHeight() + GetIconGap(); }

int PopupWindow::GetColumns() const
{
    if (m_appCtx && m_appCtx->configService) return m_appCtx->configService->GetPopupColumns();
    return 6;
}

int PopupWindow::GetRows() const
{
    if (m_appCtx && m_appCtx->configService) return m_appCtx->configService->GetPopupRows();
    return 4;
}

int PopupWindow::GetIconSize() const
{
    if (m_appCtx && m_appCtx->configService) return m_appCtx->configService->GetPopupIconSize();
    return 24;
}

int PopupWindow::GetIconGap() const
{
    if (m_appCtx && m_appCtx->configService) return m_appCtx->configService->GetPopupIconGap();
    return 4;
}

int PopupWindow::GetIconRadius() const
{
    if (m_appCtx && m_appCtx->configService) return m_appCtx->configService->GetPopupIconRadius();
    return 6;
}

int PopupWindow::GetWndPadding() const
{
    if (m_appCtx && m_appCtx->configService) return m_appCtx->configService->GetPopupWndPadding();
    return 8;
}

int PopupWindow::GetCellMarginX() const
{
    return 6;
}

int PopupWindow::GetCellMarginY() const
{
    return 6;
}
int PopupWindow::GetDockHeight() const
{
    if (m_appCtx && m_appCtx->configService) return m_appCtx->configService->GetDockHeight();
    return 50;
}


PopupWindow::PopupWindow(AppContext* ctx)
    : m_currentPage(0)
    , m_hovered(-1)
    , m_trackMouse(false)
    , m_pinned(false)
    , m_lastRt(nullptr)
    , m_lastDpi(96.0f)
    , m_animating(false)
    , m_animLastTime(0.0)
    , m_scrollPosition(0.0f)
    , m_scrollVelocity(0.0f)
    , m_searchActive(false)
    , m_selectedSearchResult(0)
    , m_hoveredTab(-1)
    , m_hoveredDock(-1)
    , m_cursorBlink(true)
{
    m_appCtx = ctx;

    if (m_appCtx)
    {
        m_viewModel = std::make_unique<PopupViewModel>(m_appCtx);

        // Use shared icon service if available, otherwise create our own
        if (m_appCtx->iconService)
        {
            // Borrow: we'll use ctx's service but wrap in a simple adapter
        }
        else
        {
            m_iconService = std::make_unique<SystemIconService>();
        }

        // Subscribe to config changes instead of polling
        m_configChangedToken = m_appCtx->eventBus->Subscribe(EventType::ConfigChanged, [this]() {
            OnConfigChanged();
        });
        m_themeChangedToken = m_appCtx->eventBus->Subscribe(EventType::ThemeChanged, [this]() {
            for (auto& page : m_pages)
            {
                for (auto* bmp : page.iconBitmaps)
                {
                    if (bmp) bmp->Release();
                }
                page.iconBitmaps.clear();
            }
            for (auto* bmp : m_dockPage.iconBitmaps)
            {
                if (bmp) bmp->Release();
            }
            m_dockPage.iconBitmaps.clear();
            m_bmpBrushCache.clear();

            UpdateTheme();
        });
        m_bgStyleChangedToken = m_appCtx->eventBus->Subscribe(EventType::BackgroundStyleChanged, [this]() {
            UpdateBackgroundStyle();
        });
        m_uiScaleChangedToken = m_appCtx->eventBus->Subscribe(EventType::UiScaleChanged, [this]() {
            UpdateWindowSize();
            if (GetHWND()) InvalidateRect(GetHWND(), nullptr, FALSE);
        });
    }
    else
    {
        m_viewModel = std::make_unique<PopupViewModel>(nullptr);
        m_iconService = std::make_unique<SystemIconService>();
    }
}

PopupWindow::~PopupWindow()
{
    if (m_appCtx && m_appCtx->eventBus)
    {
        if (m_configChangedToken)
            m_appCtx->eventBus->Unsubscribe(EventType::ConfigChanged, m_configChangedToken);
        if (m_themeChangedToken)
            m_appCtx->eventBus->Unsubscribe(EventType::ThemeChanged, m_themeChangedToken);
        if (m_bgStyleChangedToken)
            m_appCtx->eventBus->Unsubscribe(EventType::BackgroundStyleChanged, m_bgStyleChangedToken);
        if (m_uiScaleChangedToken)
            m_appCtx->eventBus->Unsubscribe(EventType::UiScaleChanged, m_uiScaleChangedToken);
    }
    ClearPages();
}

void PopupWindow::ClearPages()
{
    bool hasIconSvc = m_iconService || (m_appCtx && m_appCtx->iconService);
    for (auto& page : m_pages)
    {
        for (auto* bmp : page.iconBitmaps)
            if (bmp) bmp->Release();
        page.iconBitmaps.clear();
        if (hasIconSvc)
            page.shortcuts.clear(); // icon service owns HICON lifecycle
        else
            ShortcutManager::FreeShortcuts(page.shortcuts); // fallback: own lifecycle
    }
    m_pages.clear();

    // Clear dock page bitmaps
    for (auto* bmp : m_dockPage.iconBitmaps)
        if (bmp) bmp->Release();
    m_dockPage.iconBitmaps.clear();
    if (hasIconSvc)
        m_dockPage.shortcuts.clear();
    else
        ShortcutManager::FreeShortcuts(m_dockPage.shortcuts);
    m_dockPage.name = L"DOCK";
}

void PopupWindow::OnConfigChanged()
{
    if (m_viewModel)
    {
        m_viewModel->ReloadPages();
    }

    // Rebuild legacy render data
    ClearPages();
    if (m_viewModel)
    {
        // Convert ViewModel pages to legacy RendPopupPage format with HICON
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
                si.type = vs.type;
                si.runAsAdmin = vs.runAsAdmin;
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

        // Populate dock render data
        m_dockPage = RendPopupPage{};
        m_dockPage.name = L"DOCK";
        for (const auto& vs : m_viewModel->GetDockPage().shortcuts)
        {
            RendShortcutInfo si;
            si.name = vs.name;
            si.targetPath = vs.targetPath;
            si.arguments = vs.arguments;
            si.iconPath = vs.iconPath;
            si.type = vs.type;
            si.runAsAdmin = vs.runAsAdmin;
            si.targetKind = vs.targetKind;
            si.iconSource = vs.iconSource;
            si.builtinIconId = vs.builtinIconId;
            si.iconInvertLight = vs.iconInvertLight;
            si.iconInvertDark = vs.iconInvertDark;
            si.hIcon = ShortcutManager::GetShortcutIcon(si);
            m_dockPage.shortcuts.push_back(std::move(si));
        }

        m_currentPage = m_viewModel->GetCurrentPage();
        m_scrollPosition = m_viewModel->GetScrollPosition();
        m_scrollVelocity = 0.0f;

        if (m_rt)
        {
            EnsureIcons();
        }
    }

    if (GetHWND() && IsWindowVisible(GetHWND()))
    {
        UpdateWindowSize();
        m_bgCaptureDirty = true;
        m_bgCompositeDirty = true;
        CaptureBackground();
        CompositeBackgroundToCache();
    }

    if (GetHWND()) InvalidateRect(GetHWND(), nullptr, FALSE);
}

void PopupWindow::UpdateWindowSize()
{
    HWND hwnd = GetHWND();
    if (!hwnd) return;

    int cols = GetColumns();
    int rows = GetRows();
    int w = cols * CellWidth() + GetWndPadding() * 2 - GetIconGap();
    int indicatorHeight = 0;
    int topBarHeight = 32;
    int dockRows = GetDockHeight();
    int ch = CellHeight();
    int wndPad = GetWndPadding();
    int iconGap = GetIconGap();
    int mainGridCardBottom = wndPad + rows * ch - iconGap + topBarHeight;
    int lineY = mainGridCardBottom + wndPad;
    int dockTopY = lineY + wndPad;
    int h = dockTopY + dockRows * ch - iconGap + wndPad;
    if (w > 900) w = 900;
    if (h > 900) h = 900;

    HMONITOR hm = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(hm, &mi);
    RECT wa = mi.rcWork;

    float scale = DpiHelper::GetDpiScaleForMonitor(hm);
    int w_px = (int)(w * scale);
    int h_px = (int)(h * scale);

    RECT rect;
    GetWindowRect(hwnd, &rect);
    int currentX = rect.left;
    int currentY = rect.top;

    if (currentX + w_px > wa.right) currentX = wa.right - w_px;
    if (currentY + h_px > wa.bottom) currentY = wa.bottom - h_px;
    if (currentX < wa.left) currentX = wa.left;
    if (currentY < wa.top) currentY = wa.top;

    SetWindowPos(hwnd, HWND_TOPMOST, currentX, currentY, w_px, h_px, SWP_NOACTIVATE);

    if (m_rt)
    {
        m_rt->SetDpi(scale * 96.0f, scale * 96.0f);
        UIStyle::Typography::ApplyRenderTargetTextDefaults(m_rt.Get());
    }
}

void PopupWindow::Init(AppContext* ctx)
{
    if (s_instance) return;

    s_instance = new PopupWindow(ctx);
    s_instance->m_configDir = ShortcutManager::FindConfigDir();
    s_instance->OnConfigChanged(); // Initial load
}

bool PopupWindow::IsVisible()
{
    return s_instance && s_instance->GetHWND() && IsWindowVisible(s_instance->GetHWND());
}

HWND PopupWindow::GetRestoreForegroundWindow()
{
    if (!s_instance)
        return nullptr;

    HWND hWnd = s_instance->m_restoreForegroundWnd;
    return (hWnd && IsWindow(hWnd)) ? hWnd : nullptr;
}

void PopupWindow::Show(HWND parent, POINT pt)
{
    HWND prevActive = GetForegroundWindow();
    double showStart = GetTimeInSeconds();

    if (!s_instance)
    {
        Init(nullptr);
    }

    if (!s_instance) return;

    if (prevActive && prevActive != s_instance->GetHWND())
    {
        s_instance->m_restoreForegroundWnd = prevActive;
    }

    POINT clickPt = pt; // Store the original click position

    // 1. Load configuration and page data if not already loaded
    if (s_instance->m_pages.empty())
    {
        s_instance->OnConfigChanged();
    }
    else if (s_instance->m_viewModel)
    {
        s_instance->m_currentPage = s_instance->m_viewModel->GetCurrentPage();
        s_instance->m_scrollPosition = (float)s_instance->m_currentPage;
        s_instance->m_scrollVelocity = 0.0f;
    }

    // 2. Calculate window dimensions using user settings
    int cols = s_instance->GetColumns();
    int rows = s_instance->GetRows();
    int w = cols * s_instance->CellWidth() + s_instance->GetWndPadding() * 2 - s_instance->GetIconGap();
    int indicatorHeight = 0;
    int topBarHeight = 32;
    int dockRows = s_instance->GetDockHeight();
    int ch = s_instance->CellHeight();
    int wndPad = s_instance->GetWndPadding();
    int iconGap = s_instance->GetIconGap();
    int mainGridCardBottom = wndPad + rows * ch - iconGap + topBarHeight;
    int lineY = mainGridCardBottom + wndPad;
    int dockTopY = lineY + wndPad;
    int h = dockTopY + dockRows * ch - iconGap + wndPad;
    if (w > 900) w = 900;
    if (h > 900) h = 900;

    HMONITOR hm = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(hm, &mi);
    RECT wa = mi.rcWork;

    float scale = DpiHelper::GetDpiScaleForMonitor(hm);
    int w_px = (int)(w * scale);
    int h_px = (int)(h * scale);

    int targetX = pt.x - w_px / 2;
    int targetY = pt.y - h_px / 2;

    if (targetX + w_px > wa.right) targetX = wa.right - w_px;
    if (targetY + h_px > wa.bottom) targetY = wa.bottom - h_px;
    if (targetX < wa.left) targetX = wa.left;
    if (targetY < wa.top) targetY = wa.top;

    pt.x = targetX;
    pt.y = targetY;

    POINT popupCenter;
    popupCenter.x = targetX + w_px / 2;
    popupCenter.y = targetY + h_px / 2;

    s_instance->StartFileSelectionQuery(prevActive, clickPt, popupCenter);

    // 3. Handle window (create or reposition) and ensure stable render target
    HWND hwnd = s_instance->GetHWND();
    if (hwnd)
    {
        if (s_instance->EnsureD2D() && s_instance->m_rt)
        {
            s_instance->m_rt->SetDpi(scale * 96.0f, scale * 96.0f);
            UIStyle::Typography::ApplyRenderTargetTextDefaults(s_instance->m_rt.Get());
            s_instance->m_bgCap.Reset();
            s_instance->m_bgFinal.Reset();
            s_instance->m_compositeRt.Reset();
            s_instance->m_effectWinSize = {};
        }
        SetWindowPos(hwnd, HWND_TOPMOST, pt.x, pt.y, w_px, h_px, SWP_NOACTIVATE);
        if (s_instance->EnsureD2D())
        {
            s_instance->EnsureIcons();
        }
        if (!IsWindowVisible(hwnd))
        {
            ShowWindow(hwnd, SW_SHOW);
        }
        SetActiveWindow(hwnd);
        SetForegroundWindow(hwnd);
    }
    else
    {
        s_instance->Create(L"", WS_POPUP, WS_EX_TOOLWINDOW | WS_EX_TOPMOST, pt.x, pt.y, w_px, h_px, parent);
        if (s_instance->GetHWND())
        {
            SetWindowDisplayAffinitySafe(s_instance->GetHWND());
            s_instance->ApplySystemBackdrop();
            if (s_instance->EnsureD2D())
            {
                if (s_instance->m_rt)
                {
                    s_instance->m_rt->SetDpi(scale * 96.0f, scale * 96.0f);
                    UIStyle::Typography::ApplyRenderTargetTextDefaults(s_instance->m_rt.Get());
                }
                s_instance->EnsureIcons();
            }
            ShowWindow(s_instance->GetHWND(), SW_SHOW);
            SetActiveWindow(s_instance->GetHWND());
            SetForegroundWindow(s_instance->GetHWND());
        }
    }

    if (s_instance->GetHWND())
    {
        if (s_instance->m_rt)
        {
            s_instance->m_rt->SetDpi(scale * 96.0f, scale * 96.0f);
            UIStyle::Typography::ApplyRenderTargetTextDefaults(s_instance->m_rt.Get());
        }

        s_instance->m_hovered = -1;
        s_instance->m_trackMouse = false;
        s_instance->m_animating = false;
        s_instance->m_scrollPosition = (float)s_instance->m_currentPage;
        s_instance->m_scrollVelocity = 0.0f;
        s_instance->m_searchActive = s_instance->m_appCtx && s_instance->m_appCtx->configService
            ? s_instance->m_appCtx->configService->GetSearchMode()
            : false;
        s_instance->m_searchQuery.clear();
        s_instance->m_searchResults.clear();
        s_instance->m_selectedSearchResult = -1;
        s_instance->m_hoveredTab = -1;
        s_instance->m_hoveredDock = -1;
        s_instance->m_cursorBlink = true;
        s_instance->ResetPressedShortcut();

        // Initialize or update the bounds of the search textbox
        D2D1_RECT_F topRect = D2D1::RectF(
            (float)wndPad,
            (float)wndPad,
            (float)w - wndPad,
            (float)wndPad + 22.0f
        );

        if (!s_instance->m_searchTextBoxCreated)
        {
            UIStyle::TextBoxStyle style;
            style.bgNormal = UIStyle::ThemeColor::ButtonBgNormal();
            style.borderNormal = UIStyle::ThemeColor::ButtonBorderNormal();
            style.borderFocused = UIStyle::ThemeColor::ButtonBorderNormal();
            style.paddingLeft = 28.0f; // Leave space for magnifying glass
            style.paddingTop = 4.0f;
            style.paddingBottom = 4.0f;
            style.paddingRight = 8.0f;
            style.fontSize = s_instance->GetFontSize();
            s_instance->m_searchTextBox.SetStyle(style);

            s_instance->m_searchTextBox.Create(s_instance->GetHWND(), s_instance->m_dw.Get(), topRect, L"");
            s_instance->m_searchTextBoxCreated = true;
        }
        else
        {
            s_instance->m_searchTextBox.SetBounds(topRect);
            s_instance->m_searchTextBox.UpdateLayout(scale);
            s_instance->m_searchTextBox.SetText(L"");
        }

        s_instance->m_searchTextBox.SetFocus(s_instance->m_searchActive);

        if (s_instance->m_searchActive)
        {
            s_instance->m_searchTextBox.UpdateImeWindowPosition(s_instance->GetHWND(), scale);
        }
 
        SetFocus(s_instance->GetHWND());

        s_instance->StartAutoHideTimer();

        s_instance->m_bgCaptureDirty = true;
        s_instance->m_bgCompositeDirty = true;
        double bgStart = GetTimeInSeconds();
        s_instance->CaptureBackground();
        s_instance->CompositeBackgroundToCache();
        double bgElapsedMs = (GetTimeInSeconds() - bgStart) * 1000.0;
        if (bgElapsedMs >= POPUP_SLOW_FRAME_MS)
        {
            LOG_G_WORNING(L"PopupWindow perf: initial background refresh took %.2fms", bgElapsedMs);
        }
        InvalidateRect(s_instance->GetHWND(), nullptr, FALSE);

        if (s_instance->m_viewModel)
            s_instance->m_viewModel->NotifyPopupShown();

        double showElapsedMs = (GetTimeInSeconds() - showStart) * 1000.0;
        if (showElapsedMs >= POPUP_SLOW_SHOW_MS)
        {
            LOG_G_WORNING(
                L"PopupWindow perf: Show took %.2fms pages=%d dockItems=%d window=%dx%d",
                showElapsedMs,
                (int)s_instance->m_pages.size(),
                (int)s_instance->m_dockPage.shortcuts.size(),
                w_px,
                h_px);
        }
    }
}

void PopupWindow::Hide()
{
    if (s_instance)
    {
        HWND h = s_instance->GetHWND();
        if (h)
        {
            if (GetCapture() == h)
            {
                ReleaseCapture();
            }
            s_instance->ResetPressedShortcut();
            s_instance->StopAutoHideTimer();
            KillTimer(h, POPUP_ANIMATION_TIMER_ID);
            KillTimer(h, TIMELINE_ANIMATION_TIMER_ID);
            s_instance->m_animating = false;
        }

        if (h && UIStyle::Animation::IsEnabled())
        {
            s_instance->StartCloseTransition([h]() {
                ShowWindow(h, SW_HIDE);
                if (s_instance && s_instance->m_viewModel)
                {
                    s_instance->m_viewModel->SetCurrentPage(s_instance->m_currentPage);
                    s_instance->m_viewModel->NotifyPopupHidden();
                }
            });
        }
        else
        {
            if (h) ShowWindow(h, SW_HIDE);
            if (s_instance && s_instance->m_viewModel)
            {
                s_instance->m_viewModel->SetCurrentPage(s_instance->m_currentPage);
                s_instance->m_viewModel->NotifyPopupHidden();
            }
        }
    }
}

void PopupWindow::ResetPressedShortcut()
{
    m_pressedShortcutKind = PressedShortcutKind::None;
    m_pressedShortcutIndex = -1;
    m_pressedShortcutPage = -1;
}

void PopupWindow::Release()
{
    if (s_instance)
    {
        PopupWindow* inst = s_instance;
        s_instance = nullptr;

        HWND h = inst->GetHWND();
        if (h)
        {
            inst->StopAutoHideTimer();
            KillTimer(h, POPUP_ANIMATION_TIMER_ID);
            KillTimer(h, TIMELINE_ANIMATION_TIMER_ID);
            DestroyWindow(h);
        }

        delete inst;
    }
}

void PopupWindow::SavePopupConfig()
{
    if (!m_appCtx || !m_appCtx->configService || !m_viewModel)
        return;

    std::vector<Model::PopupPage> allPages;
    allPages.push_back(m_viewModel->GetDockPage());
    const auto& pages = m_viewModel->GetPages();
    allPages.insert(allPages.end(), pages.begin(), pages.end());
    m_appCtx->configService->SaveConfig(allPages);
}

void PopupWindow::StartAutoHideTimer()
{
    SetTimer(GetHWND(), AUTO_HIDE_TIMER_ID, 500, nullptr); // Reduced polling from 100ms to 500ms
}

void PopupWindow::StopAutoHideTimer()
{
    HWND h = GetHWND();
    if (h) KillTimer(h, AUTO_HIDE_TIMER_ID);
}

float PopupWindow::GetFontSize() const
{
    // Dynamically relate font size to the icon size:
    // Base size is 8.0f at icon size 16.0f, scaling linearly by 0.15f per pixel of icon size.
    // E.g., at default 24px icon size, fontSize = 8.0f + 8 * 0.15f = 9.2f (minimum 9.0f).
    // For 32px icon size, fontSize = 8.0f + 16 * 0.15f = 10.4f.
    float size = 8.0f + (GetIconSize() - 16.0f) * 0.15f;
    return size < 9.0f ? 9.0f : size; // 限制最小字体尺寸不低于 9.0f，防止中文小字号时缩为一坨黑
}

int PopupWindow::GetLabelHeight() const
{
    // Label height dynamically scales with font size to keep visual spacing clean
    return (int)(GetFontSize() * 1.5f + 1.0f);
}

void PopupWindow::UpdateTextFormat()
{
    if (!m_dw) return;

    m_popupTextFormat.Reset();
    m_searchTextFormat.Reset();

    float fontSize = GetFontSize();

    UIStyle::Typography::CreateTextFormat(
        m_dw.Get(),
        &m_popupTextFormat,
        fontSize,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_TEXT_ALIGNMENT_CENTER,
        DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    UIStyle::Typography::CreateTextFormat(
        m_dw.Get(),
        &m_searchTextFormat,
        fontSize,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_TEXT_ALIGNMENT_LEADING,
        DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

void PopupWindow::UpdateSearch()
{
    m_searchResults.clear();
    m_selectedSearchResult = -1;
    if (m_searchQuery.empty()) return;

    std::wstring queryLower = m_searchQuery;
    std::transform(queryLower.begin(), queryLower.end(), queryLower.begin(), [](wchar_t c) {
        return (wchar_t)towlower(c);
    });

    for (size_t pIndex = 0; pIndex < m_pages.size(); pIndex++)
    {
        const auto& page = m_pages[pIndex];
        for (size_t sIndex = 0; sIndex < page.shortcuts.size(); sIndex++)
        {
            const auto& sc = page.shortcuts[sIndex];
            std::wstring nameLower = sc.name;
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), [](wchar_t c) {
                return (wchar_t)towlower(c);
            });

            if (nameLower.find(queryLower) != std::wstring::npos)
            {
                SearchResultItem item;
                item.shortcut = sc;
                if (sIndex < page.iconBitmaps.size())
                {
                    item.bitmap = page.iconBitmaps[sIndex];
                }
                item.originalPageIndex = (int)pIndex;
                item.originalShortcutIndex = (int)sIndex;
                m_searchResults.push_back(item);
            }
        }
    }

    // Also search dock page shortcuts
    for (size_t sIndex = 0; sIndex < m_dockPage.shortcuts.size(); sIndex++)
    {
        const auto& sc = m_dockPage.shortcuts[sIndex];
        std::wstring nameLower = sc.name;
        std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), [](wchar_t c) {
            return (wchar_t)towlower(c);
        });
        if (nameLower.find(queryLower) != std::wstring::npos)
        {
            SearchResultItem item;
            item.shortcut = sc;
            if (sIndex < m_dockPage.iconBitmaps.size())
                item.bitmap = m_dockPage.iconBitmaps[sIndex];
            item.originalPageIndex = -2; // Sentinel: dock page
            item.originalShortcutIndex = (int)sIndex;
            m_searchResults.push_back(item);
        }
    }
}

void PopupWindow::UpdateImeWindowPosition()
{
    m_searchTextBox.UpdateImeWindowPosition(GetHWND(), GetWindowScale(GetHWND()));
}

void PopupWindow::DrawTopBar(ID2D1HwndRenderTarget* rt)
{
    int wndPad = GetWndPadding();
    RECT cr; GetClientRect(GetHWND(), &cr);
    float scale = GetWindowScale(GetHWND());
    float w = (float)cr.right / scale;
    
    D2D1_RECT_F topRect = D2D1::RectF(
        (float)wndPad,
        (float)wndPad,
        w - wndPad,
        (float)wndPad + 22.0f
    );

    if (m_searchActive)
    {
        // Draw textbox background, border, selection, text, and caret
        m_searchTextBox.Paint(rt, scale);

        // 2. Draw magnifying glass icon centered inside 22px height
        float cx = topRect.left + 16.0f;
        float cy = topRect.top + 11.0f;
        auto iconBrush = GetOrCreateBrush(UIStyle::ThemeColor::TextMuted().d2d);
        if (iconBrush)
        {
            rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 4.0f, 4.0f), iconBrush.Get(), UIStyle::Metrics::IconStroke());
            rt->DrawLine(D2D1::Point2F(cx + 2.8f, cy + 2.8f), D2D1::Point2F(cx + 6.5f, cy + 6.5f), iconBrush.Get(), UIStyle::Metrics::IconStroke());
        }

        // 3. Draw placeholder if empty
        if (m_searchTextBox.IsEmpty())
        {
            D2D1_COLOR_F placeholderColor = UIStyle::ThemeColor::TextNormal().d2d;
            placeholderColor.a = 0.4f;
            auto placeholderBrush = GetOrCreateBrush(placeholderColor);
            if (placeholderBrush && m_searchTextFormat)
            {
                rt->DrawTextW(L"搜索...", 5, m_searchTextFormat.Get(),
                    D2D1::RectF(topRect.left + 28.0f, topRect.top, topRect.right - 8.0f, topRect.bottom),
                    placeholderBrush.Get());
            }
        }
    }
    else
    {
        // Draw tabs
        int numPages = (int)m_pages.size();
        if (numPages > 0)
        {
            float totalWidth = topRect.right - topRect.left;
            float tabWidth = totalWidth / numPages;
            for (int i = 0; i < numPages; i++)
            {
                D2D1_RECT_F tabRect = D2D1::RectF(
                    topRect.left + i * tabWidth,
                    topRect.top,
                    topRect.left + (i + 1) * tabWidth,
                    topRect.bottom
                );

                if (i == m_hoveredTab)
                {
                    D2D1_ROUNDED_RECT roundedTab = D2D1::RoundedRect(tabRect, 4.0f, 4.0f);
                    auto hoverBg = GetOrCreateBrush(UIStyle::ThemeColor::ButtonBgHover().d2d);
                    if (hoverBg) rt->FillRoundedRectangle(roundedTab, hoverBg.Get());
                }

                // Dynamic opacity transition based on m_scrollPosition distance
                float dist = std::abs((float)i - m_scrollPosition);
                if (numPages > 1)
                {
                    float halfN = (float)numPages / 2.0f;
                    if (dist > halfN) dist = (float)numPages - dist;
                }
                float factor = 1.0f - dist;
                if (factor < 0.0f) factor = 0.0f;
                if (factor > 1.0f) factor = 1.0f;
                float alpha = 0.6f + factor * 0.4f;

                D2D1_COLOR_F textColor = UIStyle::ThemeColor::TextNormal().d2d;
                textColor.a = alpha;

                auto tabTextBrush = GetOrCreateBrush(textColor);
                if (tabTextBrush && m_popupTextFormat)
                {
                    rt->DrawTextW(m_pages[i].name.c_str(), (UINT32)m_pages[i].name.size(), m_popupTextFormat.Get(),
                        tabRect, tabTextBrush.Get());
                }
            }

            // Draw one single sliding selection indicator line after the loop
            float lineW = 32.0f;
            float lineH = 1.35f;
            float lineX = topRect.left + m_scrollPosition * tabWidth + (tabWidth - lineW) / 2.0f;
            float lineY = topRect.bottom - 1.0f;
            D2D1_ROUNDED_RECT indicatorLine = D2D1::RoundedRect(
                D2D1::RectF(lineX, lineY, lineX + lineW, lineY + lineH),
                1.0f, 1.0f
            );
            D2D1_COLOR_F accentLine = UIStyle::ThemeColor::Accent().d2d;
            accentLine.a = 0.82f;
            auto accentBrush = GetOrCreateBrush(accentLine);
            if (accentBrush) rt->FillRoundedRectangle(indicatorLine, accentBrush.Get());
        }
    }
}

void PopupWindow::DrawSearchResults(ID2D1HwndRenderTarget* rt)
{
    int n = (int)m_searchResults.size();
    RECT cr; GetClientRect(GetHWND(), &cr);
    float scale = GetWindowScale(GetHWND());
    float w = (float)cr.right / scale;

    int topBarHeight = 32;

    if (n == 0)
    {
        auto textBrush = GetOrCreateBrush(UIStyle::ThemeColor::TextMuted().d2d);
        if (textBrush && m_popupTextFormat)
        {
            rt->DrawTextW(L"无匹配结果", 5, m_popupTextFormat.Get(),
                D2D1::RectF(0.0f, (float)topBarHeight + 40.0f, w, (float)topBarHeight + 100.0f),
                textBrush.Get());
        }
        return;
    }

    int cols = GetColumns();
    int rows = GetRows();
    int maxCells = cols * rows;
    if (n > maxCells) n = maxCells;

    int cw = CellWidth(), ch = CellHeight();
    int wndPad = GetWndPadding();
    int iconGap = GetIconGap();
    float iconRad = (float)GetIconRadius();
    float cardRad = iconRad + 2.0f;

    // Card backgrounds
    for (int i = 0; i < n; i++)
    {
        float ix = (float)(wndPad + (i % cols) * cw);
        float iy = (float)(wndPad + (i / cols) * ch + topBarHeight);
        bool isSelected = (i == m_selectedSearchResult);
        bool isHovered = (i == m_hovered);

        D2D1_RECT_F cardRect = D2D1::RectF(ix, iy, ix + cw - iconGap, iy + ch - iconGap);
        D2D1_ROUNDED_RECT roundedCard = D2D1::RoundedRect(cardRect, cardRad, cardRad);

        ComPtr<ID2D1SolidColorBrush> bg;
        if (isSelected)
        {
            bg = GetOrCreateBrush(UIStyle::ThemeColor::AccentSubtle().d2d);
        }
        else if (isHovered)
        {
            bg = GetOrCreateBrush(UIStyle::ThemeColor::ButtonBgHover().d2d);
        }
        else
        {
            bg = GetOrCreateBrush(UIStyle::ThemeColor::ButtonBgNormal().d2d);
        }

        if (bg) rt->FillRoundedRectangle(roundedCard, bg.Get());

        D2D1_COLOR_F borderColor = isSelected ? UIStyle::ThemeColor::AccentHover().d2d :
            (isHovered ? UIStyle::ThemeColor::ButtonBorderHover().d2d : UIStyle::ThemeColor::ButtonBorderNormal().d2d);
        if (isSelected) borderColor.a = 0.42f;
        auto border = GetOrCreateBrush(borderColor);
        if (border) rt->DrawRoundedRectangle(roundedCard, border.Get(), UIStyle::Metrics::ControlStroke());
    }

    // Icons
    int cellMarginX = GetCellMarginX();
    int cellMarginY = GetCellMarginY();
    int iconSize = GetIconSize();

    for (int i = 0; i < n; i++)
    {
        float ix = (float)(wndPad + (i % cols) * cw);
        float iy = (float)(wndPad + (i / cols) * ch + topBarHeight);
        auto* bmp = m_searchResults[i].bitmap;
        if (bmp)
        {
            float iconX = ix + cellMarginX;
            float iconY = iy + cellMarginY;
            D2D1_RECT_F iconRect = IconRenderer::AlignToPixels(rt, iconX, iconY, (float)iconSize, (float)iconSize);
            rt->DrawBitmap(bmp, iconRect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        }
    }

    // Labels
    if (m_popupTextFormat)
    {
        for (int i = 0; i < n; i++)
        {
            int col = i % cols, row = i / cols;
            float lx = (float)(wndPad + col * cw);
            float ly = (float)(wndPad + row * ch + cellMarginY + iconSize + 2 + topBarHeight);
            auto& nm = m_searchResults[i].shortcut.name;
            
            auto tb = GetOrCreateBrush(UIStyle::ThemeColor::TextNormal().d2d);
            if (tb)
            {
                rt->DrawTextW(nm.c_str(), (UINT32)nm.size(), m_popupTextFormat.Get(),
                    D2D1::RectF(lx + 2, ly, lx + cw - iconGap - 2, ly + GetLabelHeight()),
                    tb.Get());
            }
        }
    }
}

void PopupWindow::EnsureIcons()
{
    UpdateTextFormat();

    if (m_pages.empty() || !m_rt) return;

    float currentDpi = 96.0f;
    if (m_rt)
    {
        float dpiY = 96.0f;
        m_rt->GetDpi(&currentDpi, &dpiY);
    }

    const int iconBitmapSize = IconRenderer::GetRecommendedBitmapSize(m_rt.Get(), static_cast<float>(GetIconSize()));
    bool rtChanged = (m_rt.Get() != m_lastRt) || (currentDpi != m_lastDpi) || (iconBitmapSize != m_lastIconBitmapSize);
    if (rtChanged)
    {
        m_lastRt = m_rt.Get();
        m_lastDpi = currentDpi;
        m_lastIconBitmapSize = iconBitmapSize;
        m_bmpBrushCache.clear();
    }

    bool anyRecreated = false;
    auto iconSvc = m_appCtx && m_appCtx->iconService ? m_appCtx->iconService.get() : m_iconService.get();

    for (auto& page : m_pages)
    {
        int n = (int)page.shortcuts.size();
        bool needRecreate = rtChanged || (page.iconBitmaps.size() != (size_t)n);
        if (needRecreate)
        {
            anyRecreated = true;
            for (auto* bmp : page.iconBitmaps)
            {
                if (bmp) bmp->Release();
            }
            page.iconBitmaps.clear();
            page.iconBitmaps.resize(n, nullptr);
            m_bmpBrushCache.clear();

            for (int i = 0; i < n; i++)
            {
                bool invert = (UIStyle::GetThemeMode() == UIStyle::ThemeMode::Light) ? page.shortcuts[i].iconInvertLight : page.shortcuts[i].iconInvertDark;
                if (ShouldRenderGeneratedDefaultIcon(page, page.shortcuts[i]) || page.shortcuts[i].hIcon == nullptr)
                {
                    page.iconBitmaps[i] = IconRenderer::CreateDefaultIcon(m_rt.Get(), GetDWFactory(), page.shortcuts[i].name, iconBitmapSize).Detach();
                }
                else
                {
                    page.iconBitmaps[i] = iconSvc->IconToBitmap(m_rt.Get(), page.shortcuts[i].hIcon, iconBitmapSize, invert);
                }
            }
        }
    }

    // Recreate dock page bitmaps
    {
        int dn = (int)m_dockPage.shortcuts.size();
        bool needRecreate = rtChanged || (m_dockPage.iconBitmaps.size() != (size_t)dn);
        if (needRecreate)
        {
            anyRecreated = true;
            for (auto* bmp : m_dockPage.iconBitmaps)
                if (bmp) bmp->Release();
            m_dockPage.iconBitmaps.clear();
            m_dockPage.iconBitmaps.resize(dn, nullptr);
            m_bmpBrushCache.clear();
            for (int i = 0; i < dn; i++)
            {
                bool invert = (UIStyle::GetThemeMode() == UIStyle::ThemeMode::Light) ? m_dockPage.shortcuts[i].iconInvertLight : m_dockPage.shortcuts[i].iconInvertDark;
                if (ShouldRenderGeneratedDefaultIcon(m_dockPage, m_dockPage.shortcuts[i]) || m_dockPage.shortcuts[i].hIcon == nullptr)
                {
                    m_dockPage.iconBitmaps[i] = IconRenderer::CreateDefaultIcon(m_rt.Get(), GetDWFactory(), m_dockPage.shortcuts[i].name, iconBitmapSize).Detach();
                }
                else
                {
                    m_dockPage.iconBitmaps[i] = iconSvc->IconToBitmap(m_rt.Get(), m_dockPage.shortcuts[i].hIcon, iconBitmapSize, invert);
                }
            }
        }
    }

    if (anyRecreated && m_searchActive && !m_searchQuery.empty())
    {
        UpdateSearch();
    }
}

void PopupWindow::RefreshIcons()
{
    if (!m_rt || m_refreshingIcons) return;
    m_refreshingIcons = true;

    // Step 1: Destroy all HICONs and clear D2D bitmaps → icons become default placeholders
    for (auto& page : m_pages)
    {
        for (auto& sc : page.shortcuts)
        {
            if (sc.hIcon) { DestroyIcon(sc.hIcon); sc.hIcon = nullptr; }
        }
        for (auto* bmp : page.iconBitmaps)
            if (bmp) bmp->Release();
        page.iconBitmaps.clear();
    }
    for (auto& sc : m_dockPage.shortcuts)
    {
        if (sc.hIcon) { DestroyIcon(sc.hIcon); sc.hIcon = nullptr; }
    }
    for (auto* bmp : m_dockPage.iconBitmaps)
        if (bmp) bmp->Release();
    m_dockPage.iconBitmaps.clear();
    m_bmpBrushCache.clear();

    // Let the normal paint pass show placeholders without blocking the UI thread.
    InvalidateRect(GetHWND(), nullptr, FALSE);
    UpdateWindow(GetHWND());

    // Step 2: re-extract HICONs and rebuild bitmaps in next message
    PostMessage(GetHWND(), WM_USER_REFRESH_ICONS, 0, 0);
}

void PopupWindow::DrawPage(ID2D1HwndRenderTarget* rt, int pageIndex)
{
    if (pageIndex < 0 || pageIndex >= (int)m_pages.size()) return;

    const auto& page = m_pages[pageIndex];
    int n = (int)page.shortcuts.size();
    if (n == 0) return;

    int cols = GetColumns();
    int rows = GetRows();
    int maxCells = cols * rows;
    if (n > maxCells) n = maxCells;

    int cw = CellWidth(), ch = CellHeight();
    int wndPad = GetWndPadding();
    int iconGap = GetIconGap();
    float iconRad = (float)GetIconRadius();
    float cardRad = iconRad + 2.0f;
    int topBarHeight = 32;

    // Card backgrounds
    for (int i = 0; i < n; i++)
    {
        float ix = (float)(wndPad + (i % cols) * cw);
        float iy = (float)(wndPad + (i / cols) * ch + topBarHeight);
        bool isHovered = (pageIndex == m_currentPage && i == m_hovered);

        D2D1_RECT_F cardRect = D2D1::RectF(ix, iy, ix + cw - iconGap, iy + ch - iconGap);
        D2D1_ROUNDED_RECT roundedCard = D2D1::RoundedRect(cardRect, cardRad, cardRad);

        auto bg = GetOrCreateBrush(isHovered ? UIStyle::ThemeColor::ButtonBgHover().d2d : UIStyle::ThemeColor::ButtonBgNormal().d2d);
        if (bg) rt->FillRoundedRectangle(roundedCard, bg.Get());

        auto border = GetOrCreateBrush(isHovered ? UIStyle::ThemeColor::ButtonBorderHover().d2d : UIStyle::ThemeColor::ButtonBorderNormal().d2d);
        if (border) rt->DrawRoundedRectangle(roundedCard, border.Get(), UIStyle::Metrics::ControlStroke());
    }

    // Icons
    int cellMarginX = GetCellMarginX();
    int cellMarginY = GetCellMarginY();
    int iconSize = GetIconSize();

    for (int i = 0; i < n; i++)
    {
        float ix = (float)(wndPad + (i % cols) * cw);
        float iy = (float)(wndPad + (i / cols) * ch + topBarHeight);
        if (i < (int)page.iconBitmaps.size() && page.iconBitmaps[i])
        {
            float iconX = ix + cellMarginX;
            float iconY = iy + cellMarginY;
            D2D1_RECT_F iconRect = IconRenderer::AlignToPixels(rt, iconX, iconY, (float)iconSize, (float)iconSize);

            auto* bmp = page.iconBitmaps[i];
            rt->DrawBitmap(bmp, iconRect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        }
    }

    // Labels
    if (m_popupTextFormat)
    {
        auto tb = GetOrCreateBrush(UIStyle::ThemeColor::TextNormal().d2d);
        if (tb)
        {
            for (int i = 0; i < n; i++)
            {
                int col = i % cols, row = i / cols;
                float lx = (float)(wndPad + col * cw);
                float ly = (float)(wndPad + row * ch + cellMarginY + iconSize + 2 + topBarHeight);
                auto& nm = page.shortcuts[i].name;
                rt->DrawTextW(nm.c_str(), (UINT32)nm.size(), m_popupTextFormat.Get(),
                    D2D1::RectF(lx + 2, ly, lx + cw - iconGap - 2, ly + GetLabelHeight()),
                    tb.Get());
            }
        }
    }
}

void PopupWindow::OnPaintContent(ID2D1HwndRenderTarget* rt)
{
    // Pin indicator
    if (m_pinned)
    {
        RECT cr; GetClientRect(GetHWND(), &cr);
        float scale = GetWindowScale(GetHWND());
        float w = (float)cr.right / scale;
        auto pb = GetOrCreateBrush(D2D1::ColorF(0, 0.8f, 0, 1));
        if (pb)
        {
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(w - 10, 10), 4, 4), pb.Get());
        }
    }

    EnsureIcons();

    DrawTopBar(rt);

    if (m_pages.empty()) return;

    RECT cr; GetClientRect(GetHWND(), &cr);
    float scale = GetWindowScale(GetHWND());
    float w = (float)cr.right / scale;

    if (m_searchActive && !m_searchQuery.empty())
    {
        DrawSearchResults(rt);
    }
    else
    {
        D2D1_MATRIX_3X2_F originalTransform;
        rt->GetTransform(&originalTransform);

        int numPages = (int)m_pages.size();
        for (int i = 0; i < numPages; i++)
        {
            float diff = (float)i - m_scrollPosition;
            if (numPages > 1)
            {
                float halfN = (float)numPages / 2.0f;
                if (diff > halfN) diff -= (float)numPages;
                else if (diff < -halfN) diff += (float)numPages;
            }

            float offsetX = diff * w;
            if (std::abs(offsetX) < w)
            {
                rt->SetTransform(D2D1::Matrix3x2F::Translation(offsetX, 0.0f) * originalTransform);
                DrawPage(rt, i);
            }
        }
        rt->SetTransform(originalTransform);
    }

    DrawDock(rt);
}

void PopupWindow::DrawDock(ID2D1HwndRenderTarget* rt)
{
    int dockRows = GetDockHeight();  // dockHeight stores row count
    int cols    = GetColumns();
    int cw      = CellWidth();
    int ch      = CellHeight();
    int wndPad  = GetWndPadding();
    int iconGap = GetIconGap();
    int iconSize = GetIconSize();
    int cellMarginX = GetCellMarginX();
    int cellMarginY = GetCellMarginY();
    float iconRad = (float)GetIconRadius();
    float cardRad = iconRad + 2.0f;
    int topBarHeight = 32;

    // Gap between upper section and dock = 2 * wndPad, dividing line in the middle
    int mainRows = GetRows();
    int mainGridCardBottom = wndPad + mainRows * ch - iconGap + topBarHeight;
    int lineY = mainGridCardBottom + wndPad;
    int dockTopY = lineY + wndPad;

    // Separator line
    RECT cr2; GetClientRect(GetHWND(), &cr2);
    float scale2 = GetWindowScale(GetHWND());
    float totalW = (float)cr2.right / scale2;

    D2D1_COLOR_F baseClr = UIStyle::ThemeColor::ThemeBase().d2d;
    float lineAlpha = (UIStyle::GetThemeMode() == UIStyle::ThemeMode::Light) ? 1.0f : 0.25f;
    auto lineBrush = GetOrCreateBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, lineAlpha));
    if (lineBrush)
        rt->DrawLine(
            D2D1::Point2F(0.0f, (float)lineY),
            D2D1::Point2F(totalW, (float)lineY),
            lineBrush.Get(), 0.3f);

    // Active file selection feedback (timeline)
    double now = GetTimeInSeconds();
    double elapsed = 0.0;
    bool hasActiveSelection = false;
    {
        std::lock_guard<std::mutex> lock(m_selectedFilesMutex);
        if (!m_selectedFilesCtx.isPending && !m_selectedFilesCtx.filePaths.empty())
        {
            elapsed = now - m_selectedFilesCtx.capturedTime;
            if (elapsed < FILE_SELECTION_VALIDITY_DURATION)
            {
                hasActiveSelection = true;
            }
        }
    }

    if (hasActiveSelection)
    {
        float progress = (float)(1.0 - (elapsed / FILE_SELECTION_VALIDITY_DURATION));
        if (progress < 0.0f) progress = 0.0f;
        if (progress > 1.0f) progress = 1.0f;

        float midX = totalW / 2.0f;
        float halfLength = (totalW / 2.0f) * progress;
        float left = midX - halfLength;
        float right = midX + halfLength;

        if (right > left + 0.1f)
        {
            D2D1_POINT_2F startPt = D2D1::Point2F(left, (float)lineY);
            D2D1_POINT_2F endPt = D2D1::Point2F(right, (float)lineY);

            D2D1_GRADIENT_STOP stops[3];
            stops[0].position = 0.0f;
            stops[0].color = UIStyle::ThemeColor::AccentSubtle().d2d;
            stops[1].position = 0.5f;
            stops[1].color = UIStyle::ThemeColor::Accent().d2d;
            stops[2].position = 1.0f;
            stops[2].color = UIStyle::ThemeColor::AccentSubtle().d2d;

            ComPtr<ID2D1GradientStopCollection> stopCollection;
            HRESULT hr = rt->CreateGradientStopCollection(stops, 3, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &stopCollection);
            if (SUCCEEDED(hr) && stopCollection)
            {
                ComPtr<ID2D1LinearGradientBrush> gradientBrush;
                hr = rt->CreateLinearGradientBrush(
                    D2D1::LinearGradientBrushProperties(startPt, endPt),
                    stopCollection.Get(),
                    &gradientBrush
                );
                if (SUCCEEDED(hr) && gradientBrush)
                {
                    rt->DrawLine(startPt, endPt, gradientBrush.Get(), 1.5f);
                }
            }
        }
    }

    int n = (int)m_dockPage.shortcuts.size();
    if (n == 0) return;

    int maxCells = cols * dockRows;
    if (n > maxCells) n = maxCells;

    // Card backgrounds — identical to DrawPage
    for (int i = 0; i < n; i++)
    {
        float ix = (float)(wndPad + (i % cols) * cw);
        float iy = (float)(dockTopY + (i / cols) * ch);
        bool isHovered = (i == m_hoveredDock);

        D2D1_RECT_F cardRect = D2D1::RectF(ix, iy, ix + cw - iconGap, iy + ch - iconGap);
        D2D1_ROUNDED_RECT roundedCard = D2D1::RoundedRect(cardRect, cardRad, cardRad);

        auto bg = GetOrCreateBrush(isHovered ? UIStyle::ThemeColor::ButtonBgHover().d2d : UIStyle::ThemeColor::ButtonBgNormal().d2d);
        if (bg) rt->FillRoundedRectangle(roundedCard, bg.Get());

        auto border = GetOrCreateBrush(isHovered ? UIStyle::ThemeColor::ButtonBorderHover().d2d : UIStyle::ThemeColor::ButtonBorderNormal().d2d);
        if (border) rt->DrawRoundedRectangle(roundedCard, border.Get(), UIStyle::Metrics::ControlStroke());
    }

    // Icons — identical to DrawPage
    for (int i = 0; i < n; i++)
    {
        float ix = (float)(wndPad + (i % cols) * cw);
        float iy = (float)(dockTopY + (i / cols) * ch);

        if (i < (int)m_dockPage.iconBitmaps.size() && m_dockPage.iconBitmaps[i])
        {
            float iconX = ix + cellMarginX;
            float iconY = iy + cellMarginY;
            D2D1_RECT_F iconRect = IconRenderer::AlignToPixels(rt, iconX, iconY, (float)iconSize, (float)iconSize);

            auto* bmp = m_dockPage.iconBitmaps[i];
            rt->DrawBitmap(bmp, iconRect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        }
    }

    // Labels — identical to DrawPage
    if (m_popupTextFormat)
    {
        auto tb = GetOrCreateBrush(UIStyle::ThemeColor::TextNormal().d2d);
        if (tb)
        {
            for (int i = 0; i < n; i++)
            {
                int col = i % cols, row = i / cols;
                float lx = (float)(wndPad + col * cw);
                float ly = (float)(dockTopY + row * ch + cellMarginY + iconSize + 2);
                auto& nm = m_dockPage.shortcuts[i].name;
                rt->DrawTextW(nm.c_str(), (UINT32)nm.size(), m_popupTextFormat.Get(),
                    D2D1::RectF(lx + 2, ly, lx + cw - iconGap - 2, ly + GetLabelHeight()),
                    tb.Get());
            }
        }
    }
}

int PopupWindow::HitTest(POINT pt)
{
    int topBarHeight = 32;
    int cols = GetColumns();
    int rows = GetRows();
    int cw = CellWidth(), ch = CellHeight();
    int wndPad = GetWndPadding();
    int iconGap = GetIconGap();

    if (m_searchActive && !m_searchQuery.empty())
    {
        int n = (int)m_searchResults.size();
        int maxCells = cols * rows;
        if (n > maxCells) n = maxCells;

        for (int i = 0; i < n; i++)
        {
            int col = i % cols, row = i / cols;
            RECT rc{
                wndPad + col * cw,
                wndPad + row * ch + topBarHeight,
                wndPad + col * cw + cw - iconGap,
                wndPad + row * ch + ch - iconGap + topBarHeight
            };
            if (PtInRect(&rc, pt)) return i;
        }
        return -1;
    }

    if (m_pages.empty() || m_currentPage < 0 || m_currentPage >= (int)m_pages.size())
        return -1;

    const auto& page = m_pages[m_currentPage];
    int n = (int)page.shortcuts.size();
    int maxCells = cols * rows;
    if (n > maxCells) n = maxCells;

    for (int i = 0; i < n; i++)
    {
        int col = i % cols, row = i / cols;
        RECT rc{
            wndPad + col * cw,
            wndPad + row * ch + topBarHeight,
            wndPad + col * cw + cw - iconGap,
            wndPad + row * ch + ch - iconGap + topBarHeight
        };
        if (PtInRect(&rc, pt)) return i;
    }
    return -1;
}

int PopupWindow::HitTestDot(POINT pt)
{
    return -1;
}

int PopupWindow::HitTestDock(POINT pt)
{
    int dockRows    = GetDockHeight();  // row count
    int cols        = GetColumns();
    int cw          = CellWidth();
    int ch          = CellHeight();
    int wndPad      = GetWndPadding();
    int iconGap     = GetIconGap();
    int mainRows    = GetRows();
    int topBarHeight = 32;

    int mainGridCardBottom = wndPad + mainRows * ch - iconGap + topBarHeight;
    int lineY = mainGridCardBottom + wndPad;
    int dockTopY = lineY + wndPad;

    if (pt.y < dockTopY) return -1;

    int n = (int)m_dockPage.shortcuts.size();
    int maxCells = cols * dockRows;
    if (n > maxCells) n = maxCells;

    for (int i = 0; i < n; i++)
    {
        int col = i % cols, row = i / cols;
        RECT rc {
            wndPad + col * cw,
            dockTopY + row * ch,
            wndPad + col * cw + cw - iconGap,
            dockTopY + row * ch + ch - iconGap
        };
        if (PtInRect(&rc, pt)) return i;
    }
    return -1;
}

void PopupWindow::StartPageAnimationLoop()
{
    HWND hWnd = GetHWND();
    if (!hWnd) return;

    if (!UIStyle::Animation::IsEnabled())
    {
        m_scrollPosition = (float)m_currentPage;
        m_scrollVelocity = 0.0f;
        m_animating = false;
        if (m_viewModel)
        {
            m_viewModel->ResetScroll();
        }
        InvalidateRect(hWnd, nullptr, FALSE);
        return;
    }

    if (!m_animating)
    {
        m_animating = true;
        m_animLastTime = GetTimeInSeconds();
    }

    SetTimer(hWnd, POPUP_ANIMATION_TIMER_ID, POPUP_ANIMATION_FRAME_MS, nullptr);
}

void PopupWindow::StepPageAnimationFrame(HWND hWnd)
{
    if (!m_animating)
    {
        KillTimer(hWnd, POPUP_ANIMATION_TIMER_ID);
        return;
    }

    if (!UIStyle::Animation::IsEnabled())
    {
        m_scrollPosition = (float)m_currentPage;
        m_scrollVelocity = 0.0f;
        m_animating = false;
        if (m_viewModel)
        {
            m_viewModel->ResetScroll();
        }
        InvalidateRect(hWnd, nullptr, FALSE);
        KillTimer(hWnd, POPUP_ANIMATION_TIMER_ID);
        return;
    }

    double frameStart = GetTimeInSeconds();
    double now = frameStart;
    float dt = (float)(now - m_animLastTime);
    m_animLastTime = now;

    if (dt > 0.1f) dt = 0.1f;
    if (dt <= 0.0f) dt = 0.001f;

    float target = (float)m_currentPage;
    float error = target - m_scrollPosition;
    int numPages = (int)m_pages.size();
    if (numPages > 1)
    {
        float halfN = (float)numPages / 2.0f;
        if (error > halfN) error -= (float)numPages;
        else if (error < -halfN) error += (float)numPages;
    }

    float stiffness = 400.0f;
    float damping = 40.0f;

    float force = error * stiffness - m_scrollVelocity * damping;
    m_scrollVelocity += force * dt;
    m_scrollPosition += m_scrollVelocity * dt;

    if (numPages > 1)
    {
        while (m_scrollPosition < 0.0f) m_scrollPosition += (float)numPages;
        while (m_scrollPosition >= (float)numPages) m_scrollPosition -= (float)numPages;
    }

    if (std::abs(error) < 0.002f && std::abs(m_scrollVelocity) < 0.05f)
    {
        m_scrollPosition = target;
        m_scrollVelocity = 0.0f;
        m_animating = false;
    }

    if (m_viewModel)
        m_viewModel->UpdateAnimation();

    InvalidateRect(hWnd, nullptr, FALSE);

    double frameElapsedMs = (GetTimeInSeconds() - frameStart) * 1000.0;
    double dtMs = (double)dt * 1000.0;
    if (frameElapsedMs >= POPUP_SLOW_FRAME_MS || dtMs >= 32.0)
    {
        static ULONGLONG s_lastSlowFrameLogMs = 0;
        ULONGLONG tick = GetTickCount64();
        if (tick - s_lastSlowFrameLogMs >= 1000)
        {
            s_lastSlowFrameLogMs = tick;
            LOG_G_WORNING(
                L"PopupWindow perf: animation frame slow work=%.2fms dt=%.2fms page=%d scroll=%.3f velocity=%.3f",
                frameElapsedMs,
                dtMs,
                m_currentPage,
                m_scrollPosition,
                m_scrollVelocity);
        }
    }

    if (!m_animating)
    {
        KillTimer(hWnd, POPUP_ANIMATION_TIMER_ID);
    }
}

LRESULT PopupWindow::HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_DPICHANGED:
    {
        RECT* const prcNewWindow = (RECT*)lParam;
        if (prcNewWindow)
        {
            float newDpiScale = UIStyle::Scaling::EffectiveScaleFactor(LOWORD(wParam) / 96.0f);
            
            int cols = GetColumns();
            int rows = GetRows();
            int w = cols * CellWidth() + GetWndPadding() * 2 - GetIconGap();
            int topBarHeight = 32;
            int dockRows = GetDockHeight();
            int ch = CellHeight();
            int wndPad = GetWndPadding();
            int iconGap = GetIconGap();
            int mainGridCardBottom = wndPad + rows * ch - iconGap + topBarHeight;
            int lineY = mainGridCardBottom + wndPad;
            int dockTopY = lineY + wndPad;
            int h = dockTopY + dockRows * ch - iconGap + wndPad;
            if (w > 900) w = 900;
            if (h > 900) h = 900;

            int w_px = (int)(w * newDpiScale);
            int h_px = (int)(h * newDpiScale);

            POINT ptRef = { prcNewWindow->left, prcNewWindow->top };
            HMONITOR hm = MonitorFromPoint(ptRef, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi{ sizeof(mi) };
            GetMonitorInfoW(hm, &mi);
            RECT wa = mi.rcWork;

            int currentX = prcNewWindow->left;
            int currentY = prcNewWindow->top;

            if (currentX + w_px > wa.right) currentX = wa.right - w_px;
            if (currentY + h_px > wa.bottom) currentY = wa.bottom - h_px;
            if (currentX < wa.left) currentX = wa.left;
            if (currentY < wa.top) currentY = wa.top;

            prcNewWindow->left = currentX;
            prcNewWindow->top = currentY;
            prcNewWindow->right = currentX + w_px;
            prcNewWindow->bottom = currentY + h_px;
        }
        
        LRESULT res = GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
        if (EnsureD2D())
        {
            EnsureIcons();
        }
        return res;
    }

    case WM_IME_STARTCOMPOSITION:
    case WM_IME_COMPOSITION:
    case WM_IME_ENDCOMPOSITION:
    {
        if (m_searchActive)
        {
            bool repaint = false;
            if (m_searchTextBox.HandleImeMessage(hWnd, uMsg, wParam, lParam, repaint))
            {
                if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
                return 0;
            }
        }
        break;
    }

    case WM_TIMER:
    {
        if (wParam == AUTO_HIDE_TIMER_ID)
        {
            if (m_searchActive)
            {
                m_searchTextBox.BlinkCaret();
                InvalidateRect(hWnd, nullptr, FALSE);
            }

            if (m_pinned) return 0;
            if (m_pressedShortcutKind != PressedShortcutKind::None) return 0;

            POINT pt; GetCursorPos(&pt); ScreenToClient(hWnd, &pt);
            RECT cr; GetClientRect(hWnd, &cr);
            if (pt.x < 0 || pt.y < 0 || pt.x >= cr.right || pt.y >= cr.bottom)
            {
                bool imeActive = false;
                HIMC hIMC = ImmGetContext(hWnd);
                if (hIMC)
                {
                    DWORD dwSize = ImmGetCandidateListW(hIMC, 0, nullptr, 0);
                    if (dwSize > 0)
                    {
                        imeActive = true;
                    }
                    else
                    {
                        LONG compLen = ImmGetCompositionStringW(hIMC, GCS_COMPSTR, nullptr, 0);
                        if (compLen > 0)
                        {
                            imeActive = true;
                        }
                    }
                    ImmReleaseContext(hWnd, hIMC);
                }

                if (!imeActive)
                {
                    Hide();
                }
            }
            return 0;
        }
        if (wParam == POPUP_ANIMATION_TIMER_ID)
        {
            StepPageAnimationFrame(hWnd);
            return 0;
        }
        if (wParam == TIMELINE_ANIMATION_TIMER_ID)
        {
            double now = GetTimeInSeconds();
            double elapsed = 0.0;
            bool isEmpty = false;
            {
                std::lock_guard<std::mutex> lock(m_selectedFilesMutex);
                elapsed = now - m_selectedFilesCtx.capturedTime;
                isEmpty = m_selectedFilesCtx.filePaths.empty();
            }

            if (elapsed >= FILE_SELECTION_VALIDITY_DURATION || isEmpty)
            {
                KillTimer(hWnd, TIMELINE_ANIMATION_TIMER_ID);
                if (elapsed >= FILE_SELECTION_VALIDITY_DURATION)
                {
                    std::lock_guard<std::mutex> lock(m_selectedFilesMutex);
                    m_selectedFilesCtx.filePaths.clear();
                }
            }
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        }
        break;
    }

    case WM_USER_ANIMATE:
    {
        StepPageAnimationFrame(hWnd);
        return 0;
    }

    case WM_USER_SELECTION_UPDATED:
    {
        SetTimer(hWnd, TIMELINE_ANIMATION_TIMER_ID, TIMELINE_ANIMATION_FRAME_MS, nullptr);
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }

    case WM_USER_REFRESH_ICONS:
    {
        if (!m_refreshingIcons) return 0;
        m_refreshingIcons = false;
        double refreshStart = GetTimeInSeconds();
        int shortcutCount = 0;

        // Re-extract HICONs from source
        for (auto& page : m_pages)
        {
            shortcutCount += (int)page.shortcuts.size();
            for (auto& sc : page.shortcuts)
                ShortcutManager::RefreshShortcutIcon(sc);
        }
        shortcutCount += (int)m_dockPage.shortcuts.size();
        for (auto& sc : m_dockPage.shortcuts)
            ShortcutManager::RefreshShortcutIcon(sc);

        // Clear bitmaps to force recreate from fresh HICONs
        for (auto& page : m_pages)
        {
            for (auto* bmp : page.iconBitmaps)
                if (bmp) bmp->Release();
            page.iconBitmaps.clear();
        }
        for (auto* bmp : m_dockPage.iconBitmaps)
            if (bmp) bmp->Release();
        m_dockPage.iconBitmaps.clear();
        m_bmpBrushCache.clear();

        EnsureIcons();
        InvalidateRect(hWnd, nullptr, FALSE);

        double refreshElapsedMs = (GetTimeInSeconds() - refreshStart) * 1000.0;
        if (refreshElapsedMs >= POPUP_SLOW_ICON_REFRESH_MS)
        {
            LOG_G_WORNING(
                L"PopupWindow perf: icon refresh took %.2fms shortcuts=%d pages=%d",
                refreshElapsedMs,
                shortcutCount,
                (int)m_pages.size());
        }
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        POINT pt_px{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        RECT cr; GetClientRect(hWnd, &cr);
        if (pt_px.x < 0 || pt_px.y < 0 || pt_px.x >= cr.right || pt_px.y >= cr.bottom)
        {
            if (m_pressedShortcutKind != PressedShortcutKind::None)
            {
                m_hovered = -1;
                m_hoveredDock = -1;
                InvalidateRect(hWnd, nullptr, FALSE);
                return 0;
            }
            Hide();
            return 0;
        }

        float scale = GetWindowScale(hWnd);
        POINT pt{ (int)(pt_px.x / scale), (int)(pt_px.y / scale) };

        if (m_searchActive)
        {
            bool repaint = false;
            m_searchTextBox.OnMouseMove(hWnd, pt, scale, repaint);
            if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
        }

        // Handle tab hover
        int newHoveredTab = -1;
        if (!m_searchActive && pt.y >= GetWndPadding() && pt.y <= GetWndPadding() + 22)
        {
            int numPages = (int)m_pages.size();
            if (numPages > 0)
            {
                int wndPad = GetWndPadding();
                float totalWidth = (cr.right / scale) - wndPad * 2;
                float tabWidth = totalWidth / numPages;
                
                int hoveredTab = (int)((pt.x - wndPad) / tabWidth);
                if (hoveredTab >= 0 && hoveredTab < numPages)
                {
                    newHoveredTab = hoveredTab;
                }
            }
        }

        if (newHoveredTab != m_hoveredTab)
        {
            m_hoveredTab = newHoveredTab;
            InvalidateRect(hWnd, nullptr, FALSE);
        }

        int h = HitTest(pt);
        if (h != m_hovered) { m_hovered = h; InvalidateRect(hWnd, nullptr, FALSE); }

        // Handle dock hover
        int newHoveredDock = HitTestDock(pt);
        if (newHoveredDock != m_hoveredDock)
        {
            m_hoveredDock = newHoveredDock;
            InvalidateRect(hWnd, nullptr, FALSE);
        }

        // Use TME_LEAVE for more responsive hide
        if (!m_trackMouse)
        {
            TRACKMOUSEEVENT tme{ sizeof(tme) };
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hWnd;
            TrackMouseEvent(&tme);
            m_trackMouse = true;
        }
        return 0;
    }

    case WM_MOUSELEAVE:
    {
        m_trackMouse = false;
        m_hoveredTab = -1;
        m_hoveredDock = -1;
        if (!m_pinned)
        {
            Hide();
        }
        return 0;
    }

    case WM_MOUSEWHEEL:
    {
        if (m_searchActive && !m_searchQuery.empty()) return 0;
        if (m_pages.size() <= 1) return 0;
        int zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        int targetPage = m_currentPage;
        if (zDelta > 0)
            targetPage = (m_currentPage - 1 + (int)m_pages.size()) % (int)m_pages.size();
        else if (zDelta < 0)
            targetPage = (m_currentPage + 1) % (int)m_pages.size();

        if (targetPage != m_currentPage)
        {
            m_currentPage = targetPage;
            m_hovered = -1;
            if (m_viewModel) m_viewModel->SetCurrentPage(targetPage);

            if (!m_animating)
            {
                StartPageAnimationLoop();
            }
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        POINT pt_px{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        float scale = GetWindowScale(hWnd);
        POINT pt{ (int)(pt_px.x / scale), (int)(pt_px.y / scale) };

        // Handle search box click when search is active
        if (m_searchActive && pt.y >= GetWndPadding() && pt.y <= GetWndPadding() + 22)
        {
            RECT cr; GetClientRect(hWnd, &cr);
            float w = (float)cr.right / scale;
            int wndPad = GetWndPadding();
            if (pt.x >= wndPad && pt.x <= w - wndPad)
            {
                m_searchTextBox.SetFocus(true);
                bool repaint = false;
                m_searchTextBox.OnLButtonDown(hWnd, pt, scale, repaint);
                if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
                return 0;
            }
        }
        else if (m_searchActive)
        {
            m_searchTextBox.SetFocus(false);
        }

        // Handle tab click
        if (!m_searchActive && pt.y >= GetWndPadding() && pt.y <= GetWndPadding() + 22)
        {
            int numPages = (int)m_pages.size();
            if (numPages > 0)
            {
                int wndPad = GetWndPadding();
                RECT cr; GetClientRect(hWnd, &cr);
                float totalWidth = (cr.right / scale) - wndPad * 2;
                float tabWidth = totalWidth / numPages;
                
                int clickedTab = (int)((pt.x - wndPad) / tabWidth);
                if (clickedTab >= 0 && clickedTab < numPages)
                {
                    if (clickedTab != m_currentPage)
                    {
                        m_currentPage = clickedTab;
                        m_hovered = -1;
                        m_viewModel->SwitchToPage(clickedTab);

                        if (!m_animating)
                        {
                            StartPageAnimationLoop();
                        }
                        InvalidateRect(hWnd, nullptr, FALSE);
                    }
                    return 0;
                }
            }
        }

        int hit = HitTest(pt);
        if (m_searchActive && !m_searchQuery.empty())
        {
            if (hit >= 0 && hit < (int)m_searchResults.size())
            {
                m_pressedShortcutKind = PressedShortcutKind::SearchResult;
                m_pressedShortcutIndex = hit;
                m_pressedShortcutPage = -1;
                SetCapture(hWnd);
                InvalidateRect(hWnd, nullptr, FALSE);
            }
            else if (m_pinned)
            {
                ResetPressedShortcut();
                ReleaseCapture();
                SendMessageW(hWnd, WM_SYSCOMMAND, SC_MOVE | HTCAPTION, 0);
            }
        }
        else
        {
            // Handle dock click (check before normal hit test so dock takes priority)
            int dockHit = HitTestDock(pt);
            if (dockHit >= 0 && dockHit < (int)m_dockPage.shortcuts.size())
            {
                m_pressedShortcutKind = PressedShortcutKind::Dock;
                m_pressedShortcutIndex = dockHit;
                m_pressedShortcutPage = -1;
                SetCapture(hWnd);
                InvalidateRect(hWnd, nullptr, FALSE);
                return 0;
            }

            if (hit >= 0 && hit < (int)m_pages[m_currentPage].shortcuts.size())
            {
                m_pressedShortcutKind = PressedShortcutKind::Page;
                m_pressedShortcutIndex = hit;
                m_pressedShortcutPage = m_currentPage;
                SetCapture(hWnd);
                InvalidateRect(hWnd, nullptr, FALSE);
            }
            else if (m_pinned)
            {
                ResetPressedShortcut();
                ReleaseCapture();
                SendMessageW(hWnd, WM_SYSCOMMAND, SC_MOVE | HTCAPTION, 0);
            }
        }
        return 0;
    }

    case WM_LBUTTONDBLCLK:
    {
        POINT pt_px{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        float scale = GetWindowScale(hWnd);
        POINT pt{ (int)(pt_px.x / scale), (int)(pt_px.y / scale) };

        // Handle search box double click
        if (m_searchActive && pt.y >= GetWndPadding() && pt.y <= GetWndPadding() + 22)
        {
            RECT cr; GetClientRect(hWnd, &cr);
            float w = (float)cr.right / scale;
            int wndPad = GetWndPadding();
            if (pt.x >= wndPad && pt.x <= w - wndPad)
            {
                bool repaint = false;
                m_searchTextBox.OnLButtonDblClk(hWnd, pt, scale, repaint);
                if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
                return 0;
            }
        }

        // On blank area double-click → refresh icons
        if (!m_searchActive || m_searchQuery.empty())
        {
            bool onTab = pt.y >= GetWndPadding() && pt.y <= GetWndPadding() + 22;
            int dockHit = HitTestDock(pt);
            int hit = HitTest(pt);
            bool onShortcut = hit >= 0 && hit < (int)m_pages[m_currentPage].shortcuts.size();
            bool onDock = dockHit >= 0 && dockHit < (int)m_dockPage.shortcuts.size();

            if (!onTab && !onShortcut && !onDock)
            {
                RefreshIcons();
                return 0;
            }
        }
        return 0;
    }

    case WM_LBUTTONUP:
    {
        POINT pt_px{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        float scale = GetWindowScale(hWnd);
        POINT pt{ (int)(pt_px.x / scale), (int)(pt_px.y / scale) };

        if (m_pressedShortcutKind != PressedShortcutKind::None)
        {
            PressedShortcutKind pressedKind = m_pressedShortcutKind;
            int pressedIndex = m_pressedShortcutIndex;
            int pressedPage = m_pressedShortcutPage;
            ResetPressedShortcut();
            if (GetCapture() == hWnd)
            {
                ReleaseCapture();
            }

            if (pressedKind == PressedShortcutKind::SearchResult && m_searchActive && !m_searchQuery.empty())
            {
                int hit = HitTest(pt);
                if (hit == pressedIndex && hit >= 0 && hit < (int)m_searchResults.size())
                {
                    auto& sc = m_searchResults[hit].shortcut;
                    if (!m_pinned) Hide();
                    if (HasLaunchAction(sc))
                    {
                        LOG_G_INFO(L"PopupWindow::LButtonUp: launching search result shortcut %s (Target=%s)", sc.name.c_str(), sc.targetPath.c_str());
                        LaunchShortcut(sc);
                    }
                    if (m_viewModel)
                    {
                        m_viewModel->NotifyShortcutLaunched(
                            m_searchResults[hit].originalPageIndex,
                            m_searchResults[hit].originalShortcutIndex
                        );
                    }
                }
            }
            else if (pressedKind == PressedShortcutKind::Dock)
            {
                int dockHit = HitTestDock(pt);
                if (dockHit == pressedIndex && dockHit >= 0 && dockHit < (int)m_dockPage.shortcuts.size())
                {
                    auto& sc = m_dockPage.shortcuts[dockHit];
                    if (!m_pinned) Hide();
                    if (HasLaunchAction(sc))
                    {
                        LOG_G_INFO(L"PopupWindow::LButtonUp: launching dock shortcut %s (Target=%s)", sc.name.c_str(), sc.targetPath.c_str());
                        LaunchShortcut(sc);
                    }
                }
            }
            else if (pressedKind == PressedShortcutKind::Page)
            {
                int hit = HitTest(pt);
                if (pressedPage == m_currentPage &&
                    hit == pressedIndex &&
                    pressedPage >= 0 &&
                    pressedPage < (int)m_pages.size() &&
                    hit >= 0 &&
                    hit < (int)m_pages[pressedPage].shortcuts.size())
                {
                    auto& sc = m_pages[pressedPage].shortcuts[hit];
                    if (!m_pinned) Hide();
                    if (HasLaunchAction(sc))
                    {
                        LOG_G_INFO(L"PopupWindow::LButtonUp: launching shortcut %s (Target=%s)", sc.name.c_str(), sc.targetPath.c_str());
                        LaunchShortcut(sc);
                    }
                    if (m_viewModel)
                        m_viewModel->NotifyShortcutLaunched(pressedPage, hit);
                }
            }

            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        }

        if (m_searchActive)
        {
            bool repaint = false;
            m_searchTextBox.OnLButtonUp(hWnd, pt, scale, repaint);
            if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_RBUTTONDOWN:
        m_pinned = !m_pinned;
        if (m_pinned)
        {
            SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;

    case WM_MBUTTONDOWN:
        Hide();
        return 0;

    case WM_CAPTURECHANGED:
        ResetPressedShortcut();
        m_trackMouse = false;
        m_hovered = -1;
        if (!m_pinned)
        {
            POINT pt; GetCursorPos(&pt); ScreenToClient(hWnd, &pt);
            RECT cr; GetClientRect(hWnd, &cr);
            if (pt.x < 0 || pt.y < 0 || pt.x >= cr.right || pt.y >= cr.bottom)
            {
                StopAutoHideTimer();
                ShowWindow(hWnd, SW_HIDE);
                if (m_viewModel) m_viewModel->NotifyPopupHidden();
            }
        }
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE)
        {
            if (m_searchActive)
            {
                m_searchActive = false;
                m_searchTextBox.SetText(L"");
                m_searchTextBox.SetFocus(false);
                m_searchQuery.clear();
                m_searchResults.clear();
                if (m_appCtx && m_appCtx->configService)
                {
                    m_appCtx->configService->SetSearchMode(false);
                    SavePopupConfig();
                }
                InvalidateRect(hWnd, nullptr, FALSE);
            }
            else
            {
                Hide();
            }
        }
        else if (wParam == VK_TAB)
        {
            m_searchActive = !m_searchActive;
            if (!m_searchActive)
            {
                m_searchTextBox.SetText(L"");
                m_searchTextBox.SetFocus(false);
                m_searchQuery.clear();
                m_searchResults.clear();
            }
            else
            {
                m_searchTextBox.SetFocus(true);
                UpdateSearch();
            }
            if (m_appCtx && m_appCtx->configService)
            {
                m_appCtx->configService->SetSearchMode(m_searchActive);
                SavePopupConfig();
            }
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        else if (m_searchActive)
        {
            if (wParam == VK_RETURN)
            {
                if (m_selectedSearchResult >= 0 && m_selectedSearchResult < (int)m_searchResults.size())
                {
                    auto& sc = m_searchResults[m_selectedSearchResult].shortcut;
                    if (HasLaunchAction(sc))
                    {
                        LOG_G_INFO(L"PopupWindow::OnKeyDown (Enter): launching shortcut %s (Target=%s)", sc.name.c_str(), sc.targetPath.c_str());
                        LaunchShortcut(sc);
                    }
                    if (m_viewModel)
                    {
                        m_viewModel->NotifyShortcutLaunched(
                            m_searchResults[m_selectedSearchResult].originalPageIndex,
                            m_searchResults[m_selectedSearchResult].originalShortcutIndex
                        );
                    }
                    if (!m_pinned) Hide();
                }
            }
            else if (wParam == VK_UP)
            {
                if (!m_searchResults.empty())
                {
                    if (m_selectedSearchResult < 0)
                        m_selectedSearchResult = (int)m_searchResults.size() - 1;
                    else
                        m_selectedSearchResult = (m_selectedSearchResult - 1 + (int)m_searchResults.size()) % (int)m_searchResults.size();
                    InvalidateRect(hWnd, nullptr, FALSE);
                }
            }
            else if (wParam == VK_DOWN)
            {
                if (!m_searchResults.empty())
                {
                    if (m_selectedSearchResult < 0)
                        m_selectedSearchResult = 0;
                    else
                        m_selectedSearchResult = (m_selectedSearchResult + 1) % (int)m_searchResults.size();
                    InvalidateRect(hWnd, nullptr, FALSE);
                }
            }
            else
            {
                bool repaint = false;
                std::wstring oldText = m_searchTextBox.GetText();
                m_searchTextBox.OnKeyDown(hWnd, wParam, lParam, repaint);
                if (m_searchTextBox.GetText() != oldText)
                {
                    m_searchQuery = m_searchTextBox.GetText();
                    UpdateSearch();
                    repaint = true;
                }
                if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
                return 0;
            }
        }
        else if (wParam == VK_LEFT)
        {
            if (m_pages.size() > 1)
            {
                int targetPage = (m_currentPage - 1 + (int)m_pages.size()) % (int)m_pages.size();
                if (targetPage != m_currentPage)
                {
                    m_currentPage = targetPage;
                    m_hovered = -1;
                    if (m_viewModel) m_viewModel->SetCurrentPage(targetPage);

                    if (!m_animating)
                    {
                        StartPageAnimationLoop();
                    }
                    InvalidateRect(hWnd, nullptr, FALSE);
                }
            }
        }
        else if (wParam == VK_RIGHT)
        {
            if (m_pages.size() > 1)
            {
                int targetPage = (m_currentPage + 1) % (int)m_pages.size();
                if (targetPage != m_currentPage)
                {
                    m_currentPage = targetPage;
                    m_hovered = -1;
                    if (m_viewModel) m_viewModel->SetCurrentPage(targetPage);

                    if (!m_animating)
                    {
                        StartPageAnimationLoop();
                    }
                    InvalidateRect(hWnd, nullptr, FALSE);
                }
            }
        }
        return 0;

    case WM_CHAR:
        if (wParam >= 32 && !m_searchActive)
        {
            m_searchActive = true;
            m_searchTextBox.SetFocus(true);
            m_searchTextBox.SetText(L"");
            m_searchQuery.clear();
            m_searchResults.clear();
        }

        if (m_searchActive)
        {
            bool repaint = false;
            std::wstring oldText = m_searchTextBox.GetText();
            m_searchTextBox.OnChar(hWnd, wParam, repaint);
            if (m_searchTextBox.GetText() != oldText)
            {
                m_searchQuery = m_searchTextBox.GetText();
                UpdateSearch();
                repaint = true;
            }
            if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        }
        break;

    case WM_DESTROY:
        KillTimer(hWnd, POPUP_ANIMATION_TIMER_ID);
        GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
        // s_instance is managed by Release()
        return 0;
    }

    return GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
}

#include <thread>

static WORD ParseVirtualKey(const std::wstring& name)
{
    if (name.length() == 1)
    {
        wchar_t ch = name[0];
        if (ch >= L'A' && ch <= L'Z') return ch;
        if (ch >= L'0' && ch <= L'9') return ch;
    }
    if (name.rfind(L"F", 0) == 0 && name.length() > 1)
    {
        try {
            int num = std::stoi(name.substr(1));
            if (num >= 1 && num <= 12) return VK_F1 + (num - 1);
        } catch (...) {}
    }
    if (name == L"Space") return VK_SPACE;
    if (name == L"Enter") return VK_RETURN;
    if (name == L"Tab") return VK_TAB;
    if (name == L"Esc") return VK_ESCAPE;
    if (name == L"Backspace") return VK_BACK;
    if (name == L"Insert") return VK_INSERT;
    if (name == L"Delete") return VK_DELETE;
    if (name == L"Home") return VK_HOME;
    if (name == L"End") return VK_END;
    if (name == L"PageUp") return VK_PRIOR;
    if (name == L"PageDown") return VK_NEXT;
    if (name == L"Up") return VK_UP;
    if (name == L"Down") return VK_DOWN;
    if (name == L"Left") return VK_LEFT;
    if (name == L"Right") return VK_RIGHT;
    if (name == L"Ctrl") return VK_CONTROL;
    if (name == L"Shift") return VK_SHIFT;
    if (name == L"Alt") return VK_MENU;
    if (name == L"Win") return VK_LWIN;
    return 0;
}

static void SimulateHotkey(const std::wstring& hotkeyStr, bool afterClose)
{
    std::thread([hotkeyStr, afterClose]() {
        if (afterClose)
        {
            if (UIStyle::Animation::IsEnabled())
            {
                Sleep((DWORD)(150 + UIStyle::Animation::GetDurationMs()));
            }
            else
            {
                Sleep(150);
            }
        }
        
        std::vector<WORD> keys;
        size_t pos = 0;
        std::wstring s = hotkeyStr;
        while ((pos = s.find(L"+")) != std::wstring::npos)
        {
            std::wstring part = s.substr(0, pos);
            while (!part.empty() && part.front() == L' ') part.erase(0, 1);
            while (!part.empty() && part.back() == L' ') part.pop_back();
            
            WORD vk = ParseVirtualKey(part);
            if (vk) keys.push_back(vk);
            
            s.erase(0, pos + 1);
        }
        while (!s.empty() && s.front() == L' ') s.erase(0, 1);
        while (!s.empty() && s.back() == L' ') s.pop_back();
        WORD vk = ParseVirtualKey(s);
        if (vk) keys.push_back(vk);
        
        if (keys.empty()) return;
        
        for (WORD k : keys)
        {
            keybd_event(static_cast<BYTE>(k), 0, 0, 0);
        }
        for (auto it = keys.rbegin(); it != keys.rend(); ++it)
        {
            keybd_event(static_cast<BYTE>(*it), 0, KEYEVENTF_KEYUP, 0);
        }
    }).detach();
}

static std::wstring ExpandVariables(const std::wstring& inputStr, HWND parent, AppContext* ctx, bool& cancelled)
{
    cancelled = false;
    std::wstring s = inputStr;
    
    size_t pos = 0;
    while ((pos = s.find(L"{{clipboard}}")) != std::wstring::npos)
    {
        std::wstring clipText;
        if (OpenClipboard(nullptr))
        {
            HANDLE hData = GetClipboardData(CF_UNICODETEXT);
            if (hData)
            {
                wchar_t* pText = static_cast<wchar_t*>(GlobalLock(hData));
                if (pText)
                {
                    clipText = pText;
                    GlobalUnlock(hData);
                }
            }
            CloseClipboard();
        }
        s.replace(pos, 13, clipText);
    }
    
    while ((pos = s.find(L"{{date}}")) != std::wstring::npos)
    {
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t buf[64];
        swprintf_s(buf, L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
        s.replace(pos, 8, buf);
    }
    
    while ((pos = s.find(L"{{time}}")) != std::wstring::npos)
    {
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t buf[64];
        swprintf_s(buf, L"%02d-%02d-%02d", st.wHour, st.wMinute, st.wSecond);
        s.replace(pos, 8, buf);
    }
    
    while ((pos = s.find(L"{{input}}")) != std::wstring::npos)
    {
        std::wstring outResult;
        if (PromptWindow::Show(parent, L"输入参数", L"请输入 {{input}} 的内容:", outResult, L"", ctx))
        {
            s.replace(pos, 9, outResult);
        }
        else
        {
            cancelled = true;
            return L"";
        }
    }
    
    return s;
}

static bool LaunchUrl(const RendShortcutInfo& sc, HWND parent, AppContext* ctx)
{
    bool cancelled = false;
    std::wstring url = ExpandVariables(sc.targetPath, parent, ctx, cancelled);
    if (cancelled) return false;
    
    std::wstring browserPath, browserArgs;
    size_t sep = sc.arguments.find(L"|||");
    if (sep != std::wstring::npos)
    {
        browserPath = sc.arguments.substr(0, sep);
        browserArgs = sc.arguments.substr(sep + 3);
    }
    else
    {
        browserPath = sc.arguments;
    }
    
    if (browserPath.empty())
    {
        HINSTANCE hInst = ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return (reinterpret_cast<INT_PTR>(hInst) > 32);
    }
    else
    {
        std::wstring args = ExpandVariables(browserArgs, parent, ctx, cancelled);
        if (cancelled) return false;
        
        size_t urlPos = args.find(L"{{url}}");
        if (urlPos != std::wstring::npos)
        {
            args.replace(urlPos, 7, url);
        }
        else
        {
            if (!args.empty()) args += L" ";
            args += url;
        }
        
        return PrivilegeLaunchService::Launch(browserPath, args, false);
    }
}

// Find bash.exe by locating git.exe first, then checking sibling directories.
static std::wstring FindBashExe()
{
    wchar_t foundPath[MAX_PATH]{};
    DWORD len = SearchPathW(nullptr, L"git.exe", nullptr, MAX_PATH, foundPath, nullptr);
    if (len > 0 && len < MAX_PATH)
    {
        std::wstring gp(foundPath);
        size_t cmdPos = gp.rfind(L"\\cmd\\");
        if (cmdPos != std::wstring::npos)
        {
            std::wstring root = gp.substr(0, cmdPos);
            std::wstring candidate = root + L"\\bin\\bash.exe";
            if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES)
                return candidate;
            candidate = root + L"\\usr\\bin\\bash.exe";
            if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES)
                return candidate;
        }
    }

    // Fall back to common install locations
    static const wchar_t* commonPaths[] = {
        L"C:\\Program Files\\Git\\bin\\bash.exe",
        L"C:\\Program Files\\Git\\usr\\bin\\bash.exe",
        L"C:\\Program Files (x86)\\Git\\bin\\bash.exe",
        L"C:\\Program Files (x86)\\Git\\usr\\bin\\bash.exe",
    };
    for (auto p : commonPaths)
    {
        if (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES)
            return p;
    }

    return L""; // not found
}

static std::wstring QuoteArg(const std::wstring& value)
{
    std::wstring quoted = L"\"";
    for (wchar_t ch : value)
    {
        if (ch == L'"')
            quoted += L"\\\"";
        else
            quoted += ch;
    }
    quoted += L"\"";
    return quoted;
}

static std::string ToUtf8Bytes(const std::wstring& value)
{
    if (value.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), (int)value.size(), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string result((size_t)len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), (int)value.size(), result.data(), len, nullptr, nullptr);
    return result;
}

static std::wstring CreateTempCommandScript(const std::wstring& script, const wchar_t* extension)
{
    wchar_t tempDir[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, tempDir))
        return L"";

    wchar_t tempFile[MAX_PATH]{};
    if (!GetTempFileNameW(tempDir, L"wlc", 0, tempFile))
        return L"";

    std::wstring path = tempFile;
    std::wstring ext = extension ? extension : L".txt";
    std::wstring scriptPath = path + ext;
    if (!MoveFileExW(path.c_str(), scriptPath.c_str(), MOVEFILE_REPLACE_EXISTING))
    {
        DeleteFileW(path.c_str());
        return L"";
    }

    std::string bytes = ToUtf8Bytes(script);
    HANDLE hFile = CreateFileW(scriptPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return L"";

    DWORD written = 0;
    BOOL ok = WriteFile(hFile, bytes.data(), (DWORD)bytes.size(), &written, nullptr);
    CloseHandle(hFile);
    if (!ok || written != static_cast<DWORD>(bytes.size()))
    {
        DeleteFileW(scriptPath.c_str());
        return L"";
    }

    MoveFileExW(scriptPath.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    return scriptPath;
}

static bool LaunchCommand(const RendShortcutInfo& sc, HWND parent, AppContext* ctx)
{
    std::vector<std::wstring> segments;
    std::wstring s = sc.arguments;
    size_t pos = 0;
    while ((pos = s.find(L"|||")) != std::wstring::npos)
    {
        segments.push_back(s.substr(0, pos));
        s.erase(0, pos + 3);
    }
    segments.push_back(s);
    
    std::wstring type = L"cmd";
    // Note: segments[1] was builtinCmd — now deprecated
    
    if (segments.size() > 0) type = segments[0];
    
    if (type == L"cmd")
    {
        std::wstring cmdArgs = L"/c " + sc.targetPath;
        return PrivilegeLaunchService::Launch(L"cmd.exe", cmdArgs, sc.runAsAdmin);
    }
    else if (type == L"powershell")
    {
        if (sc.runAsAdmin)
        {
            std::wstring scriptPath = CreateTempCommandScript(sc.targetPath, L".ps1");
            if (scriptPath.empty()) return false;
            return PrivilegeLaunchService::Launch(
                L"powershell.exe",
                L"-NoProfile -ExecutionPolicy Bypass -File " + QuoteArg(scriptPath),
                true);
        }

        // Pipe script via stdin instead of -Command "..." to avoid quoting issues
        return PrivilegeLaunchService::LaunchWithStdin(
            L"powershell.exe", sc.targetPath,
            L"-NoProfile -NonInteractive -Command -",
            sc.runAsAdmin);
    }
    else if (type == L"python")
    {
        if (sc.runAsAdmin)
        {
            std::wstring scriptPath = CreateTempCommandScript(sc.targetPath, L".py");
            if (scriptPath.empty()) return false;
            return PrivilegeLaunchService::Launch(L"python.exe", QuoteArg(scriptPath), true);
        }

        // Pipe script via stdin instead of -c "..." to avoid quoting issues
        return PrivilegeLaunchService::LaunchWithStdin(
            L"python.exe", sc.targetPath,
            L"-",
            sc.runAsAdmin);
    }
    else if (type == L"gitbash")
    {
        std::wstring bashPath = FindBashExe();
        if (bashPath.empty())
        {
            LOG_G_ERRA(L"LaunchCommand: gitbash — bash.exe not found (git.exe not in PATH or Git not installed?)");
            return false;
        }
        if (sc.runAsAdmin)
        {
            std::wstring scriptPath = CreateTempCommandScript(sc.targetPath, L".sh");
            if (scriptPath.empty()) return false;
            return PrivilegeLaunchService::Launch(bashPath, QuoteArg(scriptPath), true);
        }

        // Pipe script via stdin instead of -c "..." to avoid quoting issues
        return PrivilegeLaunchService::LaunchWithStdin(
            bashPath, sc.targetPath,
            L"-s",
            sc.runAsAdmin);
    }
    return false;
}

void PopupWindow::LaunchShortcut(const RendShortcutInfo& sc)
{
    HWND hWnd = GetHWND();
    std::vector<std::wstring> files;
    bool hasFiles = false;
    {
        std::lock_guard<std::mutex> lock(m_selectedFilesMutex);
        double now = GetTimeInSeconds();
        if (!m_selectedFilesCtx.isPending && !m_selectedFilesCtx.filePaths.empty() && (now - m_selectedFilesCtx.capturedTime < FILE_SELECTION_VALIDITY_DURATION))
        {
            files = m_selectedFilesCtx.filePaths;
            hasFiles = true;
            m_selectedFilesCtx.filePaths.clear();
        }
    }

    const bool acceptsSelectedFiles =
        (sc.type == Model::ShortcutType::File || sc.type == Model::ShortcutType::System) &&
        (sc.targetKind == Model::ShortcutTargetKind::Exe ||
         sc.targetKind == Model::ShortcutTargetKind::File ||
         sc.targetKind == Model::ShortcutTargetKind::Link ||
         sc.targetKind == Model::ShortcutTargetKind::Unknown);

    if (hasFiles && acceptsSelectedFiles)
    {
        std::wstring newArgs = sc.arguments;
        for (const auto& file : files)
        {
            if (!newArgs.empty()) newArgs += L" ";
            newArgs += L"\"" + file + L"\"";
        }

        LOG_G_INFO(L"PopupWindow::LaunchShortcut: Launching with selected files. target=%s, args=%s", sc.targetPath.c_str(), newArgs.c_str());
        if (!PrivilegeLaunchService::Launch(sc.targetPath, newArgs, sc.runAsAdmin))
        {
            LOG_G_ERRA(L"PopupWindow::LaunchShortcut: failed to launch shortcut %s with files", sc.name.c_str());
        }
    }
    else
    {
        ExecuteShortcut(sc, hWnd, m_appCtx);
    }
}

bool PopupWindow::ExecuteShortcut(const RendShortcutInfo& sc, HWND parent, AppContext* ctx)
{
    if (sc.type == Model::ShortcutType::Hotkey)
    {
        bool afterClose = sc.runAsAdmin;
        SimulateHotkey(sc.targetPath, afterClose);
        return true;
    }
    else if (sc.type == Model::ShortcutType::Url)
    {
        return LaunchUrl(sc, parent, ctx);
    }
    else if (sc.type == Model::ShortcutType::Command)
    {
        return LaunchCommand(sc, parent, ctx);
    }
    else if (sc.type == Model::ShortcutType::System)
    {
        if (sc.targetPath == L":config_window")
        {
            PostMessageW(ctx->hMainWnd, AppMessages::ShowConfigWindow, 0, 0);
            return true;
        }
        else if (sc.targetPath == L":topmost_toggle")
        {
            HWND targetWnd = GetForegroundWindow();
            if (targetWnd)
            {
                LONG_PTR exStyle = GetWindowLongPtrW(targetWnd, GWL_EXSTYLE);
                if (exStyle & WS_EX_TOPMOST)
                {
                    SetWindowPos(targetWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                }
                else
                {
                    SetWindowPos(targetWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                }
                return true;
            }
            return false;
        }
        else
        {
            return PrivilegeLaunchService::Launch(sc.targetPath, sc.arguments, sc.runAsAdmin);
        }
    }
    else if (sc.type == Model::ShortcutType::Macro)
    {
        double speed = 1.0;
        std::wstring triggerMode = L"immediate";
        std::vector<MacroEvent> events;
        if (MacroHelper::Parse(sc.arguments, speed, triggerMode, events))
        {
            bool launchedFromPopup = (s_instance && parent == s_instance->GetHWND());
            bool popupWillHide = launchedFromPopup && !s_instance->m_pinned;
            bool waitForClose = popupWillHide || triggerMode == L"after_close";
            HWND restoreWnd = waitForClose ? PopupWindow::GetRestoreForegroundWindow() : nullptr;
            return MacroPlayer::Play(events, speed, waitForClose ? L"after_close" : triggerMode, parent, restoreWnd);
        }
        return false;
    }
    else if (sc.type == Model::ShortcutType::Batch)
    {
        return BatchLaunchService::Execute(sc.arguments, parent, ctx);
    }
    else
    {
        return PrivilegeLaunchService::Launch(sc.targetPath, sc.arguments, sc.runAsAdmin);
    }
}

void PopupWindow::StartFileSelectionQuery(HWND activeHwnd, POINT clickPt, POINT popupCenter)
{
    HWND hWnd = GetHWND();
    if (hWnd)
    {
        KillTimer(hWnd, TIMELINE_ANIMATION_TIMER_ID);
    }

    {
        std::lock_guard<std::mutex> lock(m_selectedFilesMutex);
        m_selectedFilesCtx.filePaths.clear();
        m_selectedFilesCtx.sourceHwnd = activeHwnd;
        m_selectedFilesCtx.capturedTime = GetTimeInSeconds();
        m_selectedFilesCtx.isPending = true;
    }

    Services::FileSelectionService::CaptureSelectedFilesAsync(activeHwnd, clickPt, popupCenter, [activeHwnd](const Services::SelectionContext& ctx) {
        bool hasFiles = false;
        HWND hWnd = nullptr;
        if (s_instance)
        {
            std::lock_guard<std::mutex> lock(s_instance->m_selectedFilesMutex);
            if (s_instance->m_selectedFilesCtx.sourceHwnd == ctx.sourceHwnd)
            {
                s_instance->m_selectedFilesCtx = ctx;
                if (!s_instance->m_selectedFilesCtx.filePaths.empty())
                {
                    hasFiles = true;
                    hWnd = s_instance->GetHWND();
                }
            }
        }
        if (hasFiles && hWnd && IsWindow(hWnd))
        {
            PostMessageW(hWnd, WM_USER_SELECTION_UPDATED, 0, 0);
        }
    });
}
