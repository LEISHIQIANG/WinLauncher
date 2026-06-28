#define NOMINMAX
#include "HotkeyDialog.h"
#include "UIStyle.h"
#include "../DpiHelper.h"
#include <windowsx.h>
#include <commctrl.h>
#include <algorithm>
#include <cstring>
#include <vector>

#pragma comment(lib, "comctl32.lib")

static const int  DLG_W  = 360;
static const float Y_TITLE      = 10.0f;
static const float Y_FORM_TOP   = 36.0f;
static const float Y_BUTTONS    = Y_FORM_TOP + HotkeyEditForm::PreferredContentHeight() + 20.0f;
static const int  DLG_H         = (int)(Y_BUTTONS + 45.0f);

static HotkeyDialog* g_hdInstance = nullptr;

struct HotkeyDialogBrushCacheEntry
{
    D2D1_COLOR_F color;
    ComPtr<ID2D1SolidColorBrush> brush;
};
static std::vector<HotkeyDialogBrushCacheEntry> g_hotkeyDialogBrushCache;

static ComPtr<ID2D1SolidColorBrush> GetOrCreateDialogBrush(ID2D1HwndRenderTarget* rt, const D2D1_COLOR_F& color)
{
    for (auto& entry : g_hotkeyDialogBrushCache)
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
            g_hotkeyDialogBrushCache.push_back({ color, brush });
        }
    }
    return brush;
}

HotkeyDialog::HotkeyDialog(const wchar_t* title, const InitParams& init, AppContext* ctx)
    : m_title(title)
    , m_init(init)
    , m_okPressed(false)
{
    m_appCtx = ctx;
}

HotkeyDialog::~HotkeyDialog()
{
    m_form.Destroy();
}

