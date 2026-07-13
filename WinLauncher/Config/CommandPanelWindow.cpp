#define NOMINMAX
#include "CommandPanelWindow.h"
#include "UIStyle.h"
#include "DropDownMenu.h"
#include "../DpiHelper.h"
#include "../App/Logger.h"
#include "../App/AppContext.h"
#include <windowsx.h>
#include <commctrl.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <thread>
#include <map>
#include <mutex>
#include "../App/AppMessages.h"

#pragma comment(lib, "comctl32.lib")

static CommandPanelWindow* g_cmdPanelInstance = nullptr;
static const UINT WM_COMMAND_PANEL_APPEND = AppMessages::CommandPanelAppend;
static const UINT WM_COMMAND_PANEL_REFRESH_DONE = AppMessages::CommandPanelRefreshDone;
static const UINT_PTR COMMAND_PANEL_CARET_TIMER_ID = 0x999;
static const UINT_PTR COMMAND_PANEL_LOADING_TIMER_ID = 0x998;
static const UINT_PTR COMMAND_PANEL_APPEND_TIMER_ID = 0x997;
static const UINT COMMAND_PANEL_LOADING_FRAME_MS = 16;
static std::atomic<uint64_t> g_commandPanelGeneration{ 1 };
static thread_local uint64_t t_commandPanelGeneration = 0;
static std::atomic<uint64_t> g_commandPanelInstanceGeneration{ 1 };
static thread_local uint64_t t_commandPanelInstanceToken = 0;
static std::mutex g_commandPanelRegistryMutex;
static std::map<HWND, uint64_t> g_commandPanelRegistry;

struct CommandPanelAppendPayload
{
    uint64_t generation = 0;
    uint64_t instanceToken = 0;
    std::wstring text;
};

static void ClampWindowToWorkArea(int& x, int& y, int w, int h, const RECT& workArea)
{
    if (w >= workArea.right - workArea.left)
    {
        x = workArea.left;
    }
    else
    {
        if (x < workArea.left) x = workArea.left;
        if (x + w > workArea.right) x = workArea.right - w;
    }

    if (h >= workArea.bottom - workArea.top)
    {
        y = workArea.top;
    }
    else
    {
        if (y < workArea.top) y = workArea.top;
        if (y + h > workArea.bottom) y = workArea.bottom - h;
    }
}

CommandPanelWindow::CommandPanelWindow(const wchar_t* title, const wchar_t* outputText, AppContext* ctx, std::function<void(HWND)> refreshWorker)
    : GlassWindow()
    , m_title(title)
    , m_outputText(outputText ? outputText : L"")
    , m_initialText(outputText ? outputText : L"")
    , m_refreshWorker(std::move(refreshWorker))
    , m_hoveredOk(false)
    , m_hoveredCopy(false)
    , m_hoveredRefresh(false)
    , m_hoveredMore(false)
    , m_hoveredClose(false)
    , m_trackMouse(false)
    , m_refreshRunning(false)
    , m_loadingFrame(0)
    , m_loadingStartedTick(0)
    , m_workerGeneration(0)
    , m_instanceToken(g_commandPanelInstanceGeneration.fetch_add(1))
{
    m_appCtx = ctx;
    RegisterBuiltinActions();
}

CommandPanelWindow::~CommandPanelWindow()
{
    m_textBox.Destroy();
}

void CommandPanelWindow::ClearOutput(const wchar_t* initialText)
{
    m_pendingOutput.clear();
    m_outputText = initialText ? initialText : L"";
    m_textBox.SetText(m_outputText);
    if (GetHWND())
    {
        InvalidateRect(GetHWND(), nullptr, FALSE);
    }
}

void CommandPanelWindow::Show(HWND parent, const wchar_t* title, const wchar_t* outputText, AppContext* ctx, std::function<void(HWND)> refreshWorker)
{
    ShowLive(parent, title, outputText, nullptr, ctx, std::move(refreshWorker));
}

bool CommandPanelWindow::PostAppend(HWND hwnd, const std::wstring& text)
{
    if (!hwnd || text.empty() || !IsWindow(hwnd))
        return false;

    CommandPanelAppendPayload* payload = new CommandPanelAppendPayload();
    payload->generation = t_commandPanelGeneration;
    payload->instanceToken = t_commandPanelInstanceToken;
    payload->text = text;
    {
        std::lock_guard<std::mutex> lock(g_commandPanelRegistryMutex);
        auto it = g_commandPanelRegistry.find(hwnd);
        if (it == g_commandPanelRegistry.end() ||
            (payload->instanceToken != 0 && payload->instanceToken != it->second))
        {
            delete payload;
            return false;
        }
        if (payload->instanceToken == 0) payload->instanceToken = it->second;
    }
    if (!PostMessageW(hwnd, WM_COMMAND_PANEL_APPEND, 0, reinterpret_cast<LPARAM>(payload)))
    {
        delete payload;
        return false;
    }
    return true;
}

