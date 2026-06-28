#define NOMINMAX
#include "PromptWindow.h"
#include "UIStyle.h"
#include "../DpiHelper.h"
#include <windowsx.h>
#include <commctrl.h>

#pragma comment(lib, "comctl32.lib")

PromptWindow* g_promptInstance = nullptr;

PromptWindow::PromptWindow(const wchar_t* title, const wchar_t* prompt, const wchar_t* defaultText, AppContext* ctx)
    : GlassWindow()
    , m_title(title)
    , m_prompt(prompt)
    , m_defaultText(defaultText ? defaultText : L"")
    , m_okPressed(false)
    , m_hoveredOk(false)
    , m_hoveredCancel(false)
    , m_hoveredClose(false)
    , m_trackMouse(false)
{
    m_appCtx = ctx;
}

PromptWindow::~PromptWindow()
{
}

bool PromptWindow::Show(HWND parent, const wchar_t* title, const wchar_t* prompt,
                        std::wstring& outResult, const wchar_t* defaultText, AppContext* ctx)
{
    if (g_promptInstance) return false;

    PromptWindow* win = new PromptWindow(title, prompt, defaultText, ctx);

    int w = 240;
    int h = 135;

    HMONITOR hm = nullptr;
    if (parent)
    {
        hm = MonitorFromWindow(parent, MONITOR_DEFAULTTONEAREST);
    }
    else
    {
        POINT pt;
        GetCursorPos(&pt);
        hm = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    }

    float scale = DpiHelper::GetDpiScaleForMonitor(hm);
    int w_px = (int)(w * scale);
    int h_px = (int)(h * scale);

    int x = 0, y = 0;
    if (parent && IsWindowVisible(parent))
    {
        RECT pr;
        GetWindowRect(parent, &pr);
        x = pr.left + (pr.right - pr.left - w_px) / 2;
        y = pr.top + (pr.bottom - pr.top - h_px) / 2;
    }
    else
    {
        MONITORINFO mi{ sizeof(mi) };
        GetMonitorInfoW(hm, &mi);
        RECT wa = mi.rcWork;
        x = wa.left + (wa.right - wa.left - w_px) / 2;
        y = wa.top + (wa.bottom - wa.top - h_px) / 2;
    }

    if (parent) EnableWindow(parent, FALSE);

    win->Create(L"", WS_POPUP, WS_EX_TOOLWINDOW | WS_EX_TOPMOST, x, y, w_px, h_px, parent);
    if (!win->GetHWND())
    {
        if (parent) EnableWindow(parent, TRUE);
        delete win;
        return false;
    }

    SetWindowDisplayAffinity(win->GetHWND(), WDA_MONITOR | 0x10);

    win->ApplySystemBackdrop();
    win->EnsureD2D();

    ShowWindow(win->GetHWND(), SW_SHOW);
    UpdateWindow(win->GetHWND());
    SetForegroundWindow(win->GetHWND());
    SetFocus(win->GetHWND());

    g_promptInstance = win;

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
        outResult = win->m_result;
    }

    g_promptInstance = nullptr;
    delete win;

    if (parent)
    {
        EnableWindow(parent, TRUE);
        SetForegroundWindow(parent);
    }

    return ok;
}

