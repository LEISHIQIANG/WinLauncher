#define NOMINMAX
#include "TrayMenuWindow.h"
#include "App/AppMessages.h"
#include "DpiHelper.h"
#include <windowsx.h>
#include "PopupWindow.h"
#include "Config/UIStyle.h"

TrayMenuWindow* TrayMenuWindow::s_instance = nullptr;
HWND TrayMenuWindow::s_hMainWnd = nullptr;
AppContext* TrayMenuWindow::s_ctx = nullptr;

TrayMenuWindow::TrayMenuWindow(AppContext* ctx)
    : m_hovered(-1)
{
    m_appCtx = ctx;
}

TrayMenuWindow::~TrayMenuWindow()
{
}

void TrayMenuWindow::Init(HWND hMainWnd, AppContext* ctx)
{
    s_hMainWnd = hMainWnd;
    s_ctx = ctx;
}

void TrayMenuWindow::Show(POINT pt)
{
    if (s_instance)
    {
        SetForegroundWindow(s_instance->GetHWND());
        return;
    }

    s_instance = new TrayMenuWindow(s_ctx);

    int w = 110;
    int h = 110;

    MONITORINFO mi{ sizeof(mi) };
    HMONITOR hm = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    GetMonitorInfoW(hm, &mi);
    RECT wa = mi.rcWork;

    float scale = DpiHelper::GetDpiScaleForMonitor(hm);
    int w_px = (int)(w * scale);
    int h_px = (int)(h * scale);

    if (pt.x + w_px > wa.right) pt.x = wa.right - w_px;
    if (pt.y + h_px > wa.bottom) pt.y = wa.bottom - h_px;
    if (pt.x < wa.left) pt.x = wa.left;
    if (pt.y < wa.top) pt.y = wa.top;

    s_instance->Create(L"", WS_POPUP, WS_EX_TOOLWINDOW | WS_EX_TOPMOST, pt.x, pt.y, w_px, h_px, s_hMainWnd);
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

int TrayMenuWindow::HitTest(POINT pt)
{
    const float itemH = 26.0f;
    const float pad = 6.0f;
    RECT cr;
    GetClientRect(GetHWND(), &cr);
    float scale = GetWindowScale(GetHWND());
    float w = (float)cr.right / scale;

    for (int i = 0; i < 4; i++)
    {
        RECT rc{
            (int)pad, (int)(pad + i * itemH),
            (int)(w - pad), (int)(pad + (i + 1) * itemH - 2.0f)
        };
        if (PtInRect(&rc, pt))
            return i;
    }
    return -1;
}

void TrayMenuWindow::OnPaintContent(ID2D1HwndRenderTarget* rt)
{
    RECT cr;
    GetClientRect(GetHWND(), &cr);
    float scale = GetWindowScale(GetHWND());
    float w = (float)cr.right / scale;

    const float itemH = 26.0f;
    const float pad = 6.0f;
    const wchar_t* items[] = { L"显示弹窗", L"配置窗口", L"设置窗口", L"退出应用" };

    for (int i = 0; i < 4; i++)
    {
        D2D1_RECT_F itemRect = D2D1::RectF(pad, pad + i * itemH, w - pad, pad + (i + 1) * itemH - 2.0f);
        D2D1_ROUNDED_RECT roundedItem = D2D1::RoundedRect(itemRect, 4.0f, 4.0f);

        bool isHovered = (i == m_hovered);

        auto bgBrush = GetOrCreateBrush(isHovered ? UIStyle::ThemeColor::ButtonBgHover().d2d : UIStyle::ThemeColor::ButtonBgNormal().d2d);
        if (bgBrush) rt->FillRoundedRectangle(roundedItem, bgBrush.Get());

        auto borderBrush = GetOrCreateBrush(isHovered ? UIStyle::ThemeColor::ButtonBorderHover().d2d : UIStyle::ThemeColor::ButtonBorderNormal().d2d);
        if (borderBrush) rt->DrawRoundedRectangle(roundedItem, borderBrush.Get(), UIStyle::Metrics::ControlStroke());

        if (m_tf)
        {
            auto tb = GetOrCreateBrush(UIStyle::ThemeColor::TextNormal().d2d);
            if (tb)
            {
                rt->DrawTextW(items[i], (UINT32)wcslen(items[i]), m_tf.Get(), itemRect, tb.Get());
            }
        }
    }
}

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
        if (hit == 0)
        {
            POINT cursorPt; GetCursorPos(&cursorPt);
            PopupWindow::Show(s_hMainWnd, cursorPt);
            Hide();
        }
        else if (hit == 1)
        {
            PostMessageW(s_hMainWnd, AppMessages::ShowConfigWindow, 0, 0);
            Hide();
        }
        else if (hit == 2)
        {
            PostMessageW(s_hMainWnd, AppMessages::ShowSettingsWindow, 0, 0);
            Hide();
        }
        else if (hit == 3)
        {
            Hide();
            if (s_ctx) s_ctx->pluginHost->UnloadAll();
            // Post WM_QUIT — main.cpp's cleanup sequence handles window destruction
            PostQuitMessage(0);
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
