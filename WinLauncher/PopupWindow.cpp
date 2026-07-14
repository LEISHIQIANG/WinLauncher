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
#include "App/AppMessages.h"
#include <windowsx.h>
#include <shellapi.h>
#include <algorithm>
#include <atomic>
#include <functional>
#include <imm.h>
#include <string>
#include <tuple>
#pragma comment(lib, "imm32.lib")
#include "Services/MacroService.h"
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

static bool IsSelectionWithinValidity(double elapsedSeconds, int validitySeconds)
{
    return validitySeconds < 0 || elapsedSeconds < validitySeconds;
}

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

static bool IsSameSceneApp(const AppScene::AppIdentity& left, const AppScene::AppIdentity& right)
{
    if (left.valid != right.valid)
        return false;
    if (!left.valid)
        return true;
    return _wcsicmp(left.exePath.c_str(), right.exePath.c_str()) == 0 &&
           _wcsicmp(left.exeName.c_str(), right.exeName.c_str()) == 0;
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

static bool TimeZoneKeyEquals(const wchar_t* left, const wchar_t* right)
{
    return left && right && _wcsicmp(left, right) == 0;
}

static std::wstring GetCurrentTimeZoneKey()
{
    DYNAMIC_TIME_ZONE_INFORMATION info{};
    if (GetDynamicTimeZoneInformation(&info) == TIME_ZONE_ID_INVALID)
        return L"";
    return info.TimeZoneKeyName;
}

static std::wstring GetSystemToolPath(const wchar_t* fileName)
{
    wchar_t systemDir[MAX_PATH]{};
    if (GetSystemDirectoryW(systemDir, MAX_PATH) == 0)
        return fileName ? fileName : L"";

    std::wstring path = systemDir;
    path += L"\\";
    path += fileName;
    return path;
}

static bool RunHiddenProcessAndWait(const std::wstring& exePath, const std::wstring& arguments, DWORD timeoutMs)
{
    std::wstring commandLine = L"\"" + exePath + L"\"";
    if (!arguments.empty())
        commandLine += L" " + arguments;

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');

    if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return false;

    DWORD waitResult = WaitForSingleObject(pi.hProcess, timeoutMs);
    DWORD exitCode = 1;
    bool ok = false;
    if (waitResult == WAIT_OBJECT_0 && GetExitCodeProcess(pi.hProcess, &exitCode))
    {
        ok = (exitCode == 0);
    }
    else if (waitResult == WAIT_TIMEOUT)
    {
        TerminateProcess(pi.hProcess, 1);
        SetLastError(WAIT_TIMEOUT);
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (!ok && waitResult == WAIT_OBJECT_0)
        SetLastError(exitCode);
    return ok;
}

static bool SetTimeZoneByKey(const wchar_t* keyName)
{
    if (!keyName || !*keyName)
        return false;

    std::wstring tzutilPath = GetSystemToolPath(L"tzutil.exe");
    std::wstring arguments = L"/s \"";
    arguments += keyName;
    arguments += L"\"";

    if (!RunHiddenProcessAndWait(tzutilPath, arguments, 5000))
        return false;

    SendMessageTimeoutW(HWND_BROADCAST, WM_TIMECHANGE, 0, 0, SMTO_ABORTIFHUNG, 200, nullptr);
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"TimeZoneInformation"), SMTO_ABORTIFHUNG, 200, nullptr);
    return true;
}

static bool ToggleChinaLosAngelesTimeZone(HWND parent)
{
    static const wchar_t* kChinaTimeZone = L"China Standard Time";
    static const wchar_t* kLosAngelesTimeZone = L"Pacific Standard Time";

    std::wstring currentKey = GetCurrentTimeZoneKey();
    const wchar_t* targetKey = TimeZoneKeyEquals(currentKey.c_str(), kChinaTimeZone)
        ? kLosAngelesTimeZone
        : kChinaTimeZone;

    if (SetTimeZoneByKey(targetKey))
    {
        LOG_G_INFO(L"ToggleChinaLosAngelesTimeZone: switched from %s to %s", currentKey.c_str(), targetKey);
        return true;
    }

    DWORD err = GetLastError();
    LOG_G_ERRA(L"ToggleChinaLosAngelesTimeZone: failed, current=%s target=%s error=%lu",
               currentKey.c_str(), targetKey, err);
    MessageBoxW(parent,
                L"切换时区失败。请确认当前用户具有更改时区权限。",
                L"WinLauncher",
                MB_OK | MB_ICONWARNING);
    return false;
}

