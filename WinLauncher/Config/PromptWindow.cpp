#define NOMINMAX
#include "PromptWindow.h"
#include "UIStyle.h"
#include "../DpiHelper.h"
#include <windowsx.h>
#include <commctrl.h>

#pragma comment(lib, "comctl32.lib")

PromptWindow* g_promptInstance = nullptr;

PromptWindow::PromptWindow(Mode mode, const wchar_t* title, const wchar_t* prompt,
                           const std::vector<std::wstring>& chooseOptions,
                           const wchar_t* defaultText, AppContext* ctx)
    : GlassWindow()
    , m_mode(mode)
    , m_title(title)
    , m_prompt(prompt)
    , m_defaultText(defaultText ? defaultText : L"")
    , m_okPressed(false)
    , m_chooseOptions(chooseOptions)
    , m_selectedOption(-1)
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

// ──── Public static helpers ─────────────────────────────────────────────

bool PromptWindow::Show(HWND parent, const wchar_t* title, const wchar_t* prompt,
                        std::wstring& outResult, const wchar_t* defaultText, AppContext* ctx)
{
    return ShowInternal(parent, Mode::Input, title, prompt, outResult, {},
                        defaultText, ctx, 240, 135);
}

bool PromptWindow::ShowPassword(HWND parent, const wchar_t* title, const wchar_t* prompt,
                                std::wstring& outResult, AppContext* ctx)
{
    return ShowInternal(parent, Mode::Password, title, prompt, outResult, {},
                        L"", ctx, 240, 135);
}

bool PromptWindow::ShowChoose(HWND parent, const wchar_t* title, const wchar_t* prompt,
                              const std::vector<std::wstring>& options,
                              std::wstring& outResult, AppContext* ctx)
{
    int h = 110 + std::max(2, (int)options.size()) * 28;
    return ShowInternal(parent, Mode::Choose, title, prompt, outResult, options,
                        L"", ctx, 260, h);
}

// Measure text width using project's default font (10pt Microsoft YaHei UI)
static int MeasureConfirmWidth(const wchar_t* text)
{
    ComPtr<IDWriteFactory> dw;
    HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dw.GetAddressOf()));
    if (FAILED(hr) || !dw) return 280;

    ComPtr<IDWriteTextFormat> tf;
    hr = dw->CreateTextFormat(L"Microsoft YaHei UI", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        10.0f, L"zh-cn", &tf);
    if (FAILED(hr) || !tf) return 280;
    tf->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    ComPtr<IDWriteTextLayout> layout;
    size_t len = wcslen(text);
    hr = dw->CreateTextLayout(text, (UINT32)len, tf.Get(), 9999.0f, 9999.0f, &layout);
    if (FAILED(hr) || !layout) return 280;

    DWRITE_TEXT_METRICS metrics;
    layout->GetMetrics(&metrics);

    // padding: 20 left + 80 (title/buttons/close) + 20 right = ~120
    int w = (int)(metrics.width + 20.0f + 80.0f + 20.0f);
    // Clamp: min 200 (fits 4-char prompt + buttons), max 600
    if (w < 200) w = 200;
    if (w > 600) w = 600;
    return w;
}

bool PromptWindow::ShowConfirm(HWND parent, const wchar_t* title, const wchar_t* message,
                               AppContext* ctx)
{
    std::wstring dummy;
    int w = MeasureConfirmWidth(message);
    return ShowInternal(parent, Mode::Confirm, title, message, dummy, {},
                        L"", ctx, w, 130);
}

// ──── Internal runner ───────────────────────────────────────────────────

