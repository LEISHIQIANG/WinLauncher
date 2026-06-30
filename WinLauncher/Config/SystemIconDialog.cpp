#define NOMINMAX
#include "SystemIconDialog.h"
#include "UIStyle.h"
#include "../DpiHelper.h"
#include <windowsx.h>
#include <commctrl.h>
#include <algorithm>
#include <vector>

#pragma comment(lib, "comctl32.lib")

static const int  DLG_W  = 360;
static const float Y_TITLE      = 10.0f;
static const float Y_FORM_TOP   = 36.0f;
static const float Y_BUTTONS    = Y_FORM_TOP + SystemIconEditForm::PreferredContentHeight() + 20.0f;
static const int  DLG_H         = (int)(Y_BUTTONS + 45.0f);

static SystemIconDialog* g_sidInstance = nullptr;

struct SystemDialogBrushCacheEntry
{
    D2D1_COLOR_F color;
    ComPtr<ID2D1SolidColorBrush> brush;
};
static std::vector<SystemDialogBrushCacheEntry> g_systemDialogBrushCache;

static ComPtr<ID2D1SolidColorBrush> GetOrCreateDialogBrush(ID2D1HwndRenderTarget* rt, const D2D1_COLOR_F& color)
{
    for (auto& entry : g_systemDialogBrushCache)
    {
        if (entry.color.r == color.r && entry.color.g == color.g &&
            entry.color.b == color.b && entry.color.a == color.a)
        {
            return entry.brush;
        }
    }

    ComPtr<ID2D1SolidColorBrush> brush;
    if (rt)
    {
        rt->CreateSolidColorBrush(color, &brush);
        if (brush)
        {
            g_systemDialogBrushCache.push_back({ color, brush });
        }
    }
    return brush;
}

SystemIconDialog::SystemIconDialog(const wchar_t* title, const InitParams& init, AppContext* ctx)
    : m_title(title)
    , m_init(init)
    , m_okPressed(false)
{
    m_appCtx = ctx;
}

SystemIconDialog::~SystemIconDialog()
{
    m_form.Destroy();
}