void CommandPanelWindow::ShowLive(HWND parent, const wchar_t* title, const wchar_t* initialText, std::function<void(HWND)> worker, AppContext* ctx, std::function<void(HWND)> refreshWorker)
{
    if (!refreshWorker && worker)
        refreshWorker = worker;

    if (g_cmdPanelInstance)
    {
        HWND existing = g_cmdPanelInstance->GetHWND();
        if (existing && IsWindow(existing))
        {
            g_cmdPanelInstance->CancelWorker();
            RECT rc{};
            GetWindowRect(existing, &rc);
            LOG_G_INFO(L"CommandPanelWindow::ShowLive: existing panel hwnd=%p visible=%d rect=(%ld,%ld,%ld,%ld)",
                       existing, IsWindowVisible(existing) ? 1 : 0, rc.left, rc.top, rc.right, rc.bottom);
            g_cmdPanelInstance->m_title = title ? title : L"";
            g_cmdPanelInstance->m_initialText = initialText ? initialText : L"";
            g_cmdPanelInstance->m_refreshWorker = std::move(refreshWorker);
            g_cmdPanelInstance->m_refreshRunning = worker != nullptr;
            g_cmdPanelInstance->m_loadingFrame = 0;
            g_cmdPanelInstance->m_loadingStartedTick = worker ? GetTickCount64() : 0;
            g_cmdPanelInstance->m_workerGeneration = worker ? g_commandPanelGeneration.fetch_add(1) : 0;
            g_cmdPanelInstance->ClearOutput(initialText);
            if (worker)
                SetTimer(existing, COMMAND_PANEL_LOADING_TIMER_ID, COMMAND_PANEL_LOADING_FRAME_MS, nullptr);
            else
                KillTimer(existing, COMMAND_PANEL_LOADING_TIMER_ID);
            ShowWindow(existing, SW_SHOWNORMAL);
            SetWindowPos(existing, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOOWNERZORDER);
            BringWindowToTop(existing);
            SetForegroundWindow(existing);
            SetFocus(existing);

            if (worker)
            {
                HWND workerHwnd = existing;
                uint64_t generation = g_cmdPanelInstance->m_workerGeneration;
                uint64_t instanceToken = g_cmdPanelInstance->m_instanceToken;
                auto tasks = ctx ? ctx->backgroundTasks : nullptr;
                auto handle = tasks ? tasks->Submit(L"command.panel.worker", BackgroundTaskService::Priority::High,
                    [worker, workerHwnd, generation, instanceToken](const std::shared_ptr<BackgroundTaskService::CancellationToken>& cancellation) {
                    t_commandPanelGeneration = generation;
                    t_commandPanelInstanceToken = instanceToken;
                    if (!cancellation->IsCancellationRequested()) worker(workerHwnd);
                    if (!cancellation->IsCancellationRequested() && IsWindow(workerHwnd))
                        PostMessageW(workerHwnd, WM_COMMAND_PANEL_REFRESH_DONE, 0, (LPARAM)generation);
                    t_commandPanelGeneration = 0;
                    t_commandPanelInstanceToken = 0;
                }) : BackgroundTaskService::TaskHandle{};
                if (!handle)
                {
                    g_cmdPanelInstance->m_refreshRunning = false;
                    g_cmdPanelInstance->m_workerGeneration = 0;
                    KillTimer(workerHwnd, COMMAND_PANEL_LOADING_TIMER_ID);
                    PostAppend(workerHwnd, L"\r\n后台任务繁忙，命令未启动。\r\n");
                }
                else
                {
                    g_cmdPanelInstance->m_workerTask = std::move(handle);
                }
            }
        }
        return;
    }

    CommandPanelWindow* win = new CommandPanelWindow(title, initialText, ctx, std::move(refreshWorker));
    HWND owner = (parent && IsWindowVisible(parent)) ? parent : nullptr;

    int w = 310;
    int h = 420;

    HMONITOR hm = owner ? MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST) : ([]{
        POINT pt; GetCursorPos(&pt);
        return MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    }());

    float scale = DpiHelper::GetDpiScaleForMonitor(hm);
    int w_px = (int)(w * scale);
    int h_px = (int)(h * scale);

    int x = 0, y = 0;
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(hm, &mi);

    if (owner)
    {
        RECT pr; GetWindowRect(owner, &pr);
        x = pr.left + (pr.right - pr.left - w_px) / 2;
        y = pr.top + (pr.bottom - pr.top - h_px) / 2;
    }
    else
    {
        RECT wa = mi.rcWork;
        x = wa.left + (wa.right - wa.left - w_px) / 2;
        y = wa.top + (wa.bottom - wa.top - h_px) / 2;
    }

    ClampWindowToWorkArea(x, y, w_px, h_px, mi.rcWork);
    win->Create(L"", WS_POPUP, WS_EX_TOOLWINDOW | WS_EX_TOPMOST, x, y, w_px, h_px, owner);
    if (!win->GetHWND())
    {
        LOG_G_ERRA(L"CommandPanelWindow::ShowLive: Create failed owner=%p rect=(%d,%d,%d,%d) error=%lu",
                   owner, x, y, w_px, h_px, GetLastError());
        delete win;
        return;
    }

    LOG_G_INFO(L"CommandPanelWindow::ShowLive: created panel hwnd=%p owner=%p rect=(%d,%d,%d,%d) workArea=(%ld,%ld,%ld,%ld)",
               win->GetHWND(), owner, x, y, x + w_px, y + h_px,
               mi.rcWork.left, mi.rcWork.top, mi.rcWork.right, mi.rcWork.bottom);

    SetWindowDisplayAffinitySafe(win->GetHWND());
    win->ApplySystemBackdrop();
    win->EnsureD2D();

    ShowWindow(win->GetHWND(), SW_SHOW);
    UpdateWindow(win->GetHWND());
    SetWindowPos(win->GetHWND(), HWND_TOPMOST, x, y, w_px, h_px,
                 SWP_SHOWWINDOW | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    BringWindowToTop(win->GetHWND());
    SetForegroundWindow(win->GetHWND());
    SetFocus(win->GetHWND());

    g_cmdPanelInstance = win;
    {
        std::lock_guard<std::mutex> lock(g_commandPanelRegistryMutex);
        g_commandPanelRegistry[win->GetHWND()] = win->m_instanceToken;
    }

    if (worker)
    {
        HWND workerHwnd = win->GetHWND();
        win->m_refreshRunning = true;
        win->m_loadingFrame = 0;
        win->m_loadingStartedTick = GetTickCount64();
        win->m_workerGeneration = g_commandPanelGeneration.fetch_add(1);
        SetTimer(workerHwnd, COMMAND_PANEL_LOADING_TIMER_ID, COMMAND_PANEL_LOADING_FRAME_MS, nullptr);
        InvalidateRect(workerHwnd, nullptr, FALSE);
        uint64_t generation = win->m_workerGeneration;
        uint64_t instanceToken = win->m_instanceToken;
        auto tasks = ctx ? ctx->backgroundTasks : nullptr;
        auto handle = tasks ? tasks->Submit(L"command.panel.worker", BackgroundTaskService::Priority::High,
            [worker, workerHwnd, generation, instanceToken](const std::shared_ptr<BackgroundTaskService::CancellationToken>& cancellation) {
            t_commandPanelGeneration = generation;
            t_commandPanelInstanceToken = instanceToken;
            if (!cancellation->IsCancellationRequested()) worker(workerHwnd);
            if (!cancellation->IsCancellationRequested() && IsWindow(workerHwnd))
                PostMessageW(workerHwnd, WM_COMMAND_PANEL_REFRESH_DONE, 0, (LPARAM)generation);
            t_commandPanelGeneration = 0;
            t_commandPanelInstanceToken = 0;
        }) : BackgroundTaskService::TaskHandle{};
        if (!handle)
        {
            win->m_refreshRunning = false;
            win->m_workerGeneration = 0;
            KillTimer(workerHwnd, COMMAND_PANEL_LOADING_TIMER_ID);
            PostAppend(workerHwnd, L"\r\n后台任务繁忙，命令未启动。\r\n");
        }
        else
        {
            win->m_workerTask = std::move(handle);
        }
    }
}

