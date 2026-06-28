#define NOMINMAX
#include "WaitWindow.h"
#include "UIStyle.h"
#include "../DpiHelper.h"
#include <windowsx.h>
#include <thread>
#include <cmath>

WaitWindow::WaitWindow(const wchar_t* title, const wchar_t* prompt, AppContext* ctx)
    : GlassWindow()
    , m_title(title ? title : L"")
    , m_prompt(prompt ? prompt : L"")
    , m_angle(0.0f)
    , m_timerId(0)
    , m_finished(false)
{
    m_appCtx = ctx;
}

WaitWindow::~WaitWindow()
{
}

void WaitWindow::Show(HWND parent, const wchar_t* title, const wchar_t* prompt, std::function<void()> worker, AppContext* ctx)
{
    WaitWindow* win = new WaitWindow(title, prompt, ctx);

    // Setup window size: calculate dynamic width based on text length
    float textWidth = 0.0f;
    float titleWidth = 0.0f;

    ComPtr<IDWriteFactory> dwriteFactory;
    if (SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), &dwriteFactory)))
    {
        if (prompt && prompt[0] != L'\0')
        {
            ComPtr<IDWriteTextFormat> tfPrompt;
            UIStyle::Typography::CreateTextFormat(
                dwriteFactory.Get(),
                &tfPrompt,
                10.0f,
                DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_TEXT_ALIGNMENT_LEADING,
                DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            if (tfPrompt)
            {
                ComPtr<IDWriteTextLayout> layout;
                if (SUCCEEDED(dwriteFactory->CreateTextLayout(prompt, (UINT32)wcslen(prompt), tfPrompt.Get(), 1000.0f, 1000.0f, &layout)))
                {
                    DWRITE_TEXT_METRICS metrics{};
                    layout->GetMetrics(&metrics);
                    textWidth = metrics.width;
                }
            }
        }
        if (title && title[0] != L'\0')
        {
            ComPtr<IDWriteTextFormat> tfTitle;
            UIStyle::Typography::CreateTextFormat(
                dwriteFactory.Get(),
                &tfTitle,
                12.0f,
                DWRITE_FONT_WEIGHT_SEMI_BOLD,
                DWRITE_TEXT_ALIGNMENT_LEADING,
                DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            if (tfTitle)
            {
                ComPtr<IDWriteTextLayout> layout;
                if (SUCCEEDED(dwriteFactory->CreateTextLayout(title, (UINT32)wcslen(title), tfTitle.Get(), 1000.0f, 1000.0f, &layout)))
                {
                    DWRITE_TEXT_METRICS metrics{};
                    layout->GetMetrics(&metrics);
                    titleWidth = metrics.width;
                }
            }
        }
    }

    float requiredWidth = 80.0f + textWidth;
    if (title && title[0] != L'\0')
    {
        float titleRequired = 40.0f + titleWidth;
        if (titleRequired > requiredWidth)
        {
            requiredWidth = titleRequired;
        }
    }

    // Set a minimum width for UI aesthetics
    if (requiredWidth < 200.0f) requiredWidth = 200.0f;

    int w = (int)std::ceil(requiredWidth);
    int h = 100;


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
        return;
    }

    SetWindowDisplayAffinitySafe(win->GetHWND());

    win->ApplySystemBackdrop();
    win->EnsureD2D();

    ShowWindow(win->GetHWND(), SW_SHOW);
    UpdateWindow(win->GetHWND());
    SetForegroundWindow(win->GetHWND());
    SetFocus(win->GetHWND());

    // Spawn the background worker thread
    std::thread t([win, worker]() {
        if (worker) {
            worker();
        }
        win->m_finished = true;
        PostMessageW(win->GetHWND(), WM_CLOSE, 0, 0);
    });

    // Run a modal message loop
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

    // Join the worker thread to ensure it's fully done
    if (t.joinable()) {
        t.join();
    }

    delete win;

    if (parent)
    {
        EnableWindow(parent, TRUE);
        SetForegroundWindow(parent);
    }
}