bool SystemIconDialog::Show(HWND parent, const wchar_t* title,
                           SystemIconDialogResult& result,
                           const InitParams* init, AppContext* ctx)
{
    if (g_sidInstance) return false;

    InitParams params{};
    if (init) params = *init;

    SystemIconDialog* win = new SystemIconDialog(title, params, ctx);

    HMONITOR hm = parent
        ? MonitorFromWindow(parent, MONITOR_DEFAULTTONEAREST)
        : ([]{
              POINT pt; GetCursorPos(&pt);
              return MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
           }());

    float scale = DpiHelper::GetDpiScaleForMonitor(hm);
    int w_px = (int)(DLG_W * scale);
    int h_px = (int)(DLG_H * scale);

    int x = 0, y = 0;
    if (parent && IsWindowVisible(parent))
    {
        RECT pr; GetWindowRect(parent, &pr);
        x = pr.left + (pr.right - pr.left - w_px) / 2;
        y = pr.top  + (pr.bottom - pr.top  - h_px) / 2;
    }
    else
    {
        MONITORINFO mi{ sizeof(mi) };
        GetMonitorInfoW(hm, &mi);
        RECT wa = mi.rcWork;
        x = wa.left + (wa.right - wa.left - w_px) / 2;
        y = wa.top  + (wa.bottom - wa.top  - h_px) / 2;
    }

    if (parent) EnableWindow(parent, FALSE);

    win->Create(L"", WS_POPUP, WS_EX_TOOLWINDOW | WS_EX_TOPMOST, x, y, w_px, h_px, parent);
    if (!win->GetHWND())
    {
        if (parent) EnableWindow(parent, TRUE);
        delete win;
        return false;
    }

    SetWindowDisplayAffinitySafe(win->GetHWND());
    win->ApplySystemBackdrop();
    win->EnsureD2D();

    ShowWindow(win->GetHWND(), SW_SHOW);
    UpdateWindow(win->GetHWND());
    SetForegroundWindow(win->GetHWND());
    SetFocus(win->GetHWND());

    g_sidInstance = win;

    MSG msg;
    HWND hWnd = win->GetHWND();
    while (IsWindow(hWnd) && GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (IsWindow(hWnd))
    {
        DestroyWindow(hWnd);
    }

    if (msg.message == WM_QUIT)
    {
        PostQuitMessage((int)msg.wParam);
    }

    bool ok = win->m_okPressed;
    if (ok)
    {
        SystemIconEditFormResult formRes = win->m_form.GetResult();
        result.name = formRes.name;
        result.iconPath = formRes.iconPath;
        result.iconInvertLight = formRes.iconInvertLight;
        result.iconInvertDark = formRes.iconInvertDark;
    }

    g_sidInstance = nullptr;
    delete win;

    if (parent)
    {
        EnableWindow(parent, TRUE);
        SetForegroundWindow(parent);
    }

    g_systemDialogBrushCache.clear();
    return ok;
}

LRESULT SystemIconDialog::HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CREATE:
    {
        EnsureD2D();
        m_form.Create(hWnd, m_dw.Get(), D2D1::RectF(0, Y_FORM_TOP, DLG_W, DLG_H), m_init);
        SetTimer(hWnd, 0x999, GetCaretBlinkTime(), nullptr);
        return GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
    }

    case WM_TIMER:
    {
        if (wParam == 0x999)
        {
            m_form.BlinkCaret();
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        }
        break;
    }

    case WM_DESTROY:
    {
        KillTimer(hWnd, 0x999);
        m_form.Destroy();
        g_systemDialogBrushCache.clear();
        PostThreadMessageW(GetCurrentThreadId(), WM_NULL, 0, 0);
        GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        float scale = DpiHelper::GetWindowScale(hWnd);
        pt.x = (int)(pt.x / scale);
        pt.y = (int)(pt.y / scale);

        if (HitTestCloseButton(pt))
        {
            m_form.ResetFocus();
            PostMessageW(hWnd, WM_CLOSE, 0, 0);
            return 0;
        }

        if (HitTestOkButton(pt))
        {
            if (m_form.Validate(hWnd))
            {
                m_okPressed = true;
                m_form.ResetFocus();
                PostMessageW(hWnd, WM_CLOSE, 0, 0);
            }
            return 0;
        }

        if (HitTestCancelButton(pt))
        {
            m_form.ResetFocus();
            PostMessageW(hWnd, WM_CLOSE, 0, 0);
            return 0;
        }

        bool repaint = false;
        m_form.OnLButtonDown(hWnd, pt, scale, repaint);

        if (!repaint && m_form.IsInputFocused())
        {
            m_form.ResetFocus();
            repaint = true;
        }

        if (repaint)
        {
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        }

        if (pt.y < Y_FORM_TOP)
        {
            SetFocus(hWnd);
            ReleaseCapture();
            SendMessageW(hWnd, WM_SYSCOMMAND, SC_MOVE | HTCAPTION, 0);
        }
        return 0;
    }

    case WM_LBUTTONDBLCLK:
    {
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        float scale = DpiHelper::GetWindowScale(hWnd);
        pt.x = (int)(pt.x / scale);
        pt.y = (int)(pt.y / scale);

        bool repaint = false;
        m_form.OnLButtonDblClk(hWnd, pt, scale, repaint);
        if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }

    case WM_LBUTTONUP:
    {
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        float scale = DpiHelper::GetWindowScale(hWnd);
        pt.x = (int)(pt.x / scale);
        pt.y = (int)(pt.y / scale);

        bool repaint = false;
        m_form.OnLButtonUp(hWnd, pt, scale, repaint);
        if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        float scale = DpiHelper::GetWindowScale(hWnd);
        pt.x = (int)(pt.x / scale);
        pt.y = (int)(pt.y / scale);

        if (!m_trackMouse)
        {
            TRACKMOUSEEVENT tme{ sizeof(tme) };
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hWnd;
            TrackMouseEvent(&tme);
            m_trackMouse = true;
        }

        bool repaint = false;
        m_form.OnMouseMove(hWnd, pt, scale, repaint);

        auto update = [&](bool& state, bool newVal) {
            if (state != newVal) { state = newVal; repaint = true; }
        };

        update(m_hoveredClose,  HitTestCloseButton(pt));
        update(m_hoveredOk,     HitTestOkButton(pt));
        update(m_hoveredCancel, HitTestCancelButton(pt));

        if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }

    case WM_MOUSELEAVE:
    {
        m_trackMouse = false;
        m_hoveredClose = false;
        m_hoveredOk = false;
        m_hoveredCancel = false;
        bool repaint = false;
        m_form.OnMouseMove(hWnd, POINT{ -999, -999 }, DpiHelper::GetWindowScale(hWnd), repaint);
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }

    case WM_CHAR:
    {
        bool repaint = false;
        m_form.OnChar(hWnd, wParam, repaint);
        if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }

    case WM_KEYDOWN:
    {
        bool repaint = false;
        m_form.OnKeyDown(hWnd, wParam, lParam, repaint);
        if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }

    default:
        // Handle IME messaging
        if (m_form.IsInputFocused())
        {
            bool repaint = false;
            if (m_form.HandleImeMessage(hWnd, uMsg, wParam, lParam, repaint))
            {
                if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
                return 0;
            }
        }
        break;
    }

    return GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
}

