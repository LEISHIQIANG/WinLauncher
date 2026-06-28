#define NOMINMAX
#include "ShortcutDialog.h"
#include "UIStyle.h"
#include "../DpiHelper.h"
#include <windowsx.h>
#include <commctrl.h>
#include <algorithm>
#include <cstring>
#include <vector>

#pragma comment(lib, "comctl32.lib")

// ─────────────────────────────────────────────
//  Window dimensions (logical units, pre-scale)
// ─────────────────────────────────────────────
static const int  DLG_W  = 360;   // window width
// Vertical layout anchors (logical Y)
static const float Y_TITLE      = 10.0f;
static const float Y_FORM_TOP   = 36.0f;   // The form component starts drawing from here downwards
static const float Y_BUTTONS    = Y_FORM_TOP + ShortcutEditForm::PreferredContentHeight() + 20.0f;
static const int  DLG_H         = (int)(Y_BUTTONS + 45.0f);

static ShortcutDialog* g_sdInstance = nullptr;

// Brush cache helper declarations (implemented at bottom)
ComPtr<ID2D1SolidColorBrush> GetOrCreateDialogBrush(ID2D1HwndRenderTarget* rt, const D2D1_COLOR_F& color);
static void ClearDialogBrushCache();

// ─────────────────────────────────────────────────────────────────────────────
ShortcutDialog::ShortcutDialog(const wchar_t* title, const InitParams& init, AppContext* ctx)
    : m_title(title)
    , m_init(init)
    , m_okPressed(false)
{
    m_appCtx = ctx;
}

