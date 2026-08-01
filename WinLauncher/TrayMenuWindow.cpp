#define NOMINMAX
#include "TrayMenuWindow.h"
#include "UI/MouseCaptureController.h"
#include "App/AppMessages.h"
#include "DpiHelper.h"
#include <windowsx.h>
#include <shellapi.h>
#include "PopupWindow.h"
#include "Config/UIStyle.h"
#include "App/Logger.h"

TrayMenuWindow* TrayMenuWindow::s_instance    = nullptr;
HWND            TrayMenuWindow::s_hMainWnd    = nullptr;
AppContext*      TrayMenuWindow::s_ctx         = nullptr;
bool            TrayMenuWindow::s_popupPaused = false;

// ============================================================
// Menu layout constants
// ============================================================
static constexpr int   ITEM_COUNT  = 7;   // total menu items
static constexpr float ITEM_H      = 26.0f;
static constexpr float PAD         = 6.0f;
static constexpr int   MENU_W_LG  = 90;   // logical width
// logical height: top-pad + items * itemH + (items-1) * gap(2px) + bottom-pad
// = PAD + ITEM_COUNT * ITEM_H + PAD = 6 + 7*26 + 6 = 194
static constexpr int   MENU_H_LG  = (int)(PAD + ITEM_COUNT * ITEM_H + PAD); // ~194

// ============================================================
// Ctor / Dtor
// ============================================================
TrayMenuWindow::TrayMenuWindow(AppContext* ctx)
    : m_hovered(-1)
{
    m_appCtx = ctx;
}

TrayMenuWindow::~TrayMenuWindow()
{
}

// ============================================================
// Init / Show / Hide / Release
// ============================================================
void TrayMenuWindow::Init(HWND hMainWnd, AppContext* ctx)
{
    s_hMainWnd = hMainWnd;
    s_ctx      = ctx;
}

void TrayMenuWindow::Prewarm()
{
    if (s_instance || !s_ctx)
        return;

    HMONITOR hm = s_hMainWnd
        ? MonitorFromWindow(s_hMainWnd, MONITOR_DEFAULTTONEAREST)
        : MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
    const float scale = DpiHelper::GetDpiScaleForMonitor(hm);
    const int w_px = (int)(MENU_W_LG * scale);
    const int h_px = (int)(MENU_H_LG * scale);

    auto* instance = new TrayMenuWindow(s_ctx);
    instance->Create(L"", WS_POPUP, WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                     0, 0, w_px, h_px, s_hMainWnd);
    if (!instance->GetHWND())
    {
        delete instance;
        LOG_INFO(s_ctx->logger, L"TrayMenuWindow::Prewarm: unable to create hidden menu window");
        return;
    }

    s_instance = instance;
    SetWindowDisplayAffinitySafe(instance->GetHWND());
    instance->ApplySystemBackdrop();
    if (instance->EnsureD2D() && instance->m_rt)
    {
        instance->m_rt->SetDpi(scale * 96.0f, scale * 96.0f);
        UIStyle::Typography::ApplyRenderTargetTextDefaults(instance->m_rt.Get());
    }

    LOG_INFO(s_ctx->logger, L"TrayMenuWindow::Prewarm: hidden HWND render target prepared");
}