void SystemIconDialog::OnPaintContent(ID2D1HwndRenderTarget* rt)
{
    float scale = DpiHelper::GetWindowScale(GetHWND());

    // 1. Draw Title Text
    EnsureFonts();
    if (m_tfTitle)
    {
        auto tb = GetOrCreateDialogBrush(rt, UIStyle::ThemeColor::TextNormal().d2d);
        if (tb)
        {
            rt->DrawTextW(m_title.c_str(), (UINT32)m_title.size(), m_tfTitle.Get(),
                          D2D1::RectF(20.0f, Y_TITLE, 200.0f, Y_TITLE + 24.0f), tb.Get());
        }
    }

    // 2. Draw Close Button
    {
        float cx = DLG_W - 25.0f;
        float cy = Y_TITLE + 10.0f;
        D2D1_COLOR_F clr = m_hoveredClose ? UIStyle::ThemeColor::DangerRed().d2d : UIStyle::ThemeColor::TextMuted().d2d;
        auto clrBrush = GetOrCreateDialogBrush(rt, clr);
        if (clrBrush)
        {
            rt->DrawLine(D2D1::Point2F(cx - 5, cy - 5), D2D1::Point2F(cx + 5, cy + 5), clrBrush.Get(), 1.5f);
            rt->DrawLine(D2D1::Point2F(cx + 5, cy - 5), D2D1::Point2F(cx - 5, cy + 5), clrBrush.Get(), 1.5f);
        }
    }

    // 3. Draw separating line
    {
        D2D1_COLOR_F sepClr = UIStyle::ThemeColor::TextNormal().d2d; sepClr.a = 0.10f;
        auto sepBrush = GetOrCreateDialogBrush(rt, sepClr);
        if (sepBrush)
        {
            rt->DrawLine(D2D1::Point2F(20.0f, Y_FORM_TOP - 4.0f), D2D1::Point2F(DLG_W - 20.0f, Y_FORM_TOP - 4.0f), sepBrush.Get(), UIStyle::Metrics::ControlStroke());
            rt->DrawLine(D2D1::Point2F(20.0f, Y_BUTTONS - 4.0f), D2D1::Point2F(DLG_W - 20.0f, Y_BUTTONS - 4.0f), sepBrush.Get(), UIStyle::Metrics::ControlStroke());
        }
    }

    // 4. Draw Form content
    m_form.Paint(rt, scale);

    // 5. Draw Buttons
    DrawButton(rt, L"确定", D2D1::RectF(DLG_W - 176.0f, Y_BUTTONS + 6.0f, DLG_W - 96.0f, Y_BUTTONS + 30.0f), m_hoveredOk, true);
    DrawButton(rt, L"取消", D2D1::RectF(DLG_W - 88.0f, Y_BUTTONS + 6.0f, DLG_W - 20.0f, Y_BUTTONS + 30.0f), m_hoveredCancel, false);
}