ShortcutDialog::~ShortcutDialog()
{
    m_form.Destroy();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Static show helper
// ─────────────────────────────────────────────────────────────────────────────
bool ShortcutDialog::Show(HWND parent, const wchar_t* title,
                          ShortcutDialogResult& result,
                          const InitParams* init, AppContext* ctx)
{
    if (g_sdInstance) return false;

    InitParams params{};
    if (init) params = *init;

    ShortcutDialog* win = new ShortcutDialog(title, params, ctx);

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

    g_sdInstance = win;

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
        ShortcutEditFormResult formRes = win->m_form.GetResult();
        result.name            = formRes.name;
        result.targetPath      = formRes.targetPath;
        result.arguments       = formRes.arguments;
        result.workingDir      = formRes.workingDir;
        result.iconPath        = formRes.iconPath;
        result.runAsAdmin      = formRes.runAsAdmin;
        result.iconInvertLight = formRes.iconInvertLight;
        result.iconInvertDark  = formRes.iconInvertDark;
    }

    g_sdInstance = nullptr;
    delete win;

    if (parent)
    {
        EnableWindow(parent, TRUE);
        SetForegroundWindow(parent);
    }

    return ok;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Message handling
// ─────────────────────────────────────────────────────────────────────────────
LRESULT ShortcutDialog::HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CREATE:
    {
        EnsureD2D();
        // Create the decoupled form component within our bounds
        m_form.Create(hWnd, m_dw.Get(), D2D1::RectF(0, Y_FORM_TOP, DLG_W, DLG_H), m_init);
        SetTimer(hWnd, 0x999, GetCaretBlinkTime(), nullptr);
        return 0;
    }

    case WM_SHOWWINDOW:
    {
        return GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
    }

    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        if (id == IDOK)
        {
            // Validate basic inputs in the form
            if (!m_form.Validate(hWnd)) return 0;

            m_okPressed = true;
            DestroyWindow(hWnd);
        }
        else if (id == IDCANCEL)
        {
            m_okPressed = false;
            DestroyWindow(hWnd);
        }
        return 0;
    }

    case WM_ACTIVATE:
        if (LOWORD(wParam) != WA_INACTIVE)
            SetFocus(hWnd);
        return 0;

    case WM_SETFOCUS:
        // Do not reset focus on textboxes; let the form maintain its focus state
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;

    case WM_KILLFOCUS:
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;

    case WM_KEYDOWN:
    {
        if (wParam == VK_RETURN)
        {
            PostMessageW(hWnd, WM_COMMAND, MAKEWPARAM(IDOK, 0), 0);
            return 0;
        }
        if (wParam == VK_ESCAPE)
        {
            PostMessageW(hWnd, WM_COMMAND, MAKEWPARAM(IDCANCEL, 0), 0);
            return 0;
        }

        bool repaint = false;
        m_form.OnKeyDown(hWnd, wParam, lParam, repaint);
        if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }

    case WM_CHAR:
    {
        bool repaint = false;
        m_form.OnChar(hWnd, wParam, repaint);
        if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }

    case WM_IME_STARTCOMPOSITION:
    case WM_IME_COMPOSITION:
    {
        float scale = GetWindowScale(hWnd);
        // Form takes care of caret position for IME window
        if (m_form.IsInputFocused())
        {
            // Just trigger dummy key updates to reposition
            bool repaint = false;
            m_form.OnChar(hWnd, 0, repaint); 
        }
        break;
    }

    case WM_TIMER:
        if (wParam == 0x999)
        {
            m_form.BlinkCaret();
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        }
        break;

    case WM_MOUSEMOVE:
    {
        if (!m_trackMouse)
        {
            TRACKMOUSEEVENT tme{ sizeof(tme) };
            tme.dwFlags   = TME_LEAVE;
            tme.hwndTrack = hWnd;
            TrackMouseEvent(&tme);
            m_trackMouse = true;
        }

        float scale = GetWindowScale(hWnd);
        POINT pt    { (int)(GET_X_LPARAM(lParam) / scale), (int)(GET_Y_LPARAM(lParam) / scale) };
        bool repaint = false;

        // Forward to the form component
        m_form.OnMouseMove(hWnd, pt, scale, repaint);

        auto update = [&](bool& flag, bool newVal){ if (flag != newVal){ flag = newVal; repaint = true; } };
        update(m_hoveredOk,     HitTestOkButton(pt));
        update(m_hoveredCancel, HitTestCancelButton(pt));
        update(m_hoveredClose,  HitTestCloseButton(pt));

        if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }

    case WM_MOUSELEAVE:
        m_hoveredOk = m_hoveredCancel = m_hoveredClose = false;
        m_trackMouse = false;
        {
            bool repaint = false;
            m_form.OnMouseMove(hWnd, POINT{-999, -999}, 1.0f, repaint);
        }
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;

    case WM_LBUTTONDOWN:
    {
        float scale = GetWindowScale(hWnd);
        POINT pt    { (int)(GET_X_LPARAM(lParam) / scale), (int)(GET_Y_LPARAM(lParam) / scale) };

        if (HitTestCloseButton(pt))         { PostMessageW(hWnd, WM_COMMAND, MAKEWPARAM(IDCANCEL, 0), 0); return 0; }
        if (HitTestOkButton(pt))             { PostMessageW(hWnd, WM_COMMAND, MAKEWPARAM(IDOK, 0),     0); return 0; }
        if (HitTestCancelButton(pt))         { PostMessageW(hWnd, WM_COMMAND, MAKEWPARAM(IDCANCEL, 0), 0); return 0; }

        // Forward clicking events to form
        bool repaint = false;
        m_form.OnLButtonDown(hWnd, pt, scale, repaint);

        // Clicked outside any input control: lose focus
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

        // Drag window if click was in title area
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
        float scale = GetWindowScale(hWnd);
        POINT pt    { (int)(GET_X_LPARAM(lParam) / scale), (int)(GET_Y_LPARAM(lParam) / scale) };
        bool repaint = false;
        m_form.OnLButtonDblClk(hWnd, pt, scale, repaint);
        if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }

    case WM_LBUTTONUP:
    {
        float scale = GetWindowScale(hWnd);
        POINT pt    { (int)(GET_X_LPARAM(lParam) / scale), (int)(GET_Y_LPARAM(lParam) / scale) };
        bool repaint = false;
        m_form.OnLButtonUp(hWnd, pt, scale, repaint);
        if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
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

    case WM_DESTROY:
        KillTimer(hWnd, 0x999);
        m_form.Destroy();
        ClearDialogBrushCache();
        PostThreadMessageW(GetCurrentThreadId(), WM_NULL, 0, 0);
        break;
    }
    return GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Layout
