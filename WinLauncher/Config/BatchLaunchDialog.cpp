#define NOMINMAX
#include "BatchLaunchDialog.h"
#include "UIStyle.h"
#include "../DpiHelper.h"
#include <windowsx.h>
#include <commctrl.h>
#include <algorithm>
#include <cstring>
#include <vector>

static const int DLG_W = 360;
static const float Y_TITLE = 8.0f;
static const float Y_FORM_TOP = 36.0f;
static const float Y_BUTTONS = Y_FORM_TOP + BatchLaunchEditForm::PreferredContentHeight() + 15.0f;
static const int DLG_H = (int)(Y_BUTTONS + 38.0f);

static BatchLaunchDialog* g_bldInstance = nullptr;

struct BatchDialogBrushCacheEntry
{
    D2D1_COLOR_F color;
    ComPtr<ID2D1SolidColorBrush> brush;
};
static std::vector<BatchDialogBrushCacheEntry> g_batchDialogBrushCache;

static ComPtr<ID2D1SolidColorBrush> GetOrCreateDialogBrush(ID2D1HwndRenderTarget* rt, const D2D1_COLOR_F& color)
{
    for (auto& entry : g_batchDialogBrushCache)
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
            g_batchDialogBrushCache.push_back({ color, brush });
        }
    }
    return brush;
}

BatchLaunchDialog::BatchLaunchDialog(const wchar_t* title, const InitParams& init, AppContext* ctx)
    : m_title(title)
    , m_init(init)
    , m_okPressed(false)
{
    m_appCtx = ctx;
}

BatchLaunchDialog::~BatchLaunchDialog()
{
    m_form.Destroy();
}

bool BatchLaunchDialog::Show(HWND parent, const wchar_t* title,
                             BatchLaunchDialogResult& result,
                             const InitParams* init, AppContext* ctx)
{
    if (g_bldInstance) return false;

    InitParams params{};
    if (init) params = *init;

    BatchLaunchDialog* win = new BatchLaunchDialog(title, params, ctx);

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

    g_bldInstance = win;

    MSG msg;
    HWND hWnd = win->GetHWND();
    while (IsWindow(hWnd) && GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (msg.message == WM_QUIT)
    {
        PostQuitMessage((int)msg.wParam);
    }

    bool ok = win->m_okPressed;
    if (ok)
    {
        BatchLaunchEditFormResult formRes = win->m_form.GetResult();
        result.name = formRes.name;
        result.arguments = formRes.arguments;
        result.iconPath = formRes.iconPath;
        result.iconInvertLight = formRes.iconInvertLight;
        result.iconInvertDark = formRes.iconInvertDark;
    }

    g_bldInstance = nullptr;
    delete win;

    if (parent)
    {
        EnableWindow(parent, TRUE);
        SetForegroundWindow(parent);
    }

    g_batchDialogBrushCache.clear();
    return ok;
}

LRESULT BatchLaunchDialog::HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CREATE:
    {
        EnsureD2D();
        m_form.Create(hWnd, m_dw.Get(), D2D1::RectF(0, Y_FORM_TOP, DLG_W, Y_FORM_TOP + BatchLaunchEditForm::PreferredContentHeight()), m_init, m_appCtx);
        SetTimer(hWnd, 0x999, GetCaretBlinkTime(), nullptr);
        SetTimer(hWnd, 0x998, 16, nullptr);
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
        if (wParam == 0x998)
        {
            if (m_form.TickAnimation())
                InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        }
        break;
    }

    case WM_DESTROY:
    {
        KillTimer(hWnd, 0x999);
        KillTimer(hWnd, 0x998);
        m_form.Destroy();
        g_batchDialogBrushCache.clear();
        PostThreadMessageW(GetCurrentThreadId(), WM_NULL, 0, 0);
        GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        float scale = DpiHelper::GetWindowScale(hWnd);
        POINT logicalPt{ (long)(pt.x / scale), (long)(pt.y / scale) };

        if (HitTestCloseButton(logicalPt))
        {
            PostMessageW(hWnd, WM_CLOSE, 0, 0);
            return 0;
        }

        if (HitTestOkButton(logicalPt))
        {
            if (m_form.Validate(hWnd))
            {
                m_okPressed = true;
                PostMessageW(hWnd, WM_CLOSE, 0, 0);
            }
            return 0;
        }

        if (HitTestCancelButton(logicalPt))
        {
            PostMessageW(hWnd, WM_CLOSE, 0, 0);
            return 0;
        }

        if (logicalPt.y < Y_FORM_TOP)
        {
            SetFocus(hWnd);
            ReleaseCapture();
            SendMessageW(hWnd, WM_SYSCOMMAND, SC_MOVE | HTCAPTION, 0);
            return 0;
        }

        bool repaint = false;
        m_form.OnLButtonDown(hWnd, pt, scale, repaint);
        if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
        break;
    }

    case WM_LBUTTONUP:
    {
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        float scale = DpiHelper::GetWindowScale(hWnd);
        bool repaint = false;
        m_form.OnLButtonUp(hWnd, pt, scale, repaint);
        if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
        break;
    }

    case WM_LBUTTONDBLCLK:
    {
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        float scale = DpiHelper::GetWindowScale(hWnd);
        bool repaint = false;
        m_form.OnLButtonDblClk(hWnd, pt, scale, repaint);
        if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
        break;
    }

    case WM_MOUSEMOVE:
    {
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        float scale = DpiHelper::GetWindowScale(hWnd);
        POINT logicalPt{ (long)(pt.x / scale), (long)(pt.y / scale) };

        if (!m_trackMouse)
        {
            TRACKMOUSEEVENT tme{ sizeof(tme) };
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hWnd;
            TrackMouseEvent(&tme);
            m_trackMouse = true;
        }

        bool changed = false;
        bool hClose = HitTestCloseButton(logicalPt);
        if (hClose != m_hoveredClose) { m_hoveredClose = hClose; changed = true; }

        bool hOk = HitTestOkButton(logicalPt);
        if (hOk != m_hoveredOk) { m_hoveredOk = hOk; changed = true; }

        bool hCancel = HitTestCancelButton(logicalPt);
        if (hCancel != m_hoveredCancel) { m_hoveredCancel = hCancel; changed = true; }

        bool formRepaint = false;
        m_form.OnMouseMove(hWnd, pt, scale, formRepaint);
        if (changed || formRepaint) InvalidateRect(hWnd, nullptr, FALSE);
        break;
    }

    case WM_MOUSELEAVE:
    {
        m_trackMouse = false;
        bool changed = false;
        if (m_hoveredClose) { m_hoveredClose = false; changed = true; }
        if (m_hoveredOk) { m_hoveredOk = false; changed = true; }
        if (m_hoveredCancel) { m_hoveredCancel = false; changed = true; }
        if (changed) InvalidateRect(hWnd, nullptr, FALSE);
        break;
    }

    case WM_IME_STARTCOMPOSITION:
    case WM_IME_COMPOSITION:
    case WM_IME_ENDCOMPOSITION:
    {
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

    case WM_MOUSEWHEEL:
    {
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hWnd, &pt);
        float scale = DpiHelper::GetWindowScale(hWnd);
        short zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        bool repaint = false;
        m_form.OnMouseWheel(hWnd, zDelta, pt, scale, repaint);
        if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
        break;
    }
    }

    return GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
}