static bool LaunchChinaLosAngelesTimeZoneToggleAsync(AppContext* ctx)
{
    static std::atomic_bool s_running{ false };
    if (s_running.exchange(true))
    {
        LOG_G_WORNING(L"LaunchChinaLosAngelesTimeZoneToggleAsync: toggle already running");
        return true;
    }

    auto tasks = ctx ? ctx->backgroundTasks : nullptr;
    auto handle = tasks ? tasks->Submit(L"timezone.toggle", BackgroundTaskService::Priority::High,
        [](const std::shared_ptr<BackgroundTaskService::CancellationToken>& cancellation) {
            if (!cancellation->IsCancellationRequested()) ToggleChinaLosAngelesTimeZone(nullptr);
            s_running.store(false);
        }) : BackgroundTaskService::TaskHandle{};
    if (!handle) s_running.store(false);
    return static_cast<bool>(handle);
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
    std::lock_guard<std::mutex> lock(m_selectedFilesMutex);
    m_selectedFilesCtx.filePaths.clear();
    m_selectedFilesCtx.isPending = false;
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
}

PopupWindow::IconRefreshState::~IconRefreshState()
{
    for (const auto& result : results)
    {
        if (result.icon) DestroyIcon(result.icon);
    }
}

void PopupWindow::ClearPages()
{
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
    }

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
            pp.sceneMode = vp.sceneMode;
            pp.sceneApps = vp.sceneApps;
            pp.sceneAvailableApps = vp.sceneAvailableApps;
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
    int topBarHeight = GetHeaderLayout().topBarHeight;
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
    HWND prevActive = GetForegroundWindow();
    double showStart = GetTimeInSeconds();

    if (prevActive && prevActive != this->GetHWND())
    {
        this->m_restoreForegroundWnd = prevActive;
    }

    POINT clickPt = pt; // Store the original click position
    AppScene::AppIdentity previousSceneApp = this->m_sceneApp;
    this->m_sceneApp = AppScene::IdentifyTriggerApp(clickPt);
    const bool sceneAppChanged = !IsSameSceneApp(previousSceneApp, this->m_sceneApp);

    // 1. Load configuration and page data if not already loaded
    if (this->m_viewModel && (this->m_pages.empty() || sceneAppChanged))
    {
        this->OnConfigChanged();
    }
    else if (this->m_pages.empty())
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
        this->m_scrollVelocity = 0.0f;
    }

    // 2. Calculate window dimensions using user settings
    int cols = this->GetColumns();
    int rows = this->GetRows();
    int w = cols * this->CellWidth() + this->GetWndPadding() * 2 - this->GetIconGap();
    int indicatorHeight = 0;
    int topBarHeight = this->GetHeaderLayout().topBarHeight;
    int dockRows = this->GetDockHeight();
    int ch = this->CellHeight();
    int wndPad = this->GetWndPadding();
    int iconGap = this->GetIconGap();
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

    int alignMode = (m_appCtx && m_appCtx->configService)
        ? m_appCtx->configService->GetPopupAlignMode()
        : 0;
    int targetX = pt.x - w_px / 2;
    int targetY = pt.y - h_px / 2;
    if (alignMode == 1)
    {
        targetX = pt.x;
        targetY = pt.y;
    }
    else if (alignMode == 2)
    {
        targetX = wa.left + ((wa.right - wa.left) - w_px) / 2;
        targetY = wa.top + ((wa.bottom - wa.top) - h_px) / 2;
    }
    else if (alignMode == 3)
    {
        int margin = (int)(16.0f * scale);
        targetX = wa.right - w_px - margin;
        targetY = wa.bottom - h_px - margin;
    }

    if (targetX + w_px > wa.right) targetX = wa.right - w_px;
    if (targetY + h_px > wa.bottom) targetY = wa.bottom - h_px;
    if (targetX < wa.left) targetX = wa.left;
    if (targetY < wa.top) targetY = wa.top;

    pt.x = targetX;
    pt.y = targetY;

    POINT popupCenter;
    popupCenter.x = targetX + w_px / 2;
    popupCenter.y = targetY + h_px / 2;

    this->StartFileSelectionQuery(prevActive, clickPt, popupCenter);

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
        if (this->EnsureD2D())
        {
            this->EnsureIcons();
        }
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
                this->EnsureIcons();
            }
            needsShow = true;
        }
    }

    if (this->GetHWND())
    {
        if (this->m_rt)
        {
            this->m_rt->SetDpi(scale * 96.0f, scale * 96.0f);
            UIStyle::Typography::ApplyRenderTargetTextDefaults(this->m_rt.Get());
        }

        this->m_hovered = -1;
        this->m_trackMouse = false;
        this->m_animating = false;
        this->m_scrollPosition = (float)this->m_currentPage;
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
 
        this->StartAutoHideTimer();

        const bool backgroundRefreshNeeded = this->m_bgCaptureDirty || this->m_bgCompositeDirty || !this->m_bgFinal;
        double bgElapsedMs = 0.0;
        if (backgroundRefreshNeeded)
        {
            double bgStart = GetTimeInSeconds();
            if (this->m_bgCaptureDirty)
            {
                this->CaptureBackground();
                this->m_bgCaptureDirty = false;
                this->m_bgCompositeDirty = true;
            }
            if (this->m_bgCompositeDirty)
            {
                this->CompositeBackgroundToCache();
                this->m_bgCompositeDirty = false;
            }
            bgElapsedMs = (GetTimeInSeconds() - bgStart) * 1000.0;
        }
        LOG_G_DEBUG(L"PopupWindow perf: show_state sceneChanged=%d geometryChanged=%d dpiChanged=%d bgRefresh=%d bgMs=%.2f pages=%d",
                   sceneAppChanged ? 1 : 0, geometryChanged ? 1 : 0, dpiChanged ? 1 : 0,
                   backgroundRefreshNeeded ? 1 : 0, bgElapsedMs, static_cast<int>(this->m_pages.size()));

        if (needsShow)
        {
            this->PrepareOpenTransitionFrame();
            this->DoPaint();
            ShowWindow(this->GetHWND(), SW_SHOWNOACTIVATE);
        }
        else
        {
            InvalidateRect(this->GetHWND(), nullptr, FALSE);
        }

        SetActiveWindow(this->GetHWND());
        SetForegroundWindow(this->GetHWND());
        SetFocus(this->GetHWND());

        if (this->m_viewModel)
            this->m_viewModel->NotifyPopupShown();

        double showElapsedMs = (GetTimeInSeconds() - showStart) * 1000.0;
        if (showElapsedMs >= POPUP_SLOW_SHOW_MS)
        {
            LOG_G_WORNING(
                L"PopupWindow perf: Show took %.2fms pages=%d dockItems=%d window=%dx%d",
                showElapsedMs,
                (int)this->m_pages.size(),
                (int)this->m_dockPage.shortcuts.size(),
                w_px,
                h_px);
        }
    }
}