bool PromptWindow::ShowInternal(HWND parent, Mode mode, const wchar_t* title, const wchar_t* prompt,
                                std::wstring& outResult, const std::vector<std::wstring>& chooseOptions,
                                const wchar_t* defaultText, AppContext* ctx,
                                int w, int h)
{
    if (g_promptInstance) return false;

    PromptWindow* win = new PromptWindow(mode, title, prompt, chooseOptions, defaultText, ctx);

    HMONITOR hm = nullptr;
    if (parent)
        hm = MonitorFromWindow(parent, MONITOR_DEFAULTTONEAREST);
    else
    {
        POINT pt; GetCursorPos(&pt);
        hm = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    }

    float scale = DpiHelper::GetDpiScaleForMonitor(hm);
    int w_px = (int)(w * scale);
    int h_px = (int)(h * scale);

    int x = 0, y = 0;
    if (parent && IsWindowVisible(parent))
    {
        RECT pr; GetWindowRect(parent, &pr);
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

    SetWindowDisplayAffinitySafe(win->GetHWND());
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

    if (IsWindow(hWnd))
        DestroyWindow(hWnd);

    if (msg.message == WM_QUIT)
        PostQuitMessage((int)msg.wParam);

    bool ok = win->m_okPressed;
    if (ok)
        outResult = win->m_result;

    g_promptInstance = nullptr;
    delete win;

    if (parent)
    {
        EnableWindow(parent, TRUE);
        SetForegroundWindow(parent);
    }

    return ok;
}

// ──── WM handler ────────────────────────────────────────────────────────

LRESULT PromptWindow::HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CREATE:
    {
        EnsureD2D();
        if (m_mode == Mode::Input || m_mode == Mode::Password)
        {
            UIStyle::TextBoxStyle style;
            style.fontSize = 11;
            style.paddingTop = 4.0f;
            style.paddingBottom = 4.0f;
            m_textBox.SetStyle(style);
            m_textBox.Create(hWnd, m_dw.Get(), D2D1::RectF(20, 58, 220, 84), m_defaultText);
            if (m_mode == Mode::Password)
                m_textBox.SetPasswordMode(true);
            m_textBox.SetFocus(true);
        }
        SetTimer(hWnd, 0x999, GetCaretBlinkTime(), nullptr);
        return 0;
    }

    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        if (id == IDOK)
        {
            if (m_mode == Mode::Choose)
            {
                if (m_selectedOption < 0 || m_selectedOption >= (int)m_chooseOptions.size())
                    return 0; // nothing selected, ignore OK
                m_result = m_chooseOptions[m_selectedOption];
            }
            else if (m_mode == Mode::Input || m_mode == Mode::Password)
            {
                m_result = m_textBox.GetText();
            }
            // Confirm mode: m_result stays empty
            m_okPressed = true;
            PostMessageW(hWnd, WM_CLOSE, 0, 0);
        }
        else if (id == IDCANCEL)
        {
            m_okPressed = false;
            PostMessageW(hWnd, WM_CLOSE, 0, 0);
        }
        return 0;
    }

    case WM_ACTIVATE:
        if (LOWORD(wParam) != WA_INACTIVE) SetFocus(hWnd);
        return 0;

    case WM_SETFOCUS:
        if (m_mode == Mode::Input || m_mode == Mode::Password)
            m_textBox.SetFocus(true);
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;

    case WM_KILLFOCUS:
        if (m_mode == Mode::Input || m_mode == Mode::Password)
            m_textBox.SetFocus(false);
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;

    case WM_KEYDOWN:
        if (m_mode == Mode::Input || m_mode == Mode::Password)
        {
            if (m_textBox.HasFocus())
            {
                bool repaint = false;
                m_textBox.OnKeyDown(hWnd, wParam, lParam, repaint);
                if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
            }
        }
        else if (m_mode == Mode::Choose)
        {
            // Keyboard navigation for choose mode
            if (wParam == VK_UP && m_selectedOption > 0)
            {
                m_selectedOption--;
                InvalidateRect(hWnd, nullptr, FALSE);
            }
            else if (wParam == VK_DOWN && m_selectedOption < (int)m_chooseOptions.size() - 1)
            {
                m_selectedOption++;
                InvalidateRect(hWnd, nullptr, FALSE);
            }
            else if (wParam == VK_RETURN)
                PostMessageW(hWnd, WM_COMMAND, MAKEWPARAM(IDOK, 0), 0);
            else if (wParam == VK_ESCAPE)
                PostMessageW(hWnd, WM_COMMAND, MAKEWPARAM(IDCANCEL, 0), 0);
        }
        else if (m_mode == Mode::Confirm)
        {
            if (wParam == VK_RETURN || wParam == VK_SPACE)
                PostMessageW(hWnd, WM_COMMAND, MAKEWPARAM(IDOK, 0), 0);
            else if (wParam == VK_ESCAPE)
                PostMessageW(hWnd, WM_COMMAND, MAKEWPARAM(IDCANCEL, 0), 0);
        }
        return 0;

    case WM_CHAR:
        if ((m_mode == Mode::Input || m_mode == Mode::Password) && m_textBox.HasFocus())
        {
            bool repaint = false;
            m_textBox.OnChar(hWnd, wParam, repaint);
            if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;

    case WM_IME_STARTCOMPOSITION:
    case WM_IME_COMPOSITION:
    case WM_IME_ENDCOMPOSITION:
        if (m_mode == Mode::Input || m_mode == Mode::Password)
        {
            bool repaint = false;
            if (m_textBox.HandleImeMessage(hWnd, uMsg, wParam, lParam, repaint))
            {
                if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
                return 0;
            }
        }
        break;

    case WM_TIMER:
        if (wParam == 0x999)
        {
            if (m_mode == Mode::Input || m_mode == Mode::Password)
            {
                m_textBox.BlinkCaret();
                InvalidateRect(hWnd, nullptr, FALSE);
            }
            return 0;
        }
        break;

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

        if (m_mode == Mode::Input || m_mode == Mode::Password)
        {
            m_textBox.OnMouseMove(hWnd, rawPt, scale, repaint);
        }

        bool ho = HitTestOkButton(pt);
        if (ho != m_hoveredOk) { m_hoveredOk = ho; repaint = true; }

        bool hc = HitTestCancelButton(pt);
        if (hc != m_hoveredCancel) { m_hoveredCancel = hc; repaint = true; }

        bool hcl = HitTestCloseButton(pt);
        if (hcl != m_hoveredClose) { m_hoveredClose = hcl; repaint = true; }

        // Hover for choose options (only update on valid options, keep last selection on blank)
        if (m_mode == Mode::Choose)
        {
            int ho2 = HitTestChooseOption(pt);
            if (ho2 >= 0 && ho2 != m_selectedOption)
            {
                m_selectedOption = ho2;
                repaint = true;
            }
        }

        if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }

    case WM_MOUSELEAVE:
        m_hoveredOk = false;
        m_hoveredCancel = false;
        m_hoveredClose = false;
        m_trackMouse = false;
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;

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
        if (m_mode == Mode::Choose)
        {
            int opt = HitTestChooseOption(pt);
            if (opt >= 0)
            {
                m_selectedOption = opt;
                InvalidateRect(hWnd, nullptr, FALSE);
                // Double-click on option = OK
                return 0;
            }
        }
        if (m_mode == Mode::Input || m_mode == Mode::Password)
        {
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
        }
        SetFocus(hWnd);
        ReleaseCapture();
        SendMessageW(hWnd, WM_SYSCOMMAND, SC_MOVE | HTCAPTION, 0);
        return 0;
    }

    case WM_LBUTTONDBLCLK:
        if (m_mode == Mode::Choose)
        {
            float scale = GetWindowScale(hWnd);
            POINT pt{ (int)(GET_X_LPARAM(lParam) / scale), (int)(GET_Y_LPARAM(lParam) / scale) };
            int opt = HitTestChooseOption(pt);
            if (opt >= 0)
            {
                m_selectedOption = opt;
                PostMessageW(hWnd, WM_COMMAND, MAKEWPARAM(IDOK, 0), 0);
                return 0;
            }
        }
        if (m_mode == Mode::Input || m_mode == Mode::Password)
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
        }
        return 0;

    case WM_LBUTTONUP:
        if (m_mode == Mode::Input || m_mode == Mode::Password)
        {
            float scale = GetWindowScale(hWnd);
            POINT rawPt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            bool repaint = false;
            m_textBox.OnLButtonUp(hWnd, rawPt, scale, repaint);
            if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;

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
        return GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);

    case WM_DESTROY:
        KillTimer(hWnd, 0x999);
        m_textBox.Destroy();
        PostThreadMessageW(GetCurrentThreadId(), WM_NULL, 0, 0);
        break;
    }
    return GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
}

