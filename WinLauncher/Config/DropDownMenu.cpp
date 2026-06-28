#define NOMINMAX
#include "DropDownMenu.h"
#include "../DpiHelper.h"
#include <windowsx.h>
#include "UIStyle.h"

DropDownMenu* DropDownMenu::s_instance = nullptr;
HWND DropDownMenu::s_hMainWnd = nullptr;
AppContext* DropDownMenu::s_ctx = nullptr;

DropDownMenu::DropDownMenu(AppContext* ctx, const std::vector<Item>& items)
    : m_items(items)
    , m_hovered(-1)
{
    m_appCtx = ctx;
}

DropDownMenu::~DropDownMenu()
{
}

void DropDownMenu::Show(HWND parent, POINT pt, const std::vector<Item>& items, AppContext* ctx)
{
    Hide();

    s_hMainWnd = parent;
    s_ctx = ctx;

    if (items.empty()) return;

    s_instance = new DropDownMenu(s_ctx, items);

    const float itemH = 26.0f;
    const float pad = 6.0f;

    float maxW = 80.0f;
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
        itemW += 24.0f;
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

    if (s_instance->m_dw && !s_instance->m_tfMenu)
    {
        UIStyle::Typography::CreateTextFormat(
            s_instance->m_dw.Get(),
            &s_instance->m_tfMenu,
            12.0f,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_TEXT_ALIGNMENT_CENTER,
            DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    s_instance->m_hovered = -1;

    SetWindowPos(s_instance->GetHWND(), HWND_TOPMOST, pt.x, pt.y, w_px, h_px, SWP_NOACTIVATE);
    ShowWindow(s_instance->GetHWND(), SW_SHOW);
    SetForegroundWindow(s_instance->GetHWND());

    s_instance->CaptureBackground();
    s_instance->CompositeBackgroundToCache();
    InvalidateRect(s_instance->GetHWND(), nullptr, FALSE);
}

void DropDownMenu::Hide()
{
    if (s_instance)
    {
        HWND parent = GetParent(s_instance->GetHWND());
        DropDownMenu* inst = s_instance;
        s_instance = nullptr;
        HWND h = inst->GetHWND();
        if (h) DestroyWindow(h);
        delete inst;
        if (parent)
            InvalidateRect(parent, nullptr, FALSE);
    }
}

bool DropDownMenu::IsVisible()
{
    return s_instance != nullptr;
}

int DropDownMenu::HitTest(POINT pt)
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

void DropDownMenu::OnPaintContent(ID2D1HwndRenderTarget* rt)
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

        if (m_tfMenu)
        {
            auto tb = GetOrCreateBrush(UIStyle::ThemeColor::TextNormal().d2d);
            if (tb)
            {
                rt->DrawTextW(m_items[i].text.c_str(), (UINT32)m_items[i].text.size(),
                              m_tfMenu.Get(), itemRect, tb.Get());
            }
        }
    }
}

LRESULT DropDownMenu::HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
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
            Hide();
            if (callback) callback();
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
        GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
        return 0;
    }
    return GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
}
