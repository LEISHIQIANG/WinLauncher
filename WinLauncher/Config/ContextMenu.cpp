#define NOMINMAX
#include "ContextMenu.h"
#include "../App/CallbackGuard.h"
#include "../DpiHelper.h"
#include <windowsx.h>
#include "UIStyle.h"
#include <algorithm>

ContextMenu* ContextMenu::s_instance = nullptr;
std::vector<ContextMenu*> ContextMenu::s_closingInstances;
HWND ContextMenu::s_hMainWnd = nullptr;
AppContext* ContextMenu::s_ctx = nullptr;

ContextMenu::ContextMenu(AppContext* ctx, const std::vector<Item>& items)
    : m_items(items)
    , m_hovered(-1)
{
    m_appCtx = ctx;
}

ContextMenu::~ContextMenu()
{
}

void ContextMenu::Show(HWND parent, POINT pt, const std::vector<Item>& items, AppContext* ctx, float minWidth)
{
    CloseExisting(true);

    s_hMainWnd = parent;
    s_ctx = ctx;

    if (items.empty()) return;

    s_instance = new ContextMenu(s_ctx, items);

    const float itemH = 26.0f;
    const float pad = 6.0f;

    // Calculate dynamic width based on text length
    float maxW = 100.0f;
    for (const auto& item : items)
    {
        float itemW = 0.0f;
        for (wchar_t c : item.text)
        {
            if (c >= 0x4e00 && c <= 0x9fff)
                itemW += 13.0f;
            else
                itemW += 7.0f;
        }
        itemW += 32.0f; // Padding/margins
        if (itemW > maxW) maxW = itemW;
    }
    if (minWidth > maxW) maxW = minWidth;

    int w = (int)maxW;
    int h = (int)(pad * 2 + items.size() * itemH - 2.0f);

    MONITORINFO mi{ sizeof(mi) };
    HMONITOR hm = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    GetMonitorInfoW(hm, &mi);
    RECT wa = mi.rcWork;

    float scale = DpiHelper::GetDpiScaleForMonitor(hm);
    int w_px = (int)(w * scale);
    int h_px = (int)(h * scale);

    // Keep menu inside monitor work area
    if (pt.x + w_px > wa.right) pt.x = wa.right - w_px;
    if (pt.y + h_px > wa.bottom) pt.y = wa.bottom - h_px;
    if (pt.x < wa.left) pt.x = wa.left;
    if (pt.y < wa.top) pt.y = wa.top;

    s_instance->Create(L"", WS_POPUP, WS_EX_TOOLWINDOW | WS_EX_TOPMOST, pt.x, pt.y, w_px, h_px, parent);
    if (!s_instance->GetHWND())
    {
        delete s_instance;
        s_instance = nullptr;
        return;
    }

    SetWindowDisplayAffinitySafe(s_instance->GetHWND());
    s_instance->ApplySystemBackdrop();
    if (s_instance->EnsureD2D() && s_instance->m_rt)
    {
        s_instance->m_rt->SetDpi(scale * 96.0f, scale * 96.0f);
        UIStyle::Typography::ApplyRenderTargetTextDefaults(s_instance->m_rt.Get());
    }

    // Create the left-aligned text format
    if (s_instance->m_dw && !s_instance->m_tfMenu)
    {
        UIStyle::Typography::CreateTextFormat(
            s_instance->m_dw.Get(),
            &s_instance->m_tfMenu,
            12.0f,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_TEXT_ALIGNMENT_LEADING,
            DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    s_instance->m_hovered = -1;

    SetWindowPos(s_instance->GetHWND(), HWND_TOPMOST, pt.x, pt.y, w_px, h_px, SWP_NOACTIVATE);
    s_instance->RevealAfterFirstPaint();
    SetForegroundWindow(s_instance->GetHWND());
    s_instance->CaptureMouse();
}

void ContextMenu::Hide()
{
    CloseExisting(false);
}

void ContextMenu::DestroyInstance(ContextMenu* inst)
{
    if (!inst)
        return;

    HWND h = inst->GetHWND();
    inst->ReleaseMouseCapture();
    if (h && IsWindow(h))
        DestroyWindow(h);
    delete inst;
}

void ContextMenu::RemoveClosingInstance(ContextMenu* inst)
{
    s_closingInstances.erase(
        std::remove(s_closingInstances.begin(), s_closingInstances.end(), inst),
        s_closingInstances.end());
}

void ContextMenu::CloseExisting(bool immediate)
{
    if (immediate)
    {
        if (s_instance)
        {
            ContextMenu* inst = s_instance;
            s_instance = nullptr;
            DestroyInstance(inst);
        }

        auto closingInstances = s_closingInstances;
        s_closingInstances.clear();
        for (ContextMenu* inst : closingInstances)
        {
            DestroyInstance(inst);
        }
        return;
    }

    if (s_instance)
    {
        ContextMenu* inst = s_instance;
        s_instance = nullptr;
        if (UIStyle::Animation::IsEnabled())
        {
            s_closingInstances.push_back(inst);
            inst->ReleaseMouseCapture();
            inst->StartCloseTransition([inst]() {
                RemoveClosingInstance(inst);
                DestroyInstance(inst);
            });
        }
        else
        {
            inst->ReleaseMouseCapture();
            DestroyInstance(inst);
        }
    }
}

bool ContextMenu::IsVisible()
{
    return s_instance != nullptr;
}

bool ContextMenu::IsInsideClient(POINT pt) const
{
    HWND h = GetHWND();
    if (!h)
        return false;

    RECT cr{};
    GetClientRect(h, &cr);
    float scale = GetWindowScale(h);
    int right = (int)((float)cr.right / scale);
    int bottom = (int)((float)cr.bottom / scale);
    return pt.x >= 0 && pt.x < right && pt.y >= 0 && pt.y < bottom;
}

void ContextMenu::CaptureMouse()
{
    HWND h = GetHWND();
    if (!h || !IsWindow(h))
        return;

    SetCapture(h);
    m_mouseCaptured = (GetCapture() == h);
}

void ContextMenu::ReleaseMouseCapture()
{
    HWND h = GetHWND();
    if (m_mouseCaptured && h && GetCapture() == h)
    {
        m_mouseCaptured = false;
        ReleaseCapture();
        return;
    }
    m_mouseCaptured = false;
}

int ContextMenu::HitTest(POINT pt)
{
    const float itemH = 26.0f;
    const float pad = 6.0f;
    RECT cr;
    GetClientRect(GetHWND(), &cr);
    float scale = GetWindowScale(GetHWND());
    float w = (float)cr.right / scale;

    for (int i = 0; i < (int)m_items.size(); i++)
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

void ContextMenu::OnPaintContent(ID2D1HwndRenderTarget* rt)
{
    RECT cr;
    GetClientRect(GetHWND(), &cr);
    float scale = GetWindowScale(GetHWND());
    float w = (float)cr.right / scale;

    const float itemH = 26.0f;
    const float pad = 6.0f;

    for (int i = 0; i < (int)m_items.size(); i++)
    {
        bool isDisabled = m_items[i].disabled;
        bool isHovered = (i == m_hovered) && !isDisabled;

        D2D1_RECT_F itemRect = D2D1::RectF(pad, pad + i * itemH, w - pad, pad + (i + 1) * itemH - 2.0f);
        D2D1_ROUNDED_RECT roundedItem = D2D1::RoundedRect(itemRect, 4.0f, 4.0f);

        // Background: no highlight for disabled items
        D2D1_COLOR_F bgColor = UIStyle::ThemeColor::ButtonBgNormal().d2d;
        if (isHovered) bgColor = UIStyle::ThemeColor::ButtonBgHover().d2d;
        auto bgBrush = GetOrCreateBrush(bgColor);
        if (bgBrush) rt->FillRoundedRectangle(roundedItem, bgBrush.Get());

        // Border
        D2D1_COLOR_F borderColor = isHovered
            ? UIStyle::ThemeColor::ButtonBorderHover().d2d
            : UIStyle::ThemeColor::ButtonBorderNormal().d2d;
        auto borderBrush = GetOrCreateBrush(borderColor);
        if (borderBrush) rt->DrawRoundedRectangle(roundedItem, borderBrush.Get(), UIStyle::Metrics::ControlStroke());

        // Text: dimmed for disabled items
        IDWriteTextFormat* tf = m_tfMenu ? m_tfMenu.Get() : m_tf.Get();
        if (tf)
        {
            D2D1_COLOR_F textColor = UIStyle::ThemeColor::TextNormal().d2d;
            if (isDisabled) textColor.a *= 0.35f; // dim to 35% alpha
            auto tb = GetOrCreateBrush(textColor);
            if (tb)
            {
                D2D1_RECT_F textRect = D2D1::RectF(itemRect.left + 12.0f, itemRect.top, itemRect.right, itemRect.bottom);
                rt->DrawTextW(m_items[i].text.c_str(), (UINT32)m_items[i].text.size(), tf, textRect, tb.Get());
            }
        }
    }
}

LRESULT ContextMenu::HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_ACTIVATE:
        GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
        if (LOWORD(wParam) == WA_INACTIVE)
            Hide();
        return 0;

    case WM_CAPTURECHANGED:
        if (m_mouseCaptured && reinterpret_cast<HWND>(lParam) != hWnd)
        {
            m_mouseCaptured = false;
            Hide();
        }
        return 0;

    case WM_MOUSEMOVE:
    {
        float scale = GetWindowScale(hWnd);
        POINT pt{ (int)(GET_X_LPARAM(lParam) / scale), (int)(GET_Y_LPARAM(lParam) / scale) };
        if (!IsInsideClient(pt))
        {
            if (m_hovered != -1) { m_hovered = -1; InvalidateRect(hWnd, nullptr, FALSE); }
            return 0;
        }
        int h = HitTest(pt);
        // Don't highlight disabled items
        if (h >= 0 && h < (int)m_items.size() && m_items[h].disabled)
            h = -1;
        if (h != m_hovered) { m_hovered = h; InvalidateRect(hWnd, nullptr, FALSE); }
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        float scale = GetWindowScale(hWnd);
        POINT pt{ (int)(GET_X_LPARAM(lParam) / scale), (int)(GET_Y_LPARAM(lParam) / scale) };
        if (!IsInsideClient(pt))
        {
            Hide();
            return 0;
        }
        int hit = HitTest(pt);
        if (hit >= 0 && hit < (int)m_items.size() && !m_items[hit].disabled)
        {
            auto callback = m_items[hit].callback;
            Hide(); // Hide menu before callback triggers modal dialogs
            if (callback) CallbackGuard::Invoke(Logger::GetDefault(), L"context_menu", callback);
        }
        return 0;
    }

    case WM_RBUTTONDOWN:
        Hide();
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) Hide();
        return 0;

    case WM_DESTROY:
        ReleaseMouseCapture();
        GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
        return 0;
    }
    return GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
}