void PopupWindow::Hide()
{
    if (s_instance)
    {
        s_instance->HideSelf();
    }
}

void PopupWindow::HideSelf()
{
    HWND h = GetHWND();
    if (h)
    {
        if (GetCapture() == h)
        {
            ReleaseCapture();
        }
        ResetPressedShortcut();
        StopAutoHideTimer();
        KillTimer(h, CLICK_CLOSE_TIMER_ID);
        KillTimer(h, POPUP_ANIMATION_TIMER_ID);
        KillTimer(h, TIMELINE_ANIMATION_TIMER_ID);
        KillTimer(h, PLUGIN_SEARCH_TIMER_ID);
        CancelIconRefresh();
        CancelFileSelectionQuery();
        m_animating = false;
    }

    if (m_destroyOnHide)
    {
        DestroySelf();
        return;
    }

    if (h && UIStyle::Animation::IsEnabled())
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
        if (h) ShowWindow(h, SW_HIDE);
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
            ReleaseCapture();
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
    std::transform(queryLower.begin(), queryLower.end(), queryLower.begin(), [](wchar_t c) {
        return (wchar_t)towlower(c);
    });

    bool slashMode = !m_searchQuery.empty() && m_searchQuery.front() == L'/';
    if (slashMode)
    {
        if (m_appCtx && m_appCtx->pluginManager)
        {
            m_appCtx->pluginManager->RequestSearch(L"");
            auto slashCommands = m_appCtx->pluginManager->SearchSlashCommands(m_searchQuery);
            for (const auto& command : slashCommands)
            {
                SearchResultItem item;
                item.kind = SearchResultItem::Kind::SlashCommand;
                item.shortcut.name = L"/" + command.commandName;
                item.originalPageIndex = -1;
                item.originalShortcutIndex = -1;
                item.pluginId = command.pluginId;
                item.pluginCommandId = command.commandId;
                item.subtitle = command.usage.empty() ? command.description : command.usage;
                item.iconPath = command.icon;
                m_searchResults.push_back(item);
            }
        }
        KillTimer(GetHWND(), PLUGIN_SEARCH_TIMER_ID);
    }
    else
    {
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
                    item.originalPageIndex = ToModelPageIndex((int)pIndex);
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

        if (m_appCtx && m_appCtx->pluginManager)
        {
            m_appCtx->pluginManager->RequestSearch(m_searchQuery);

            auto pluginCommands = m_appCtx->pluginManager->SearchCommands(m_searchQuery);
            for (const auto& command : pluginCommands)
            {
                SearchResultItem item;
                item.kind = SearchResultItem::Kind::PluginCommand;
                item.shortcut.name = command.title;
                item.originalPageIndex = -1;
                item.originalShortcutIndex = -1;
            item.pluginId = command.pluginId;
            item.pluginCommandId = command.commandId;
            item.subtitle = command.description;
            item.iconPath = command.icon;
            m_searchResults.push_back(std::move(item));
            }

            auto pluginResults = m_appCtx->pluginManager->GetCachedSearchResults(m_searchQuery);
            for (const auto& result : pluginResults)
            {
                SearchResultItem item;
                item.kind = SearchResultItem::Kind::PluginSearchResult;
                item.shortcut.name = result.title;
                item.originalPageIndex = -1;
                item.originalShortcutIndex = -1;
                item.pluginId = result.pluginId;
                item.pluginCommandId = result.commandId;
                item.subtitle = result.description;
                item.iconPath = result.icon;
                m_searchResults.push_back(std::move(item));
            }

            if (m_appCtx->pluginManager->IsSearchRunning(m_searchQuery))
                SetTimer(GetHWND(), PLUGIN_SEARCH_TIMER_ID, PLUGIN_SEARCH_REFRESH_MS, nullptr);
            else
                KillTimer(GetHWND(), PLUGIN_SEARCH_TIMER_ID);
        }
    }

    int sortMode = (m_appCtx && m_appCtx->configService)
        ? m_appCtx->configService->GetSortMode()
        : 0;
    if (sortMode == 1 && !slashMode)
    {
        std::stable_sort(m_searchResults.begin(), m_searchResults.end(), [&](const SearchResultItem& a, const SearchResultItem& b) {
            auto score = [&](const SearchResultItem& item) {
                std::wstring name = item.shortcut.name;
                std::transform(name.begin(), name.end(), name.begin(), [](wchar_t c) {
                    return (wchar_t)towlower(c);
                });
                size_t pos = name.find(queryLower);
                int posScore = (pos == std::wstring::npos) ? 10000 : (int)pos;
                UsageHistoryEntry usage{};
                if (m_appCtx && m_appCtx->usageHistory) {
                    std::wstring key = item.kind == SearchResultItem::Kind::LocalShortcut
                        ? L"shortcut:" + item.shortcut.id
                        : L"plugin:" + item.pluginId + L":" + item.pluginCommandId;
                    usage = m_appCtx->usageHistory->Get(key);
                }
                const int prefixScore = name.rfind(queryLower, 0) == 0 ? 0 : 1;
                return std::make_tuple(prefixScore, posScore, 0ULL - usage.launchCount, 0ULL - usage.lastUsedUtc, item.originalPageIndex, item.originalShortcutIndex);
            };
            return score(a) < score(b);
        });
    }
}

