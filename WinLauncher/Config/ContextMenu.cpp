#define NOMINMAX
#include "ContextMenu.h"
#include "../DpiHelper.h"
#include <windowsx.h>
#include "UIStyle.h"

ContextMenu* ContextMenu::s_instance = nullptr;
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

void ContextMenu::Show(HWND parent, POINT pt, const std::vector<Item>& items, AppContext* ctx)
{
    Hide();

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

    SetWindowDisplayAffinity(s_instance->GetHWND(), WDA_MONITOR | 0x10);
    s_instance->ApplySystemBackdrop();
    s_instance->EnsureD2D();

    // Create the left-aligned text format
    if (s_instance->m_dw && !s_instance->m_tfMenu)
    {
        s_instance->m_dw->CreateTextFormat(L"Microsoft YaHei UI", nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 12, L"", &s_instance->m_tfMenu);
        if (s_instance->m_tfMenu)
        {
            s_instance->m_tfMenu->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            s_instance->m_tfMenu->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }

    s_instance->m_hovered = -1;

    SetWindowPos(s_instance->GetHWND(), HWND_TOPMOST, pt.x, pt.y, w_px, h_px, SWP_NOACTIVATE);
    ShowWindow(s_instance->GetHWND(), SW_SHOW);
    SetForegroundWindow(s_instance->GetHWND());

    s_instance->CaptureBackground();
    s_instance->CompositeBackgroundToCache();
    InvalidateRect(s_instance->GetHWND(), nullptr, FALSE);
}

void ContextMenu::Hide()
{
    if (s_instance)
    {
        ContextMenu* inst = s_instance;
        s_instance = nullptr;
        HWND h = inst->GetHWND();
        if (h) DestroyWindow(h);
        delete inst;
    }
}

bool ContextMenu::IsVisible()
{
    return s_instance != nullptr;
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
        D2D1_RECT_F itemRect = D2D1::RectF(pad, pad + i * itemH, w - pad, pad + (i + 1) * itemH - 2.0f);
        D2D1_ROUNDED_RECT roundedItem = D2D1::RoundedRect(itemRect, 4.0f, 4.0f);

        bool isHovered = (i == m_hovered);

        auto bgBrush = GetOrCreateBrush(isHovered ? UIStyle::ThemeColor::ButtonBgHover().d2d : UIStyle::ThemeColor::ButtonBgNormal().d2d);
        if (bgBrush) rt->FillRoundedRectangle(roundedItem, bgBrush.Get());

        auto borderBrush = GetOrCreateBrush(isHovered ? UIStyle::ThemeColor::ButtonBorderHover().d2d : UIStyle::ThemeColor::ButtonBorderNormal().d2d);
        if (borderBrush) rt->DrawRoundedRectangle(roundedItem, borderBrush.Get(), UIStyle::Metrics::ControlStroke());

        IDWriteTextFormat* tf = m_tfMenu ? m_tfMenu.Get() : m_tf.Get();
        if (tf)
        {
            auto tb = GetOrCreateBrush(UIStyle::ThemeColor::TextNormal().d2d);
            if (tb)
            {
                // Left padding of 12.0f for a cleaner layout
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
        if (LOWORD(wParam) == WA_INACTIVE)
            Hide();
        return 0;

    case WM_MOUSEMOVE:
    {
        float scale = GetWindowScale(hWnd);
        POINT pt{ (int)(GET_X_LPARAM(lParam) / scale), (int)(GET_Y_LPARAM(lParam) / scale) };
        int h = HitTest(pt);
        if (h != m_hovered) { m_hovered = h; InvalidateRect(hWnd, nullptr, FALSE); }
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        float scale = GetWindowScale(hWnd);
        POINT pt{ (int)(GET_X_LPARAM(lParam) / scale), (int)(GET_Y_LPARAM(lParam) / scale) };
        int hit = HitTest(pt);
        if (hit >= 0 && hit < (int)m_items.size())
        {
            auto callback = m_items[hit].callback;
            Hide(); // Hide menu before callback triggers modal dialogs
            if (callback) callback();
        }
        return 0;
    }

    case WM_RBUTTONDOWN:
        // Hide context menu when right-clicked on context menu
        Hide();
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) Hide();
        return 0;

    case WM_DESTROY:
        GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
        return 0;
    }
    return GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
}