LRESULT WaitWindow::HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CREATE:
    {
        EnsureD2D();
        m_timerId = SetTimer(hWnd, 1, 30, nullptr); // ~33 FPS
        return GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
    }
    case WM_TIMER:
    {
        if (wParam == 1)
        {
            m_angle += 8.0f;
            if (m_angle >= 360.0f) m_angle -= 360.0f;
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
    {
        if (m_finished)
        {
            if (UIStyle::Animation::IsEnabled() && m_animState != AnimState::Closing)
            {
                StartCloseTransition([hWnd]() {
                    DestroyWindow(hWnd);
                });
            }
            else
            {
                DestroyWindow(hWnd);
            }
        }
        return 0; // Prevent manual close (like Alt+F4) unless task is finished
    }
    case WM_LBUTTONDOWN:
    {
        SetFocus(hWnd);
        ReleaseCapture();
        SendMessageW(hWnd, WM_SYSCOMMAND, SC_MOVE | HTCAPTION, 0);
        return 0;
    }
    case WM_SHOWWINDOW:
    {
        return GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
    }
    case WM_DESTROY:
    {
        if (m_timerId)
        {
            KillTimer(hWnd, m_timerId);
            m_timerId = 0;
        }
        PostThreadMessageW(GetCurrentThreadId(), WM_NULL, 0, 0);
        break;
    }
    }
    return GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
}

void WaitWindow::EnsureFonts()
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
}

void WaitWindow::OnPaintContent(ID2D1HwndRenderTarget* rt)
{
    EnsureFonts();

    RECT cr;
    GetClientRect(GetHWND(), &cr);
    float scale = GetWindowScale(GetHWND());
    float w = (float)cr.right / scale;
    float h = (float)cr.bottom / scale;

    // 1. Header Title (optional)
    bool hasTitle = !m_title.empty();
    if (hasTitle && m_tfTitle)
    {
        auto titleBrush = GetOrCreateBrush(UIStyle::ThemeColor::TextNormal().d2d);
        if (titleBrush)
        {
            rt->DrawTextW(m_title.c_str(), (UINT32)m_title.size(), m_tfTitle.Get(),
                D2D1::RectF(15, 8, 200, 28), titleBrush.Get());
        }
    }

    // 2. Spinning Loading Animation
    float cx = 35.0f;
    float cy = hasTitle ? 58.0f : (h / 2.0f);
    float r_ring = 12.0f;
    float r_dot = 2.5f;

    D2D1_COLOR_F baseColor = UIStyle::ThemeColor::Accent().d2d;

    for (int i = 0; i < 8; ++i)
    {
        // Compute angle for this dot
        float dotAngle = i * 45.0f + m_angle;
        float rad = dotAngle * 3.14159265f / 180.0f;

        float x = cx + r_ring * std::cos(rad);
        float y = cy + r_ring * std::sin(rad);

        // Opacity gradient for fading tail effect
        float opacity = 0.2f + 0.8f * ((float)i / 7.0f);
        D2D1_COLOR_F dotColor = baseColor;
        dotColor.a = opacity;

        auto dotBrush = GetOrCreateBrush(dotColor);
        if (dotBrush)
        {
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), r_dot, r_dot), dotBrush.Get());
        }
    }

    // 3. Prompt Text
    if (m_tfPrompt)
    {
        D2D1_COLOR_F promptColor = UIStyle::ThemeColor::TextNormal().d2d;
        promptColor.a = 0.8f;
        auto promptBrush = GetOrCreateBrush(promptColor);
        if (promptBrush)
        {
            float textLeft = cx + r_ring + 15.0f;
            float textTop = hasTitle ? 32.0f : 10.0f;
            float textBottom = h - 10.0f;
            rt->DrawTextW(m_prompt.c_str(), (UINT32)m_prompt.size(), m_tfPrompt.Get(),
                D2D1::RectF(textLeft, textTop, w - 15.0f, textBottom), promptBrush.Get());
        }
    }
}