void TrayMenuWindow::Show(POINT pt)
{
    bool reopeningVisibleWindow = false;
    if (s_instance && (!s_instance->GetHWND() || !IsWindow(s_instance->GetHWND())))
    {
        delete s_instance;
        s_instance = nullptr;
    }

    if (s_instance && IsWindowVisible(s_instance->GetHWND()) &&
        s_instance->m_animState != AnimState::Closing)
    {
        SetForegroundWindow(s_instance->GetHWND());
        return;
    }

    if (s_instance && s_instance->m_animState == AnimState::Closing)
    {
        // A second tray click can arrive while the prior close animation is
        // still active.  Cancel its completion callback before reusing the
        // visible HWND, otherwise that stale callback hides the new menu.
        reopeningVisibleWindow = IsWindowVisible(s_instance->GetHWND());
        KillTimer(s_instance->GetHWND(), 0x889);
        s_instance->m_animState = AnimState::None;
        s_instance->m_animOnComplete = nullptr;
        s_instance->ApplyVisibilityFrame(1.0f, 1.0f);
    }

    RECT iconRect{};
    bool hasIconRect = false;
    if (s_hMainWnd)
    {
        NOTIFYICONIDENTIFIER nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = s_hMainWnd;
        nid.uID = 1;
        hasIconRect = SUCCEEDED(Shell_NotifyIconGetRect(&nid, &iconRect));
    }

    HMONITOR hm = hasIconRect ? MonitorFromRect(&iconRect, MONITOR_DEFAULTTONEAREST)
                              : MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(hm, &mi);
    RECT wa = mi.rcWork;
    RECT rcMonitor = mi.rcMonitor;

    float scale  = DpiHelper::GetDpiScaleForMonitor(hm);
    int   w_px   = (int)(MENU_W_LG * scale);
    int   h_px   = (int)(MENU_H_LG * scale);

    if (hasIconRect)
    {
        pt.x = iconRect.left;
        int iconCenterY = (iconRect.top + iconRect.bottom) / 2;
        int monitorCenterY = (rcMonitor.top + rcMonitor.bottom) / 2;
        int gap = (int)(2.0f * scale);
        if (iconCenterY < monitorCenterY)
        {
            // Taskbar is at the top
            pt.y = iconRect.bottom + gap;
        }
        else
        {
            // Taskbar is at the bottom
            pt.y = iconRect.top - h_px - gap;
        }
    }

    if (pt.x + w_px > wa.right)  pt.x = wa.right  - w_px;
    if (pt.y + h_px > wa.bottom) pt.y = wa.bottom  - h_px;
    if (pt.x < wa.left)          pt.x = wa.left;
    if (pt.y < wa.top)           pt.y = wa.top;

    bool created = false;
    if (!s_instance)
    {
        s_instance = new TrayMenuWindow(s_ctx);
        s_instance->Create(L"", WS_POPUP, WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                           pt.x, pt.y, w_px, h_px, s_hMainWnd);
        if (!s_instance->GetHWND())
        {
            delete s_instance;
            s_instance = nullptr;
            return;
        }
        created = true;
    }

    if (created)
    {
        SetWindowDisplayAffinitySafe(s_instance->GetHWND());
        s_instance->ApplySystemBackdrop();
    }

    SetWindowPos(s_instance->GetHWND(), HWND_TOPMOST, pt.x, pt.y, w_px, h_px, SWP_NOACTIVATE);

    if (s_instance->EnsureD2D() && s_instance->m_rt)
    {
        s_instance->m_rt->SetDpi(scale * 96.0f, scale * 96.0f);
        UIStyle::Typography::ApplyRenderTargetTextDefaults(s_instance->m_rt.Get());
    }

    s_instance->m_hovered = -1;
    if (reopeningVisibleWindow)
    {
        // The HWND is already visible, so ShowWindow will not emit
        // WM_SHOWWINDOW and cannot restart the normal open animation.  Keep
        // the restored opaque frame instead of resetting it to transparent.
        s_instance->EnsureShadowForCurrentBounds(1.0f);
        s_instance->ApplyVisibilityFrame(1.0f, 1.0f);
        s_instance->RefreshBackgroundCache();
        InvalidateRect(s_instance->GetHWND(), nullptr, FALSE);
    }

    if (!IsWindowVisible(s_instance->GetHWND()))
        s_instance->RevealAfterFirstPaint();
    SetForegroundWindow(s_instance->GetHWND());
    s_instance->CaptureMouse();
}

void TrayMenuWindow::Hide()
{
    if (!s_instance)
        return;

    TrayMenuWindow* inst = s_instance;
    HWND h = inst->GetHWND();
    inst->ReleaseMouseCapture();
    if (!h || !IsWindow(h) || !IsWindowVisible(h) || inst->m_animState == AnimState::Closing)
        return;

    auto hideWindow = [inst, h]() {
        if (s_instance == inst && h && IsWindow(h))
            ShowWindow(h, SW_HIDE);
    };

    if (h && IsWindowVisible(h) && UIStyle::Animation::IsEnabled())
        inst->StartCloseTransition(hideWindow);
    else
        hideWindow();
}

void TrayMenuWindow::Release()
{
    TrayMenuWindow* inst = s_instance;
    s_instance = nullptr;
    if (!inst)
        return;

    HWND h = inst->GetHWND();
    inst->ReleaseMouseCapture();
    if (h && IsWindow(h))
        DestroyWindow(h);
    delete inst;
}