void SystemIconDialog::EnsureFonts()
{
    if (m_tfTitle) return;
    UIStyle::Typography::CreateTextFormat(GetDWFactory(), &m_tfTitle, 13.0f, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    UIStyle::Typography::CreateTextFormat(GetDWFactory(), &m_tfBtn,   10.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

bool SystemIconDialog::HitTestRect(POINT pt, const D2D1_RECT_F& rect)
{
    return (pt.x >= rect.left && pt.x <= rect.right && pt.y >= rect.top && pt.y <= rect.bottom);
}

bool SystemIconDialog::HitTestCloseButton(POINT pt)
{
    return (pt.x >= DLG_W - 35 && pt.x <= DLG_W - 15 && pt.y >= Y_TITLE && pt.y <= Y_TITLE + 20);
}

bool SystemIconDialog::HitTestOkButton(POINT pt)
{
    return HitTestRect(pt, D2D1::RectF(DLG_W - 176.0f, Y_BUTTONS + 6.0f, DLG_W - 96.0f, Y_BUTTONS + 30.0f));
}

bool SystemIconDialog::HitTestCancelButton(POINT pt)
{
    return HitTestRect(pt, D2D1::RectF(DLG_W - 88.0f, Y_BUTTONS + 6.0f, DLG_W - 20.0f, Y_BUTTONS + 30.0f));
}

void SystemIconDialog::DrawButton(ID2D1HwndRenderTarget* rt, const wchar_t* text, const D2D1_RECT_F& rect, bool hovered, bool accent)
{
    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(rect, 4.0f, 4.0f);
    bool isLight = (UIStyle::GetThemeMode() == UIStyle::ThemeMode::Light);
    D2D1_COLOR_F base = isLight ? D2D1::ColorF(0.f, 0.f, 0.f) : D2D1::ColorF(1.f, 1.f, 1.f);

    if (accent)
    {
        D2D1_COLOR_F bg = UIStyle::ThemeColor::Accent().d2d;
        bg.a = hovered ? 0.85f : 0.70f;
        auto bgBrush = GetOrCreateDialogBrush(rt, bg);
        if (bgBrush) rt->FillRoundedRectangle(rr, bgBrush.Get());

        if (m_tfBtn)
        {
            auto txtBrush = GetOrCreateDialogBrush(rt, UIStyle::ThemeColor::TextOnAccent().d2d);
            if (txtBrush) rt->DrawTextW(text, (UINT32)wcslen(text), m_tfBtn.Get(), rect, txtBrush.Get());
        }
    }
    else
    {
        float bgA = hovered ? 0.08f : 0.03f;
        float borderA = hovered ? 0.15f : 0.08f;

        auto bgBrush = GetOrCreateDialogBrush(rt, D2D1::ColorF(base.r, base.g, base.b, bgA));
        if (bgBrush) rt->FillRoundedRectangle(rr, bgBrush.Get());

        auto borderBrush = GetOrCreateDialogBrush(rt, D2D1::ColorF(base.r, base.g, base.b, borderA));
        if (borderBrush) rt->DrawRoundedRectangle(rr, borderBrush.Get(), UIStyle::Metrics::ControlStroke());

        if (m_tfBtn)
        {
            auto txtBrush = GetOrCreateDialogBrush(rt, UIStyle::ThemeColor::TextNormal().d2d);
            if (txtBrush) rt->DrawTextW(text, (UINT32)wcslen(text), m_tfBtn.Get(), rect, txtBrush.Get());
        }
    }
}