// ──── Layout ────────────────────────────────────────────────────────────

void PromptWindow::UpdateChildLayout()
{
    float scale = GetWindowScale(GetHWND());
    if (m_mode == Mode::Input || m_mode == Mode::Password)
        m_textBox.UpdateLayout(scale);
}

// ──── Fonts ─────────────────────────────────────────────────────────────

void PromptWindow::EnsureFonts()
{
    if (m_dw && !m_tfTitle)
    {
        UIStyle::Typography::CreateTextFormat(
            m_dw.Get(), &m_tfTitle,
            12.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_TEXT_ALIGNMENT_LEADING,
            DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    if (m_dw && !m_tfPrompt)
    {
        UIStyle::Typography::CreateTextFormat(
            m_dw.Get(), &m_tfPrompt,
            10.0f, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_TEXT_ALIGNMENT_LEADING,
            DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    if (m_dw && !m_tfBtn)
    {
        UIStyle::Typography::CreateTextFormat(
            m_dw.Get(), &m_tfBtn,
            10.0f, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_TEXT_ALIGNMENT_CENTER,
            DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
}

// ──── Paint ─────────────────────────────────────────────────────────────

void PromptWindow::OnPaintContent(ID2D1HwndRenderTarget* rt)
{
    EnsureFonts();

    RECT cr; GetClientRect(GetHWND(), &cr);
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
                          D2D1::RectF(15, 8, w - 30, 28), titleBrush.Get());
        }
    }

    // 2. Close Button "X"
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

    // 3. Prompt message
    if (m_tfPrompt)
    {
        D2D1_COLOR_F promptColor = UIStyle::ThemeColor::TextNormal().d2d;
        promptColor.a = 0.8f;
        auto promptBrush = GetOrCreateBrush(promptColor);
        if (promptBrush)
        {
            float promptTop = 36.0f;
            float promptH = (m_mode == Mode::Confirm) ? 42.0f : 16.0f; // original: 52-36=16
            rt->DrawTextW(m_prompt.c_str(), (UINT32)m_prompt.size(), m_tfPrompt.Get(),
                          D2D1::RectF(20, promptTop, w - 20, promptTop + promptH), promptBrush.Get());
        }
    }

    // 4. Content area
    if (m_mode == Mode::Input || m_mode == Mode::Password)
    {
        m_textBox.Paint(rt, scale);
    }
    else if (m_mode == Mode::Choose)
    {
        // Render option list
        float optX = 20.0f;
        float optW = w - 40.0f;
        float optYBase = 56.0f;
        float optH = 26.0f;

        for (size_t i = 0; i < m_chooseOptions.size(); i++)
        {
            float optY = optYBase + i * optH;
            D2D1_RECT_F optRect = D2D1::RectF(optX, optY, optX + optW, optY + optH);
            D2D1_ROUNDED_RECT roundedOpt = D2D1::RoundedRect(optRect, 4.0f, 4.0f);

            bool isHovered = ((int)i == m_selectedOption);

            // Background
            if (isHovered)
            {
                D2D1_COLOR_F selBg = UIStyle::ThemeColor::Accent().d2d;
                selBg.a = 0.18f;
                auto selBrush = GetOrCreateBrush(selBg);
                if (selBrush) rt->FillRoundedRectangle(roundedOpt, selBrush.Get());
            }
            else
            {
                D2D1_COLOR_F bg = UIStyle::ThemeColor::ThemeBase().d2d;
                bg.a = (i % 2 == 0) ? 0.03f : 0.0f;
                auto bgBrush = GetOrCreateBrush(bg);
                if (bgBrush) rt->FillRoundedRectangle(roundedOpt, bgBrush.Get());
            }

            // Border
            if (isHovered)
            {
                D2D1_COLOR_F bd = UIStyle::ThemeColor::Accent().d2d;
                bd.a = 0.35f;
                auto bdBrush = GetOrCreateBrush(bd);
                if (bdBrush) rt->DrawRoundedRectangle(roundedOpt, bdBrush.Get(), UIStyle::Metrics::ControlStroke());
            }

            // Text
            if (m_tfPrompt)
            {
                D2D1_COLOR_F tc = isHovered ? UIStyle::ThemeColor::Accent().d2d : UIStyle::ThemeColor::TextNormal().d2d;
                auto txtBrush = GetOrCreateBrush(tc);
                if (txtBrush)
                {
                    rt->DrawTextW(m_chooseOptions[i].c_str(), (UINT32)m_chooseOptions[i].size(),
                                  m_tfPrompt.Get(), D2D1::RectF(optX + 8, optY, optX + optW - 8, optY + optH), txtBrush.Get());
                }
            }
        }
    }
    // Confirm mode: just prompt text, no extra content

    // 5. OK and Cancel Buttons
    {
        float btnH = 24.0f;
        float btnBaseY = h - 37.0f; // original layout: buttons at y=98 on 135px window (135-37=98)

        // OK Button
        float okLeft = w - 155.0f;
        float okRight = w - 90.0f;
        D2D1_RECT_F okRect = D2D1::RectF(okLeft, btnBaseY, okRight, btnBaseY + btnH);
        D2D1_ROUNDED_RECT roundedOk = D2D1::RoundedRect(okRect, 5.0f, 5.0f);

        float okAlpha = m_hoveredOk ? 0.80f : 0.64f;
        D2D1_COLOR_F okBg = UIStyle::ThemeColor::Accent().d2d;
        okBg.a = okAlpha;
        auto okBgBrush = GetOrCreateBrush(okBg);
        if (okBgBrush) rt->FillRoundedRectangle(roundedOk, okBgBrush.Get());

        auto okBorderBrush = GetOrCreateBrush(UIStyle::ThemeColor::Accent().d2d);
        if (okBorderBrush)
            rt->DrawRoundedRectangle(roundedOk, okBorderBrush.Get(), UIStyle::Metrics::ControlStroke());

        // Cancel Button
        float cancelLeft = w - 85.0f;
        float cancelRight = w - 20.0f;
        D2D1_RECT_F cancelRect = D2D1::RectF(cancelLeft, btnBaseY, cancelRight, btnBaseY + btnH);
        D2D1_ROUNDED_RECT roundedCancel = D2D1::RoundedRect(cancelRect, 5.0f, 5.0f);

        float cancelAlphaBg = m_hoveredCancel ? 0.09f : 0.04f;
        float cancelAlphaBorder = m_hoveredCancel ? 0.16f : 0.075f;
        D2D1_COLOR_F baseClr = UIStyle::ThemeColor::ThemeBase().d2d;
        auto cancelBgBrush = GetOrCreateBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, cancelAlphaBg));
        if (cancelBgBrush) rt->FillRoundedRectangle(roundedCancel, cancelBgBrush.Get());

        auto cancelBorderBrush = GetOrCreateBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, cancelAlphaBorder));
        if (cancelBorderBrush)
            rt->DrawRoundedRectangle(roundedCancel, cancelBorderBrush.Get(), UIStyle::Metrics::ControlStroke());

        if (m_tfBtn)
        {
            auto okTextBrush = GetOrCreateBrush(UIStyle::ThemeColor::TextOnAccent().d2d);
            if (okTextBrush)
                rt->DrawTextW(L"\u786E\u5B9A", 2, m_tfBtn.Get(), okRect, okTextBrush.Get());

            auto cancelTextBrush = GetOrCreateBrush(UIStyle::ThemeColor::TextNormal().d2d);
            if (cancelTextBrush)
                rt->DrawTextW(L"\u53D6\u6D88", 2, m_tfBtn.Get(), cancelRect, cancelTextBrush.Get());
        }
    }
}