void PopupWindow::ExecuteSearchResult(int index)
{
    if (index < 0 || index >= (int)m_searchResults.size())
        return;

    auto& item = m_searchResults[index];
    if (m_appCtx && m_appCtx->usageHistory) {
        const std::wstring key = item.kind == SearchResultItem::Kind::LocalShortcut
            ? L"shortcut:" + item.shortcut.id
            : L"plugin:" + item.pluginId + L":" + item.pluginCommandId;
        // A result reached execution dispatch; failures/cancellations do not record a completion later.
        m_appCtx->usageHistory->RecordAccepted(key);
    }
    if (item.kind == SearchResultItem::Kind::SlashCommand)
    {
        if (!m_appCtx || !m_appCtx->pluginManager)
            return;

        if (item.pluginId.empty() &&
            item.pluginCommandId == L"winlauncher.settings")
        {
            if (m_appCtx->hMainWnd)
                PostMessageW(m_appCtx->hMainWnd, AppMessages::ShowConfigWindow, 0, 0);
            return;
        }

        // Reload is a quick built-in maintenance action. It has no output the
        // user needs to inspect, so avoid opening an otherwise empty panel.
        if (item.pluginId.empty() &&
            item.pluginCommandId == L"winlauncher.reload")
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
            bool pending = false;
            {
                std::lock_guard<std::mutex> lock(m_selectedFilesMutex);
                pending = m_selectedFilesCtx.isPending;
            }
            if (!pending)
                break;
            Sleep(10);
        }

        {
            std::lock_guard<std::mutex> lock(m_selectedFilesMutex);
            double now = GetTimeInSeconds();
            if (!m_selectedFilesCtx.isPending &&
                !m_selectedFilesCtx.filePaths.empty() &&
                IsFileSelectionValid(now - m_selectedFilesCtx.capturedTime))
            {
                files = m_selectedFilesCtx.filePaths;
                m_selectedFilesCtx.filePaths.clear();
            }
        }

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

            if (message.empty())
                message = ok ? L"命令已执行，无输出。" : L"命令执行失败，无错误详情。";
            if (!ok)
                message = L"执行失败：\r\n" + message;
            CommandPanelWindow::PostAppend(panelHwnd, message);
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

            if (message.empty())
                message = ok ? L"命令已执行，无输出。" : L"命令执行失败，无错误详情。";
            if (!ok)
                message = L"执行失败：\r\n" + message;
            CommandPanelWindow::PostAppend(panelHwnd, message);
        };
        CommandPanelWindow::ShowLive(GetHWND(), panelTitle.c_str(), L"", worker, m_appCtx, worker);
        return;
    }

    auto& sc = item.shortcut;
    if (HasLaunchAction(sc))
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
                if (tabTextBrush && m_tabTextFormat)
                {
                    rt->DrawTextW(m_pages[i].name.c_str(), (UINT32)m_pages[i].name.size(), m_tabTextFormat.Get(),
                        tabRect, tabTextBrush.Get());
                }
            }

            float lineW = std::min(header.selectionIndicatorWidth, std::max(10.0f, tabWidth - 4.0f));
            float lineH = std::max(1.2f, header.controlHeight * 0.06f);
            float lineX = topRect.left + m_scrollPosition * tabWidth + (tabWidth - lineW) * 0.5f;
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

    if (anyRecreated && m_searchActive && !m_searchQuery.empty())
    {
        UpdateSearch();
    }
}