// ============================================================
// Hit testing
// ============================================================
int TrayMenuWindow::HitTest(POINT pt)
{
    RECT cr;
    GetClientRect(GetHWND(), &cr);
    float scale = GetWindowScale(GetHWND());
    float w     = (float)cr.right / scale;

    for (int i = 0; i < ITEM_COUNT; i++)
    {
        RECT rc{
            (int)PAD,
            (int)(PAD + i * ITEM_H),
            (int)(w - PAD),
            (int)(PAD + (i + 1) * ITEM_H - 2.0f)
        };
        if (PtInRect(&rc, pt))
            return i;
    }
    return -1;
}

bool TrayMenuWindow::IsInsideClient(POINT pt) const
{
    HWND h = GetHWND();
    if (!h)
        return false;

    RECT cr{};
    GetClientRect(h, &cr);
    float scale = GetWindowScale(h);
    int right = (int)((float)cr.right / scale);
    int bottom = (int)((float)cr.bottom / scale);
    return pt.x >= 0 && pt.x < right && pt.y >= 0 && pt.y < bottom;
}

void TrayMenuWindow::CaptureMouse()
{
    HWND h = GetHWND();
    if (!h || !IsWindow(h))
        return;

    MouseCaptureController::CapturePersistent(h);
    m_mouseCaptured = (GetCapture() == h);
}

void TrayMenuWindow::ReleaseMouseCapture()
{
    HWND h = GetHWND();
    if (m_mouseCaptured && h && GetCapture() == h)
    {
        m_mouseCaptured = false;
        MouseCaptureController::Complete(h);
        return;
    }
    m_mouseCaptured = false;
}

// ============================================================
// Painting
// ============================================================
void TrayMenuWindow::OnPaintContent(ID2D1HwndRenderTarget* rt)
{
    RECT cr;
    GetClientRect(GetHWND(), &cr);
    float scale = GetWindowScale(GetHWND());
    float w     = (float)cr.right / scale;

    // Menu item labels (index must match the HandleMessage switch below)
    // 0: 显示弹窗
    // 1: 配置窗口
    // 2: 设置窗口
    // 3: 暂停/启用弹窗
    // 4: 重启钩子
    // 5: 重启应用
    // 6: 退出应用
    const wchar_t* pauseLabel = s_popupPaused ? L"启用弹窗" : L"暂停弹窗";
    const wchar_t* items[ITEM_COUNT] = {
        L"显示弹窗",
        L"配置窗口",
        L"设置窗口",
        pauseLabel,
        L"重启钩子",
        L"重启应用",
        L"退出应用",
    };

    for (int i = 0; i < ITEM_COUNT; i++)
    {
        // Separator between primary actions and status/system actions.
        if (i == 3)
        {
            float lineY = PAD + 3 * ITEM_H - 1.0f;
            D2D1_POINT_2F p1 = D2D1::Point2F(PAD + 4.0f, lineY);
            D2D1_POINT_2F p2 = D2D1::Point2F(w - PAD - 4.0f, lineY);
            auto lineBrush = GetOrCreateBrush(UIStyle::ThemeColor::ButtonBorderNormal().d2d);
            if (lineBrush) rt->DrawLine(p1, p2, lineBrush.Get(), 0.75f);
        }

        D2D1_RECT_F itemRect = D2D1::RectF(
            PAD,
            PAD + i * ITEM_H,
            w - PAD,
            PAD + (i + 1) * ITEM_H - 2.0f);
        D2D1_ROUNDED_RECT roundedItem = D2D1::RoundedRect(itemRect, 4.0f, 4.0f);

        bool isHovered = (i == m_hovered);

        // Highlight the pause item differently when paused
        D2D1_COLOR_F bgColor    = isHovered
            ? UIStyle::ThemeColor::ButtonBgHover().d2d
            : UIStyle::ThemeColor::ButtonBgNormal().d2d;
        D2D1_COLOR_F borderColor = isHovered
            ? UIStyle::ThemeColor::ButtonBorderHover().d2d
            : UIStyle::ThemeColor::ButtonBorderNormal().d2d;

        // Special highlight for pause item when active
        if (i == 3 && s_popupPaused && !isHovered)
        {
            bgColor.r = 0.8f; bgColor.g = 0.3f; bgColor.b = 0.1f; bgColor.a = 0.15f;
            borderColor.r = 0.8f; borderColor.g = 0.3f; borderColor.b = 0.1f; borderColor.a = 0.5f;
        }

        auto bgBrush = GetOrCreateBrush(bgColor);
        if (bgBrush) rt->FillRoundedRectangle(roundedItem, bgBrush.Get());

        auto borderBrush = GetOrCreateBrush(borderColor);
        if (borderBrush) rt->DrawRoundedRectangle(roundedItem, borderBrush.Get(), UIStyle::Metrics::ControlStroke());

        if (m_tf)
        {
            D2D1_COLOR_F textColor = UIStyle::ThemeColor::TextNormal().d2d;
            // Dim maintenance actions slightly.
            if (i == 4 || i == 5)
                textColor.a *= 0.85f;

            auto tb = GetOrCreateBrush(textColor);
            if (tb)
                rt->DrawTextW(items[i], (UINT32)wcslen(items[i]), m_tf.Get(), itemRect, tb.Get());
        }
    }
}