// ─────────────────────────────────────────────────────────────────────────────
void ShortcutDialog::UpdateChildLayout()
{
    float scale = GetWindowScale(GetHWND());
    m_form.UpdateLayout(D2D1::RectF(0, Y_FORM_TOP, DLG_W, DLG_H), scale);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Fonts
// ─────────────────────────────────────────────────────────────────────────────
void ShortcutDialog::EnsureFonts()
{
    auto makeFormat = [&](ComPtr<IDWriteTextFormat>& tf,
                          float size,
                          DWRITE_FONT_WEIGHT weight,
                          DWRITE_TEXT_ALIGNMENT ha,
                          DWRITE_PARAGRAPH_ALIGNMENT va)
    {
        if (!m_dw || tf) return;
        UIStyle::Typography::CreateTextFormat(
            m_dw.Get(),
            &tf,
            size,
            weight,
            ha,
            va);
    };

    makeFormat(m_tfTitle, 12.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD,
               DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    makeFormat(m_tfBtn,   10.0f, DWRITE_FONT_WEIGHT_NORMAL,
               DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Hit-tests
// ─────────────────────────────────────────────────────────────────────────────
bool ShortcutDialog::HitTestRect(POINT pt, const D2D1_RECT_F& rect)
{
    return (pt.x >= rect.left && pt.x <= rect.right && pt.y >= rect.top && pt.y <= rect.bottom);
}

bool ShortcutDialog::HitTestCloseButton(POINT pt)
{
    RECT cr; GetClientRect(GetHWND(), &cr);
    float scale = GetWindowScale(GetHWND());
    float w = (float)cr.right / scale;
    return HitTestRect(pt, D2D1::RectF(w - 25, 8, w - 9, 24));
}

bool ShortcutDialog::HitTestOkButton(POINT pt)
{
    return HitTestRect(pt, D2D1::RectF(DLG_W - 175, Y_BUTTONS, DLG_W - 95, Y_BUTTONS + 26));
}

bool ShortcutDialog::HitTestCancelButton(POINT pt)
{
    return HitTestRect(pt, D2D1::RectF(DLG_W - 90, Y_BUTTONS, DLG_W - 20, Y_BUTTONS + 26));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Drawing helpers
// ─────────────────────────────────────────────────────────────────────────────
void ShortcutDialog::DrawButton(ID2D1HwndRenderTarget* rt, const wchar_t* text,
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

// ─────────────────────────────────────────────────────────────────────────────
//  Paint
// ─────────────────────────────────────────────────────────────────────────────
void ShortcutDialog::OnPaintContent(ID2D1HwndRenderTarget* rt)
{
    EnsureFonts();

    RECT cr;
    GetClientRect(GetHWND(), &cr);
    float scale = GetWindowScale(GetHWND());
    float w = (float)cr.right / scale;
    float h = (float)cr.bottom / scale;

    // ── Title ────────────────────────────────────────────────────────────────
    if (m_tfTitle)
    {
        auto titleBrush = GetOrCreateDialogBrush(rt, UIStyle::ThemeColor::TextNormal().d2d);
        if (titleBrush)
            rt->DrawTextW(m_title.c_str(), (UINT32)m_title.size(), m_tfTitle.Get(),
                          D2D1::RectF(15, Y_TITLE, w - 40, Y_TITLE + 22), titleBrush.Get());
    }

    // ── Close button ─────────────────────────────────────────────────────────
    {
        D2D1_RECT_F closeRect = D2D1::RectF(w - 25, 8, w - 9, 24);
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
            rt->DrawLine(D2D1::Point2F(w - 21, 12), D2D1::Point2F(w - 13, 20), xb.Get(), UIStyle::Metrics::IconStroke());
            rt->DrawLine(D2D1::Point2F(w - 13, 12), D2D1::Point2F(w - 21, 20), xb.Get(), UIStyle::Metrics::IconStroke());
        }
    }

    // ── Paint the decoupled form component ──────────────────────────────────
    m_form.Paint(rt, scale);

    // ── Separator line above buttons ─────────────────────────────────────────
    {
        D2D1_COLOR_F sepClr = UIStyle::ThemeColor::TextNormal().d2d; sepClr.a = 0.10f;
        auto sb = GetOrCreateDialogBrush(rt, sepClr);
        if (sb) rt->DrawLine(D2D1::Point2F(10, Y_BUTTONS - 8), D2D1::Point2F(w - 10, Y_BUTTONS - 8), sb.Get(), 0.5f);
    }

    // ── Buttons: 确定 / 取消 ─────────────────────────────────────────────────
    DrawButton(rt, L"确定", D2D1::RectF(DLG_W - 175, Y_BUTTONS, DLG_W - 95, Y_BUTTONS + 26), m_hoveredOk, true);
    DrawButton(rt, L"取消", D2D1::RectF(DLG_W - 90,  Y_BUTTONS, DLG_W - 20, Y_BUTTONS + 26), m_hoveredCancel, false);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Brush Cache Entry mapping helper
// ─────────────────────────────────────────────────────────────────────────────
struct D2DDialogBrushCacheEntry
{
    D2D1_COLOR_F color;
    ComPtr<ID2D1SolidColorBrush> brush;
};
static std::vector<D2DDialogBrushCacheEntry> g_dialogBrushCache;

void ClearDialogBrushCache() { g_dialogBrushCache.clear(); }

ComPtr<ID2D1SolidColorBrush> GetOrCreateDialogBrush(ID2D1HwndRenderTarget* rt, const D2D1_COLOR_F& color)
{
    for (auto& entry : g_dialogBrushCache)
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
            g_dialogBrushCache.push_back({ color, brush });
        }
    }
    return brush;
}