void PopupWindow::RefreshIcons()
{
    if (!m_rt || !m_appCtx || !m_appCtx->backgroundTasks) return;
    if (m_refreshingIcons)
    {
        // One active Shell extraction already reflects the latest shortcut
        // state.  Coalesce repeated refresh gestures rather than enqueueing a
        // second whole-dataset disk pass.
        return;
    }
    m_refreshingIcons = true;
    const uint64_t generation = ++m_iconRefreshGeneration;
    auto state = std::make_shared<IconRefreshState>();
    state->generation = generation;
    m_iconRefreshState = state;
    HWND hwnd = GetHWND();

    // Keep the familiar refresh feedback: visible icons briefly return to
    // placeholders, while expensive Shell extraction continues off the UI
    // thread.  Off-screen pages retain their cache until they are visited.
    const int pageCount = static_cast<int>(m_pages.size());
    for (int pageIndex = 0; pageIndex < pageCount; ++pageIndex)
    {
        int distance = std::abs(pageIndex - m_currentPage);
        if (pageCount > 1) distance = (std::min)(distance, pageCount - distance);
        if (distance > 1) continue;
        auto& page = m_pages[pageIndex];
        for (auto*& bitmap : page.iconBitmaps)
        {
            if (bitmap) bitmap->Release();
            bitmap = nullptr;
        }
        for (auto& shortcut : page.shortcuts)
        {
            if (shortcut.hIcon) DestroyIcon(shortcut.hIcon);
            shortcut.hIcon = nullptr;
        }
    }
    for (auto*& bitmap : m_dockPage.iconBitmaps)
    {
        if (bitmap) bitmap->Release();
        bitmap = nullptr;
    }
    for (auto& shortcut : m_dockPage.shortcuts)
    {
        if (shortcut.hIcon) DestroyIcon(shortcut.hIcon);
        shortcut.hIcon = nullptr;
    }
    m_bmpBrushCache.clear();
    EnsureIcons();
    InvalidateRect(hwnd, nullptr, FALSE);
    UpdateWindow(hwnd);

    std::vector<std::tuple<bool, size_t, size_t, RendShortcutInfo>> jobs;
    for (size_t pageIndex = 0; pageIndex < m_pages.size(); ++pageIndex)
        for (size_t shortcutIndex = 0; shortcutIndex < m_pages[pageIndex].shortcuts.size(); ++shortcutIndex)
            jobs.emplace_back(false, pageIndex, shortcutIndex, m_pages[pageIndex].shortcuts[shortcutIndex]);
    for (size_t shortcutIndex = 0; shortcutIndex < m_dockPage.shortcuts.size(); ++shortcutIndex)
        jobs.emplace_back(true, 0, shortcutIndex, m_dockPage.shortcuts[shortcutIndex]);

    m_iconRefreshTask = m_appCtx->backgroundTasks->Submit(L"popup.icon_refresh", BackgroundTaskService::Priority::Normal,
        [state, hwnd, jobs = std::move(jobs)](const std::shared_ptr<BackgroundTaskService::CancellationToken>& cancellation) mutable {
            const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            for (auto& job : jobs)
            {
                if (state->cancelled || cancellation->IsCancellationRequested()) break;
                auto& shortcut = std::get<3>(job);
                HICON icon = ShortcutManager::GetShortcutIcon(shortcut);
                if (state->cancelled || cancellation->IsCancellationRequested())
                {
                    if (icon) DestroyIcon(icon);
                    break;
                }
                std::lock_guard<std::mutex> lock(state->mutex);
                state->results.push_back({ std::get<0>(job), std::get<1>(job), std::get<2>(job), icon });
            }
            if (SUCCEEDED(comResult)) CoUninitialize();
            if (!state->cancelled && !cancellation->IsCancellationRequested() && IsWindow(hwnd))
                PostMessageW(hwnd, WM_USER_REFRESH_ICONS, 0, 0);
        });
    if (!m_iconRefreshTask)
    {
        m_refreshingIcons = false;
        m_iconRefreshState.reset();
        LOG_G_WORNING(L"PopupWindow perf: icon refresh was not queued");
    }
}

void PopupWindow::CancelIconRefresh()
{
    m_iconRefreshTask.Cancel();
    m_iconRefreshTask = {};
    if (m_iconRefreshState) m_iconRefreshState->cancelled = true;
    m_iconRefreshState.reset();
    m_refreshingIcons = false;
    m_iconRefreshPending = false;
    ++m_iconRefreshGeneration;
}

