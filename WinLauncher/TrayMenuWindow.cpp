#define NOMINMAX
#include "TrayMenuWindow.h"
#include "App/AppMessages.h"
#include "DpiHelper.h"
#include <windowsx.h>
#include "PopupWindow.h"
#include "Config/UIStyle.h"

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
static constexpr int   MENU_W_LG  = 110;  // logical width
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

void TrayMenuWindow::Show(POINT pt)
{
    if (s_instance)
    {
        SetForegroundWindow(s_instance->GetHWND());
        return;
    }

    s_instance = new TrayMenuWindow(s_ctx);

    MONITORINFO mi{ sizeof(mi) };
    HMONITOR hm = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    GetMonitorInfoW(hm, &mi);
    RECT wa = mi.rcWork;

    float scale  = DpiHelper::GetDpiScaleForMonitor(hm);
    int   w_px   = (int)(MENU_W_LG * scale);
    int   h_px   = (int)(MENU_H_LG * scale);

    if (pt.x + w_px > wa.right)  pt.x = wa.right  - w_px;
    if (pt.y + h_px > wa.bottom) pt.y = wa.bottom  - h_px;
    if (pt.x < wa.left)          pt.x = wa.left;
    if (pt.y < wa.top)           pt.y = wa.top;

    s_instance->Create(L"", WS_POPUP, WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                       pt.x, pt.y, w_px, h_px, s_hMainWnd);
    if (!s_instance->GetHWND())
    {
        delete s_instance;
        s_instance = nullptr;
        return;
    }

    SetWindowDisplayAffinitySafe(s_instance->GetHWND());
    s_instance->ApplySystemBackdrop();
    s_instance->EnsureD2D();

    s_instance->m_hovered = -1;

    SetWindowPos(s_instance->GetHWND(), HWND_TOPMOST, pt.x, pt.y, w_px, h_px, SWP_NOACTIVATE);
    ShowWindow(s_instance->GetHWND(), SW_SHOW);
    SetForegroundWindow(s_instance->GetHWND());

    s_instance->CaptureBackground();
    s_instance->CompositeBackgroundToCache();
    InvalidateRect(s_instance->GetHWND(), nullptr, FALSE);
}

void TrayMenuWindow::Hide()
{
    if (s_instance)
    {
        TrayMenuWindow* inst = s_instance;
        s_instance = nullptr;
        HWND h = inst->GetHWND();
        if (h) DestroyWindow(h);
        delete inst;
    }
}

void TrayMenuWindow::Release()
{
    Hide();
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
    // 3: --- separator ---
    // 4: 暂停/启用弹窗
    // 5: 重启钩子
    // 6: 重启应用
    // (7 would be 退出, keep original last)
    const wchar_t* pauseLabel = s_popupPaused ? L"启用弹窗" : L"暂停弹窗";
    const wchar_t* items[ITEM_COUNT] = {
        L"显示弹窗",
        L"配置窗口",
        L"设置窗口",
        L"退出应用",
        pauseLabel,
        L"重启钩子",
        L"重启应用",
    };

    for (int i = 0; i < ITEM_COUNT; i++)
    {
        // Separator between item 3 and 4: draw a thin line instead of a button
        if (i == 4)
        {
            // Draw a divider line above item 4
            float lineY = PAD + 4 * ITEM_H - 1.0f;
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
        if (i == 4 && s_popupPaused && !isHovered)
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
            // Dim "重启应用"/"重启钩子" slightly
            if (i == 5 || i == 6)
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
        if (LOWORD(wParam) == WA_INACTIVE)
            Hide();
        return 0;

    case WM_MOUSEMOVE:
    {
        float scale = GetWindowScale(hWnd);
        POINT pt{ (int)(GET_X_LPARAM(lParam) / scale), (int)(GET_Y_LPARAM(lParam) / scale) };
        int h = HitTest(pt);
        if (h != m_hovered) { m_hovered = h; InvalidateRect(hWnd, nullptr, FALSE); }
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        float scale = GetWindowScale(hWnd);
        POINT pt{ (int)(GET_X_LPARAM(lParam) / scale), (int)(GET_Y_LPARAM(lParam) / scale) };
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
        else if (hit == 3)  // 退出应用
        {
            Hide();
            if (s_ctx) s_ctx->pluginHost->UnloadAll();
            PostQuitMessage(0);
        }
        else if (hit == 4)  // 暂停/启用弹窗
        {
            PostMessageW(s_hMainWnd, AppMessages::TogglePopupPause, 0, 0);
            Hide();
        }
        else if (hit == 5)  // 重启钩子
        {
            PostMessageW(s_hMainWnd, AppMessages::RestartHook, 0, 0);
            Hide();
        }
        else if (hit == 6)  // 重启应用
        {
            PostMessageW(s_hMainWnd, AppMessages::RestartApp, 0, 0);
            Hide();
        }
        return 0;
    }

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) Hide();
        return 0;

    case WM_DESTROY:
        GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
        // s_instance is managed by Hide()/Release()
        return 0;
    }
    return GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
}
