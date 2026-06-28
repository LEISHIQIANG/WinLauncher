/**
 * ToastWindow.cpp
 *
 * 屏幕中央短暂提示，继承 GlassWindow，与主弹窗外观完全一致。
 * 窗口宽高根据文字测量结果动态计算，四边各留 10 逻辑像素边距。
 */
#define NOMINMAX
#include "ToastWindow.h"
#include "DpiHelper.h"
#include "Config/UIStyle.h"
#include <algorithm>

ToastWindow* ToastWindow::s_instance = nullptr;

// ============================================================
// Public API
// ============================================================

void ToastWindow::Show(const std::wstring& message, DWORD durationMs)
{
    Hide(); // 关掉已有的

    // --- 计算文字尺寸（用临时 DWrite 工厂，无需窗口就绪）---
    const float PAD_LOGICAL = 12.0f;   // 四边各留的逻辑像素

    // 以主显示器 DPI 换算
    HMONITOR hm = MonitorFromPoint({ GetSystemMetrics(SM_CXSCREEN) / 2,
                                     GetSystemMetrics(SM_CYSCREEN) / 2 },
                                   MONITOR_DEFAULTTOPRIMARY);
    float scale = DpiHelper::GetDpiScaleForMonitor(hm);

    // 字体大小（与主弹窗正文保持一致）
    const float fontSize = 13.0f;

    // 用 DWrite 测量文字宽高（logical px）
    float textW = 0.0f, textH = 0.0f;
    {
        IDWriteFactory* dwf = nullptr;
        if (SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                          __uuidof(IDWriteFactory),
                                          reinterpret_cast<IUnknown**>(&dwf))) && dwf)
        {
            IDWriteTextFormat* fmt = nullptr;
            dwf->CreateTextFormat(L"Segoe UI", nullptr,
                                  DWRITE_FONT_WEIGHT_NORMAL,
                                  DWRITE_FONT_STYLE_NORMAL,
                                  DWRITE_FONT_STRETCH_NORMAL,
                                  fontSize, L"", &fmt);
            if (fmt)
            {
                IDWriteTextLayout* layout = nullptr;
                dwf->CreateTextLayout(message.c_str(), (UINT32)message.size(),
                                      fmt, 1000.0f, 100.0f, &layout);
                if (layout)
                {
                    DWRITE_TEXT_METRICS m{};
                    layout->GetMetrics(&m);
                    textW = m.width;
                    textH = m.height;
                    layout->Release();
                }
                fmt->Release();
            }
            dwf->Release();
        }
    }

    // 窗口逻辑尺寸 = 文字 + 边距
    float logW = textW + PAD_LOGICAL * 2.0f;
    float logH = textH + PAD_LOGICAL * 2.0f;

    // 转物理像素
    int W = (int)std::max(logW * scale, 60.0f);
    int H = (int)std::max(logH * scale, 24.0f);

    // 居中于主显示器工作区
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(hm, &mi);
    RECT wa = mi.rcWork;
    int x = (wa.left + wa.right  - W) / 2;
    int y = (wa.top  + wa.bottom - H) / 2;

    // 创建窗口
    s_instance = new ToastWindow(message);
    s_instance->Create(L"", WS_POPUP,
                       WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                       x, y, W, H, nullptr);

    if (!s_instance->GetHWND())
    {
        delete s_instance;
        s_instance = nullptr;
        return;
    }

    SetWindowDisplayAffinitySafe(s_instance->GetHWND());
    s_instance->ApplySystemBackdrop();
    s_instance->EnsureD2D();

    SetWindowPos(s_instance->GetHWND(), HWND_TOPMOST, x, y, W, H, SWP_NOACTIVATE);
    ShowWindow(s_instance->GetHWND(), SW_SHOWNOACTIVATE);

    s_instance->CaptureBackground();
    s_instance->CompositeBackgroundToCache();
    InvalidateRect(s_instance->GetHWND(), nullptr, FALSE);

    // 定时自动关闭
    SetTimer(s_instance->GetHWND(), TIMER_CLOSE, durationMs, nullptr);
}

void ToastWindow::Hide()
{
    if (s_instance)
    {
        ToastWindow* inst = s_instance;
        s_instance = nullptr;
        HWND h = inst->GetHWND();
        if (h && IsWindow(h))
        {
            KillTimer(h, TIMER_CLOSE);
            DestroyWindow(h);
        }
        delete inst;
    }
}

// ============================================================
// Paint
// ============================================================

void ToastWindow::OnPaintContent(ID2D1HwndRenderTarget* rt)
{
    RECT cr;
    GetClientRect(GetHWND(), &cr);
    float scale = GetWindowScale(GetHWND());
    float W = (float)cr.right  / scale;
    float H = (float)cr.bottom / scale;

    // 直接用文字颜色绘制，居中显示
    if (m_tf)
    {
        D2D1_RECT_F rc = D2D1::RectF(0.0f, 0.0f, W, H);
        auto brush = GetOrCreateBrush(UIStyle::ThemeColor::TextNormal().d2d);
        if (brush)
            rt->DrawTextW(m_message.c_str(), (UINT32)m_message.size(),
                          m_tf.Get(), rc, brush.Get(),
                          D2D1_DRAW_TEXT_OPTIONS_NONE,
                          DWRITE_MEASURING_MODE_NATURAL);
    }
}

// ============================================================
// Message handling
// ============================================================

LRESULT ToastWindow::HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_TIMER:
        if (wParam == TIMER_CLOSE)
        {
            Hide();   // 自我销毁
            return 0;
        }
        break;

    case WM_DESTROY:
        // 确保定时器清理
        KillTimer(hWnd, TIMER_CLOSE);
        GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
        return 0;
    }
    return GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
}