LRESULT PromptWindow::HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CREATE:
    {
        EnsureD2D();
        UIStyle::TextBoxStyle style;
        style.fontSize = 11;
        style.paddingTop = 4.0f;
        style.paddingBottom = 4.0f;
        m_textBox.SetStyle(style);
        m_textBox.Create(hWnd, m_dw.Get(), D2D1::RectF(20, 58, 220, 84), m_defaultText);
        m_textBox.SetFocus(true);
        SetTimer(hWnd, 0x999, GetCaretBlinkTime(), nullptr);
        return 0;
    }
    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        if (id == IDOK)
        {
            m_result = m_textBox.GetText();
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
    {
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
    case WM_KEYDOWN:
    {
        if (m_textBox.HasFocus())
        {
            bool repaint = false;
            m_textBox.OnKeyDown(hWnd, wParam, lParam, repaint);
            if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_CHAR:
    {
        if (m_textBox.HasFocus())
        {
            bool repaint = false;
            m_textBox.OnChar(hWnd, wParam, repaint);
            if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_IME_STARTCOMPOSITION:
    case WM_IME_COMPOSITION:
    {
        float scale = GetWindowScale(hWnd);
        m_textBox.UpdateImeWindowPosition(hWnd, scale);
        break;
    }
    case WM_TIMER:
    {
        if (wParam == 0x999)
        {
            m_textBox.BlinkCaret();
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        }
        break;
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
        POINT rawPt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        bool repaint = false;

        m_textBox.OnMouseMove(hWnd, rawPt, scale, repaint);

        bool ho = HitTestOkButton(pt);
        if (ho != m_hoveredOk)
        {
            m_hoveredOk = ho;
            repaint = true;
        }

        bool hc = HitTestCancelButton(pt);
        if (hc != m_hoveredCancel)
        {
            m_hoveredCancel = hc;
            repaint = true;
        }

        bool hcl = HitTestCloseButton(pt);
        if (hcl != m_hoveredClose)
        {
            m_hoveredClose = hcl;
            repaint = true;
        }

        if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSELEAVE:
    {
        m_hoveredOk = false;
        m_hoveredCancel = false;
        m_hoveredClose = false;
        m_trackMouse = false;
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONDOWN:
    {
        float scale = GetWindowScale(hWnd);
        POINT pt{ (int)(GET_X_LPARAM(lParam) / scale), (int)(GET_Y_LPARAM(lParam) / scale) };
        POINT rawPt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        if (HitTestCloseButton(pt))
        {
            PostMessageW(hWnd, WM_COMMAND, MAKEWPARAM(IDCANCEL, 0), 0);
            return 0;
        }
        if (HitTestOkButton(pt))
        {
            PostMessageW(hWnd, WM_COMMAND, MAKEWPARAM(IDOK, 0), 0);
            return 0;
        }
        if (HitTestCancelButton(pt))
        {
            PostMessageW(hWnd, WM_COMMAND, MAKEWPARAM(IDCANCEL, 0), 0);
            return 0;
        }
        if (m_textBox.HitTest(pt))
        {
            SetFocus(hWnd);
            m_textBox.SetFocus(true);
            bool repaint = false;
            m_textBox.OnLButtonDown(hWnd, rawPt, scale, repaint);
            if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        }
        else
        {
            m_textBox.SetFocus(false);
            InvalidateRect(hWnd, nullptr, FALSE);
        }

        SetFocus(hWnd);
        ReleaseCapture();
        SendMessageW(hWnd, WM_SYSCOMMAND, SC_MOVE | HTCAPTION, 0);
        return 0;
    }
    case WM_LBUTTONDBLCLK:
    {
        float scale = GetWindowScale(hWnd);
        POINT rawPt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        POINT pt{ (int)(rawPt.x / scale), (int)(rawPt.y / scale) };
        if (m_textBox.HitTest(pt))
        {
            m_textBox.SetFocus(true);
            bool repaint = false;
            m_textBox.OnLButtonDblClk(hWnd, rawPt, scale, repaint);
            if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONUP:
    {
        float scale = GetWindowScale(hWnd);
        POINT rawPt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        bool repaint = false;
        m_textBox.OnLButtonUp(hWnd, rawPt, scale, repaint);
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
    case WM_SHOWWINDOW:
    {
        return GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
    }
    case WM_DESTROY:
    {
        KillTimer(hWnd, 0x999);
        m_textBox.Destroy();
        PostThreadMessageW(GetCurrentThreadId(), WM_NULL, 0, 0);
        break;
    }
    }
    return GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
}

void PromptWindow::UpdateChildLayout()
{
    float scale = GetWindowScale(GetHWND());
    m_textBox.UpdateLayout(scale);
}

void PromptWindow::EnsureFonts()
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
    if (m_dw && !m_tfPrompt)
    {
        UIStyle::Typography::CreateTextFormat(
            m_dw.Get(),
            &m_tfPrompt,
            10.0f,
            DWRITE_FONT_WEIGHT_NORMAL,
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

void PromptWindow::OnPaintContent(ID2D1HwndRenderTarget* rt)
{
    EnsureFonts();

    RECT cr;
    GetClientRect(GetHWND(), &cr);
    float scale = GetWindowScale(GetHWND());
    float w = (float)cr.right / scale;
    float h = (float)cr.bottom / scale;

    // 1. Header Title
    if (m_tfTitle)
    {
        auto titleBrush = GetOrCreateBrush(UIStyle::ThemeColor::TextNormal().d2d);
        if (titleBrush)
        {
            rt->DrawTextW(m_title.c_str(), (UINT32)m_title.size(), m_tfTitle.Get(),
                D2D1::RectF(15, 8, 200, 28), titleBrush.Get());
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
            if (closeBg)
            {
                rt->FillRoundedRectangle(roundedClose, closeBg.Get());
            }
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

    // 3. Prompt Message
    if (m_tfPrompt)
    {
        D2D1_COLOR_F promptColor = UIStyle::ThemeColor::TextNormal().d2d;
        promptColor.a = 0.8f;
        auto promptBrush = GetOrCreateBrush(promptColor);
        if (promptBrush)
        {
            rt->DrawTextW(m_prompt.c_str(), (UINT32)m_prompt.size(), m_tfPrompt.Get(),
                D2D1::RectF(20, 36, w - 20, 52), promptBrush.Get());
        }
    }

    // 4. Edit control container
    m_textBox.Paint(rt, scale);

    // 5. OK and Cancel Buttons
    {
        // OK Button: warm accent style
        D2D1_RECT_F okRect = D2D1::RectF(w - 165, 98, w - 95, 122);
        D2D1_ROUNDED_RECT roundedOk = D2D1::RoundedRect(okRect, 5.0f, 5.0f);

        float okAlpha = m_hoveredOk ? 0.80f : 0.64f;
        D2D1_COLOR_F okBg = UIStyle::ThemeColor::Accent().d2d;
        okBg.a = okAlpha;
        auto okBgBrush = GetOrCreateBrush(okBg);
        if (okBgBrush)
        {
            rt->FillRoundedRectangle(roundedOk, okBgBrush.Get());
        }

        auto okBorderBrush = GetOrCreateBrush(UIStyle::ThemeColor::Accent().d2d);
        if (okBorderBrush)
        {
            rt->DrawRoundedRectangle(roundedOk, okBorderBrush.Get(), UIStyle::Metrics::ControlStroke());
        }

        // Cancel Button: Flat translucent gray style
        D2D1_RECT_F cancelRect = D2D1::RectF(w - 90, 98, w - 20, 122);
        D2D1_ROUNDED_RECT roundedCancel = D2D1::RoundedRect(cancelRect, 5.0f, 5.0f);

        float cancelAlphaBg = m_hoveredCancel ? 0.09f : 0.04f;
        float cancelAlphaBorder = m_hoveredCancel ? 0.16f : 0.075f;

        D2D1_COLOR_F baseClr = UIStyle::ThemeColor::ThemeBase().d2d;
        auto cancelBgBrush = GetOrCreateBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, cancelAlphaBg));
        if (cancelBgBrush)
        {
            rt->FillRoundedRectangle(roundedCancel, cancelBgBrush.Get());
        }

        auto cancelBorderBrush = GetOrCreateBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, cancelAlphaBorder));
        if (cancelBorderBrush)
        {
            rt->DrawRoundedRectangle(roundedCancel, cancelBorderBrush.Get(), UIStyle::Metrics::ControlStroke());
        }

        // Text on buttons
        if (m_tfBtn)
        {
            auto okTextBrush = GetOrCreateBrush(UIStyle::ThemeColor::TextOnAccent().d2d);
            if (okTextBrush)
            {
                rt->DrawTextW(L"确定", 2, m_tfBtn.Get(), okRect, okTextBrush.Get());
            }
            auto cancelTextBrush = GetOrCreateBrush(UIStyle::ThemeColor::TextNormal().d2d);
            if (cancelTextBrush)
            {
                rt->DrawTextW(L"取消", 2, m_tfBtn.Get(), cancelRect, cancelTextBrush.Get());
            }
        }
    }
}

bool PromptWindow::HitTestRect(POINT pt, const D2D1_RECT_F& rect)
{
    return (pt.x >= rect.left && pt.x <= rect.right && pt.y >= rect.top && pt.y <= rect.bottom);
}

bool PromptWindow::HitTestCloseButton(POINT pt)
{
    RECT cr; GetClientRect(GetHWND(), &cr);
    float scale = GetWindowScale(GetHWND());
    float w = (float)cr.right / scale;
    return HitTestRect(pt, D2D1::RectF(w - 25, 8, w - 9, 24));
}

bool PromptWindow::HitTestOkButton(POINT pt)
{
    RECT cr; GetClientRect(GetHWND(), &cr);
    float scale = GetWindowScale(GetHWND());
    float w = (float)cr.right / scale;
    return HitTestRect(pt, D2D1::RectF(w - 165, 98, w - 95, 122));
}

bool PromptWindow::HitTestCancelButton(POINT pt)
{
    RECT cr; GetClientRect(GetHWND(), &cr);
    float scale = GetWindowScale(GetHWND());
    float w = (float)cr.right / scale;
    return HitTestRect(pt, D2D1::RectF(w - 90, 98, w - 20, 122));
}