void BatchLaunchDialog::OnPaintContent(ID2D1HwndRenderTarget* rt)
{
    EnsureFonts();
    float scale = DpiHelper::GetWindowScale(GetHWND());
    D2D1_SIZE_F sz = rt->GetSize();
    float W = sz.width;
    float H = sz.height;

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

    // Paint Form Content
    m_form.Paint(rt, scale);

    // Divider
    {
        D2D1_COLOR_F sepClr = UIStyle::ThemeColor::TextNormal().d2d; sepClr.a = 0.10f;
        auto sb = GetOrCreateDialogBrush(rt, sepClr);
        if (sb) rt->DrawLine(D2D1::Point2F(10, Y_BUTTONS - 8), D2D1::Point2F(W - 10, Y_BUTTONS - 8), sb.Get(), 0.5f);
    }

    // Buttons
    DrawButton(rt, L"确定", D2D1::RectF(W - 175, Y_BUTTONS, W - 95, Y_BUTTONS + 26), m_hoveredOk, true);
    DrawButton(rt, L"取消", D2D1::RectF(W - 90, Y_BUTTONS, W - 20, Y_BUTTONS + 26), m_hoveredCancel, false);
}

void BatchLaunchDialog::EnsureFonts()
{
    if (!m_tfTitle && m_dw)
    {
        m_dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"zh-CN", &m_tfTitle);
        m_dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"zh-CN", &m_tfBtn);
        if (m_tfBtn)
        {
            m_tfBtn->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            m_tfBtn->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }
}

bool BatchLaunchDialog::HitTestRect(POINT pt, const D2D1_RECT_F& rect)
{
    return (pt.x >= rect.left && pt.x <= rect.right && pt.y >= rect.top && pt.y <= rect.bottom);
}

bool BatchLaunchDialog::HitTestCloseButton(POINT pt)
{
    return HitTestRect(pt, D2D1::RectF(DLG_W - 25, 8, DLG_W - 9, 24));
}

bool BatchLaunchDialog::HitTestOkButton(POINT pt)
{
    return HitTestRect(pt, D2D1::RectF(DLG_W - 175, Y_BUTTONS, DLG_W - 95, Y_BUTTONS + 26));
}

bool BatchLaunchDialog::HitTestCancelButton(POINT pt)
{
    return HitTestRect(pt, D2D1::RectF(DLG_W - 90, Y_BUTTONS, DLG_W - 20, Y_BUTTONS + 26));
}

void BatchLaunchDialog::DrawButton(ID2D1HwndRenderTarget* rt, const wchar_t* text, const D2D1_RECT_F& rect, bool hovered, bool accent)
{
    D2D1_COLOR_F bgClr = accent
        ? UIStyle::ThemeColor::Accent().d2d
        : UIStyle::ThemeColor::ButtonBgNormal().d2d;
    if (hovered)
    {
        bgClr.a = accent ? 0.9f : 0.25f;
    }
    else
    {
        bgClr.a = accent ? 0.8f : 0.15f;
    }

    auto bgBrush = GetOrCreateDialogBrush(rt, bgClr);
    if (bgBrush)
    {
        rt->FillRoundedRectangle(D2D1::RoundedRect(rect, 4.0f, 4.0f), bgBrush.Get());
    }

    auto borderBrush = GetOrCreateDialogBrush(rt, UIStyle::ThemeColor::ButtonBorderNormal().d2d);
    if (borderBrush && !accent)
    {
        rt->DrawRoundedRectangle(D2D1::RoundedRect(rect, 4.0f, 4.0f), borderBrush.Get(), 1.0f);
    }

    D2D1_COLOR_F textClr = accent
        ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f)
        : UIStyle::ThemeColor::TextNormal().d2d;
    auto textBrush = GetOrCreateDialogBrush(rt, textClr);
    if (textBrush && m_tfBtn)
    {
        rt->DrawTextW(text, (UINT32)wcslen(text), m_tfBtn.Get(), rect, textBrush.Get());
    }
}