// ──── Hit testing ───────────────────────────────────────────────────────

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
    float h = (float)cr.bottom / scale;
    float btnBaseY = h - 37.0f;
    return HitTestRect(pt, D2D1::RectF(w - 155.0f, btnBaseY, w - 90.0f, btnBaseY + 24.0f));
}

bool PromptWindow::HitTestCancelButton(POINT pt)
{
    RECT cr; GetClientRect(GetHWND(), &cr);
    float scale = GetWindowScale(GetHWND());
    float w = (float)cr.right / scale;
    float h = (float)cr.bottom / scale;
    float btnBaseY = h - 37.0f;
    return HitTestRect(pt, D2D1::RectF(w - 85.0f, btnBaseY, w - 20.0f, btnBaseY + 24.0f));
}

int PromptWindow::HitTestChooseOption(POINT pt)
{
    if (m_mode != Mode::Choose) return -1;
    RECT cr; GetClientRect(GetHWND(), &cr);
    float scale = GetWindowScale(GetHWND());
    float w = (float)cr.right / scale;
    float optX = 20.0f;
    float optW = w - 40.0f;
    float optYBase = 56.0f;
    float optH = 26.0f;

    for (int i = 0; i < (int)m_chooseOptions.size(); i++)
    {
        float optY = optYBase + i * optH;
        if (HitTestRect(pt, D2D1::RectF(optX, optY, optX + optW, optY + optH)))
            return i;
    }
    return -1;
}