// ============================================================
// Message handling
// ============================================================
LRESULT TrayMenuWindow::HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_ACTIVATE:
        GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
        if (LOWORD(wParam) == WA_INACTIVE)
            Hide();
        return 0;

    case WM_CAPTURECHANGED:
        MouseCaptureController::OnCaptureChanged(hWnd, reinterpret_cast<HWND>(lParam));
        if (m_mouseCaptured && reinterpret_cast<HWND>(lParam) != hWnd)
        {
            m_mouseCaptured = false;
            Hide();
        }
        return 0;

    case WM_MOUSEMOVE:
    {
        float scale = GetWindowScale(hWnd);
        POINT pt{ (int)(GET_X_LPARAM(lParam) / scale), (int)(GET_Y_LPARAM(lParam) / scale) };
        if (!IsInsideClient(pt))
        {
            if (m_hovered != -1) { m_hovered = -1; InvalidateRect(hWnd, nullptr, FALSE); }
            return 0;
        }

        // The tray menu keeps mouse capture so an outside click can dismiss it.
        // After a notification-area right click, that capture path does not
        // reliably generate WM_SETCURSOR when the pointer re-enters this HWND;
        // explicitly restore the client cursor from the Shell's stale wait cursor.
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));

        int h = HitTest(pt);
        if (h != m_hovered) { m_hovered = h; InvalidateRect(hWnd, nullptr, FALSE); }
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        float scale = GetWindowScale(hWnd);
        POINT pt{ (int)(GET_X_LPARAM(lParam) / scale), (int)(GET_Y_LPARAM(lParam) / scale) };
        if (!IsInsideClient(pt))
        {
            Hide();
            return 0;
        }
        int hit = HitTest(pt);

        if (hit == 0)   // 显示弹窗
        {
            POINT cursorPt; GetCursorPos(&cursorPt);
            PopupWindow::Show(s_hMainWnd, cursorPt);
            Hide();
        }
        else if (hit == 1)  // 配置窗口
        {
            PostMessageW(s_hMainWnd, AppMessages::ShowConfigWindow, 0, 0);
            Hide();
        }
        else if (hit == 2)  // 设置窗口
        {
            PostMessageW(s_hMainWnd, AppMessages::ShowSettingsWindow, 0, 0);
            Hide();
        }
        else if (hit == 3)  // 暂停/启用弹窗
        {
            PostMessageW(s_hMainWnd, AppMessages::TogglePopupPause, 0, 0);
            Hide();
        }
        else if (hit == 4)  // 重启钩子
        {
            PostMessageW(s_hMainWnd, AppMessages::RestartHook, 0, 0);
            Hide();
        }
        else if (hit == 5)  // 重启应用
        {
            PostMessageW(s_hMainWnd, AppMessages::RestartApp, 0, 0);
            Hide();
        }
        else if (hit == 6)  // 退出应用
        {
            Hide();
            PostQuitMessage(0);
        }
        return 0;
    }

    case WM_RBUTTONDOWN:
    {
        float scale = GetWindowScale(hWnd);
        POINT pt{ (int)(GET_X_LPARAM(lParam) / scale), (int)(GET_Y_LPARAM(lParam) / scale) };
        if (!IsInsideClient(pt))
            Hide();
        return 0;
    }

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) Hide();
        return 0;

    case WM_DESTROY:
        ReleaseMouseCapture();
        GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
        // s_instance is managed by Hide()/Release()
        return 0;
    }
    return GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
}
