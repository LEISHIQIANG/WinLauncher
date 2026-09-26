#define NOMINMAX
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCK_DEPRECATED_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <regex>
#include <thread>
#include "PopupWindow.h"
#include "Popup/PopupLayout.h"
#include "Popup/PopupSearchModel.h"
#include "Popup/PopupCommandDispatcher.h"
#include "Popup/PopupRenderHelper.h"
#include "Popup/PopupShortcutSorter.h"
#include "Popup/PopupShortcutLauncher.h"
#include "Popup/PopupTimeZoneAction.h"
#include "Contracts/ICommandExecutionService.h"
#include "Contracts/IUserInteractionService.h"
#include "DpiHelper.h"
#include "UI/Controls/IconRenderer.h"
#include "Services/SystemIconService.h"
#include "Services/PrivilegeLaunchService.h"
#include "Config/UIStyle.h"
#include "Config/PromptWindow.h"
#include "Config/ConfirmWindow.h"
#include "Config/CommandPanelWindow.h"
#include "ToastWindow.h"
#include "App/Logger.h"
#include "App/CrashReporter.h"
#include "App/AppMessages.h"
#include "App/UiDispatcher.h"
#include <windowsx.h>
#include <shellapi.h>
#include <algorithm>
#include <atomic>
#include <functional>
#include <imm.h>
#include <numeric>
#include <string>
#include <tuple>
#pragma comment(lib, "imm32.lib")
#include "Services/MacroService.h"
#include "UI/MouseCaptureController.h"
#include "Services/BatchLaunchService.h"
#include "Services/CommandVariableService.h"
#include "Services/EnvironmentDetector.h"

PopupWindow* PopupWindow::s_instance = nullptr;
std::vector<PopupWindow*> PopupWindow::s_extraWindows;

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
static const UINT_PTR CLICK_CLOSE_TIMER_ID = 4;
static const UINT_PTR PLUGIN_SEARCH_TIMER_ID = 5;
static const UINT_PTR FILE_SELECTION_TIMER_ID = 6;
static const UINT TIMELINE_ANIMATION_FRAME_MS = 16;
static const UINT PLUGIN_SEARCH_REFRESH_MS = 120;
// Only snap after the spring has become visually stationary.  A larger
// threshold makes the last visible pixels jump instead of settling smoothly.
static constexpr float POPUP_PAGE_SETTLE_DISTANCE_PX = 0.75f;
static constexpr float POPUP_PAGE_SETTLE_VELOCITY_PX_PER_SECOND = 36.0f;
static const UINT WM_USER_ANIMATE = WM_USER + 100;
static const UINT WM_USER_REFRESH_ICONS = WM_USER + 101;
static const UINT WM_USER_SELECTION_UPDATED = WM_USER + 102;
static const double POPUP_SLOW_SHOW_MS = 24.0;
static const double POPUP_SLOW_FRAME_MS = 16.0;
static const double POPUP_SLOW_ICON_REFRESH_MS = 40.0;
static const int DEFAULT_FILE_SELECTION_VALIDITY_SECONDS = 15;
static constexpr UINT_PTR POPUP_ICON_PROGRESS_TIMER = 0xA713;
static constexpr UINT_PTR POPUP_ICON_FLASH_TIMER = 0xA714;

static bool IsSelectionWithinValidity(double elapsedSeconds, int validitySeconds)
{
    return validitySeconds < 0 || elapsedSeconds < validitySeconds;
}

static bool IsSameSceneApp(const AppScene::AppIdentity& left, const AppScene::AppIdentity& right)
{
    if (left.valid != right.valid)
        return false;
    if (!left.valid)
        return true;
    return _wcsicmp(left.exePath.c_str(), right.exePath.c_str()) == 0 &&
           _wcsicmp(left.exeName.c_str(), right.exeName.c_str()) == 0;
}

static DWORD TriggerProcessIdAtPoint(POINT point)
{
    HWND window = WindowFromPoint(point);
    HWND root = window ? GetAncestor(window, GA_ROOT) : nullptr;
    if (root)
        window = root;

    DWORD pid = 0;
    if (window)
        GetWindowThreadProcessId(window, &pid);
    if (pid == GetCurrentProcessId())
    {
        HWND foreground = GetForegroundWindow();
        GetWindowThreadProcessId(foreground, &pid);
    }
    return pid;
}