void PopupWindow::ApplyRefreshedIcons()
{
    auto state = m_iconRefreshState;
    if (!state || state->cancelled || state->generation != m_iconRefreshGeneration) return;
    std::vector<RefreshedIcon> results;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        results.swap(state->results);
    }
    const double started = GetTimeInSeconds();
    int applied = 0;
    for (auto& result : results)
    {
        auto* page = result.dock ? &m_dockPage : (result.pageIndex < m_pages.size() ? &m_pages[result.pageIndex] : nullptr);
        if (!page || result.shortcutIndex >= page->shortcuts.size()) { if (result.icon) DestroyIcon(result.icon); continue; }
        auto& shortcut = page->shortcuts[result.shortcutIndex];
        if (shortcut.hIcon) DestroyIcon(shortcut.hIcon);
        shortcut.hIcon = result.icon;
        if (result.shortcutIndex < page->iconBitmaps.size() && page->iconBitmaps[result.shortcutIndex])
        {
            page->iconBitmaps[result.shortcutIndex]->Release();
            page->iconBitmaps[result.shortcutIndex] = nullptr;
        }
        ++applied;
    }
    m_bmpBrushCache.clear();
    m_refreshingIcons = false;
    m_iconRefreshTask = {};
    EnsureIcons();
    InvalidateRect(GetHWND(), nullptr, FALSE);
    LOG_G_DEBUG(L"PopupWindow perf: icon refresh applied=%d total=%zu ui_ms=%.2f generation=%llu",
               applied, results.size(), (GetTimeInSeconds() - started) * 1000.0,
               static_cast<unsigned long long>(m_iconRefreshGeneration));
    if (m_iconRefreshPending)
    {
        m_iconRefreshPending = false;
        RefreshIcons();
    }
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
    int topBarHeight = GetHeaderLayout().topBarHeight;

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
            if (IsFileSelectionValid(elapsed))
            {
                hasActiveSelection = true;
            }
        }
    }

    if (hasActiveSelection)
    {
        const int validitySeconds = GetFileSelectionValiditySeconds();
        float progress = validitySeconds < 0
            ? 1.0f
            : (float)(1.0 - (elapsed / validitySeconds));
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
    int topBarHeight = GetHeaderLayout().topBarHeight;
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
    int topBarHeight = GetHeaderLayout().topBarHeight;

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
        if (numPages > 1)
        {
            float halfN = (float)numPages / 2.0f;
            if (error > halfN) error -= (float)numPages;
            else if (error < -halfN) error += (float)numPages;
        }

        float force = error * stiffness - m_scrollVelocity * damping;
        m_scrollVelocity += force * currentStep;
        m_scrollPosition += m_scrollVelocity * currentStep;

        if (numPages > 1)
        {
            while (m_scrollPosition < 0.0f) m_scrollPosition += (float)numPages;
            while (m_scrollPosition >= (float)numPages) m_scrollPosition -= (float)numPages;
        }

        remainingTime -= currentStep;
    }

    float finalError = target - m_scrollPosition;
    if (numPages > 1)
    {
        float halfN = (float)numPages / 2.0f;
        if (finalError > halfN) finalError -= (float)numPages;
        else if (finalError < -halfN) finalError += (float)numPages;
    }

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
        m_animating = false;
    }

    if (m_viewModel)
        m_viewModel->UpdateAnimation();

    InvalidateRect(hWnd, nullptr, FALSE);
    // Present each timer step instead of allowing the final spring frames to
    // be coalesced with the next message-loop pass.
    UpdateWindow(hWnd);

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
            int topBarHeight = GetHeaderLayout().topBarHeight;
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
            bool isEmpty = false;
            {
                std::lock_guard<std::mutex> lock(m_selectedFilesMutex);
                elapsed = now - m_selectedFilesCtx.capturedTime;
                isEmpty = m_selectedFilesCtx.filePaths.empty();
            }

            const int validitySeconds = GetFileSelectionValiditySeconds();
            const bool selectionExpired = !IsSelectionWithinValidity(elapsed, validitySeconds);
            if (selectionExpired || isEmpty || validitySeconds < 0)
            {
                KillTimer(hWnd, TIMELINE_ANIMATION_TIMER_ID);
                if (selectionExpired)
                {
                    std::lock_guard<std::mutex> lock(m_selectedFilesMutex);
                    m_selectedFilesCtx.filePaths.clear();
                }
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
        if (!m_refreshingIcons) return 0;
        ApplyRefreshedIcons();
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
            if (m_viewModel) m_viewModel->SetCurrentPage(ToModelPageIndex(targetPage));

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
                ReleaseCapture();
            }
            ResetPressedShortcut();

            if (pressedKind == PressedShortcutKind::SearchResult && m_searchActive && !m_searchQuery.empty())
            {
                int hit = HitTest(pt);
                if (hit == pressedIndex && hit >= 0 && hit < (int)m_searchResults.size())
                {
                    if (!m_pinned) HideSelf();
                    ExecuteSearchResult(hit);
                }
            }
            else if (pressedKind == PressedShortcutKind::Dock)
            {
                int dockHit = HitTestDock(pt);
                if (dockHit == pressedIndex && dockHit >= 0 && dockHit < (int)m_dockPage.shortcuts.size())
                {
                    auto& sc = m_dockPage.shortcuts[dockHit];
                    if (!m_pinned) HideSelf();
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
                    if (!m_pinned) HideSelf();
                    if (HasLaunchAction(sc))
                    {
                        LOG_G_INFO(L"PopupWindow::LButtonUp: launching shortcut %s (Target=%s)", sc.name.c_str(), sc.targetPath.c_str());
                        LaunchShortcut(sc);
                    }
                    if (m_viewModel)
                        m_viewModel->NotifyShortcutLaunched(ToModelPageIndex(pressedPage), hit);
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
        m_trackMouse = false;
        m_hovered = -1;
        if (!m_pinned && m_pressedShortcutKind == PressedShortcutKind::None)
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
                    ExecuteSearchResult(m_selectedSearchResult);
                    if (!m_pinned) HideSelf();
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

static void SimulateHotkey(const std::wstring& hotkeyStr, bool afterClose, AppContext* ctx)
{
    auto tasks = ctx ? ctx->backgroundTasks : nullptr;
    if (!tasks) return;
    tasks->Submit(L"hotkey.simulate", BackgroundTaskService::Priority::High,
        [hotkeyStr, afterClose](const std::shared_ptr<BackgroundTaskService::CancellationToken>& cancellation) {
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
        
        if (keys.empty() || cancellation->IsCancellationRequested()) return;
        
        for (WORD k : keys)
        {
            keybd_event(static_cast<BYTE>(k), 0, 0, 0);
        }
        for (auto it = keys.rbegin(); it != keys.rend(); ++it)
        {
            keybd_event(static_cast<BYTE>(*it), 0, KEYEVENTF_KEYUP, 0);
        }
    });
}

static std::wstring ExpandVariables(const std::wstring& inputStr, HWND parent, AppContext* ctx, bool& cancelled)
{
    cancelled = false;
    
    std::vector<std::wstring> files;
    if (PopupWindow::s_instance)
    {
        std::lock_guard<std::mutex> lock(PopupWindow::s_instance->m_selectedFilesMutex);
        double now = GetTimeInSeconds();
        if (!PopupWindow::s_instance->m_selectedFilesCtx.isPending && 
            !PopupWindow::s_instance->m_selectedFilesCtx.filePaths.empty() && 
            PopupWindow::s_instance->IsFileSelectionValid(now - PopupWindow::s_instance->m_selectedFilesCtx.capturedTime))
        {
            files = PopupWindow::s_instance->m_selectedFilesCtx.filePaths;
        }
    }

    std::map<std::wstring, std::wstring> inputValues;
    if (!Services::CommandVariableService::ResolveInputs(parent, inputStr, inputValues))
    {
        cancelled = true;
        return L"";
    }

    return Services::CommandVariableService::ResolveVariables(inputStr, L"cmd", files, inputValues);
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

static bool LaunchCommand(const RendShortcutInfo& sc, HWND parent, AppContext* ctx, const std::vector<std::wstring>& selectedFiles)
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
    bool showWindow = false;
    bool captureOutput = false;
    int timeoutSeconds = 300;
    int maxChars = 50000;

    if (segments.size() > 0) type = segments[0];
    if (segments.size() > 2) showWindow = (segments[2] == L"1");
    if (segments.size() > 3) captureOutput = (segments[3] == L"1");
    if (captureOutput) showWindow = false;
    if (segments.size() > 4)
    {
        try
        {
            const int configuredTimeout = std::stoi(segments[4]);
            if (configuredTimeout >= 1 && configuredTimeout <= 3600)
                timeoutSeconds = configuredTimeout;
            else
                LOG_G_WORNING(L"ExecuteCommand: invalid timeout=%d; using default 300 seconds", configuredTimeout);
        }
        catch (...)
        {
            LOG_G_WORNING(L"ExecuteCommand: invalid timeout text; using default 300 seconds");
        }
    }
    if (segments.size() > 5) { try { int v = std::stoi(segments[5]); if (v > 2000) maxChars = v; } catch(...) {} }
    
    std::map<std::wstring, std::wstring> inputValues;
    if (!Services::CommandVariableService::ResolveInputs(parent, sc.targetPath, inputValues, ctx ? ctx->userInteraction.get() : nullptr))
    {
        return false;
    }

    std::wstring resolvedCmd = Services::CommandVariableService::ResolveVariables(sc.targetPath, type, selectedFiles, inputValues);

    if (ctx && ctx->userInteraction)
    {
        if (!ctx->userInteraction->ConfirmHighRiskCommand(parent, resolvedCmd, sc.name))
        {
            return false;
        }
    }

    auto commandExec = ctx ? ctx->commandExecution : nullptr;

    if (captureOutput)
    {
        maxChars = 0;
        std::wstring panelTitle = L"命令输出 - " + sc.name;
        std::wstring initialText;
        CommandPanelWindow::ShowLive(parent, panelTitle.c_str(), initialText.c_str(),
            [commandExec, type, resolvedCmd, timeoutSeconds, maxChars](HWND panelHwnd) {
                auto append = [panelHwnd](const std::wstring& text) {
                    CommandPanelWindow::PostAppend(panelHwnd, text);
                };

                if (commandExec)
                {
                    CommandExecutionRequest request;
                    request.type = type;
                    request.commandText = resolvedCmd;
                    request.timeoutSeconds = timeoutSeconds;
                    request.maxChars = maxChars;
                    commandExec->ExecuteStreaming(request, append);
                }
                else
                {
                    append(L"\r\n未找到命令执行服务。\r\n状态: 失败\r\n");
                }
            },
            ctx);
        return true;
    }

    std::wstring output;
    bool ok = false;

    if (commandExec)
    {
        CommandExecutionRequest request;
        request.type = type;
        request.commandText = resolvedCmd;
        request.showWindow = showWindow;
        request.captureOutput = captureOutput;
        request.timeoutSeconds = timeoutSeconds;
        request.maxChars = maxChars;

        CommandExecutionResult result;
        ok = commandExec->Execute(request, result);
        output = result.output;
    }
    else
    {
        output = L"未找到命令执行服务。";
        ok = false;
    }

    if (captureOutput && (ok || !output.empty()))
    {
        std::wstring panelTitle = L"命令输出 - " + sc.name;
        CommandPanelWindow::Show(parent, panelTitle.c_str(), output.c_str(), ctx);
    }

    return ok;
}

void PopupWindow::LaunchShortcut(const RendShortcutInfo& sc)
{
    HWND hWnd = GetHWND();
    std::vector<std::wstring> files;
    bool hasFiles = false;
    {
        std::lock_guard<std::mutex> lock(m_selectedFilesMutex);
        double now = GetTimeInSeconds();
        if (!m_selectedFilesCtx.isPending && !m_selectedFilesCtx.filePaths.empty() && IsFileSelectionValid(now - m_selectedFilesCtx.capturedTime))
        {
            files = m_selectedFilesCtx.filePaths;
            hasFiles = true;
            m_selectedFilesCtx.filePaths.clear();
        }
    }

    const bool isVirtualSystemAction =
        sc.type == Model::ShortcutType::System &&
        !sc.targetPath.empty() &&
        sc.targetPath.front() == L':';

    const bool acceptsSelectedFiles =
        !isVirtualSystemAction &&
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
        ExecuteShortcut(sc, hWnd, m_appCtx, files);
    }
}

bool PopupWindow::ExecuteShortcut(const RendShortcutInfo& sc, HWND parent, AppContext* ctx, const std::vector<std::wstring>& selectedFiles)
{
    if (sc.type == Model::ShortcutType::Hotkey)
    {
        bool afterClose = sc.runAsAdmin;
        SimulateHotkey(sc.targetPath, afterClose, ctx);
        return true;
    }
    else if (sc.type == Model::ShortcutType::Url)
    {
        return LaunchUrl(sc, parent, ctx);
    }
    else if (sc.type == Model::ShortcutType::Command)
    {
        return LaunchCommand(sc, parent, ctx, selectedFiles);
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
        else if (sc.targetPath == L":timezone_cn_la_toggle")
        {
            return LaunchChinaLosAngelesTimeZoneToggleAsync(ctx);
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
            PopupWindow* sourcePopup = PopupWindow::FindByHwnd(parent);
            bool launchedFromPopup = sourcePopup != nullptr;
            bool popupWillHide = launchedFromPopup && !sourcePopup->m_pinned;
            bool waitForClose = popupWillHide || triggerMode == L"after_close";
            HWND restoreWnd = nullptr;
            if (waitForClose && sourcePopup)
            {
                restoreWnd = sourcePopup->m_restoreForegroundWnd && IsWindow(sourcePopup->m_restoreForegroundWnd)
                    ? sourcePopup->m_restoreForegroundWnd
                    : nullptr;
            }
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

    CancelFileSelectionQuery();
    m_selectionRequest = Services::FileSelectionService::CaptureSelectedFilesAsync(
        activeHwnd, clickPt, popupCenter, m_appCtx ? m_appCtx->backgroundTasks : nullptr);
    if (hWnd) SetTimer(hWnd, FILE_SELECTION_TIMER_ID, 30, nullptr);
}

void PopupWindow::CancelFileSelectionQuery()
{
    if (m_selectionRequest)
    {
        m_selectionRequest->Cancel();
        m_selectionRequest.reset();
    }

    if (HWND hWnd = GetHWND())
        KillTimer(hWnd, FILE_SELECTION_TIMER_ID);
}

void PopupWindow::PollFileSelectionQuery()
{
    if (!m_selectionRequest) return;
    Services::SelectionContext result;
    if (!m_selectionRequest->TryGetResult(result)) return;

    HWND hWnd = GetHWND();
    if (hWnd) KillTimer(hWnd, FILE_SELECTION_TIMER_ID);
    auto completed = std::move(m_selectionRequest);

    bool hasFiles = false;
    {
        std::lock_guard<std::mutex> lock(m_selectedFilesMutex);
        if (m_selectedFilesCtx.sourceHwnd == result.sourceHwnd)
        {
            m_selectedFilesCtx = std::move(result);
            hasFiles = !m_selectedFilesCtx.filePaths.empty();
        }
    }
    if (hasFiles && hWnd && IsWindow(hWnd))
        PostMessageW(hWnd, WM_USER_SELECTION_UPDATED, 0, 0);
}
