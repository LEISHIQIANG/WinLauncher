#define NOMINMAX
#include "CommandPanelWindow.h"
#include "UIStyle.h"
#include "../DpiHelper.h"
#include "../App/Logger.h"
#include <windowsx.h>
#include <commctrl.h>
#include <cstring>
#include <thread>

#pragma comment(lib, "comctl32.lib")

static CommandPanelWindow* g_cmdPanelInstance = nullptr;
static const UINT WM_COMMAND_PANEL_APPEND = WM_APP + 0x310;

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

CommandPanelWindow::CommandPanelWindow(const wchar_t* title, const wchar_t* outputText, AppContext* ctx)
    : GlassWindow()
    , m_title(title)
    , m_outputText(outputText ? outputText : L"")
    , m_hoveredOk(false)
    , m_hoveredCopy(false)
    , m_hoveredClose(false)
    , m_trackMouse(false)
{
    m_appCtx = ctx;
}

CommandPanelWindow::~CommandPanelWindow()
{
    m_textBox.Destroy();
}

void CommandPanelWindow::Show(HWND parent, const wchar_t* title, const wchar_t* outputText, AppContext* ctx)
{
    ShowLive(parent, title, outputText, nullptr, ctx);
}

bool CommandPanelWindow::PostAppend(HWND hwnd, const std::wstring& text)
{
    if (!hwnd || text.empty())
        return false;

    std::wstring* payload = new std::wstring(text);
    if (!PostMessageW(hwnd, WM_COMMAND_PANEL_APPEND, 0, reinterpret_cast<LPARAM>(payload)))
    {
        delete payload;
        return false;
    }
    return true;
}

void CommandPanelWindow::ShowLive(HWND parent, const wchar_t* title, const wchar_t* initialText, std::function<void(HWND)> worker, AppContext* ctx)
{
    if (g_cmdPanelInstance)
    {
        HWND existing = g_cmdPanelInstance->GetHWND();
        if (existing && IsWindow(existing))
        {
            RECT rc{};
            GetWindowRect(existing, &rc);
            LOG_G_INFO(L"CommandPanelWindow::ShowLive: existing panel hwnd=%p visible=%d rect=(%ld,%ld,%ld,%ld)",
                       existing, IsWindowVisible(existing) ? 1 : 0, rc.left, rc.top, rc.right, rc.bottom);
            ShowWindow(existing, SW_SHOWNORMAL);
            SetWindowPos(existing, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOOWNERZORDER);
            BringWindowToTop(existing);
            SetForegroundWindow(existing);
            SetFocus(existing);
        }
        return;
    }

    CommandPanelWindow* win = new CommandPanelWindow(title, initialText, ctx);
    HWND owner = (parent && IsWindowVisible(parent)) ? parent : nullptr;

    int w = 620;
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

    if (worker)
    {
        HWND workerHwnd = win->GetHWND();
        std::thread([worker, workerHwnd]() {
            worker(workerHwnd);
        }).detach();
    }

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

    g_cmdPanelInstance = nullptr;
    delete win;
}