static AppScene::AppIdentity CachedSceneIdentity(AppContext* context, POINT point)
{
    AppScene::AppIdentity result;
    if (!context || !context->triggerProcessResolver)
        return result;

    const DWORD pid = TriggerProcessIdAtPoint(point);
    TriggerProcessResolver::Identity identity;
    if (context->triggerProcessResolver->TryGetIdentity(pid, identity))
    {
        result.exePath = std::move(identity.exePath);
        result.exeName = std::move(identity.exeName);
        result.valid = true;
    }
    else
    {
        context->triggerProcessResolver->Prefetch(pid);
    }
    return result;
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

int PopupWindow::GetHeaderSizeLevel() const
{
    int level = (m_appCtx && m_appCtx->configService)
        ? m_appCtx->configService->GetPopupHeaderSizeLevel()
        : 5;
    return (level >= 1 && level <= 9) ? level : 5;
}

PopupWindow::HeaderLayout PopupWindow::GetHeaderLayout() const
{
    // Keep the header compact at every level.  The typography has a slightly
    // wider range, while the total title-bar height changes only one pixel per
    // level around the default fifth level.
    static constexpr HeaderLayout kLayouts[] = {
        { 28, 19.0f,  7.5f, 3.0f, 24.0f, 24.0f },
        { 29, 20.0f,  8.0f, 3.0f, 26.0f, 25.0f },
        { 30, 20.5f, 8.5f, 3.5f, 28.0f, 26.0f },
        { 31, 21.0f, 8.75f,4.0f, 30.0f, 27.0f },
        { 32, 22.0f, 9.0f, 4.0f, 32.0f, 28.0f },
        { 33, 22.5f, 9.75f,4.0f, 34.0f, 29.0f },
        { 34, 23.0f,10.5f, 4.5f, 36.0f, 30.0f },
        { 35, 24.0f,11.25f,4.5f, 38.0f, 31.0f },
        { 36, 25.0f,12.0f, 5.0f, 40.0f, 32.0f }
    };
    return kLayouts[GetHeaderSizeLevel() - 1];
}

int PopupWindow::GetFileSelectionValiditySeconds() const
{
    const int seconds = (m_appCtx && m_appCtx->configService)
        ? m_appCtx->configService->GetFileSelectionValiditySeconds()
        : DEFAULT_FILE_SELECTION_VALIDITY_SECONDS;
    return (seconds == -1 || (seconds >= 0 && seconds <= 20)) ? seconds : DEFAULT_FILE_SELECTION_VALIDITY_SECONDS;
}

bool PopupWindow::IsFileSelectionValid(double elapsedSeconds) const
{
    return IsSelectionWithinValidity(elapsedSeconds, GetFileSelectionValiditySeconds());
}

void PopupWindow::ClearCapturedFileSelection()
{
    m_fileSelection.Clear();
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
    , m_showTimeSeconds(0.0)
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
    CancelIconRefresh();
    CancelFileSelectionQuery();
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
    m_iconCache.Clear();
}

void PopupWindow::ClearPages()
{
    ++m_iconLayoutGeneration;
    CancelIconRefresh();
    m_searchResults.clear();
    m_selectedSearchResult = -1;
    m_bmpBrushCache.clear();

    for (auto& page : m_pages)
    {
        for (auto* bmp : page.iconBitmaps)
            if (bmp) bmp->Release();
        page.iconBitmaps.clear();
        ShortcutManager::FreeShortcuts(page.shortcuts);
    }
    m_pages.clear();
    m_pageModelIndices.clear();

    // Clear dock page bitmaps
    for (auto* bmp : m_dockPage.iconBitmaps)
        if (bmp) bmp->Release();
    m_dockPage.iconBitmaps.clear();
    ShortcutManager::FreeShortcuts(m_dockPage.shortcuts);
    m_dockPage.name = L"DOCK";
}

void PopupWindow::OnConfigChanged()
{
    if (m_viewModel)
    {
        m_viewModel->ReloadPages();
        m_hasSceneRules = std::any_of(
            m_viewModel->GetPages().begin(),
            m_viewModel->GetPages().end(),
            [](const auto& page) { return !page.sceneApps.empty(); });
    }
    else
    {
        m_hasSceneRules = false;
    }
    m_configurationLoaded = true;
    if (!m_hasSceneRules)
        m_sceneApp = {};
    RebuildRenderPagesForScene(true);
}

void PopupWindow::RebuildRenderPagesForScene(bool configurationReloaded)
{

    // A slow Shell target may still keep one preload worker busy when a scene
    // change rebuilds the render pages. Harvest every result already produced
    // by the other workers before cancelling that generation.
    ApplyRefreshedIcons(false);

    // Preserve real device-independent icons before scene/config filtering
    // destroys the current render pages. A foreground-app scene switch must
    // never regress an unchanged shortcut to generated text artwork.
    m_iconCache.Preserve(m_pages, m_dockPage);

    // Rebuild legacy render data
    ClearPages();
    if (m_viewModel)
    {
        // Convert ViewModel pages to legacy RendPopupPage format with HICON
        int modelPageIndex = 0;
        for (const auto& vp : m_viewModel->GetPages())
        {
            if (!AppScene::IsPageVisibleForApp(vp.sceneApps, vp.sceneMode, m_sceneApp))
            {
                modelPageIndex++;
                continue;
            }

            RendPopupPage pp;
            pp.name = vp.name;
            pp.isSyncFolder = vp.isSyncFolder;
            pp.folderPath = vp.folderPath;
            pp.syncAutoPaused = vp.syncAutoPaused;
            pp.sceneMode = vp.sceneMode;
            pp.sceneApps = vp.sceneApps;
            pp.sceneAvailableApps = vp.sceneAvailableApps;
            for (const auto& vs : vp.shortcuts)
            {
                RendShortcutInfo si;
                si.id = vs.id;
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
                si.hIcon = m_iconCache.Copy(si);
                if (!si.hIcon)
                    si.hIcon = ShortcutManager::GetShortcutIcon(si, true);
                pp.shortcuts.push_back(std::move(si));
            }
            m_pages.push_back(std::move(pp));
            m_pageModelIndices.push_back(modelPageIndex);
            modelPageIndex++;
        }

        // Populate dock render data
        m_dockPage = RendPopupPage{};
        m_dockPage.name = L"DOCK";
        m_dockPage.sceneMode = m_viewModel->GetDockPage().sceneMode;
        m_dockPage.sceneApps = m_viewModel->GetDockPage().sceneApps;
        m_dockPage.sceneAvailableApps = m_viewModel->GetDockPage().sceneAvailableApps;
        for (const auto& vs : m_viewModel->GetDockPage().shortcuts)
        {
            RendShortcutInfo si;
            si.id = vs.id;
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
            si.hIcon = m_iconCache.Copy(si);
            if (!si.hIcon)
                si.hIcon = ShortcutManager::GetShortcutIcon(si, true);
            m_dockPage.shortcuts.push_back(std::move(si));
        }

        ApplyShortcutSortMode();

        int modelCurrentPage = m_viewModel->GetCurrentPage();
        m_currentPage = 0;
        for (int i = 0; i < (int)m_pageModelIndices.size(); ++i)
        {
            if (m_pageModelIndices[i] == modelCurrentPage)
            {
                m_currentPage = i;
                break;
            }
        }
        if (m_currentPage >= (int)m_pages.size())
            m_currentPage = 0;
        m_scrollPosition = (float)m_currentPage;
        m_wheel.Reset(m_currentPage);
        m_scrollVelocity = 0.0f;

        if (m_rt)
        {
            EnsureIcons();
        }
    }

    if (GetHWND() && IsWindowVisible(GetHWND()))
    {
        UpdateWindowSize();
    }

    if (GetHWND()) InvalidateRect(GetHWND(), nullptr, FALSE);

    // Start Shell extraction while the popup remains hidden.  The first show
    // will consume this work before painting, so transient generated icons
    // never become a visible intermediate frame.
    if (configurationReloaded)
        RefreshIcons(false);
}

PopupLayout::WindowMetrics PopupWindow::ComputeWindowMetrics() const
{
    PopupLayout::WindowMetricsInputs inputs;
    inputs.columns = GetColumns();
    inputs.rows = GetRows();
    inputs.dockRows = GetDockHeight();
    inputs.cellWidth = CellWidth();
    inputs.cellHeight = CellHeight();
    inputs.wndPadding = GetWndPadding();
    inputs.iconGap = GetIconGap();
    inputs.topBarHeight = GetHeaderLayout().topBarHeight;
    return PopupLayout::ComputeWindowMetrics(inputs);
}

void PopupWindow::UpdateWindowSize()
{
    HWND hwnd = GetHWND();
    if (!hwnd) return;

    const PopupLayout::WindowMetrics metrics = ComputeWindowMetrics();
    int w = metrics.width;
    int h = metrics.height;

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

void PopupWindow::ApplyShortcutSortMode()
{
    if (!m_appCtx || !m_appCtx->configService ||
        m_appCtx->configService->GetSortMode() != 1)
    {
        return;
    }

    for (auto& page : m_pages)
        PopupShortcutSorter::SortPageByUsage(page, m_appCtx->usageHistory);
    PopupShortcutSorter::SortPageByUsage(m_dockPage, m_appCtx->usageHistory);
}

void PopupWindow::RecordShortcutUsage(const RendShortcutInfo& shortcut)
{
    if (!shortcut.id.empty() && m_appCtx && m_appCtx->usageHistory)
        m_appCtx->usageHistory->RecordAccepted(L"shortcut:" + shortcut.id);
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

HWND PopupWindow::GetHWNDStatic()
{
    return s_instance ? s_instance->GetHWND() : nullptr;
}

void PopupWindow::PruneExtraWindows()
{
    s_extraWindows.erase(
        std::remove_if(s_extraWindows.begin(), s_extraWindows.end(), [](PopupWindow* window) {
            return !window || !window->GetHWND() || !IsWindow(window->GetHWND());
        }),
        s_extraWindows.end());
}

void PopupWindow::RemoveExtraWindow(PopupWindow* window)
{
    s_extraWindows.erase(
        std::remove(s_extraWindows.begin(), s_extraWindows.end(), window),
        s_extraWindows.end());
}

PopupWindow* PopupWindow::FindByHwnd(HWND hwnd)
{
    if (!hwnd) return nullptr;
    if (s_instance && s_instance->GetHWND() == hwnd)
        return s_instance;

    PruneExtraWindows();
    for (PopupWindow* window : s_extraWindows)
    {
        if (window && window->GetHWND() == hwnd)
            return window;
    }
    return nullptr;
}

void PopupWindow::Show(HWND parent, POINT pt)
{
    if (!s_instance)
    {
        Init(nullptr);
    }

    if (!s_instance) return;

    PruneExtraWindows();
    bool multiOpenPinned = s_instance->m_appCtx &&
        s_instance->m_appCtx->configService &&
        s_instance->m_appCtx->configService->GetPopupMultiOpenWhenPinned();

    if (multiOpenPinned &&
        s_instance->GetHWND() &&
        IsWindowVisible(s_instance->GetHWND()) &&
        s_instance->m_pinned)
    {
        const size_t maxExtraWindows = 2;
        while (s_extraWindows.size() >= maxExtraWindows)
        {
            PopupWindow* oldWindow = s_extraWindows.front();
            s_extraWindows.erase(s_extraWindows.begin());
            if (!oldWindow)
            {
                continue;
            }
            HWND oldHwnd = oldWindow->GetHWND();
            if (oldHwnd)
            {
                oldWindow->StopAutoHideTimer();
                KillTimer(oldHwnd, CLICK_CLOSE_TIMER_ID);
                KillTimer(oldHwnd, POPUP_ANIMATION_TIMER_ID);
                KillTimer(oldHwnd, TIMELINE_ANIMATION_TIMER_ID);
                KillTimer(oldHwnd, PLUGIN_SEARCH_TIMER_ID);
                oldWindow->CancelFileSelectionQuery();
                DestroyWindow(oldHwnd);
            }
            delete oldWindow;
        }

        PopupWindow* extra = new PopupWindow(s_instance->m_appCtx);
        extra->m_configDir = s_instance->m_configDir;
        extra->OnConfigChanged();
        s_extraWindows.push_back(extra);
        extra->ShowAt(parent, pt);
        return;
    }

    s_instance->ShowAt(parent, pt);
}

void PopupWindow::ShowAt(HWND parent, POINT pt)
{
    CrashReporter::RecordBreadcrumb(L"popup.show", L"");
    HWND prevActive = GetForegroundWindow();
    double showStart = GetTimeInSeconds();
    double dataReadyMs = 0.0;
    double windowReadyMs = 0.0;
    double iconReadyMs = 0.0;
    double firstFrameMs = 0.0;
    m_visibilityRequestTick = GetTickCount64();

    if (GetHWND())
        CancelVisibilityTransitionForShow();

    if (prevActive && prevActive != this->GetHWND())
    {
        this->m_restoreForegroundWnd = prevActive;
    }

    POINT clickPt = pt; // Store the original click position
    bool sceneAppChanged = false;
    if (m_hasSceneRules)
    {
        AppScene::AppIdentity nextSceneApp = CachedSceneIdentity(m_appCtx, clickPt);
        sceneAppChanged = !IsSameSceneApp(m_sceneApp, nextSceneApp);
        if (sceneAppChanged)
        {
            m_sceneApp = std::move(nextSceneApp);
            if (m_configurationLoaded)
                RebuildRenderPagesForScene(false);
        }
    }

    // 1. Load configuration and page data if not already loaded
    if (!m_configurationLoaded)
    {
        this->OnConfigChanged();
    }
    else if (this->m_viewModel)
    {
        const int modelCurrentPage = this->m_viewModel->GetCurrentPage();
        this->m_currentPage = 0;
        for (int i = 0; i < static_cast<int>(this->m_pageModelIndices.size()); ++i)
        {
            if (this->m_pageModelIndices[i] == modelCurrentPage)
            {
                this->m_currentPage = i;
                break;
            }
        }
        if (this->m_currentPage >= static_cast<int>(this->m_pages.size()))
            this->m_currentPage = 0;
        this->m_scrollPosition = (float)this->m_currentPage;
        this->m_wheel.Reset(this->m_currentPage);
        this->m_scrollVelocity = 0.0f;
    }

    // Usage changes while the popup is hidden, so re-evaluate the display
    // order for every show rather than waiting for a configuration reload.
    ApplyShortcutSortMode();

    // Never gate the first frame on Shell extraction or the background queue.
    if (auto state = m_iconRefresh.Current())
        ApplyRefreshedIcons(m_iconRefresh.WaitForCompletion(state, 0));

    // 2. Calculate window dimensions using user settings
    const PopupLayout::WindowMetrics metrics = ComputeWindowMetrics();
    int w = metrics.width;
    int h = metrics.height;
    int wndPad = this->GetWndPadding();

    HMONITOR hm = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(hm, &mi);
    RECT wa = mi.rcWork;

    float scale = DpiHelper::GetDpiScaleForMonitor(hm);
    int w_px = (int)(w * scale);
    int h_px = (int)(h * scale);

    int alignMode = (m_appCtx && m_appCtx->configService)
        ? m_appCtx->configService->GetPopupAlignMode()
        : 0;
    int targetX = pt.x - w_px / 2;
    int targetY = pt.y - h_px / 2;
    const int margin = (int)(16.0f * scale);
    const int centeredX = wa.left + ((wa.right - wa.left) - w_px) / 2;
    const int centeredY = wa.top + ((wa.bottom - wa.top) - h_px) / 2;
    // These values are stored in the user configuration. Keep mode 3 as the
    // original screen-right-bottom position for compatibility with old INI files.
    switch (alignMode)
    {
    case 1: // Mouse top-left
        targetX = pt.x;
        targetY = pt.y;
        break;
    case 2: // Screen center
        targetX = centeredX;
        targetY = centeredY;
        break;
    case 3: // Screen right-bottom
        targetX = wa.right - w_px - margin;
        targetY = wa.bottom - h_px - margin;
        break;
    case 4: // Screen center-bottom
        targetX = centeredX;
        targetY = wa.bottom - h_px - margin;
        break;
    case 5: // Screen left-bottom
        targetX = wa.left + margin;
        targetY = wa.bottom - h_px - margin;
        break;
    case 6: // Screen left-top
        targetX = wa.left + margin;
        targetY = wa.top + margin;
        break;
    case 7: // Screen center-top
        targetX = centeredX;
        targetY = wa.top + margin;
        break;
    case 8: // Screen right-top
        targetX = wa.right - w_px - margin;
        targetY = wa.top + margin;
        break;
    case 9: // Screen center-left
        targetX = wa.left + margin;
        targetY = centeredY;
        break;
    case 10: // Screen center-right
        targetX = wa.right - w_px - margin;
        targetY = centeredY;
        break;
    default:
        break;
    }

    if (targetX + w_px > wa.right) targetX = wa.right - w_px;
    if (targetY + h_px > wa.bottom) targetY = wa.bottom - h_px;
    if (targetX < wa.left) targetX = wa.left;
    if (targetY < wa.top) targetY = wa.top;

    pt.x = targetX;
    pt.y = targetY;

    this->StartFileSelectionQuery(prevActive, clickPt);
    dataReadyMs = (GetTimeInSeconds() - showStart) * 1000.0;

    // 3. Handle window (create or reposition) and ensure stable render target.
    // Keep hidden windows hidden until the first frame is ready, otherwise a
    // cold startup trigger can briefly expose the empty DWM frame.
    HWND hwnd = this->GetHWND();
    bool needsShow = false;
    bool geometryChanged = true;
    bool dpiChanged = false;
    if (hwnd)
    {
        RECT currentRect{};
        GetWindowRect(hwnd, &currentRect);
        geometryChanged = currentRect.left != pt.x || currentRect.top != pt.y ||
            (currentRect.right - currentRect.left) != w_px ||
            (currentRect.bottom - currentRect.top) != h_px;
        if (this->EnsureD2D() && this->m_rt)
        {
            float currentDpiX = 96.0f, currentDpiY = 96.0f;
            this->m_rt->GetDpi(&currentDpiX, &currentDpiY);
            dpiChanged = currentDpiX != scale * 96.0f || currentDpiY != scale * 96.0f;
            this->m_rt->SetDpi(scale * 96.0f, scale * 96.0f);
            UIStyle::Typography::ApplyRenderTargetTextDefaults(this->m_rt.Get());
            if (dpiChanged)
                this->ResetBackgroundResources(L"popup_show_dpi_changed", false);
        }
        if (geometryChanged)
            SetWindowPos(hwnd, HWND_TOPMOST, pt.x, pt.y, w_px, h_px, SWP_NOACTIVATE);
        needsShow = !IsWindowVisible(hwnd);
    }
    else
    {
        this->Create(L"", WS_POPUP, WS_EX_TOOLWINDOW | WS_EX_TOPMOST, pt.x, pt.y, w_px, h_px, parent);
        if (this->GetHWND())
        {
            SetWindowDisplayAffinitySafe(this->GetHWND());
            this->ApplySystemBackdrop();
            if (this->EnsureD2D())
            {
                if (this->m_rt)
                {
                    this->m_rt->SetDpi(scale * 96.0f, scale * 96.0f);
                    UIStyle::Typography::ApplyRenderTargetTextDefaults(this->m_rt.Get());
                }
            }
            needsShow = true;
        }
    }

    if (this->GetHWND())
    {
        DragAcceptFiles(this->GetHWND(), TRUE);
        const double windowReadyStart = GetTimeInSeconds();
        if (this->m_rt)
        {
            this->m_rt->SetDpi(scale * 96.0f, scale * 96.0f);
            UIStyle::Typography::ApplyRenderTargetTextDefaults(this->m_rt.Get());
        }

        this->m_hovered = -1;
        this->m_trackMouse = false;
        this->m_animating = false;
        this->m_scrollPosition = (float)this->m_currentPage;
        this->m_wheel.Reset(this->m_currentPage);
        this->m_scrollVelocity = 0.0f;
        this->m_searchActive = this->m_appCtx && this->m_appCtx->configService
            ? this->m_appCtx->configService->GetSearchMode()
            : false;
        this->m_searchQuery.clear();
        this->m_searchResults.clear();
        this->m_selectedSearchResult = -1;
        this->m_hoveredTab = -1;
        this->m_hoveredDock = -1;
        this->m_cursorBlink = true;
        this->ResetPressedShortcut();

        // Initialize or update the bounds of the search textbox.  The same
        // level drives this control and the category title bar.
        HeaderLayout header = this->GetHeaderLayout();
        D2D1_RECT_F topRect = D2D1::RectF(
            (float)wndPad,
            (float)wndPad,
            (float)w - wndPad,
            (float)wndPad + header.controlHeight
        );

        UIStyle::TextBoxStyle style;
        style.bgNormal = UIStyle::ThemeColor::ButtonBgNormal();
        style.borderNormal = UIStyle::ThemeColor::ButtonBorderNormal();
        style.borderFocused = UIStyle::ThemeColor::ButtonBorderNormal();
        style.paddingLeft = header.searchTextInset;
        style.paddingTop = std::max(2.0f, (header.controlHeight - header.textSize) * 0.5f - 1.0f);
        style.paddingBottom = style.paddingTop;
        style.paddingRight = 8.0f;
        style.fontSize = this->GetSearchFontSize();
        this->m_searchTextBox.SetStyle(style);

        if (!this->m_searchTextBoxCreated)
        {
            this->m_searchTextBox.Create(this->GetHWND(), this->m_dw.Get(), topRect, L"");
            this->m_searchTextBoxCreated = true;
        }
        else
        {
            this->m_searchTextBox.SetBounds(topRect);
            this->m_searchTextBox.UpdateLayout(scale);
            this->m_searchTextBox.SetText(L"");
        }

        this->m_searchTextBox.SetFocus(this->m_searchActive);

        if (this->m_searchActive)
        {
            this->m_searchTextBox.UpdateImeWindowPosition(this->GetHWND(), scale);
        }

        if (this->EnsureD2D())
        {
            this->EnsureIcons();
        }
 
        this->StartAutoHideTimer();
        windowReadyMs = (GetTimeInSeconds() - windowReadyStart) * 1000.0;

        const bool usesCapturedBackground = UIStyle::GetWindowMode() != 1;
        const bool backgroundRefreshNeeded = usesCapturedBackground &&
            (this->m_bgCaptureDirty || this->m_bgCompositeDirty || !this->m_bgFinal);
        double bgElapsedMs = 0.0;
        if (backgroundRefreshNeeded)
        {
            double bgStart = GetTimeInSeconds();
            if (this->m_bgCaptureDirty)
            {
                this->RefreshBackgroundCache();
            }
            else if (this->m_bgCompositeDirty)
            {
                this->CompositeBackgroundToCache();
                this->m_bgCompositeDirty = false;
            }
            bgElapsedMs = (GetTimeInSeconds() - bgStart) * 1000.0;
        }
        LOG_G_DEBUG(L"PopupWindow perf: show_state sceneChanged=%d geometryChanged=%d dpiChanged=%d bgRefresh=%d bgMs=%.2f pages=%d",
                   sceneAppChanged ? 1 : 0, geometryChanged ? 1 : 0, dpiChanged ? 1 : 0,
                   backgroundRefreshNeeded ? 1 : 0, bgElapsedMs, static_cast<int>(this->m_pages.size()));

        if (this->m_iconRefresh.IsRefreshing())
            SetTimer(this->GetHWND(), POPUP_ICON_PROGRESS_TIMER, 16, nullptr);

        if (needsShow)
        {
            const double firstFrameStart = GetTimeInSeconds();
            // Reveal the prepared content before allowing the companion shadow.
            this->RevealAfterFirstPaint(SW_SHOWNOACTIVATE, false);
            firstFrameMs = (GetTimeInSeconds() - firstFrameStart) * 1000.0;
        }
        else
        {
            InvalidateRect(this->GetHWND(), nullptr, FALSE);
        }

        SetActiveWindow(this->GetHWND());
        SetForegroundWindow(this->GetHWND());
        SetFocus(this->GetHWND());
        this->m_showTimeSeconds = GetTimeInSeconds();

        if (this->m_viewModel)
            this->m_viewModel->NotifyPopupShown();

        if ((needsShow || sceneAppChanged) && !this->m_iconRefresh.IsRefreshing())
        {
            this->RefreshIcons(false);
        }

        double showElapsedMs = (GetTimeInSeconds() - showStart) * 1000.0;
        LOG_G_INFO_NODE(
            L"ui.popup", L"show_timing",
            L"total_ms=%.2f data_ms=%.2f window_ms=%.2f icon_ms=%.2f background_ms=%.2f first_frame_ms=%.2f cold=%d",
            showElapsedMs, dataReadyMs, windowReadyMs, iconReadyMs, bgElapsedMs, firstFrameMs, needsShow ? 1 : 0);
        if (!UIStyle::Animation::IsEnabled() && m_visibilityRequestTick != 0)
        {
            LOG_G_INFO_NODE(
                L"ui.popup", L"fully_visible",
                L"elapsed_ms=%llu",
                static_cast<unsigned long long>(GetTickCount64() - m_visibilityRequestTick));
            m_visibilityRequestTick = 0;
        }
        if (showElapsedMs >= POPUP_SLOW_SHOW_MS)
        {
            LOG_G_WARNING_NODE(
                L"ui.popup", L"show_slow",
                L"total_ms=%.2f data_ms=%.2f window_ms=%.2f icon_ms=%.2f background_ms=%.2f first_frame_ms=%.2f pages=%d dock_items=%d size=%dx%d",
                showElapsedMs, dataReadyMs, windowReadyMs, iconReadyMs, bgElapsedMs, firstFrameMs,
                (int)this->m_pages.size(),
                (int)this->m_dockPage.shortcuts.size(),
                w_px,
                h_px);
        }
    }
}

float PopupWindow::GetVisibilityAnimationDurationMs(AnimState state) const
{
    const float configured = GlassWindow::GetVisibilityAnimationDurationMs(state);
    return state == AnimState::Opening ? (std::min)(configured, 60.0f) : configured;
}

void PopupWindow::OnVisibilityTransitionCompleted(AnimState state)
{
    if (state != AnimState::Opening || m_visibilityRequestTick == 0)
        return;
    LOG_G_INFO_NODE(
        L"ui.popup", L"fully_visible",
        L"elapsed_ms=%llu",
        static_cast<unsigned long long>(GetTickCount64() - m_visibilityRequestTick));
    m_visibilityRequestTick = 0;
}

void PopupWindow::Hide()
{
    if (s_instance)
    {

        s_instance->HideSelf();
    }
}

void PopupWindow::HideSelf(bool immediate)
{
    m_wheel.Reset(m_currentPage);
    m_iconFlashStart = 0;
    m_iconFlashBitmaps.clear();
    CrashReporter::RecordBreadcrumb(L"popup.hide", L"");
    HWND h = GetHWND();
    if (h && !IsWindowVisible(h)) HideImmediately();
    if (h)
    {
        if (GetCapture() == h)
        {
            MouseCaptureController::Release(h, L"popup_hidden");
        }
        ResetPressedShortcut();
        StopAutoHideTimer();
        KillTimer(h, CLICK_CLOSE_TIMER_ID);
        KillTimer(h, POPUP_ANIMATION_TIMER_ID);
        KillTimer(h, TIMELINE_ANIMATION_TIMER_ID);
        KillTimer(h, PLUGIN_SEARCH_TIMER_ID);
        CancelIconRefresh(true);
        CancelFileSelectionQuery();
        m_animating = false;
    }

    if (m_destroyOnHide)
    {
        DestroySelf();
        return;
    }

    if (h && !immediate && UIStyle::Animation::IsEnabled())
    {
        PopupWindow* inst = this;
        StartCloseTransition([h, inst]() {
            ShowWindow(h, SW_HIDE);
            if (inst->m_viewModel)
            {
                inst->m_viewModel->SetCurrentPage(inst->ToModelPageIndex(inst->m_currentPage));
                inst->m_viewModel->NotifyPopupHidden();
            }
        });
    }
    else
    {
        if (h)
        {
            if (immediate)
                HideImmediately();
            else
                ShowWindow(h, SW_HIDE);
        }
        if (m_viewModel)
        {
            m_viewModel->SetCurrentPage(ToModelPageIndex(m_currentPage));
            m_viewModel->NotifyPopupHidden();
        }
    }
}

void PopupWindow::DestroySelf()
{
    PopupWindow* inst = this;
    HWND h = GetHWND();
    if (h)
    {
        if (GetCapture() == h)
        {
            MouseCaptureController::Release(h, L"popup_destroyed");
        }
        ResetPressedShortcut();
        StopAutoHideTimer();
        KillTimer(h, CLICK_CLOSE_TIMER_ID);
        KillTimer(h, POPUP_ANIMATION_TIMER_ID);
        KillTimer(h, TIMELINE_ANIMATION_TIMER_ID);
        KillTimer(h, PLUGIN_SEARCH_TIMER_ID);
        CancelFileSelectionQuery();
        m_animating = false;
    }

    auto finishDestroy = [h, inst]() {
        if (inst->m_viewModel)
        {
            inst->m_viewModel->SetCurrentPage(inst->ToModelPageIndex(inst->m_currentPage));
            inst->m_viewModel->NotifyPopupHidden();
        }
        if (h && IsWindow(h))
        {
            DestroyWindow(h);
        }
        RemoveExtraWindow(inst);
        delete inst;
    };

    if (h && IsWindowVisible(h) && UIStyle::Animation::IsEnabled())
    {
        StartCloseTransition(finishDestroy);
    }
    else
    {
        finishDestroy();
    }
}

void PopupWindow::ResetPressedShortcut()
{
    m_pressedShortcutKind = PressedShortcutKind::None;
    m_pressedShortcutIndex = -1;
    m_pressedShortcutPage = -1;
}

int PopupWindow::ToModelPageIndex(int renderPageIndex) const
{
    if (renderPageIndex >= 0 && renderPageIndex < (int)m_pageModelIndices.size())
        return m_pageModelIndices[renderPageIndex];
    return renderPageIndex;
}

void PopupWindow::Release()
{
    auto extraWindows = s_extraWindows;
    s_extraWindows.clear();
    for (PopupWindow* extra : extraWindows)
    {
        if (!extra) continue;
        HWND h = extra->GetHWND();
        if (h)
        {
            extra->StopAutoHideTimer();
            KillTimer(h, CLICK_CLOSE_TIMER_ID);
            KillTimer(h, POPUP_ANIMATION_TIMER_ID);
            KillTimer(h, TIMELINE_ANIMATION_TIMER_ID);
            KillTimer(h, PLUGIN_SEARCH_TIMER_ID);
            extra->CancelFileSelectionQuery();
            DestroyWindow(h);
        }
        delete extra;
    }

    if (s_instance)
    {
        PopupWindow* inst = s_instance;
        s_instance = nullptr;

        HWND h = inst->GetHWND();
        if (h)
        {
            inst->StopAutoHideTimer();
            KillTimer(h, CLICK_CLOSE_TIMER_ID);
            KillTimer(h, POPUP_ANIMATION_TIMER_ID);
            KillTimer(h, TIMELINE_ANIMATION_TIMER_ID);
            KillTimer(h, PLUGIN_SEARCH_TIMER_ID);
            inst->CancelFileSelectionQuery();
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
    if (m_appCtx && m_appCtx->configService)
        return static_cast<float>(m_appCtx->configService->GetPopupIconLabelFontSize());
    return 9.0f;
}

float PopupWindow::GetSearchFontSize() const
{
    return GetHeaderLayout().textSize;
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
    m_tabTextFormat.Reset();

    float fontSize = GetFontSize();
    float searchFontSize = GetSearchFontSize();

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
        searchFontSize,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_TEXT_ALIGNMENT_LEADING,
        DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    UIStyle::Typography::CreateTextFormat(
        m_dw.Get(),
        &m_tabTextFormat,
        GetHeaderLayout().textSize,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_TEXT_ALIGNMENT_CENTER,
        DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

void PopupWindow::UpdateSearch()
{
    m_wheel.Reset(m_currentPage);
    m_searchResults.clear();
    m_selectedSearchResult = -1;
    if (m_searchQuery.empty())
    {
        if (m_appCtx && m_appCtx->pluginManager)
            m_appCtx->pluginManager->RequestSearch(L"");
        KillTimer(GetHWND(), PLUGIN_SEARCH_TIMER_ID);
        return;
    }

    std::wstring queryLower = m_searchQuery;
    std::transform(queryLower.begin(), queryLower.end(), queryLower.begin(),
        [](wchar_t c) { return (wchar_t)towlower(c); });

    bool slashMode = !m_searchQuery.empty() && m_searchQuery.front() == L'/';
    if (slashMode)
    {
        if (m_appCtx && m_appCtx->pluginManager)
        {
            m_appCtx->pluginManager->RequestSearch(L"");
            m_searchResults = PopupSearchService::CollectSlashCommands(
                m_searchQuery, m_appCtx->pluginManager.get());
        }
        KillTimer(GetHWND(), PLUGIN_SEARCH_TIMER_ID);
    }
    else
    {
        m_searchResults = PopupSearchService::CollectLocalShortcuts(
            queryLower, m_pages, m_dockPage, &m_pageModelIndices);

        if (m_appCtx && m_appCtx->pluginManager)
        {
            m_appCtx->pluginManager->RequestSearch(m_searchQuery);

            bool pluginSearchRunning = false;
            auto pluginResults = PopupSearchService::CollectPluginResults(
                m_searchQuery, m_appCtx->pluginManager.get(), pluginSearchRunning);
            m_searchResults.insert(m_searchResults.end(),
                std::make_move_iterator(pluginResults.begin()),
                std::make_move_iterator(pluginResults.end()));

            if (pluginSearchRunning)
                SetTimer(GetHWND(), PLUGIN_SEARCH_TIMER_ID, PLUGIN_SEARCH_REFRESH_MS, nullptr);
            else
                KillTimer(GetHWND(), PLUGIN_SEARCH_TIMER_ID);
        }
    }

    PopupSearchService::SortByRelevance(m_searchResults, queryLower);
}

void PopupWindow::ExecuteSearchResult(int index)
{
    if (index < 0 || index >= (int)m_searchResults.size())
        return;

    auto& item = m_searchResults[index];
    if (item.kind == SearchResultItem::Kind::SlashCommand)
    {
        if (!m_appCtx || !m_appCtx->pluginManager)
            return;

        if (PopupCommandDispatcher::IsBuiltin(item.pluginId, item.pluginCommandId, L"winlauncher.settings"))
        {
            if (m_appCtx->hMainWnd)
                PostMessageW(m_appCtx->hMainWnd, AppMessages::ShowConfigWindow, 0, 0);
            return;
        }

        // Reload is a quick built-in maintenance action. It has no output the
        // user needs to inspect, so avoid opening an otherwise empty panel.
        if (PopupCommandDispatcher::IsBuiltin(item.pluginId, item.pluginCommandId, L"winlauncher.reload"))
        {
            std::wstring message;
            const bool ok = m_appCtx->pluginManager->ExecuteSlashCommand(
                L"", item.pluginCommandId, m_searchQuery, {}, message, nullptr);
            LOG_G_INFO(L"PopupWindow::ExecuteSearchResult: silent reload result=%d", ok ? 1 : 0);
            ToastWindow::Show(ok ? L"插件已重新加载" : L"插件重新加载失败", ok ? 1200 : 2200);
            return;
        }

        std::vector<std::wstring> files;
        for (int wait = 0; wait < 25; ++wait)
        {
            if (!m_fileSelection.IsPending())
                break;
            Sleep(10);
        }

        m_fileSelection.Consume(GetTimeInSeconds(), GetFileSelectionValiditySeconds(), files);

        std::wstring panelTitle = L"/ 命令输出 - " + item.shortcut.name;
        std::wstring pluginId = item.pluginId;
        std::wstring commandId = item.pluginCommandId;
        std::wstring rawInput = m_searchQuery;
        auto selectedFiles = files;
        std::shared_ptr<PluginManager> pluginManager = m_appCtx->pluginManager;
        HWND mainHwnd = m_appCtx->hMainWnd;
        auto worker = [pluginId, commandId, rawInput, selectedFiles, pluginManager, mainHwnd](HWND panelHwnd) {
            if (!pluginManager || (mainHwnd && !IsWindow(mainHwnd)))
                return;

            std::wstring message;
            bool ok = pluginManager->ExecuteSlashCommand(pluginId, commandId, rawInput, selectedFiles, message, panelHwnd);
            LOG_G_INFO(L"PopupWindow::ExecuteSearchResult: slash command plugin=%s command=%s result=%d",
                pluginId.c_str(),
                commandId.c_str(),
                ok ? 1 : 0);

            CommandPanelWindow::PostAppend(panelHwnd, PopupCommandDispatcher::NormalizeResultMessage(ok, std::move(message)));
        };
        CommandPanelWindow::ShowLive(GetHWND(), panelTitle.c_str(), L"", worker, m_appCtx, worker);
        return;
    }

    if (item.kind == SearchResultItem::Kind::PluginCommand ||
        item.kind == SearchResultItem::Kind::PluginSearchResult)
    {
        if (!m_appCtx || !m_appCtx->pluginManager)
            return;

        std::wstring panelTitle = L"插件输出 - " + item.shortcut.name;
        std::wstring pluginId = item.pluginId;
        std::wstring commandId = item.pluginCommandId;
        std::wstring rawInput = m_searchQuery;
        std::shared_ptr<PluginManager> pluginManager = m_appCtx->pluginManager;
        HWND mainHwnd = m_appCtx->hMainWnd;
        auto worker = [pluginId, commandId, rawInput, pluginManager, mainHwnd](HWND panelHwnd) {
            if (!pluginManager || (mainHwnd && !IsWindow(mainHwnd)))
                return;

            std::wstring message;
            bool ok = pluginManager->ExecuteCommand(pluginId, commandId, rawInput, message, panelHwnd);
            LOG_G_INFO(L"PopupWindow::ExecuteSearchResult: plugin command plugin=%s command=%s result=%d",
                pluginId.c_str(),
                commandId.c_str(),
                ok ? 1 : 0);

            CommandPanelWindow::PostAppend(panelHwnd, PopupCommandDispatcher::NormalizeResultMessage(ok, std::move(message)));
        };
        CommandPanelWindow::ShowLive(GetHWND(), panelTitle.c_str(), L"", worker, m_appCtx, worker);
        return;
    }

    auto& sc = item.shortcut;
    if (PopupShortcutLauncher::HasLaunchAction(sc))
    {
        LOG_G_INFO(L"PopupWindow::ExecuteSearchResult: launching search result shortcut %s (Target=%s)", sc.name.c_str(), sc.targetPath.c_str());
        LaunchShortcut(sc);
    }
    if (m_viewModel)
    {
        m_viewModel->NotifyShortcutLaunched(item.originalPageIndex, item.originalShortcutIndex);
    }
}

void PopupWindow::UpdateImeWindowPosition()
{
    m_searchTextBox.UpdateImeWindowPosition(GetHWND(), GetWindowScale(GetHWND()));
}

void PopupWindow::DrawTopBar(ID2D1HwndRenderTarget* rt)
{
    int wndPad = GetWndPadding();
    HeaderLayout header = GetHeaderLayout();
    RECT cr; GetClientRect(GetHWND(), &cr);
    float scale = GetWindowScale(GetHWND());
    float w = (float)cr.right / scale;
    
    D2D1_RECT_F topRect = D2D1::RectF(
        (float)wndPad,
        (float)wndPad,
        w - wndPad,
        (float)wndPad + header.controlHeight
    );

    if (m_searchActive)
    {
        // Draw textbox background, border, selection, text, and caret
        m_searchTextBox.Paint(rt, scale);

        // Keep the search glyph in proportion with the configured header.
        float cx = topRect.left + header.searchTextInset * 0.55f;
        float cy = topRect.top + header.controlHeight * 0.5f;
        float iconRadius = std::max(3.0f, header.controlHeight * 0.18f);
        auto iconBrush = GetOrCreateBrush(UIStyle::ThemeColor::TextMuted().d2d);
        if (iconBrush)
        {
            rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), iconRadius, iconRadius), iconBrush.Get(), UIStyle::Metrics::IconStroke());
            rt->DrawLine(D2D1::Point2F(cx + iconRadius * 0.7f, cy + iconRadius * 0.7f),
                D2D1::Point2F(cx + iconRadius * 1.6f, cy + iconRadius * 1.6f), iconBrush.Get(), UIStyle::Metrics::IconStroke());
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
                    D2D1::RectF(topRect.left + header.searchTextInset, topRect.top, topRect.right - 8.0f, topRect.bottom),
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
                    D2D1_ROUNDED_RECT roundedTab = D2D1::RoundedRect(tabRect, header.tabHoverRadius, header.tabHoverRadius);
                    auto hoverBg = GetOrCreateBrush(UIStyle::ThemeColor::ButtonBgHover().d2d);
                    if (hoverBg) rt->FillRoundedRectangle(roundedTab, hoverBg.Get());
                }

                // Dynamic opacity transition based on m_scrollPosition distance
                float dist = std::abs(std::remainder((float)i - m_scrollPosition, static_cast<float>(m_pages.size())));
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
                if (tabTextBrush && m_tabTextFormat)
                {
                    rt->DrawTextW(m_pages[i].name.c_str(), (UINT32)m_pages[i].name.size(), m_tabTextFormat.Get(),
                        tabRect, tabTextBrush.Get());
                }
            }

            float lineW = std::min(header.selectionIndicatorWidth, std::max(10.0f, tabWidth - 4.0f));
            float lineH = std::max(1.2f, header.controlHeight * 0.06f);
            float lineX = topRect.left + (m_scrollPosition - std::floor(m_scrollPosition / m_pages.size()) * m_pages.size()) * tabWidth + (tabWidth - lineW) * 0.5f;
            float lineY = topRect.bottom - lineH;
            D2D1_ROUNDED_RECT indicatorLine = D2D1::RoundedRect(
                D2D1::RectF(lineX, lineY, lineX + lineW, lineY + lineH),
                lineH * 0.5f, lineH * 0.5f);
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

    int topBarHeight = GetHeaderLayout().topBarHeight;

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
    D2D1_COLOR_F selectedBorderColor = UIStyle::ThemeColor::AccentHover().d2d;
    selectedBorderColor.a = 0.42f;

    PopupRenderHelper::ShortcutCellMetrics cellMetrics;
    cellMetrics.count = n;
    cellMetrics.columns = cols;
    cellMetrics.cellWidth = cw;
    cellMetrics.cellHeight = ch;
    cellMetrics.padding = wndPad;
    cellMetrics.gridTop = wndPad + topBarHeight;
    cellMetrics.gap = iconGap;
    cellMetrics.cardCornerRadius = cardRad;

    PopupRenderHelper::ShortcutCellBrushes cellBrushes;
    cellBrushes.bgSelected = GetOrCreateBrush(UIStyle::ThemeColor::AccentSubtle().d2d).Get();
    cellBrushes.bgHover = GetOrCreateBrush(UIStyle::ThemeColor::ButtonBgHover().d2d).Get();
    cellBrushes.bgNormal = GetOrCreateBrush(UIStyle::ThemeColor::ButtonBgNormal().d2d).Get();
    cellBrushes.borderSelected = GetOrCreateBrush(selectedBorderColor).Get();
    cellBrushes.borderHover = GetOrCreateBrush(UIStyle::ThemeColor::ButtonBorderHover().d2d).Get();
    cellBrushes.borderNormal = GetOrCreateBrush(UIStyle::ThemeColor::ButtonBorderNormal().d2d).Get();
    cellBrushes.stroke = UIStyle::Metrics::ControlStroke();

    PopupRenderHelper::RenderShortcutCards(rt, cellMetrics, cellBrushes,
        [this](int i) { return i == m_hovered; },
        [this](int i) { return i == m_selectedSearchResult; });

    // Icons
    int cellMarginX = GetCellMarginX();
    int cellMarginY = GetCellMarginY();
    int iconSize = GetIconSize();

    for (int i = 0; i < n; i++)
    {
        float ix = (float)(wndPad + (i % cols) * cw);
        float iy = (float)(wndPad + (i / cols) * ch + topBarHeight);
        const auto& item = m_searchResults[i];
        auto* bmp = item.bitmap;
        ComPtr<ID2D1Bitmap> generatedBitmap;
        bool commandLike =
            item.kind == SearchResultItem::Kind::PluginCommand ||
            item.kind == SearchResultItem::Kind::PluginSearchResult ||
            item.kind == SearchResultItem::Kind::SlashCommand;
        if (!bmp && commandLike)
        {
            HICON hIcon = nullptr;
            if (!item.iconPath.empty())
            {
                ExtractIconExW(item.iconPath.c_str(), 0, &hIcon, nullptr, 1);
            }
            if (hIcon)
            {
                generatedBitmap = IconRenderer::HicontoD2D(rt, hIcon, IconRenderer::GetRecommendedBitmapSize(rt, static_cast<float>(iconSize)));
                DestroyIcon(hIcon);
            }
            if (!generatedBitmap)
            {
                generatedBitmap = IconRenderer::CreateDefaultIcon(rt, GetDWFactory(), item.shortcut.name, IconRenderer::GetRecommendedBitmapSize(rt, static_cast<float>(iconSize)));
            }
            bmp = generatedBitmap.Get();
        }
        if (bmp)
        {
            float iconX = ix + cellMarginX;
            float iconY = iy + cellMarginY;
            D2D1_RECT_F iconRect = IconRenderer::AlignToPixels(rt, iconX, iconY, (float)iconSize, (float)iconSize);
            DrawShortcutIcon(rt, bmp, iconRect, item.shortcut.name);
        }
    }

    // Labels
    if (m_popupTextFormat)
    {
        for (int i = 0; i < n; i++)
        {
            int col = i % cols, row = i / cols;
            float lx = (float)(wndPad + col * cw);
            bool commandLike = (m_searchResults[i].kind == SearchResultItem::Kind::PluginCommand ||
                m_searchResults[i].kind == SearchResultItem::Kind::PluginSearchResult ||
                m_searchResults[i].kind == SearchResultItem::Kind::SlashCommand);
            float ly = (float)(wndPad + row * ch + cellMarginY + iconSize + 2 + topBarHeight);
            auto& nm = m_searchResults[i].shortcut.name;
            
            auto tb = GetOrCreateBrush(UIStyle::ThemeColor::TextNormal().d2d);
            if (tb)
            {
                rt->DrawTextW(nm.c_str(), (UINT32)nm.size(), m_popupTextFormat.Get(),
                    D2D1::RectF(lx + 2, ly, lx + cw - iconGap - 2, ly + GetLabelHeight()),
                    tb.Get());
            }
            if (commandLike &&
                m_searchResults[i].kind != SearchResultItem::Kind::SlashCommand &&
                !m_searchResults[i].subtitle.empty())
            {
                auto subBrush = GetOrCreateBrush(UIStyle::ThemeColor::TextMuted().d2d);
                if (subBrush)
                {
                    const auto& subtitle = m_searchResults[i].subtitle;
                    rt->DrawTextW(subtitle.c_str(), (UINT32)subtitle.size(), m_popupTextFormat.Get(),
                        D2D1::RectF(lx + 2, ly + GetLabelHeight() + 2, lx + cw - iconGap - 2, ly + GetLabelHeight() * 2 + 4),
                        subBrush.Get());
                }
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
        m_iconFlashBitmaps.clear();
        m_lastRt = m_rt.Get();
        m_lastDpi = currentDpi;
        m_lastIconBitmapSize = iconBitmapSize;
        m_bmpBrushCache.clear();
    }

    bool anyRecreated = false;
    auto iconSvc = m_appCtx && m_appCtx->iconService ? m_appCtx->iconService.get() : m_iconService.get();

    const int pageCount = static_cast<int>(m_pages.size());
    for (int pageIndex = 0; pageIndex < pageCount; ++pageIndex)
    {
        int distance = std::abs(pageIndex - m_currentPage);
        if (pageCount > 1) distance = (std::min)(distance, pageCount - distance);
        if (distance > 1) continue;
        auto& page = m_pages[pageIndex];
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
        }
        for (int i = 0; i < n; i++)
        {
            if (page.iconBitmaps[i]) continue;
            anyRecreated = true;
            bool invert = (UIStyle::GetThemeMode() == UIStyle::ThemeMode::Light) ? page.shortcuts[i].iconInvertLight : page.shortcuts[i].iconInvertDark;
            if (page.shortcuts[i].hIcon == nullptr)
            {
                page.iconBitmaps[i] = IconRenderer::CreateDefaultIcon(m_rt.Get(), GetDWFactory(), page.shortcuts[i].name, iconBitmapSize).Detach();
            }
            else
            {
                page.iconBitmaps[i] = iconSvc->IconToBitmap(m_rt.Get(), page.shortcuts[i].hIcon, iconBitmapSize, invert);
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
        }
        for (int i = 0; i < dn; i++)
        {
            if (m_dockPage.iconBitmaps[i]) continue;
            anyRecreated = true;
            bool invert = (UIStyle::GetThemeMode() == UIStyle::ThemeMode::Light) ? m_dockPage.shortcuts[i].iconInvertLight : m_dockPage.shortcuts[i].iconInvertDark;
            if (m_dockPage.shortcuts[i].hIcon == nullptr)
            {
                m_dockPage.iconBitmaps[i] = IconRenderer::CreateDefaultIcon(m_rt.Get(), GetDWFactory(), m_dockPage.shortcuts[i].name, iconBitmapSize).Detach();
            }
            else
            {
                m_dockPage.iconBitmaps[i] = iconSvc->IconToBitmap(m_rt.Get(), m_dockPage.shortcuts[i].hIcon, iconBitmapSize, invert);
            }
        }
    }

    if (anyRecreated && m_searchActive && !m_searchQuery.empty())
    {
        UpdateSearch();
    }
}

void PopupWindow::RefreshIcons(bool forceRefresh, bool showFeedback)
{
    if (!m_appCtx || !m_appCtx->backgroundTasks) return;
    if (forceRefresh && showFeedback && IsWindowVisible(GetHWND()))
    {
        m_iconFlashStart = GetTickCount64();
        SetTimer(GetHWND(), POPUP_ICON_FLASH_TIMER, 16, nullptr);
        InvalidateRect(GetHWND(), nullptr, FALSE);
    }
    auto state = m_iconRefresh.Begin(forceRefresh);
    if (!state) return;
    state->layoutGeneration = m_iconLayoutGeneration;
    HWND hwnd = GetHWND();
    std::shared_ptr<UiDispatcher> dispatcher = m_appCtx->uiDispatcher;
    if (hwnd) SetTimer(hwnd, POPUP_ICON_PROGRESS_TIMER, 16, nullptr);

    std::vector<std::tuple<bool, size_t, size_t, RendShortcutInfo>> jobs;
    for (size_t pageIndex = 0; pageIndex < m_pages.size(); ++pageIndex)
    {
        for (size_t shortcutIndex = 0; shortcutIndex < m_pages[pageIndex].shortcuts.size(); ++shortcutIndex)
        {
            auto& sc = m_pages[pageIndex].shortcuts[shortcutIndex];
            if (!forceRefresh && sc.hIcon != nullptr) continue;
            jobs.emplace_back(false, pageIndex, shortcutIndex, sc);
        }
    }
    for (size_t shortcutIndex = 0; shortcutIndex < m_dockPage.shortcuts.size(); ++shortcutIndex)
    {
        auto& sc = m_dockPage.shortcuts[shortcutIndex];
        if (!forceRefresh && sc.hIcon != nullptr) continue;
        jobs.emplace_back(true, 0, shortcutIndex, sc);
    }

    if (jobs.empty())
    {
        m_iconRefresh.Complete();
        if (hwnd) KillTimer(hwnd, POPUP_ICON_PROGRESS_TIMER);
        return;
    }

    std::stable_sort(jobs.begin(), jobs.end(), [this](const auto& a, const auto& b) {
        auto rank = [this](const auto& job) {
            if (std::get<0>(job)) return 0;
            const int count = static_cast<int>(m_pages.size());
            const int distance = std::abs(static_cast<int>(std::get<1>(job)) - m_currentPage);
            return (std::min)(distance, count - distance);
        };
        return rank(a) < rank(b);
    });

    // Automatic icon preparation starts with the application and should run
    // ahead of ordinary background work so the first popup can reuse it.
    // Explicit visible refreshes remain normal-priority user maintenance.
    const auto refreshPriority = forceRefresh
        ? BackgroundTaskService::Priority::Normal
        : BackgroundTaskService::Priority::High;
    constexpr size_t MaximumIconWorkers = 4;
    const size_t workerCount = (std::min)(MaximumIconWorkers, jobs.size());
    auto sharedJobs = std::make_shared<
        std::vector<std::tuple<bool, size_t, size_t, RendShortcutInfo>>>(std::move(jobs));
    auto nextJob = std::make_shared<std::atomic_size_t>(0);

    state->pendingWorkers.store(workerCount);
    m_iconRefreshTasks.clear();
    m_iconRefreshTasks.reserve(workerCount);
    auto sharedIconService = m_appCtx ? m_appCtx->iconService : nullptr;
    auto finishWorker = [state, hwnd, dispatcher]() {
        if (state->pendingWorkers.fetch_sub(1) != 1)
            return;
        if (state->cancelled)
            return;
        SetEvent(state->completionEvent);
        // A first trigger can arrive before a popup HWND exists. Route
        // completion through the app UI dispatcher so it can safely resume
        // that trigger after all available real icons are ready.
        if (dispatcher && dispatcher->Post(L"popup.icon_preload_complete", [state]() {
            PopupWindow::OnAnyIconPreloadCompleted(state);
        }))
        {
            return;
        }
        if (IsWindow(hwnd))
            PostMessageW(hwnd, WM_USER_REFRESH_ICONS, 0, 0);
    };

    for (size_t workerIndex = 0; workerIndex < workerCount; ++workerIndex)
    {
        auto handle = m_appCtx->backgroundTasks->Submit(
            L"popup.icon_refresh." + std::to_wstring(workerIndex),
            refreshPriority,
            [state, sharedJobs, nextJob, finishWorker, sharedIconService](
                const std::shared_ptr<BackgroundTaskService::CancellationToken>& cancellation) mutable {
            const auto completion = std::shared_ptr<void>(nullptr, [finishWorker](void*) { finishWorker(); });
            const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            while (!state->cancelled && !cancellation->IsCancellationRequested())
            {
                const size_t jobIndex = nextJob->fetch_add(1);
                if (jobIndex >= sharedJobs->size())
                    break;
                auto& job = (*sharedJobs)[jobIndex];
                auto& shortcut = std::get<3>(job);
                HICON icon = ShortcutManager::GetShortcutIcon(shortcut, false, sharedIconService.get());
                if (state->cancelled || cancellation->IsCancellationRequested())
                {
                    if (icon) DestroyIcon(icon);
                    break;
                }
                std::lock_guard<std::mutex> lock(state->mutex);
                state->results.push_back({ std::get<0>(job), std::get<1>(job), std::get<2>(job), icon, PopupIconCache::Key(shortcut), state->layoutGeneration });
            }
            if (SUCCEEDED(comResult)) CoUninitialize();
        });
        if (handle)
        {
            m_iconRefreshTasks.push_back(handle);
        }
        else
        {
            finishWorker();
            LOG_G_WORNING(L"PopupWindow perf: icon refresh worker was not queued");
        }
    }
}

void PopupWindow::OnIconPreloadCompleted(const std::shared_ptr<PopupIconRefreshController::State>& state)
{
    if (!m_iconRefresh.IsCurrent(state)) return;
    // The timer coalesces visible updates; hidden preload can be consumed now.
    if (!IsWindowVisible(GetHWND()))
        ApplyRefreshedIcons(m_iconRefresh.WaitForCompletion(state, 0));
    else
        SetTimer(GetHWND(), POPUP_ICON_PROGRESS_TIMER, 16, nullptr);
}

void PopupWindow::OnAnyIconPreloadCompleted(const std::shared_ptr<PopupIconRefreshController::State>& state)
{
    if (!state) return;
    if (s_instance && s_instance->m_iconRefresh.IsCurrent(state))
        s_instance->OnIconPreloadCompleted(state);
    // Instances may still be preloading before they own a HWND.
    for (PopupWindow* window : s_extraWindows)
    {
        if (window && window->m_iconRefresh.IsCurrent(state))
            window->OnIconPreloadCompleted(state);
    }
}

void PopupWindow::CancelIconRefresh(bool preservePreload)
{
    if (preservePreload) return; // Hide keeps useful preload alive.
    if (GetHWND()) KillTimer(GetHWND(), POPUP_ICON_PROGRESS_TIMER);
    for (const auto& task : m_iconRefreshTasks) task.Cancel();
    m_iconRefreshTasks.clear();
    m_iconRefresh.Cancel();
}

void PopupWindow::ApplyRefreshedIcons(bool refreshCompleted)
{
    auto state = m_iconRefresh.Current();
    if (!m_iconRefresh.IsCurrent(state)) return;
    auto results = m_iconRefresh.Take(state);
    const double started = GetTimeInSeconds();
    int applied = 0;
    for (auto& result : results)
    {
        auto* page = result.dock ? &m_dockPage : (result.pageIndex < m_pages.size() ? &m_pages[result.pageIndex] : nullptr);
        if (!page || result.shortcutIndex >= page->shortcuts.size()) { if (result.icon) DestroyIcon(result.icon); continue; }
        if (result.layoutGeneration != m_iconLayoutGeneration)
        { if (result.icon) DestroyIcon(result.icon); continue; }
        if (PopupIconCache::Key(page->shortcuts[result.shortcutIndex]) != result.identity)
        {
            const auto found = std::find_if(page->shortcuts.begin(), page->shortcuts.end(),
                [&result](const auto& shortcut) { return PopupIconCache::Key(shortcut) == result.identity; });
            if (found == page->shortcuts.end()) { if (result.icon) DestroyIcon(result.icon); continue; }
            result.shortcutIndex = static_cast<size_t>(found - page->shortcuts.begin());
        }
        auto& shortcut = page->shortcuts[result.shortcutIndex];
        if (!result.icon || result.layoutGeneration != m_iconLayoutGeneration ||
            result.identity != PopupIconCache::Key(shortcut))
        {
            if (result.icon) DestroyIcon(result.icon);
            continue;
        }
        if (shortcut.hIcon) DestroyIcon(shortcut.hIcon);
        shortcut.hIcon = result.icon;
            m_iconCache.Remember(shortcut);
        if (result.shortcutIndex < page->iconBitmaps.size() && page->iconBitmaps[result.shortcutIndex])
        {
            page->iconBitmaps[result.shortcutIndex]->Release();
            page->iconBitmaps[result.shortcutIndex] = nullptr;
        }
        ++applied;
    }
    if (applied) m_bmpBrushCache.clear();
    if (refreshCompleted)
    {
        m_iconRefresh.Complete();
        if (GetHWND()) KillTimer(GetHWND(), POPUP_ICON_PROGRESS_TIMER);
        m_iconRefreshTasks.clear();
    }
    if (GetHWND() && applied > 0)
    {
        EnsureIcons();
        InvalidateRect(GetHWND(), nullptr, FALSE);
    }
    if (applied || refreshCompleted) LOG_G_DEBUG(L"PopupWindow perf: icon refresh applied=%d total=%zu ui_ms=%.2f generation=%llu",
               applied, results.size(), (GetTimeInSeconds() - started) * 1000.0,
               static_cast<unsigned long long>(m_iconRefresh.Generation()));
    if (refreshCompleted && m_iconRefresh.TakePending())
    {
        RefreshIcons(m_iconRefresh.TakePendingForce(), false);
    }
}

void PopupWindow::DrawShortcutIcon(ID2D1HwndRenderTarget* rt, ID2D1Bitmap* bitmap,
    const D2D1_RECT_F& rect, const std::wstring& name)
{
    // Preserve the original text-icon flash without destroying real artwork.
    if (m_iconFlashStart && GetTickCount64() - m_iconFlashStart < 120)
    {
        auto& flash = m_iconFlashBitmaps[name];
        if (!flash)
            flash = IconRenderer::CreateDefaultIcon(rt, GetDWFactory(), name,
                IconRenderer::GetRecommendedBitmapSize(rt, static_cast<float>(GetIconSize())));
        if (flash) bitmap = flash.Get();
    }
    if (bitmap) rt->DrawBitmap(bitmap, rect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
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
    int topBarHeight = GetHeaderLayout().topBarHeight;

    // Card backgrounds / icons / labels via the shared grid helpers
    int cellMarginX = GetCellMarginX();
    int cellMarginY = GetCellMarginY();
    int iconSize = GetIconSize();

    PopupRenderHelper::ShortcutCellMetrics cellMetrics;
    cellMetrics.count = n;
    cellMetrics.columns = cols;
    cellMetrics.cellWidth = cw;
    cellMetrics.cellHeight = ch;
    cellMetrics.padding = wndPad;
    cellMetrics.gridTop = wndPad + topBarHeight;
    cellMetrics.gap = iconGap;
    cellMetrics.cardCornerRadius = cardRad;

    PopupRenderHelper::ShortcutCellBrushes cellBrushes;
    cellBrushes.bgHover = GetOrCreateBrush(UIStyle::ThemeColor::ButtonBgHover().d2d).Get();
    cellBrushes.bgNormal = GetOrCreateBrush(UIStyle::ThemeColor::ButtonBgNormal().d2d).Get();
    cellBrushes.borderHover = GetOrCreateBrush(UIStyle::ThemeColor::ButtonBorderHover().d2d).Get();
    cellBrushes.borderNormal = GetOrCreateBrush(UIStyle::ThemeColor::ButtonBorderNormal().d2d).Get();
    cellBrushes.stroke = UIStyle::Metrics::ControlStroke();

    PopupRenderHelper::RenderShortcutCards(rt, cellMetrics, cellBrushes,
        [this, pageIndex](int i) { return pageIndex == m_currentPage && i == m_hovered; });

    PopupRenderHelper::RenderCellIcons(rt, cellMetrics, cellMarginX, cellMarginY, iconSize,
        [&page](int i) { return (i < (int)page.iconBitmaps.size()) ? page.iconBitmaps[i] : nullptr; },
        [this, rt, &page](int i, const D2D1_RECT_F& iconRect) {
            DrawShortcutIcon(rt, page.iconBitmaps[i], iconRect, page.shortcuts[i].name);
        });

    if (m_popupTextFormat)
    {
        auto tb = GetOrCreateBrush(UIStyle::ThemeColor::TextNormal().d2d);
        if (tb)
        {
            PopupRenderHelper::RenderShortcutLabels(rt, cellMetrics, m_popupTextFormat.Get(), tb.Get(),
                cellMarginY, iconSize, GetLabelHeight(),
                [&page](int i) -> const std::wstring& { return page.shortcuts[i].name; });
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
                diff = std::remainder(diff, static_cast<float>(numPages));
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

    // Gap between upper section and dock = 2 * wndPad, dividing line in the middle
    const PopupLayout::WindowMetrics windowMetrics = ComputeWindowMetrics();
    const int lineY = windowMetrics.lineY;
    const int dockTopY = windowMetrics.dockTopY;

    // Separator line
    RECT cr2; GetClientRect(GetHWND(), &cr2);
    float scale2 = GetWindowScale(GetHWND());
    float totalW = (float)cr2.right / scale2;

    D2D1_COLOR_F baseClr = UIStyle::ThemeColor::ThemeBase().d2d;
    float lineAlpha = (UIStyle::GetThemeMode() == UIStyle::ThemeMode::Light) ? 1.0f : 0.25f;
    auto lineBrush = GetOrCreateBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, lineAlpha));
    if (lineBrush)
    {
        PopupRenderHelper::RenderDockSeparator(rt, totalW, (float)lineY, lineBrush.Get());
    }

    // Active file selection feedback (timeline)
    double now = GetTimeInSeconds();
    double elapsed = 0.0;
    std::vector<std::wstring> selectionPreview;
    const bool hasActiveSelection = m_fileSelection.Peek(now, GetFileSelectionValiditySeconds(), selectionPreview, &elapsed);

    if (hasActiveSelection)
    {
        const int validitySeconds = GetFileSelectionValiditySeconds();
        float progress = validitySeconds < 0
            ? 1.0f
            : (float)(1.0 - (elapsed / validitySeconds));
        if (progress < 0.0f) progress = 0.0f;
        if (progress > 1.0f) progress = 1.0f;

        PopupRenderHelper::RenderFileSelectionTimeline(rt, totalW, (float)lineY, progress);
    }

    int n = (int)m_dockPage.shortcuts.size();
    if (n == 0) return;

    int maxCells = cols * dockRows;
    if (n > maxCells) n = maxCells;

    // Card backgrounds / icons / labels via the shared grid helpers
    PopupRenderHelper::ShortcutCellMetrics cellMetrics;
    cellMetrics.count = n;
    cellMetrics.columns = cols;
    cellMetrics.cellWidth = cw;
    cellMetrics.cellHeight = ch;
    cellMetrics.padding = wndPad;
    cellMetrics.gridTop = dockTopY;
    cellMetrics.gap = iconGap;
    cellMetrics.cardCornerRadius = cardRad;

    PopupRenderHelper::ShortcutCellBrushes cellBrushes;
    cellBrushes.bgHover = GetOrCreateBrush(UIStyle::ThemeColor::ButtonBgHover().d2d).Get();
    cellBrushes.bgNormal = GetOrCreateBrush(UIStyle::ThemeColor::ButtonBgNormal().d2d).Get();
    cellBrushes.borderHover = GetOrCreateBrush(UIStyle::ThemeColor::ButtonBorderHover().d2d).Get();
    cellBrushes.borderNormal = GetOrCreateBrush(UIStyle::ThemeColor::ButtonBorderNormal().d2d).Get();
    cellBrushes.stroke = UIStyle::Metrics::ControlStroke();

    PopupRenderHelper::RenderShortcutCards(rt, cellMetrics, cellBrushes,
        [this](int i) { return i == m_hoveredDock; });

    PopupRenderHelper::RenderCellIcons(rt, cellMetrics, cellMarginX, cellMarginY, iconSize,
        [this](int i) { return (i < (int)m_dockPage.iconBitmaps.size()) ? m_dockPage.iconBitmaps[i] : nullptr; },
        [this, rt](int i, const D2D1_RECT_F& iconRect) {
            DrawShortcutIcon(rt, m_dockPage.iconBitmaps[i], iconRect, m_dockPage.shortcuts[i].name);
        });

    if (m_popupTextFormat)
    {
        auto tb = GetOrCreateBrush(UIStyle::ThemeColor::TextNormal().d2d);
        if (tb)
        {
            PopupRenderHelper::RenderShortcutLabels(rt, cellMetrics, m_popupTextFormat.Get(), tb.Get(),
                cellMarginY, iconSize, GetLabelHeight(),
                [this](int i) -> const std::wstring& { return m_dockPage.shortcuts[i].name; });
        }
    }
}

int PopupWindow::HitTest(POINT pt)
{
    PopupLayout::GridMetrics metrics{ GetColumns(), GetRows(), CellWidth(), CellHeight(), GetWndPadding(), GetIconGap(), GetHeaderLayout().topBarHeight };
    const int gridTop = metrics.padding + metrics.headerHeight;

    if (m_searchActive && !m_searchQuery.empty())
        return PopupLayout::HitTestGrid(metrics, (int)m_searchResults.size(), pt, gridTop);

    if (m_pages.empty() || m_currentPage < 0 || m_currentPage >= (int)m_pages.size())
        return -1;
    return PopupLayout::HitTestGrid(metrics, (int)m_pages[m_currentPage].shortcuts.size(), pt, gridTop);
}

int PopupWindow::HitTestDot(POINT pt)
{
    return -1;
}

int PopupWindow::HitTestDock(POINT pt)
{
    const int dockRows = GetDockHeight();
    PopupLayout::GridMetrics metrics{ GetColumns(), GetRows(), CellWidth(), CellHeight(), GetWndPadding(), GetIconGap(), GetHeaderLayout().topBarHeight };
    const int dockTopY = PopupLayout::DockTop(metrics, dockRows);

    if (pt.y < dockTopY) return -1;
    metrics.rows = dockRows;
    return PopupLayout::HitTestGrid(metrics, (int)m_dockPage.shortcuts.size(), pt, dockTopY);
}

void PopupWindow::StartPageAnimationLoop()
{
    HWND hWnd = GetHWND();
    if (!hWnd) return;

    if (!UIStyle::Animation::IsEnabled())
    {
        const int remainder = m_wheel.remainder;
        m_scrollPosition = (float)m_currentPage;
        m_wheel.Reset(m_currentPage);
        m_wheel.remainder = remainder;
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
        const int remainder = m_wheel.remainder;
        m_scrollPosition = (float)m_currentPage;
        m_wheel.Reset(m_currentPage);
        m_wheel.remainder = remainder;
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

    if (m_wheel.AdvanceQueued(m_scrollPosition))
    {
        m_currentPage = PopupWheelState::Page(m_wheel.target, static_cast<int>(m_pages.size()));
        if (m_viewModel) m_viewModel->SetCurrentPage(ToModelPageIndex(m_currentPage));
    }
    float target = m_wheel.active ? static_cast<float>(m_wheel.target) : static_cast<float>(m_currentPage);
    int numPages = (int)m_pages.size();
    float stiffness = 400.0f;
    float damping = 40.0f;

    // Physics sub-stepping for stability and smoothness
    float remainingTime = dt;
    const float stepSize = 0.002f; // 2ms steps
    while (remainingTime > 0.0f)
    {
        float currentStep = (std::min)(remainingTime, stepSize);
        if (currentStep <= 0.0f) break;

        float error = target - m_scrollPosition;

        float force = error * stiffness - m_scrollVelocity * damping;
        m_scrollVelocity += force * currentStep;
        m_scrollPosition += m_scrollVelocity * currentStep;

        remainingTime -= currentStep;
    }

    float finalError = target - m_scrollPosition;
    RECT clientRect{};
    GetClientRect(hWnd, &clientRect);
    const float pageWidthPx = (std::max)(1.0f, static_cast<float>(clientRect.right - clientRect.left));
    const float remainingDistancePx = std::abs(finalError) * pageWidthPx;
    const float remainingVelocityPx = std::abs(m_scrollVelocity) * pageWidthPx;

    if (remainingDistancePx <= POPUP_PAGE_SETTLE_DISTANCE_PX &&
        remainingVelocityPx <= POPUP_PAGE_SETTLE_VELOCITY_PX_PER_SECOND)
    {
        m_scrollPosition = target;
        m_scrollVelocity = 0.0f;
        m_animating = m_wheel.active && m_wheel.Arrive();
        if (m_animating)
        {
            m_currentPage = PopupWheelState::Page(m_wheel.target, numPages);
            if (m_viewModel) m_viewModel->SetCurrentPage(ToModelPageIndex(m_currentPage));
        }
        else
        {
            const int remainder = m_wheel.remainder;
            m_scrollPosition = static_cast<float>(m_currentPage);
            m_wheel.Reset(m_currentPage);
            m_wheel.remainder = remainder;
        }
    }

    if (m_viewModel)
        m_viewModel->UpdateAnimation();

    InvalidateRect(hWnd, nullptr, FALSE);
    // The normal paint queue keeps frame work bounded.  Forcing a synchronous
    // paint here can re-enter the legacy glass path and turn a 16 ms frame
    // into a visible hitch on mixed-DPI displays.

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
    case WM_DROPFILES:
    {
        HDROP hDrop = reinterpret_cast<HDROP>(wParam);
        POINT pt_px{};
        DragQueryPoint(hDrop, &pt_px);
        float scale = GetWindowScale(hWnd);
        POINT pt{ (int)(pt_px.x / scale), (int)(pt_px.y / scale) };

        UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
        std::vector<std::wstring> droppedFiles;
        droppedFiles.reserve(count);
        for (UINT i = 0; i < count; ++i)
        {
            wchar_t szPath[MAX_PATH]{};
            if (DragQueryFileW(hDrop, i, szPath, MAX_PATH))
            {
                droppedFiles.push_back(szPath);
            }
        }
        DragFinish(hDrop);

        if (!droppedFiles.empty())
        {
            int dockHit = HitTestDock(pt);
            int hit = HitTest(pt);

            if (dockHit >= 0 && dockHit < (int)m_dockPage.shortcuts.size())
            {
                auto& sc = m_dockPage.shortcuts[dockHit];
                if (!m_pinned) HideSelf(PopupShortcutLauncher::HasLaunchAction(sc) && PopupShortcutLauncher::IsBackgroundExternalLaunch(sc));
                ExecuteShortcut(sc, hWnd, m_appCtx, droppedFiles);
            }
            else if (hit >= 0 && m_currentPage >= 0 && m_currentPage < (int)m_pages.size() &&
                     hit < (int)m_pages[m_currentPage].shortcuts.size())
            {
                auto& sc = m_pages[m_currentPage].shortcuts[hit];
                if (!m_pinned) HideSelf(PopupShortcutLauncher::HasLaunchAction(sc) && PopupShortcutLauncher::IsBackgroundExternalLaunch(sc));
                ExecuteShortcut(sc, hWnd, m_appCtx, droppedFiles);
            }
        }
        return 0;
    }

    case WM_DPICHANGED:
    {
        m_bmpBrushCache.clear();
        RECT* const prcNewWindow = (RECT*)lParam;
        if (prcNewWindow)
        {
            float newDpiScale = UIStyle::Scaling::EffectiveScaleFactor(LOWORD(wParam) / 96.0f);

            const PopupLayout::WindowMetrics metrics = ComputeWindowMetrics();
            int w = metrics.width;
            int h = metrics.height;

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
        // If the user starts typing Chinese/Japanese/Korean via IME while the
        // popup is showing the category tab bar, switch to search mode now so
        // the composition string appears inside the search box.
        // Do NOT save this to config – next popup open should still show tabs.
        if (!m_searchActive && uMsg == WM_IME_STARTCOMPOSITION)
        {
            m_searchActive = true;
            m_searchTextBox.SetFocus(true);
            m_searchTextBox.SetText(L"");
            m_searchQuery.clear();
            m_searchResults.clear();
            InvalidateRect(hWnd, nullptr, FALSE);
        }

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
        if (wParam == POPUP_ICON_FLASH_TIMER)
        {
            if (!m_iconFlashStart || GetTickCount64() - m_iconFlashStart >= 120)
            {
                m_iconFlashStart = 0;
                m_iconFlashBitmaps.clear();
                KillTimer(hWnd, POPUP_ICON_FLASH_TIMER);
            }
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        }
        if (wParam == POPUP_ICON_PROGRESS_TIMER)
        {
            auto state = m_iconRefresh.Current();
            if (m_iconRefresh.IsRefreshing() && state)
                ApplyRefreshedIcons(m_iconRefresh.WaitForCompletion(state, 0));
            else
                KillTimer(hWnd, POPUP_ICON_PROGRESS_TIMER);
            return 0;
        }

    {
        if (wParam == FILE_SELECTION_TIMER_ID)
        {
            PollFileSelectionQuery();
            return 0;
        }
        if (wParam == CLICK_CLOSE_TIMER_ID)
        {
            KillTimer(hWnd, CLICK_CLOSE_TIMER_ID);
            bool autoClose = !m_appCtx || !m_appCtx->configService || m_appCtx->configService->GetPopupAutoClose();
            if (!autoClose && !m_pinned && m_pressedShortcutKind == PressedShortcutKind::None)
            {
                ClearCapturedFileSelection();
                HideSelf();
            }
            return 0;
        }
        if (wParam == AUTO_HIDE_TIMER_ID)
        {
            if (m_searchActive)
            {
                m_searchTextBox.BlinkCaret();
                InvalidateRect(hWnd, nullptr, FALSE);
            }

            bool autoClose = !m_appCtx || !m_appCtx->configService || m_appCtx->configService->GetPopupAutoClose();
            if (m_pinned) return 0;
            if (m_pressedShortcutKind != PressedShortcutKind::None) return 0;

            // Grace period: do not close within 500ms of showing the popup
            if (GetTimeInSeconds() - m_showTimeSeconds < 0.5)
            {
                return 0;
            }

            POINT pt; GetCursorPos(&pt); ScreenToClient(hWnd, &pt);
            RECT cr; GetClientRect(hWnd, &cr);
            bool outside = pt.x < 0 || pt.y < 0 || pt.x >= cr.right || pt.y >= cr.bottom;
            if (!autoClose)
            {
                if (outside)
                {
                    bool mousePressed =
                        (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 ||
                        (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0 ||
                        (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0 ||
                        (GetAsyncKeyState(VK_XBUTTON1) & 0x8000) != 0 ||
                        (GetAsyncKeyState(VK_XBUTTON2) & 0x8000) != 0;
                    if (mousePressed)
                    {
                        SetTimer(hWnd, CLICK_CLOSE_TIMER_ID, 50, nullptr);
                    }
                }
                return 0;
            }
            if (outside)
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
                    ClearCapturedFileSelection();
                    HideSelf();
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
            std::vector<std::wstring> selectionPreview;
            const bool hasSelection = m_fileSelection.Peek(now, -1, selectionPreview, &elapsed);
            const bool isEmpty = !hasSelection;

            const int validitySeconds = GetFileSelectionValiditySeconds();
            const bool selectionExpired = !IsSelectionWithinValidity(elapsed, validitySeconds);
            if (selectionExpired || isEmpty || validitySeconds < 0)
            {
                KillTimer(hWnd, TIMELINE_ANIMATION_TIMER_ID);
                if (selectionExpired) m_fileSelection.ExpireIfNeeded(now, validitySeconds);
            }
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        }
        if (wParam == PLUGIN_SEARCH_TIMER_ID)
        {
            if (!m_searchActive || m_searchQuery.empty() || !m_appCtx || !m_appCtx->pluginManager)
            {
                KillTimer(hWnd, PLUGIN_SEARCH_TIMER_ID);
                return 0;
            }

            bool wasRunning = m_appCtx->pluginManager->IsSearchRunning(m_searchQuery);
            UpdateSearch();
            InvalidateRect(hWnd, nullptr, FALSE);
            if (!wasRunning && !m_appCtx->pluginManager->IsSearchRunning(m_searchQuery))
                KillTimer(hWnd, PLUGIN_SEARCH_TIMER_ID);
            return 0;
        }
        break;
    }

    case WM_ACTIVATE:
    {
        // The popup can remain open while inactive (for example when it is
        // pinned or auto-close is disabled).  Always forward activation to
        // GlassWindow so its companion shadow stays directly behind it.
        GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
        if (LOWORD(wParam) == WA_INACTIVE)
        {
            bool autoClose = !m_appCtx || !m_appCtx->configService || m_appCtx->configService->GetPopupAutoClose();
            if (!autoClose && !m_pinned && m_pressedShortcutKind == PressedShortcutKind::None)
            {
                SetTimer(hWnd, CLICK_CLOSE_TIMER_ID, 100, nullptr);
            }
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
        if (!m_iconRefresh.IsRefreshing()) return 0;
        OnIconPreloadCompleted(m_iconRefresh.Current());
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
            bool autoClose = !m_appCtx || !m_appCtx->configService || m_appCtx->configService->GetPopupAutoClose();
            if (autoClose && !m_pinned)
            {
                UINT delay = (UINT)(m_appCtx && m_appCtx->configService ? m_appCtx->configService->GetHoverLeaveDelay() : 200);
                if (delay == 0) delay = 1;
                SetTimer(hWnd, AUTO_HIDE_TIMER_ID, delay, nullptr);
            }
            else if (!autoClose && !m_pinned)
            {
                SetTimer(hWnd, AUTO_HIDE_TIMER_ID, 50, nullptr);
            }
            return 0;
        }

        float scale = GetWindowScale(hWnd);
        POINT pt{ (int)(pt_px.x / scale), (int)(pt_px.y / scale) };
        StartAutoHideTimer();

        if (m_searchActive)
        {
            bool repaint = false;
            m_searchTextBox.OnMouseMove(hWnd, pt, scale, repaint);
            if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
        }

        // Handle tab hover
        int newHoveredTab = -1;
        if (!m_searchActive && pt.y >= GetWndPadding() && pt.y <= GetWndPadding() + GetHeaderLayout().controlHeight)
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
        bool autoClose = !m_appCtx || !m_appCtx->configService || m_appCtx->configService->GetPopupAutoClose();
        if (autoClose && !m_pinned)
        {
            UINT delay = (UINT)(m_appCtx && m_appCtx->configService ? m_appCtx->configService->GetHoverLeaveDelay() : 200);
            if (delay == 0) delay = 1;
            SetTimer(hWnd, AUTO_HIDE_TIMER_ID, delay, nullptr);
        }
        else if (!autoClose && !m_pinned)
        {
            SetTimer(hWnd, AUTO_HIDE_TIMER_ID, 50, nullptr);
        }
        return 0;
    }

    case WM_MOUSEWHEEL:
    {
        if (m_searchActive && !m_searchQuery.empty()) return 0;
        if (m_pages.size() <= 1) return 0;
        if (m_wheel.Wheel(GET_WHEEL_DELTA_WPARAM(wParam), m_scrollPosition))
        {
            m_currentPage = PopupWheelState::Page(m_wheel.target, static_cast<int>(m_pages.size()));
            m_hovered = -1;
            if (m_viewModel) m_viewModel->SetCurrentPage(ToModelPageIndex(m_currentPage));
            if (!m_animating) StartPageAnimationLoop();
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
        if (m_searchActive && pt.y >= GetWndPadding() && pt.y <= GetWndPadding() + GetHeaderLayout().controlHeight)
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
        if (!m_searchActive && pt.y >= GetWndPadding() && pt.y <= GetWndPadding() + GetHeaderLayout().controlHeight)
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
                        m_wheel.Reset(clickedTab);
                        m_currentPage = clickedTab;
                        m_hovered = -1;
                        if (m_viewModel) m_viewModel->SetCurrentPage(ToModelPageIndex(clickedTab));

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
                MouseCaptureController::CaptureGesture(hWnd);
                InvalidateRect(hWnd, nullptr, FALSE);
            }
            else if (m_pinned)
            {
                ResetPressedShortcut();
                MouseCaptureController::ReleaseCurrent(L"popup_window_move");
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
                MouseCaptureController::CaptureGesture(hWnd);
                InvalidateRect(hWnd, nullptr, FALSE);
                return 0;
            }

            if (hit >= 0 && hit < (int)m_pages[m_currentPage].shortcuts.size())
            {
                m_pressedShortcutKind = PressedShortcutKind::Page;
                m_pressedShortcutIndex = hit;
                m_pressedShortcutPage = m_currentPage;
                MouseCaptureController::CaptureGesture(hWnd);
                InvalidateRect(hWnd, nullptr, FALSE);
            }
            else if (m_pinned)
            {
                ResetPressedShortcut();
                MouseCaptureController::ReleaseCurrent(L"popup_window_move");
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
        if (m_searchActive && pt.y >= GetWndPadding() && pt.y <= GetWndPadding() + GetHeaderLayout().controlHeight)
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
            bool onTab = pt.y >= GetWndPadding() && pt.y <= GetWndPadding() + GetHeaderLayout().controlHeight;
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
            if (GetCapture() == hWnd)
            {
                MouseCaptureController::Complete(hWnd);
            }
            ResetPressedShortcut();

            if (pressedKind == PressedShortcutKind::SearchResult && m_searchActive && !m_searchQuery.empty())
            {
                int hit = HitTest(pt);
                if (hit == pressedIndex && hit >= 0 && hit < (int)m_searchResults.size())
                {
                    if (!m_pinned) HideSelf(PopupShortcutLauncher::IsBackgroundExternalLaunch(m_searchResults[hit].shortcut));
                    ExecuteSearchResult(hit);
                }
            }
            else if (pressedKind == PressedShortcutKind::Dock)
            {
                int dockHit = HitTestDock(pt);
                if (dockHit == pressedIndex && dockHit >= 0 && dockHit < (int)m_dockPage.shortcuts.size())
                {
                    auto& sc = m_dockPage.shortcuts[dockHit];
                    if (!m_pinned) HideSelf(PopupShortcutLauncher::HasLaunchAction(sc) && PopupShortcutLauncher::IsBackgroundExternalLaunch(sc));
                    if (PopupShortcutLauncher::HasLaunchAction(sc))
                    {
                        LOG_G_INFO(L"PopupWindow::LButtonUp: launching dock shortcut %s (Target=%s)", sc.name.c_str(), sc.targetPath.c_str());
                        RecordShortcutUsage(sc);
                        LaunchShortcut(sc);
                        if (m_pinned)
                            ApplyShortcutSortMode();
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
                    if (!m_pinned) HideSelf(PopupShortcutLauncher::HasLaunchAction(sc) && PopupShortcutLauncher::IsBackgroundExternalLaunch(sc));
                    if (PopupShortcutLauncher::HasLaunchAction(sc))
                    {
                        LOG_G_INFO(L"PopupWindow::LButtonUp: launching shortcut %s (Target=%s)", sc.name.c_str(), sc.targetPath.c_str());
                        RecordShortcutUsage(sc);
                        LaunchShortcut(sc);
                    }
                    if (m_viewModel)
                        m_viewModel->NotifyShortcutLaunched(ToModelPageIndex(pressedPage), hit);
                    if (m_pinned)
                        ApplyShortcutSortMode();
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
        HideSelf();
        return 0;

    case WM_CAPTURECHANGED:
        MouseCaptureController::OnCaptureChanged(hWnd, reinterpret_cast<HWND>(lParam));
        m_trackMouse = false;
        m_hovered = -1;
        if (!m_pinned && m_pressedShortcutKind == PressedShortcutKind::None)
        {
            // Grace period: do not close within 500ms of showing the popup
            if (GetTimeInSeconds() - m_showTimeSeconds < 0.5)
            {
                ResetPressedShortcut();
                return 0;
            }

            POINT pt; GetCursorPos(&pt); ScreenToClient(hWnd, &pt);
            RECT cr; GetClientRect(hWnd, &cr);
            if (pt.x < 0 || pt.y < 0 || pt.x >= cr.right || pt.y >= cr.bottom)
            {
                StopAutoHideTimer();
                ShowWindow(hWnd, SW_HIDE);
                if (m_viewModel) m_viewModel->NotifyPopupHidden();
            }
        }
        ResetPressedShortcut();
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
                HideSelf();
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
                    const auto& result = m_searchResults[m_selectedSearchResult];
                    if (!m_pinned) HideSelf(PopupShortcutLauncher::IsBackgroundExternalLaunch(result.shortcut));
                    ExecuteSearchResult(m_selectedSearchResult);
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
        else if ((!m_searchActive || m_searchTextBox.IsEmpty()) && wParam >= '1' && wParam <= '9')
        {
            int targetIdx = static_cast<int>(wParam - '1');
            if (m_currentPage >= 0 && m_currentPage < static_cast<int>(m_pages.size()) &&
                targetIdx < static_cast<int>(m_pages[m_currentPage].shortcuts.size()))
            {
                auto& sc = m_pages[m_currentPage].shortcuts[targetIdx];
                if (!m_pinned) HideSelf(PopupShortcutLauncher::HasLaunchAction(sc) && PopupShortcutLauncher::IsBackgroundExternalLaunch(sc));
                if (PopupShortcutLauncher::HasLaunchAction(sc))
                {
                    LOG_G_INFO(L"PopupWindow::WM_KEYDOWN: quick launching shortcut %s (Index=%d)", sc.name.c_str(), targetIdx);
                    RecordShortcutUsage(sc);
                    LaunchShortcut(sc);
                }
                if (m_viewModel)
                    m_viewModel->NotifyShortcutLaunched(ToModelPageIndex(m_currentPage), targetIdx);
                if (m_pinned)
                    ApplyShortcutSortMode();
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
                    m_wheel.Reset(targetPage);
                    m_currentPage = targetPage;
                    m_hovered = -1;
                    if (m_viewModel) m_viewModel->SetCurrentPage(ToModelPageIndex(targetPage));

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
                    m_wheel.Reset(targetPage);
                    m_currentPage = targetPage;
                    m_hovered = -1;
                    if (m_viewModel) m_viewModel->SetCurrentPage(ToModelPageIndex(targetPage));

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
        if (wParam >= '1' && wParam <= '9' && !m_searchActive && m_searchQuery.empty())
        {
            return 0;
        }

        if (wParam >= 32 && !m_searchActive)
        {
            // Activate search mode temporarily and immediately repaint so the
            // search box appears before the character is processed.
            // NOTE: do NOT persist this activation to config – the next popup
            // open should still show the category tab bar as before.
            m_searchActive = true;
            m_searchTextBox.SetFocus(true);
            m_searchTextBox.SetText(L"");
            m_searchQuery.clear();
            m_searchResults.clear();
            InvalidateRect(hWnd, nullptr, FALSE);
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
        return 0;

    case WM_DESTROY:
        KillTimer(hWnd, POPUP_ANIMATION_TIMER_ID);
        KillTimer(hWnd, PLUGIN_SEARCH_TIMER_ID);
        GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
        // s_instance is managed by Release()
        return 0;
    }

    return GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
}

void PopupWindow::LaunchShortcut(const RendShortcutInfo& sc)
{
    HWND hWnd = GetHWND();
    std::vector<std::wstring> files;
    const bool hasFiles = m_fileSelection.Consume(GetTimeInSeconds(), GetFileSelectionValiditySeconds(), files);
    PopupShortcutLauncher::LaunchFromPopup(sc, hWnd, m_appCtx, files, hasFiles, BuildLaunchContext(hWnd, files));
}

PopupShortcutLauncher::LaunchContext PopupWindow::BuildLaunchContext(HWND parent, const std::vector<std::wstring>& selectedFiles)
{
    PopupShortcutLauncher::LaunchContext launch;
    launch.selectedFiles = selectedFiles;
    launch.peekLiveSelection = []() -> std::vector<std::wstring> {
        std::vector<std::wstring> files;
        if (PopupWindow::s_instance)
        {
            PopupWindow::s_instance->m_fileSelection.Peek(
                GetTimeInSeconds(), PopupWindow::s_instance->GetFileSelectionValiditySeconds(), files);
        }
        return files;
    };
    if (PopupWindow* sourcePopup = PopupWindow::FindByHwnd(parent))
    {
        launch.popupWillHide = !sourcePopup->m_pinned;
        launch.restoreForegroundWnd = sourcePopup->m_restoreForegroundWnd && IsWindow(sourcePopup->m_restoreForegroundWnd)
            ? sourcePopup->m_restoreForegroundWnd
            : nullptr;
    }
    return launch;
}

bool PopupWindow::ExecuteShortcut(const RendShortcutInfo& sc, HWND parent, AppContext* ctx, const std::vector<std::wstring>& selectedFiles)
{
    return PopupShortcutLauncher::Execute(sc, parent, ctx, BuildLaunchContext(parent, selectedFiles));
}

void PopupWindow::StartFileSelectionQuery(HWND activeHwnd, POINT triggerPt)
{
    HWND hWnd = GetHWND();
    if (hWnd)
    {
        KillTimer(hWnd, TIMELINE_ANIMATION_TIMER_ID);
    }

    CancelFileSelectionQuery();
    const auto request = Services::FileSelectionService::CaptureSelectedFilesAsync(
        activeHwnd, triggerPt, m_appCtx ? m_appCtx->backgroundTasks : nullptr);
    m_fileSelection.Begin(request, activeHwnd, GetTimeInSeconds());
    if (hWnd) SetTimer(hWnd, FILE_SELECTION_TIMER_ID, 30, nullptr);
}

void PopupWindow::CancelFileSelectionQuery()
{
    m_fileSelection.Cancel();

    if (HWND hWnd = GetHWND())
        KillTimer(hWnd, FILE_SELECTION_TIMER_ID);
}

void PopupWindow::PollFileSelectionQuery()
{
    HWND hWnd = GetHWND();
    if (!m_fileSelection.Poll()) return;
    if (hWnd) KillTimer(hWnd, FILE_SELECTION_TIMER_ID);
    std::vector<std::wstring> files;
    if (m_fileSelection.Peek(GetTimeInSeconds(), -1, files) && hWnd && IsWindow(hWnd))
        PostMessageW(hWnd, WM_USER_SELECTION_UPDATED, 0, 0);
}