bool HotkeyDialog::Show(HWND parent, const wchar_t* title,
                        HotkeyDialogResult& result,
                        const InitParams* init, AppContext* ctx)
{
    if (g_hdInstance) return false;

    InitParams params{};
    if (init) params = *init;

    HotkeyDialog* win = new HotkeyDialog(title, params, ctx);

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

    g_hdInstance = win;

    MSG msg;
    HWND hWnd = win->GetHWND();
    while (IsWindow(hWnd) && GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    bool ok = win->m_okPressed;
    if (ok)
    {
        HotkeyEditFormResult formRes = win->m_form.GetResult();
        result.name = formRes.name;
        result.hotkey = formRes.hotkey;
        result.iconPath = formRes.iconPath;
        result.afterClose = formRes.afterClose;
        result.iconInvertLight = formRes.iconInvertLight;
        result.iconInvertDark = formRes.iconInvertDark;
    }

    g_hdInstance = nullptr;
    delete win;

    if (parent)
    {
        EnableWindow(parent, TRUE);
        SetForegroundWindow(parent);
    }

    g_hotkeyDialogBrushCache.clear();
    return ok;
}

LRESULT HotkeyDialog::HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CREATE:
    {
        EnsureD2D();
        m_form.Create(hWnd, m_dw.Get(), D2D1::RectF(0, Y_FORM_TOP, DLG_W, DLG_H), m_init);
        SetTimer(hWnd, 0x999, GetCaretBlinkTime(), nullptr);
        return 0;
    }

    case WM_TIMER:
    {
        if (wParam == 0x999)
        {
            m_form.BlinkCaret();
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_DESTROY:
    {
        KillTimer(hWnd, 0x999);
        // Make sure recording stops if the dialog is force-closed
        if (KeyboardHook::IsRecording()) KeyboardHook::StopRecording();
        m_form.Destroy();
        g_hotkeyDialogBrushCache.clear();
        // Wake the local modal loop without sending WM_QUIT to the whole app
        PostThreadMessageW(GetCurrentThreadId(), WM_NULL, 0, 0);
        return 0;
    }

    case KeyboardHookMsg::KeyCaptured:
    case KeyboardHookMsg::ChordComplete:
    {
        bool repaint = false;
        m_form.HandleHookMessage(hWnd, uMsg, wParam, lParam, repaint);
        if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
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
        if (!m_trackMouse)
        {
            TRACKMOUSEEVENT tme{ sizeof(tme) };
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hWnd;
            TrackMouseEvent(&tme);
            m_trackMouse = true;
        }

        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        float scale = DpiHelper::GetWindowScale(hWnd);
        pt.x = (int)(pt.x / scale);
        pt.y = (int)(pt.y / scale);

        bool repaint = false;
        m_form.OnMouseMove(hWnd, pt, scale, repaint);

        auto update = [&](bool& flag, bool newVal){ if (flag != newVal){ flag = newVal; repaint = true; } };
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
    case WM_SYSKEYDOWN:
    {
        if (wParam == VK_ESCAPE)
        {
            m_form.ResetFocus();
            PostMessageW(hWnd, WM_CLOSE, 0, 0);
            return 0;
        }
        if (wParam == VK_RETURN && !m_form.IsInputFocused())
        {
            if (m_form.Validate(hWnd))
            {
                m_okPressed = true;
                m_form.ResetFocus();
                PostMessageW(hWnd, WM_CLOSE, 0, 0);
            }
            return 0;
        }

        bool repaint = false;
        m_form.OnKeyDown(hWnd, wParam, lParam, repaint);
        if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }

    case WM_CLOSE:
    {
        DestroyWindow(hWnd);
        return 0;
    }

    case WM_WINDOWPOSCHANGED:
    {
        LRESULT res = GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
        UpdateChildLayout();
        return res;
    }

    case WM_DPICHANGED:
    {
        LRESULT res = GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
        UpdateChildLayout();
        return res;
    }
    }
    return GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
}

void HotkeyDialog::OnPaintContent(ID2D1HwndRenderTarget* rt)
{
    EnsureFonts();

    D2D1_SIZE_F sz = rt->GetSize();
    float scale = DpiHelper::GetWindowScale(GetHWND());
    float W = sz.width / scale;
    float H = sz.height / scale;

    // Title
    if (m_tfTitle)
    {
        auto titleBrush = GetOrCreateDialogBrush(rt, UIStyle::ThemeColor::TextNormal().d2d);
        if (titleBrush) rt->DrawTextW(m_title.c_str(), (UINT32)m_title.size(), m_tfTitle.Get(), D2D1::RectF(15, Y_TITLE, W - 40, Y_TITLE + 22), titleBrush.Get());
    }

    // Close button
    {
        D2D1_RECT_F closeRect = D2D1::RectF(W - 25, 8, W - 9, 24);
        D2D1_ROUNDED_RECT rrClose = D2D1::RoundedRect(closeRect, 4.0f, 4.0f);
        if (m_hoveredClose)
        {
            D2D1_COLOR_F clr = UIStyle::ThemeColor::DangerRed().d2d; clr.a = 0.4f;
            auto cb = GetOrCreateDialogBrush(rt, clr);
            if (cb) rt->FillRoundedRectangle(rrClose, cb.Get());
        }
        D2D1_COLOR_F xc = UIStyle::ThemeColor::TextNormal().d2d; xc.a = 0.8f;
        auto xb = GetOrCreateDialogBrush(rt, xc);
        if (xb)
        {
            rt->DrawLine(D2D1::Point2F(W - 21, 12), D2D1::Point2F(W - 13, 20), xb.Get(), UIStyle::Metrics::IconStroke());
            rt->DrawLine(D2D1::Point2F(W - 13, 12), D2D1::Point2F(W - 21, 20), xb.Get(), UIStyle::Metrics::IconStroke());
        }
    }

    // Form content
    m_form.Paint(rt, scale);

    {
        D2D1_COLOR_F sepClr = UIStyle::ThemeColor::TextNormal().d2d; sepClr.a = 0.10f;
        auto sb = GetOrCreateDialogBrush(rt, sepClr);
        if (sb) rt->DrawLine(D2D1::Point2F(10, Y_BUTTONS - 8), D2D1::Point2F(W - 10, Y_BUTTONS - 8), sb.Get(), 0.5f);
    }

    // Bottom Action Buttons
    DrawButton(rt, L"确定", D2D1::RectF(W - 175, Y_BUTTONS, W - 95, Y_BUTTONS + 26), m_hoveredOk, true);
    DrawButton(rt, L"取消", D2D1::RectF(W - 90, Y_BUTTONS, W - 20, Y_BUTTONS + 26), m_hoveredCancel, false);
}

void HotkeyDialog::EnsureFonts()
{
    if (m_tfTitle) return;
    UIStyle::Typography::CreateTextFormat(
        m_dw.Get(),
        &m_tfTitle,
        12.0f,
        DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_TEXT_ALIGNMENT_LEADING,
        DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    UIStyle::Typography::CreateTextFormat(
        m_dw.Get(),
        &m_tfBtn,
        10.0f,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_TEXT_ALIGNMENT_CENTER,
        DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

void HotkeyDialog::UpdateChildLayout()
{
    float scale = GetWindowScale(GetHWND());
    m_form.UpdateLayout(D2D1::RectF(0, Y_FORM_TOP, DLG_W, DLG_H), scale);
}

bool HotkeyDialog::HitTestRect(POINT pt, const D2D1_RECT_F& rect)
{
    return (pt.x >= rect.left && pt.x <= rect.right && pt.y >= rect.top && pt.y <= rect.bottom);
}

bool HotkeyDialog::HitTestCloseButton(POINT pt)
{
    RECT cr; GetClientRect(GetHWND(), &cr);
    float scale = DpiHelper::GetWindowScale(GetHWND());
    float w = (float)cr.right / scale;
    return HitTestRect(pt, D2D1::RectF(w - 25, 8, w - 9, 24));
}

bool HotkeyDialog::HitTestOkButton(POINT pt)
{
    return HitTestRect(pt, D2D1::RectF(DLG_W - 175, Y_BUTTONS, DLG_W - 95, Y_BUTTONS + 26));
}

bool HotkeyDialog::HitTestCancelButton(POINT pt)
{
    return HitTestRect(pt, D2D1::RectF(DLG_W - 90, Y_BUTTONS, DLG_W - 20, Y_BUTTONS + 26));
}

void HotkeyDialog::DrawButton(ID2D1HwndRenderTarget* rt, const wchar_t* text,
                              const D2D1_RECT_F& rect, bool hovered, bool accent)
{
    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(rect, 4.0f, 4.0f);

    if (accent)
    {
        D2D1_COLOR_F bg = UIStyle::ThemeColor::Accent().d2d;
        bg.a = hovered ? 0.80f : 0.64f;
        auto bgBrush = GetOrCreateDialogBrush(rt, bg);
        if (bgBrush) rt->FillRoundedRectangle(rr, bgBrush.Get());

        auto borderBrush = GetOrCreateDialogBrush(rt, UIStyle::ThemeColor::Accent().d2d);
        if (borderBrush) rt->DrawRoundedRectangle(rr, borderBrush.Get(), UIStyle::Metrics::ControlStroke());

        if (m_tfBtn)
        {
            auto textBrush = GetOrCreateDialogBrush(rt, UIStyle::ThemeColor::TextOnAccent().d2d);
            if (textBrush) rt->DrawTextW(text, (UINT32)wcslen(text), m_tfBtn.Get(), rect, textBrush.Get());
        }
    }
    else
    {
        bool isLight = (UIStyle::GetThemeMode() == UIStyle::ThemeMode::Light);
        D2D1_COLOR_F base = isLight ? D2D1::ColorF(0.f, 0.f, 0.f) : D2D1::ColorF(1.f, 1.f, 1.f);
        float bgA     = hovered ? 0.09f : 0.04f;
        float borderA = hovered ? 0.16f : 0.075f;

        auto bgBrush = GetOrCreateDialogBrush(rt, D2D1::ColorF(base.r, base.g, base.b, bgA));
        if (bgBrush) rt->FillRoundedRectangle(rr, bgBrush.Get());

        auto borderBrush = GetOrCreateDialogBrush(rt, D2D1::ColorF(base.r, base.g, base.b, borderA));
        if (borderBrush) rt->DrawRoundedRectangle(rr, borderBrush.Get(), UIStyle::Metrics::ControlStroke());

        if (m_tfBtn)
        {
            auto textBrush = GetOrCreateDialogBrush(rt, UIStyle::ThemeColor::TextNormal().d2d);
            if (textBrush) rt->DrawTextW(text, (UINT32)wcslen(text), m_tfBtn.Get(), rect, textBrush.Get());
        }
    }
}