LRESULT CommandPanelWindow::HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_COMMAND_PANEL_APPEND:
    {
        std::wstring* payload = reinterpret_cast<std::wstring*>(lParam);
        if (payload)
        {
            AppendOutput(*payload);
            delete payload;
        }
        return 0;
    }
    case WM_CREATE:
    {
        EnsureD2D();
        UIStyle::TextBoxStyle style;
        style.fontSize = 10;
        style.paddingTop = 6.0f;
        style.paddingBottom = 6.0f;
        m_textBox.SetStyle(style);
        m_textBox.SetMultiline(true);
        m_textBox.Create(hWnd, m_dw.Get(), D2D1::RectF(20.0f, 44.0f, 600.0f, 365.0f), m_outputText);
        m_textBox.SetFocus(true);
        SetTimer(hWnd, 0x999, GetCaretBlinkTime(), nullptr);
        return 0;
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
        if (wParam == 0x999)
        {
            m_textBox.BlinkCaret();
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;
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
    {
        PostThreadMessageW(GetCurrentThreadId(), WM_NULL, 0, 0);
        break;
    }
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

void CommandPanelWindow::UpdateChildLayout()
{
    RECT cr; GetClientRect(GetHWND(), &cr);
    float scale = GetWindowScale(GetHWND());
    float w = (float)cr.right / scale;
    float h = (float)cr.bottom / scale;

    m_textBox.SetBounds(D2D1::RectF(20.0f, 44.0f, w - 20.0f, h - 58.0f));
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
                D2D1::RectF(20, 8, w - 40, 28), titleBrush.Get());
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

    // 4. Footer Buttons
    {
        D2D1_RECT_F copyRect = D2D1::RectF(w - 180, h - 40, w - 105, h - 16);
        D2D1_ROUNDED_RECT roundedCopy = D2D1::RoundedRect(copyRect, 5.0f, 5.0f);

        D2D1_COLOR_F copyBg = m_hoveredCopy
            ? UIStyle::ThemeColor::ButtonBgHover().d2d
            : UIStyle::ThemeColor::ButtonBgNormal().d2d;
        auto copyBgBrush = GetOrCreateBrush(copyBg);
        if (copyBgBrush) rt->FillRoundedRectangle(roundedCopy, copyBgBrush.Get());

        D2D1_COLOR_F copyBorder = m_hoveredCopy
            ? UIStyle::ThemeColor::ButtonBorderHover().d2d
            : UIStyle::ThemeColor::ButtonBorderNormal().d2d;
        auto copyBorderBrush = GetOrCreateBrush(copyBorder);
        if (copyBorderBrush) rt->DrawRoundedRectangle(roundedCopy, copyBorderBrush.Get(), UIStyle::Metrics::ControlStroke());

        if (m_tfBtn)
        {
            auto copyTextBrush = GetOrCreateBrush(UIStyle::ThemeColor::TextNormal().d2d);
            if (copyTextBrush) rt->DrawTextW(L"复制", 2, m_tfBtn.Get(), copyRect, copyTextBrush.Get());
        }
    }

    {
        D2D1_RECT_F okRect = D2D1::RectF(w - 95, h - 40, w - 20, h - 16);
        D2D1_ROUNDED_RECT roundedOk = D2D1::RoundedRect(okRect, 5.0f, 5.0f);

        float okAlpha = m_hoveredOk ? 0.80f : 0.64f;
        D2D1_COLOR_F okBg = UIStyle::ThemeColor::Accent().d2d;
        okBg.a = okAlpha;
        auto okBgBrush = GetOrCreateBrush(okBg);
        if (okBgBrush) rt->FillRoundedRectangle(roundedOk, okBgBrush.Get());

        auto okBorderBrush = GetOrCreateBrush(UIStyle::ThemeColor::Accent().d2d);
        if (okBorderBrush) rt->DrawRoundedRectangle(roundedOk, okBorderBrush.Get(), UIStyle::Metrics::ControlStroke());

        if (m_tfBtn)
        {
            auto okTextBrush = GetOrCreateBrush(UIStyle::ThemeColor::TextOnAccent().d2d);
            if (okTextBrush) rt->DrawTextW(L"确定", 2, m_tfBtn.Get(), okRect, okTextBrush.Get());
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

bool CommandPanelWindow::HitTestCopyButton(POINT pt)
{
    RECT cr; GetClientRect(GetHWND(), &cr);
    float scale = GetWindowScale(GetHWND());
    float w = (float)cr.right / scale;
    float h = (float)cr.bottom / scale;
    return HitTestRect(pt, D2D1::RectF(w - 180, h - 40, w - 105, h - 16));
}

bool CommandPanelWindow::HitTestOkButton(POINT pt)
{
    RECT cr; GetClientRect(GetHWND(), &cr);
    float scale = GetWindowScale(GetHWND());
    float w = (float)cr.right / scale;
    float h = (float)cr.bottom / scale;
    return HitTestRect(pt, D2D1::RectF(w - 95, h - 40, w - 20, h - 16));
}

void CommandPanelWindow::CopyOutputToClipboard()
{
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