LRESULT CommandPanelWindow::HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_COMMAND_PANEL_APPEND:
    {
        CommandPanelAppendPayload* payload = reinterpret_cast<CommandPanelAppendPayload*>(lParam);
        if (payload)
        {
            if (payload->instanceToken == m_instanceToken &&
                (payload->generation == 0 || payload->generation == m_workerGeneration))
            {
                m_pendingOutput += payload->text;
                if (m_pendingOutput.size() >= 4096)
                    FlushPendingOutput();
                else
                    SetTimer(hWnd, COMMAND_PANEL_APPEND_TIMER_ID, 50, nullptr);
            }
            delete payload;
        }
        return 0;
    }
    case WM_COMMAND_PANEL_REFRESH_DONE:
    {
        uint64_t generation = (uint64_t)lParam;
        if (generation != 0 && generation != m_workerGeneration)
            return 0;
        m_refreshRunning = false;
        m_workerGeneration = 0;
        m_workerTask = {};
        KillTimer(hWnd, COMMAND_PANEL_LOADING_TIMER_ID);
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }
    case WM_CREATE:
    {
        EnsureD2D();
        RECT cr; GetClientRect(hWnd, &cr);
        float s = GetWindowScale(hWnd);
        float cw = (float)cr.right / s;
        float ch = (float)cr.bottom / s;
        UIStyle::TextBoxStyle style;
        style.fontFamily = L"Consolas";
        style.fontSize = 10;
        style.paddingTop = 6.0f;
        style.paddingBottom = 6.0f;
        m_textBox.SetStyle(style);
        m_textBox.SetMultiline(true);
        m_textBox.Create(hWnd, m_dw.Get(), D2D1::RectF(10.0f, 44.0f, cw - 10.0f, ch - 55.0f), m_outputText);
        m_textBox.SetFocus(true);
        SetTimer(hWnd, COMMAND_PANEL_CARET_TIMER_ID, GetCaretBlinkTime(), nullptr);
        if (m_refreshRunning)
            SetTimer(hWnd, COMMAND_PANEL_LOADING_TIMER_ID, COMMAND_PANEL_LOADING_FRAME_MS, nullptr);
        return 0;
    }
    case WM_NCHITTEST:
    {
        LRESULT result = GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
        if (result == HTCLIENT)
        {
            POINT pt{ (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam) };
            ScreenToClient(hWnd, &pt);
            float s = GetWindowScale(hWnd);
            int y = (int)(pt.y / s);
            if (y >= 0 && y < 32)
            {
                int w = 0;
                {
                    RECT cr; GetClientRect(hWnd, &cr);
                    w = (int)((cr.right - cr.left) / s);
                }
                if (pt.x / s < w - 30)
                    result = HTCAPTION;
            }
        }
        return result;
    }
    case WM_SIZE:
    {
        UpdateChildLayout();
        break;
    }
    case WM_COMMAND:
    {
        PostMessageW(hWnd, WM_CLOSE, 0, 0);
        return 0;
    }
    case WM_ACTIVATE:
    {
        GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
        if (LOWORD(wParam) != WA_INACTIVE)
        {
            SetFocus(hWnd);
        }
        return 0;
    }
    case WM_SETFOCUS:
    {
        m_textBox.SetFocus(true);
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }
    case WM_KILLFOCUS:
    {
        m_textBox.SetFocus(false);
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }
    case WM_TIMER:
    {
        if (wParam == COMMAND_PANEL_CARET_TIMER_ID)
        {
            m_textBox.BlinkCaret();
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        }
        if (wParam == COMMAND_PANEL_LOADING_TIMER_ID)
        {
            if (!m_refreshRunning)
            {
                KillTimer(hWnd, COMMAND_PANEL_LOADING_TIMER_ID);
                return 0;
            }
            m_loadingFrame = (m_loadingFrame + 1) % 60;
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        }
        if (wParam == COMMAND_PANEL_APPEND_TIMER_ID)
        {
            KillTimer(hWnd, COMMAND_PANEL_APPEND_TIMER_ID);
            FlushPendingOutput();
            return 0;
        }
        break;
    }
    case WM_KEYDOWN:
    {
        if (wParam == VK_RETURN || wParam == VK_SPACE || wParam == VK_ESCAPE)
        {
            PostMessageW(hWnd, WM_CLOSE, 0, 0);
            return 0;
        }
        bool repaint = false;
        m_textBox.OnKeyDown(hWnd, wParam, lParam, repaint);
        if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }
    case WM_CHAR:
    {
        // Multiline read-only textbox: ignore character inputs (typing), but repaint if needed
        bool repaint = false;
        if (wParam == 3 || wParam == 24 || wParam == 22) // Ctrl+C, Ctrl+X, Ctrl+V can be handled by textbox
        {
            m_textBox.OnChar(hWnd, wParam, repaint);
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

        bool ho = HitTestOkButton(pt);
        if (ho != m_hoveredOk) { m_hoveredOk = ho; repaint = true; }

        bool hCopy = HitTestCopyButton(pt);
        if (hCopy != m_hoveredCopy) { m_hoveredCopy = hCopy; repaint = true; }

        bool hRefresh = HitTestRefreshButton(pt);
        if (hRefresh != m_hoveredRefresh) { m_hoveredRefresh = hRefresh; repaint = true; }

        bool hMore = HitTestMoreButton(pt);
        if (hMore != m_hoveredMore) { m_hoveredMore = hMore; repaint = true; }

        bool hc = HitTestCloseButton(pt);
        if (hc != m_hoveredClose) { m_hoveredClose = hc; repaint = true; }

        POINT rawPt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        m_textBox.OnMouseMove(hWnd, rawPt, scale, repaint);

        if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSELEAVE:
    {
        m_hoveredOk = false;
        m_hoveredCopy = false;
        m_hoveredRefresh = false;
        m_hoveredMore = false;
        m_hoveredClose = false;
        m_trackMouse = false;
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONDOWN:
    {
        float scale = GetWindowScale(hWnd);
        POINT pt{ (int)(GET_X_LPARAM(lParam) / scale), (int)(GET_Y_LPARAM(lParam) / scale) };

        if (HitTestCloseButton(pt) || HitTestOkButton(pt))
        {
            PostMessageW(hWnd, WM_CLOSE, 0, 0);
            return 0;
        }

        if (HitTestCopyButton(pt))
        {
            CopyOutputToClipboard();
            return 0;
        }

        if (HitTestRefreshButton(pt))
        {
            RunRefresh();
            return 0;
        }

        if (HitTestMoreButton(pt))
        {
            ShowMoreDropdown();
            return 0;
        }

        SetFocus(hWnd);
        bool repaint = false;
        POINT rawPt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        m_textBox.OnLButtonDown(hWnd, rawPt, scale, repaint);
        if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONDBLCLK:
    {
        float scale = GetWindowScale(hWnd);
        bool repaint = false;
        POINT rawPt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        m_textBox.OnLButtonDblClk(hWnd, rawPt, scale, repaint);
        if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONUP:
    {
        float scale = GetWindowScale(hWnd);
        bool repaint = false;
        POINT rawPt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        m_textBox.OnLButtonUp(hWnd, rawPt, scale, repaint);
        if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSEWHEEL:
    {
        short zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        float scale = GetWindowScale(hWnd);
        bool repaint = false;
        m_textBox.OnMouseWheel(hWnd, zDelta, pt, scale, repaint);
        if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }
    case WM_RBUTTONUP:
    {
        bool repaint = false;
        m_textBox.OnRButtonUp(hWnd, repaint);
        if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }
    case WM_DESTROY:
        CancelWorker();
        m_pendingOutput.clear();
        KillTimer(hWnd, COMMAND_PANEL_CARET_TIMER_ID);
        KillTimer(hWnd, COMMAND_PANEL_LOADING_TIMER_ID);
        KillTimer(hWnd, COMMAND_PANEL_APPEND_TIMER_ID);
        break;

    case WM_NCDESTROY:
        {
            std::lock_guard<std::mutex> lock(g_commandPanelRegistryMutex);
            auto it = g_commandPanelRegistry.find(hWnd);
            if (it != g_commandPanelRegistry.end() && it->second == m_instanceToken)
                g_commandPanelRegistry.erase(it);
        }
        g_cmdPanelInstance = nullptr;
        delete this;
        return 0;
    }
    return GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
}

void CommandPanelWindow::AppendOutput(const std::wstring& text)
{
    if (text.empty())
        return;

    m_outputText += text;
    m_textBox.SetText(m_outputText);
    if (GetHWND())
    {
        InvalidateRect(GetHWND(), nullptr, FALSE);
    }
}

void CommandPanelWindow::FlushPendingOutput()
{
    if (m_pendingOutput.empty()) return;
    std::wstring pending;
    pending.swap(m_pendingOutput);
    AppendOutput(pending);
}

void CommandPanelWindow::CancelWorker()
{
    m_workerTask.Cancel();
    m_workerTask = {};
    // Invalidate all queued append/completion messages from the old operation.
    m_workerGeneration = 0;
    m_refreshRunning = false;
    m_loadingStartedTick = 0;
}

void CommandPanelWindow::RunRefresh()
{
    HWND hwnd = GetHWND();
    if (!hwnd || !m_refreshWorker || m_refreshRunning)
        return;

    CancelWorker();
    m_pendingOutput.clear();
    m_refreshRunning = true;
    m_loadingFrame = 0;
    m_loadingStartedTick = GetTickCount64();
    m_workerGeneration = g_commandPanelGeneration.fetch_add(1);
    ClearOutput(L"");
    SetTimer(hwnd, COMMAND_PANEL_LOADING_TIMER_ID, COMMAND_PANEL_LOADING_FRAME_MS, nullptr);

    auto worker = m_refreshWorker;
    uint64_t generation = m_workerGeneration;
    uint64_t instanceToken = m_instanceToken;
    auto tasks = m_appCtx ? m_appCtx->backgroundTasks : nullptr;
    auto handle = tasks ? tasks->Submit(L"command.panel.refresh", BackgroundTaskService::Priority::High,
        [worker, hwnd, generation, instanceToken](const std::shared_ptr<BackgroundTaskService::CancellationToken>& cancellation) {
        t_commandPanelGeneration = generation;
        t_commandPanelInstanceToken = instanceToken;
        if (!cancellation->IsCancellationRequested()) worker(hwnd);
        if (!cancellation->IsCancellationRequested() && IsWindow(hwnd))
            PostMessageW(hwnd, WM_COMMAND_PANEL_REFRESH_DONE, 0, (LPARAM)generation);
        t_commandPanelGeneration = 0;
        t_commandPanelInstanceToken = 0;
    }) : BackgroundTaskService::TaskHandle{};
    if (!handle)
    {
        m_refreshRunning = false;
        KillTimer(hwnd, COMMAND_PANEL_LOADING_TIMER_ID);
        AppendOutput(L"\r\n后台任务繁忙，无法刷新。\r\n");
    }
    else
    {
        m_workerTask = std::move(handle);
    }
}

void CommandPanelWindow::UpdateChildLayout()
{
    RECT cr; GetClientRect(GetHWND(), &cr);
    float scale = GetWindowScale(GetHWND());
    float w = (float)cr.right / scale;
    float h = (float)cr.bottom / scale;

    m_textBox.SetBounds(D2D1::RectF(10.0f, 44.0f, w - 10.0f, h - 55.0f));
    m_textBox.UpdateLayout(scale);
}

void CommandPanelWindow::EnsureFonts()
{
    if (m_dw && !m_tfTitle)
    {
        UIStyle::Typography::CreateTextFormat(
            m_dw.Get(),
            &m_tfTitle,
            12.0f,
            DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_TEXT_ALIGNMENT_LEADING,
            DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    if (m_dw && !m_tfBtn)
    {
        UIStyle::Typography::CreateTextFormat(
            m_dw.Get(),
            &m_tfBtn,
            10.0f,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_TEXT_ALIGNMENT_CENTER,
            DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
}

void CommandPanelWindow::OnPaintContent(ID2D1HwndRenderTarget* rt)
{
    EnsureFonts();

    RECT cr; GetClientRect(GetHWND(), &cr);
    float scale = GetWindowScale(GetHWND());
    float w = (float)cr.right / scale;
    float h = (float)cr.bottom / scale;

    // 1. Draw Title
    if (m_tfTitle)
    {
        auto titleBrush = GetOrCreateBrush(UIStyle::ThemeColor::TextNormal().d2d);
        if (titleBrush)
        {
            rt->DrawTextW(m_title.c_str(), (UINT32)m_title.size(), m_tfTitle.Get(),
                D2D1::RectF(10, 8, w - 30, 28), titleBrush.Get());
        }
    }

    // 2. Custom Close Button "X"
    {
        D2D1_RECT_F closeRect = D2D1::RectF(w - 25, 8, w - 9, 24);
        D2D1_ROUNDED_RECT roundedClose = D2D1::RoundedRect(closeRect, 4.0f, 4.0f);
        if (m_hoveredClose)
        {
            D2D1_COLOR_F clr = UIStyle::ThemeColor::DangerRed().d2d;
            clr.a = 0.4f;
            auto closeBg = GetOrCreateBrush(clr);
            if (closeBg) rt->FillRoundedRectangle(roundedClose, closeBg.Get());
        }

        D2D1_COLOR_F xColor = UIStyle::ThemeColor::TextNormal().d2d;
        xColor.a = 0.8f;
        auto xBrush = GetOrCreateBrush(xColor);
        if (xBrush)
        {
            rt->DrawLine(D2D1::Point2F(w - 21, 12), D2D1::Point2F(w - 13, 20), xBrush.Get(), UIStyle::Metrics::IconStroke());
            rt->DrawLine(D2D1::Point2F(w - 13, 12), D2D1::Point2F(w - 21, 20), xBrush.Get(), UIStyle::Metrics::IconStroke());
        }
    }

    // 3. Paint Textbox
    m_textBox.Paint(rt, scale);

    if (m_refreshRunning && m_outputText.empty())
    {
        D2D1_RECT_F box = m_textBox.GetBounds();
        D2D1_POINT_2F center = D2D1::Point2F((box.left + box.right) * 0.5f, (box.top + box.bottom) * 0.5f);
        const int dotCount = 12;
        const float radius = 15.0f;
        const float minDotRadius = 1.55f;
        const float maxDotRadius = 2.75f;
        const float rotationMs = 760.0f;
        constexpr float pi = 3.14159265f;
        ULONGLONG startedTick = m_loadingStartedTick ? m_loadingStartedTick : GetTickCount64();
        float elapsed = (float)(GetTickCount64() - startedTick);
        float headAngle = std::fmod(elapsed / rotationMs, 1.0f) * 2.0f * pi - pi * 0.5f;
        for (int i = 0; i < dotCount; ++i)
        {
            float angle = ((float)i / (float)dotCount) * 2.0f * pi - pi * 0.5f;
            float trailDistance = headAngle - angle;
            while (trailDistance < 0.0f)
                trailDistance += 2.0f * pi;
            while (trailDistance >= 2.0f * pi)
                trailDistance -= 2.0f * pi;

            float intensity = 1.0f - std::min(trailDistance / (2.0f * pi * 0.70f), 1.0f);
            intensity = intensity * intensity;
            D2D1_COLOR_F dot = UIStyle::ThemeColor::Accent().d2d;
            dot.a = 0.16f + intensity * 0.72f;
            auto dotBrush = GetOrCreateBrush(dot);
            if (!dotBrush)
                continue;

            D2D1_POINT_2F p = D2D1::Point2F(
                center.x + std::cos(angle) * radius,
                center.y + std::sin(angle) * radius);
            float dotRadius = minDotRadius + intensity * (maxDotRadius - minDotRadius);
            rt->FillEllipse(D2D1::Ellipse(p, dotRadius, dotRadius), dotBrush.Get());
        }
    }

    // 4. Footer Buttons — 4 equal-width buttons filling the full width
    {
        const float barPad = 10.0f;
        const float barTop = h - 40.0f;
        const float barH = 24.0f;
        const float barW = w - barPad * 2;
        const float btnGap = 6.0f;
        const int numButtons = 4;
        const float btnW = (barW - btnGap * (numButtons - 1)) / (float)numButtons;
        const float btnRadius = 5.0f;

        struct BtnInfo {
            const wchar_t* label;
            bool* hovered;
            bool accent;
            bool disabled;
        };
        BtnInfo btns[] = {
            { L"更多",  &m_hoveredMore,   false, false },
            { nullptr,  &m_hoveredRefresh, false, !m_refreshWorker || m_refreshRunning },
            { L"复制",  &m_hoveredCopy,   false, false },
            { L"确定",  &m_hoveredOk,     true,  false },
        };

        for (int i = 0; i < numButtons; i++)
        {
            float bx = barPad + i * (btnW + btnGap);
            D2D1_RECT_F rect = D2D1::RectF(bx, barTop, bx + btnW, barTop + barH);
            D2D1_ROUNDED_RECT rounded = D2D1::RoundedRect(rect, btnRadius, btnRadius);

            bool isHovered = *btns[i].hovered && !btns[i].disabled;
            D2D1_COLOR_F bg, border, text;

            if (btns[i].disabled)
            {
                bg = UIStyle::ThemeColor::ButtonBgNormal().d2d;
                bg.a *= 0.55f;
                border = UIStyle::ThemeColor::ButtonBorderNormal().d2d;
                border.a *= 0.55f;
                text = UIStyle::ThemeColor::TextNormal().d2d;
                text.a *= 0.55f;
            }
            else if (isHovered)
            {
                bg = btns[i].accent ? UIStyle::ThemeColor::AccentHover().d2d : UIStyle::ThemeColor::ButtonBgHover().d2d;
                border = btns[i].accent ? UIStyle::ThemeColor::AccentHover().d2d : UIStyle::ThemeColor::ButtonBorderHover().d2d;
                text = UIStyle::ThemeColor::TextNormal().d2d;
            }
            else
            {
                bg = btns[i].accent ? UIStyle::ThemeColor::Accent().d2d : UIStyle::ThemeColor::ButtonBgNormal().d2d;
                if (btns[i].accent) bg.a = 0.64f;
                border = btns[i].accent ? UIStyle::ThemeColor::Accent().d2d : UIStyle::ThemeColor::ButtonBorderNormal().d2d;
                text = btns[i].accent ? UIStyle::ThemeColor::TextOnAccent().d2d : UIStyle::ThemeColor::TextNormal().d2d;
            }

            auto bgBrush = GetOrCreateBrush(bg);
            if (bgBrush) rt->FillRoundedRectangle(rounded, bgBrush.Get());
            auto borderBrush = GetOrCreateBrush(border);
            if (borderBrush) rt->DrawRoundedRectangle(rounded, borderBrush.Get(), UIStyle::Metrics::ControlStroke());

            if (m_tfBtn)
            {
                auto textBrush = GetOrCreateBrush(text);
                if (textBrush)
                {
                    const wchar_t* label = nullptr;
                    UINT32 labelLen = 0;
                    if (i == 1) // 刷新 (index 1) — dynamic label
                    {
                        label = m_refreshRunning ? L"执行中" : L"刷新";
                        labelLen = m_refreshRunning ? 3 : 2;
                    }
                    else
                    {
                        label = btns[i].label;
                        labelLen = (UINT32)wcslen(label);
                    }
                    if (label) rt->DrawTextW(label, labelLen, m_tfBtn.Get(), rect, textBrush.Get());
                }
            }
        }
    }
}

bool CommandPanelWindow::HitTestRect(POINT pt, const D2D1_RECT_F& rect)
{
    return (pt.x >= rect.left && pt.x <= rect.right && pt.y >= rect.top && pt.y <= rect.bottom);
}

bool CommandPanelWindow::HitTestCloseButton(POINT pt)
{
    RECT cr; GetClientRect(GetHWND(), &cr);
    float scale = GetWindowScale(GetHWND());
    float w = (float)cr.right / scale;
    return HitTestRect(pt, D2D1::RectF(w - 25, 8, w - 9, 24));
}

D2D1_RECT_F CommandPanelWindow::GetFooterButtonRect(int index) const
{
    RECT cr; GetClientRect(GetHWND(), &cr);
    float scale = GetWindowScale(GetHWND());
    float w = (float)cr.right / scale;
    float h = (float)cr.bottom / scale;
    const float barPad = 10.0f;
    const float barTop = h - 40.0f;
    const float barH = 24.0f;
    const float barW = w - barPad * 2;
    const float btnGap = 6.0f;
    const int numButtons = 4;
    const float btnW = (barW - btnGap * (numButtons - 1)) / (float)numButtons;
    float bx = barPad + index * (btnW + btnGap);
    return D2D1::RectF(bx, barTop, bx + btnW, barTop + barH);
}

bool CommandPanelWindow::HitTestRefreshButton(POINT pt)
{
    return HitTestRect(pt, GetFooterButtonRect(1));
}

bool CommandPanelWindow::HitTestCopyButton(POINT pt)
{
    return HitTestRect(pt, GetFooterButtonRect(2));
}

bool CommandPanelWindow::HitTestOkButton(POINT pt)
{
    return HitTestRect(pt, GetFooterButtonRect(3));
}

bool CommandPanelWindow::HitTestMoreButton(POINT pt)
{
    return HitTestRect(pt, GetFooterButtonRect(0));
}

void CommandPanelWindow::RegisterBuiltinPopupAction(const std::wstring& title, std::function<void()> callback)
{
    m_builtinActions.push_back({ title, std::move(callback) });
}

void CommandPanelWindow::RegisterBuiltinActions()
{
    // Reserved for future built-in actions in the "More" dropdown.
    // Example:
    //   RegisterBuiltinPopupAction(L"刷新图标", [this]() { ... });
}

void CommandPanelWindow::ShowMoreDropdown()
{
    HWND hwnd = GetHWND();
    if (!hwnd)
        return;

    RECT cr; GetClientRect(hwnd, &cr);
    float scale = GetWindowScale(hwnd);
    float w = (float)cr.right / scale;
    float h = (float)cr.bottom / scale;

    D2D1_RECT_F btnRect = GetFooterButtonRect(0); // 更多 is at index 0
    float dropWidth = btnRect.right - btnRect.left;

    int px = (int)(btnRect.left * scale);
    int py = (int)((btnRect.bottom + 2) * scale);
    POINT pt{ px, py };
    ClientToScreen(hwnd, &pt);

    // Collect built-in actions
    auto& builtinActions = m_builtinActions;

    // Collect plugin-registered actions
    std::vector<PopupActionInfo> pluginActions;
    if (m_appCtx && m_appCtx->pluginManager)
        pluginActions = m_appCtx->pluginManager->GetPopupActions();

    bool hasAny = !builtinActions.empty() || !pluginActions.empty();

    if (!hasAny)
    {
        std::vector<DropDownMenu::Item> items;
        items.push_back({ L"无", nullptr, true });
        DropDownMenu::Show(hwnd, pt, items, m_appCtx, dropWidth, true);
        return;
    }

    std::vector<DropDownMenu::Item> items;

    // Built-in actions come first
    for (const auto& action : builtinActions)
    {
        items.push_back({ action.title, action.callback, false });
    }

    // Visual separator
    if (!builtinActions.empty() && !pluginActions.empty())
    {
        items.push_back({ L"────────", nullptr, true });
    }

    // Plugin actions follow
    for (const auto& action : pluginActions)
    {
        std::wstring displayText = action.title;
        items.push_back({ displayText, [pluginId = action.pluginId, commandId = action.actionId, ctx = m_appCtx]() {
            std::wstring msg;
            if (ctx->pluginManager)
                ctx->pluginManager->ExecuteCommand(pluginId, commandId, L"", msg);
        }, false });
    }

    DropDownMenu::Show(hwnd, pt, items, m_appCtx, dropWidth, true);
}

void CommandPanelWindow::CopyOutputToClipboard()
{
    FlushPendingOutput();
    HWND hwnd = GetHWND();
    if (!hwnd || !OpenClipboard(hwnd))
        return;

    EmptyClipboard();
    size_t bytes = (m_outputText.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem)
    {
        void* dest = GlobalLock(hMem);
        if (dest)
        {
            memcpy(dest, m_outputText.c_str(), bytes);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
            hMem = nullptr;
        }
    }

    if (hMem)
    {
        GlobalFree(hMem);
    }
    CloseClipboard();
}
