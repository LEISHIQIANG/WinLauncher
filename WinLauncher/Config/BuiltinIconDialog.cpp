#define NOMINMAX
#include "BuiltinIconDialog.h"
#include "UIStyle.h"
#include "../DpiHelper.h"
#include "../UI/Controls/IconRenderer.h"
#include <windowsx.h>
#include <algorithm>
#include <cmath>

static const int DLG_W = 360;
static const int DLG_H = 400;

static const float Y_TITLE = 12.0f;
static const float Y_TABS  = 44.0f;
static const float Y_GRID  = 84.0f;
static const float Y_BOT   = 340.0f;

static BuiltinIconDialog* g_bidInstance = nullptr;

static const std::vector<BuiltinIconPreset> G_BUILTIN_PRESETS = {
    // ── 1. 系统 (Category 0) ──
    { L"控制面板", L"control.exe", L"", Model::ShortcutType::System, 0, L"" },
    { L"任务管理器", L"taskmgr.exe", L"", Model::ShortcutType::System, 0, L"" },
    { L"资源管理器", L"explorer.exe", L"", Model::ShortcutType::System, 0, L"" },
    { L"设备管理器", L"devmgmt.msc", L"", Model::ShortcutType::System, 0, L"" },
    { L"注册表编辑器", L"regedit.exe", L"", Model::ShortcutType::System, 0, L"" },
    { L"计算器", L"calc.exe", L"", Model::ShortcutType::System, 0, L"" },
    { L"记事本", L"notepad.exe", L"", Model::ShortcutType::System, 0, L"" },
    { L"命令行", L"cmd.exe", L"", Model::ShortcutType::System, 0, L"" },
    { L"PowerShell", L"powershell.exe", L"", Model::ShortcutType::System, 0, L"" },
    { L"屏幕键盘", L"osk.exe", L"", Model::ShortcutType::System, 0, L"" },
    { L"配置窗口", L":config_window", L"", Model::ShortcutType::System, 0, L"" },
    { L"窗口置顶", L":topmost_toggle", L"", Model::ShortcutType::System, 0, L"" },
    
    // ── 2. 网站 (Category 1) ──
    { L"百度", L"https://www.baidu.com", L"", Model::ShortcutType::Url, 1, L"" },
    { L"谷歌", L"https://www.google.com", L"", Model::ShortcutType::Url, 1, L"" },
    { L"GitHub", L"https://github.com", L"", Model::ShortcutType::Url, 1, L"" },
    { L"哔哩哔哩", L"https://www.bilibili.com", L"", Model::ShortcutType::Url, 1, L"" },
    { L"淘宝", L"https://www.taobao.com", L"", Model::ShortcutType::Url, 1, L"" },

    // ── 3. 网络 (Category 2) ──
    { L"Ping百度", L"ping www.baidu.com -t", L"cmd||||||1|||0|||300|||2000", Model::ShortcutType::Command, 2, L"" },
    { L"IP配置", L"ipconfig /all", L"cmd||||||1|||1|||300|||2000", Model::ShortcutType::Command, 2, L"" },

    // ── 4. 其他 (Category 3) ──
    { L"锁屏", L"rundll32.exe user32.dll,LockWorkStation", L"", Model::ShortcutType::Command, 3, L"" },
    { L"休眠", L"rundll32.exe powrprof.dll,SetSuspendState 0,1,0", L"", Model::ShortcutType::Command, 3, L"" },
    { L"清空回收站", L"wscript.exe", L"macro", Model::ShortcutType::Macro, 3, L"macro" }
};

struct BuiltinBrushCacheEntry
{
    D2D1_COLOR_F color;
    ComPtr<ID2D1SolidColorBrush> brush;
};
static std::vector<BuiltinBrushCacheEntry> g_builtinBrushCache;

static ComPtr<ID2D1SolidColorBrush> GetOrCreateDialogBrush(ID2D1HwndRenderTarget* rt, const D2D1_COLOR_F& color)
{
    for (auto& entry : g_builtinBrushCache)
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
            g_builtinBrushCache.push_back({ color, brush });
        }
    }
    return brush;
}

static std::wstring ResolveSystemPath(const std::wstring& target)
{
    if (target.find(L':') != std::wstring::npos || target.find(L"http") == 0)
        return target;

    wchar_t sysDir[MAX_PATH]{};
    GetSystemDirectoryW(sysDir, MAX_PATH);
    std::wstring sysPath = sysDir;

    wchar_t winDir[MAX_PATH]{};
    GetWindowsDirectoryW(winDir, MAX_PATH);
    std::wstring winPath = winDir;

    if (target == L"explorer.exe")
        return winPath + L"\\explorer.exe";
    if (target == L"regedit.exe")
        return winPath + L"\\regedit.exe";
    if (target == L"powershell.exe")
        return sysPath + L"\\WindowsPowerShell\\v1.0\\powershell.exe";

    return sysPath + L"\\" + target;
}

BuiltinIconDialog::BuiltinIconDialog(AppContext* ctx)
{
    m_appCtx = ctx;
}

BuiltinIconDialog::~BuiltinIconDialog()
{
    ReleasePresetBitmaps();
}

bool BuiltinIconDialog::Show(HWND parent, std::vector<RendShortcutInfo>& selectedIcons, AppContext* ctx)
{
    if (g_bidInstance) return false;

    BuiltinIconDialog* win = new BuiltinIconDialog(ctx);

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

    g_bidInstance = win;

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

    bool ok = win->m_okPressed;
    if (ok)
    {
        selectedIcons = win->m_selectedIcons;
    }

    g_bidInstance = nullptr;
    delete win;

    if (parent)
    {
        EnableWindow(parent, TRUE);
        SetForegroundWindow(parent);
    }

    g_builtinBrushCache.clear();
    return ok;
}

LRESULT BuiltinIconDialog::HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CREATE:
    {
        EnsureD2D();
        UpdateFilteredItems();
        return GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
    }

    case WM_DESTROY:
    {
        ReleasePresetBitmaps();
        g_builtinBrushCache.clear();
        PostThreadMessageW(GetCurrentThreadId(), WM_NULL, 0, 0);
        GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        float scale = DpiHelper::GetWindowScale(hWnd);
        pt.x = (int)(pt.x / scale);
        pt.y = (int)(pt.y / scale);

        if (pt.y < Y_TABS && !HitTestCloseButton(pt))
        {
            SetFocus(hWnd);
            ReleaseCapture();
            SendMessageW(hWnd, WM_SYSCOMMAND, SC_MOVE | HTCAPTION, 0);
            return 0;
        }

        if (HitTestCloseButton(pt))
        {
            PostMessageW(hWnd, WM_CLOSE, 0, 0);
            return 0;
        }

        if (HitTestCancelButton(pt))
        {
            PostMessageW(hWnd, WM_CLOSE, 0, 0);
            return 0;
        }

        if (HitTestOkButton(pt))
        {
            // Gather selected items
            m_selectedIcons.clear();
            for (size_t i = 0; i < m_presetStates.size(); ++i)
            {
                if (m_presetStates[i].selected)
                {
                    m_selectedIcons.push_back(m_filteredItems[i].info);
                }
            }
            if (!m_selectedIcons.empty())
            {
                m_okPressed = true;
                PostMessageW(hWnd, WM_CLOSE, 0, 0);
            }
            return 0;
        }

        int clickedTab = HitTestTab(pt);
        if (clickedTab >= 0 && clickedTab != m_activeTab)
        {
            m_activeTab = clickedTab;
            UpdateFilteredItems();
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        }

        int clickedPreset = HitTestIconCard(pt);
        if (clickedPreset >= 0)
        {
            bool ctrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            bool shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

            if (shiftPressed)
            {
                if (m_selectionAnchorIndex == -1)
                {
                    m_selectionAnchorIndex = clickedPreset;
                }
                for (auto& s : m_presetStates) s.selected = false;
                int start = std::min(m_selectionAnchorIndex, clickedPreset);
                int end = std::max(m_selectionAnchorIndex, clickedPreset);
                for (int i = start; i <= end; ++i)
                {
                    m_presetStates[i].selected = true;
                }
            }
            else if (ctrlPressed)
            {
                m_presetStates[clickedPreset].selected = !m_presetStates[clickedPreset].selected;
                if (m_presetStates[clickedPreset].selected)
                {
                    m_selectionAnchorIndex = clickedPreset;
                }
            }
            else
            {
                for (auto& s : m_presetStates) s.selected = false;
                m_presetStates[clickedPreset].selected = true;
                m_selectionAnchorIndex = clickedPreset;
            }
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        }

        // Click on empty grid area clears selection
        if (pt.x >= 20 && pt.x <= DLG_W - 20 && pt.y >= Y_GRID && pt.y <= Y_BOT)
        {
            for (auto& s : m_presetStates) s.selected = false;
            m_selectionAnchorIndex = -1;
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        float scale = DpiHelper::GetWindowScale(hWnd);
        pt.x = (int)(pt.x / scale);
        pt.y = (int)(pt.y / scale);

        if (!m_trackMouse)
        {
            TRACKMOUSEEVENT tme{ sizeof(tme) };
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hWnd;
            TrackMouseEvent(&tme);
            m_trackMouse = true;
        }

        bool repaint = false;
        auto update = [&](bool& state, bool newVal) {
            if (state != newVal) { state = newVal; repaint = true; }
        };
        auto updateInt = [&](int& state, int newVal) {
            if (state != newVal) { state = newVal; repaint = true; }
        };

        update(m_hoveredClose,  HitTestCloseButton(pt));
        update(m_hoveredOk,     HitTestOkButton(pt));
        update(m_hoveredCancel, HitTestCancelButton(pt));

        updateInt(m_hoveredTab, HitTestTab(pt));
        updateInt(m_hoveredPreset, HitTestIconCard(pt));

        if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }

    case WM_MOUSELEAVE:
    {
        m_trackMouse = false;
        m_hoveredClose = false;
        m_hoveredOk = false;
        m_hoveredCancel = false;
        m_hoveredTab = -1;
        m_hoveredPreset = -1;
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }

    case WM_MOUSEWHEEL:
    {
        POINT pt; GetCursorPos(&pt); ScreenToClient(hWnd, &pt);
        float scale = DpiHelper::GetWindowScale(hWnd);
        pt.x = (int)(pt.x / scale);
        pt.y = (int)(pt.y / scale);

        if (pt.x >= 20 && pt.x <= DLG_W - 20 && pt.y >= Y_GRID && pt.y <= Y_BOT)
        {
            short zDelta = (short)HIWORD(wParam);
            int n = (int)m_filteredItems.size();
            int rows = (n + 3) / 4;
            float maxScrollY = std::max(0.0f, (rows * 76.0f) - 240.0f);

            m_targetScrollY -= (zDelta / 120.0f) * 76.0f;
            m_targetScrollY = std::max(0.0f, std::min(m_targetScrollY, maxScrollY));

            if (m_targetScrollY != m_scrollY && !m_animating)
            {
                m_animating = true;
                // Animate scroll changes smoothly
                SetTimer(hWnd, 0x101, 16, nullptr);
            }
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_TIMER:
    {
        if (wParam == 0x101)
        {
            // Linear interpolation scroll animation
            float diff = m_targetScrollY - m_scrollY;
            if (std::abs(diff) < 0.5f)
            {
                m_scrollY = m_targetScrollY;
                m_animating = false;
                KillTimer(hWnd, 0x101);
            }
            else
            {
                m_scrollY += diff * 0.25f;
            }
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        }
        break;
    }

    default:
        break;
    }

    return GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
}

void BuiltinIconDialog::OnPaintContent(ID2D1HwndRenderTarget* rt)
{
    EnsureFonts();
    EnsurePresetBitmaps(rt);

    // 1. Draw Title
    if (m_tfTitle)
    {
        auto brush = GetOrCreateDialogBrush(rt, UIStyle::ThemeColor::TextNormal().d2d);
        if (brush)
        {
            rt->DrawTextW(L"内置图标", 4, m_tfTitle.Get(), D2D1::RectF(20.0f, Y_TITLE, 200.0f, Y_TITLE + 24.0f), brush.Get());
        }
    }

    // 2. Draw Close Button
    {
        float cx = DLG_W - 25.0f;
        float cy = Y_TITLE + 10.0f;
        D2D1_COLOR_F clr = m_hoveredClose ? UIStyle::ThemeColor::DangerRed().d2d : UIStyle::ThemeColor::TextMuted().d2d;
        auto clrBrush = GetOrCreateDialogBrush(rt, clr);
        if (clrBrush)
        {
            rt->DrawLine(D2D1::Point2F(cx - 5, cy - 5), D2D1::Point2F(cx + 5, cy + 5), clrBrush.Get(), 1.5f);
            rt->DrawLine(D2D1::Point2F(cx + 5, cy - 5), D2D1::Point2F(cx - 5, cy + 5), clrBrush.Get(), 1.5f);
        }
    }

    // 3. Draw Category Tabs
    if (m_tfTab)
    {
        std::vector<std::wstring> tabs = { L"系统", L"网站", L"网络", L"其他" };
        for (int i = 0; i < 4; ++i)
        {
            float tx1 = 20.0f + i * 80.0f;
            float tx2 = tx1 + 70.0f;
            D2D1_RECT_F tabRect = D2D1::RectF(tx1, Y_TABS, tx2, Y_TABS + 30.0f);

            bool isActive = (m_activeTab == i);
            bool isHovered = (m_hoveredTab == i);

            D2D1_COLOR_F textClr = isActive ? UIStyle::ThemeColor::Accent().d2d :
                                   (isHovered ? UIStyle::ThemeColor::TextNormal().d2d : UIStyle::ThemeColor::TextMuted().d2d);
            auto textB = GetOrCreateDialogBrush(rt, textClr);
            if (textB)
            {
                rt->DrawTextW(tabs[i].c_str(), (UINT32)tabs[i].size(), m_tfTab.Get(), tabRect, textB.Get());
            }

            if (isActive)
            {
                auto activeLineBrush = GetOrCreateDialogBrush(rt, UIStyle::ThemeColor::Accent().d2d);
                if (activeLineBrush)
                {
                    rt->DrawLine(D2D1::Point2F(tx1 + 5.0f, Y_TABS + 30.0f), D2D1::Point2F(tx2 - 5.0f, Y_TABS + 30.0f), activeLineBrush.Get(), 2.0f);
                }
            }
        }
    }

    // Draw Tab Separator line
    {
        D2D1_COLOR_F sepClr = UIStyle::ThemeColor::TextNormal().d2d; sepClr.a = 0.10f;
        auto sepBrush = GetOrCreateDialogBrush(rt, sepClr);
        if (sepBrush)
        {
            rt->DrawLine(D2D1::Point2F(20.0f, Y_TABS + 31.0f), D2D1::Point2F(DLG_W - 20.0f, Y_TABS + 31.0f), sepBrush.Get(), UIStyle::Metrics::ControlStroke());
            rt->DrawLine(D2D1::Point2F(20.0f, Y_BOT - 4.0f), D2D1::Point2F(DLG_W - 20.0f, Y_BOT - 4.0f), sepBrush.Get(), UIStyle::Metrics::ControlStroke());
        }
    }

    // 4. Draw Filtered Grid inside clipping region
    rt->PushAxisAlignedClip(D2D1::RectF(20.0f, Y_GRID, DLG_W - 20.0f, Y_BOT - 10.0f), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    {
        for (size_t i = 0; i < m_filteredItems.size(); ++i)
        {
            int col = (int)(i % 4);
            int row = (int)(i / 4);

            float cx = std::roundf(35.0f + col * 76.0f);
            float cy = std::roundf(Y_GRID + row * 76.0f - m_scrollY);

            D2D1_RECT_F cardRect = D2D1::RectF(cx, cy, cx + 62.0f, cy + 62.0f);
            D2D1_ROUNDED_RECT roundedCard = D2D1::RoundedRect(cardRect, 8.0f, 8.0f);

            bool isSelected = m_presetStates[i].selected;
            bool isHovered = (m_hoveredPreset == (int)i);

            D2D1_COLOR_F baseClr = UIStyle::ThemeColor::ThemeBase().d2d;
            float alphaBg = isHovered ? 0.105f : 0.035f;
            float alphaBorder = isHovered ? 0.18f : 0.065f;

            D2D1_COLOR_F selBg = UIStyle::ThemeColor::Accent().d2d;
            selBg.a = isHovered ? 0.20f : 0.13f;
            D2D1_COLOR_F normBg = baseClr;
            normBg.a = alphaBg;

            auto bgBrush = GetOrCreateDialogBrush(rt, isSelected ? selBg : normBg);
            if (bgBrush)
            {
                rt->FillRoundedRectangle(roundedCard, bgBrush.Get());
            }

            D2D1_COLOR_F selBorder = UIStyle::ThemeColor::Accent().d2d;
            selBorder.a = isHovered ? 0.42f : 0.30f;
            D2D1_COLOR_F normBorder = baseClr;
            normBorder.a = alphaBorder;

            auto borderBrush = GetOrCreateDialogBrush(rt, isSelected ? selBorder : normBorder);
            if (borderBrush)
            {
                rt->DrawRoundedRectangle(roundedCard, borderBrush.Get(), UIStyle::Metrics::ControlStroke());
            }

            // Draw Icon preview
            ID2D1Bitmap* bmp = m_presetBitmaps[m_filteredItems[i].originalPresetIndex];
            if (bmp)
            {
                float iconX = cx + 19.0f;
                float iconY = cy + 8.0f;
                D2D1_RECT_F iconRect = IconRenderer::AlignToPixels(rt, iconX, iconY, 24.0f, 24.0f);
                rt->DrawBitmap(bmp, iconRect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            }

            // Draw Name
            if (m_tfCardLabel)
            {
                auto nameBrush = GetOrCreateDialogBrush(rt, UIStyle::ThemeColor::TextNormal().d2d);
                if (nameBrush)
                {
                    std::wstring dispName = m_filteredItems[i].info.name;
                    if (dispName.size() > 6) dispName = dispName.substr(0, 5) + L"…";
                    D2D1_RECT_F nameRect = D2D1::RectF(cx + 1.0f, cy + 36.0f, cx + 61.0f, cy + 58.0f);
                    rt->DrawTextW(dispName.c_str(), (UINT32)dispName.size(), m_tfCardLabel.Get(), nameRect, nameBrush.Get());
                }
            }
        }
    }
    rt->PopAxisAlignedClip();

    // 5. Draw Buttons
    // Check if at least one selected to enable OK button
    bool hasSelection = false;
    for (const auto& s : m_presetStates)
    {
        if (s.selected) { hasSelection = true; break; }
    }
    DrawButton(rt, L"添加", D2D1::RectF(DLG_W - 176.0f, Y_BOT + 12.0f, DLG_W - 96.0f, Y_BOT + 36.0f), m_hoveredOk, hasSelection, true);
    DrawButton(rt, L"取消", D2D1::RectF(DLG_W - 88.0f, Y_BOT + 12.0f, DLG_W - 20.0f, Y_BOT + 36.0f), m_hoveredCancel, true, false);
}

void BuiltinIconDialog::EnsureFonts()
{
    if (m_tfTitle) return;
    UIStyle::Typography::CreateTextFormat(GetDWFactory(), &m_tfTitle, 13.0f, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    UIStyle::Typography::CreateTextFormat(GetDWFactory(), &m_tfBtn,   10.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    UIStyle::Typography::CreateTextFormat(GetDWFactory(), &m_tfTab,   11.0f, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    UIStyle::Typography::CreateTextFormat(GetDWFactory(), &m_tfCardLabel, 11.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

void BuiltinIconDialog::EnsurePresetBitmaps(ID2D1HwndRenderTarget* rt)
{
    if (m_bitmapsLoaded) return;

    m_presetBitmaps.resize(G_BUILTIN_PRESETS.size(), nullptr);
    for (size_t i = 0; i < G_BUILTIN_PRESETS.size(); ++i)
    {
        RendShortcutInfo sc;
        sc.name = G_BUILTIN_PRESETS[i].name;
        // Resolve system path dynamically for icons extraction
        sc.targetPath = ResolveSystemPath(G_BUILTIN_PRESETS[i].targetPath);
        sc.arguments = G_BUILTIN_PRESETS[i].arguments;
        sc.type = G_BUILTIN_PRESETS[i].type;
        sc.iconSource = Model::IconSource::Auto;
        sc.targetKind = ShortcutManager::InferTargetKind(sc.targetPath);

        sc.hIcon = ShortcutManager::GetShortcutIcon(sc);
        
        bool invert = (UIStyle::GetThemeMode() == UIStyle::ThemeMode::Light) ? sc.iconInvertLight : sc.iconInvertDark;
        m_presetBitmaps[i] = CreateD2DBitmapFromHicon(sc.hIcon, sc.name, invert);

        if (sc.hIcon)
        {
            DestroyIcon(sc.hIcon);
        }
    }
    m_bitmapsLoaded = true;
}

void BuiltinIconDialog::ReleasePresetBitmaps()
{
    for (auto* bmp : m_presetBitmaps)
    {
        if (bmp) bmp->Release();
    }
    m_presetBitmaps.clear();
    m_bitmapsLoaded = false;
}

void BuiltinIconDialog::UpdateFilteredItems()
{
    m_filteredItems.clear();
    for (size_t i = 0; i < G_BUILTIN_PRESETS.size(); ++i)
    {
        if (G_BUILTIN_PRESETS[i].category == m_activeTab)
        {
            FilteredItem item;
            item.originalPresetIndex = i;
            item.info.name = G_BUILTIN_PRESETS[i].name;
            item.info.targetPath = ResolveSystemPath(G_BUILTIN_PRESETS[i].targetPath);
            item.info.arguments = G_BUILTIN_PRESETS[i].arguments;
            item.info.type = G_BUILTIN_PRESETS[i].type;
            item.info.iconSource = Model::IconSource::Auto;
            item.info.targetKind = ShortcutManager::InferTargetKind(item.info.targetPath);
            m_filteredItems.push_back(item);
        }
    }

    m_presetStates.clear();
    m_presetStates.resize(m_filteredItems.size(), PresetVisualState{ false });

    // Reset scroll values
    m_scrollY = 0.0f;
    m_targetScrollY = 0.0f;
    m_scrollVelocity = 0.0f;
    m_animating = false;
    m_hoveredPreset = -1;
    m_selectionAnchorIndex = -1;

    HWND hWnd = GetHWND();
    if (hWnd) KillTimer(hWnd, 0x101);
}

ID2D1Bitmap* BuiltinIconDialog::CreateD2DBitmapFromHicon(HICON hIcon, const std::wstring& name, bool invert)
{
    ID2D1HwndRenderTarget* rt = GetRenderTarget();
    if (hIcon == nullptr)
    {
        auto bmp = IconRenderer::CreateDefaultIcon(rt, GetDWFactory(), name, 48);
        return bmp.Detach();
    }
    auto bmp = IconRenderer::HicontoD2D(rt, hIcon, 48, invert);
    return bmp.Detach();
}

bool BuiltinIconDialog::HitTestRect(POINT pt, const D2D1_RECT_F& rect)
{
    return (pt.x >= rect.left && pt.x <= rect.right && pt.y >= rect.top && pt.y <= rect.bottom);
}

bool BuiltinIconDialog::HitTestCloseButton(POINT pt)
{
    return (pt.x >= DLG_W - 35 && pt.x <= DLG_W - 15 && pt.y >= Y_TITLE && pt.y <= Y_TITLE + 20);
}

bool BuiltinIconDialog::HitTestOkButton(POINT pt)
{
    return HitTestRect(pt, D2D1::RectF(DLG_W - 176.0f, Y_BOT + 12.0f, DLG_W - 96.0f, Y_BOT + 36.0f));
}

bool BuiltinIconDialog::HitTestCancelButton(POINT pt)
{
    return HitTestRect(pt, D2D1::RectF(DLG_W - 88.0f, Y_BOT + 12.0f, DLG_W - 20.0f, Y_BOT + 36.0f));
}

int BuiltinIconDialog::HitTestTab(POINT pt)
{
    for (int i = 0; i < 4; ++i)
    {
        float tx1 = 20.0f + i * 80.0f;
        float tx2 = tx1 + 70.0f;
        if (pt.x >= tx1 && pt.x <= tx2 && pt.y >= Y_TABS && pt.y <= Y_TABS + 30.0f)
            return i;
    }
    return -1;
}

int BuiltinIconDialog::HitTestIconCard(POINT pt)
{
    if (pt.x < 20 || pt.x > DLG_W - 20 || pt.y < Y_GRID || pt.y > Y_BOT - 10)
        return -1;

    float scrolledY = pt.y - Y_GRID + m_scrollY;
    for (size_t i = 0; i < m_filteredItems.size(); ++i)
    {
        int col = (int)(i % 4);
        int row = (int)(i / 4);

        float cx = 35.0f + col * 76.0f;
        float cy = row * 76.0f;

        if (pt.x >= cx && pt.x <= cx + 62.0f && scrolledY >= cy && scrolledY <= cy + 62.0f)
            return (int)i;
    }
    return -1;
}

void BuiltinIconDialog::DrawButton(ID2D1HwndRenderTarget* rt, const wchar_t* text, const D2D1_RECT_F& rect, bool hovered, bool enabled, bool accent)
{
    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(rect, 4.0f, 4.0f);
    bool isLight = (UIStyle::GetThemeMode() == UIStyle::ThemeMode::Light);
    D2D1_COLOR_F base = isLight ? D2D1::ColorF(0.f, 0.f, 0.f) : D2D1::ColorF(1.f, 1.f, 1.f);

    if (!enabled)
    {
        float bgA = 0.02f;
        auto bgBrush = GetOrCreateDialogBrush(rt, D2D1::ColorF(base.r, base.g, base.b, bgA));
        if (bgBrush) rt->FillRoundedRectangle(rr, bgBrush.Get());

        auto borderBrush = GetOrCreateDialogBrush(rt, D2D1::ColorF(base.r, base.g, base.b, 0.05f));
        if (borderBrush) rt->DrawRoundedRectangle(rr, borderBrush.Get(), UIStyle::Metrics::ControlStroke());

        if (m_tfBtn)
        {
            auto txtBrush = GetOrCreateDialogBrush(rt, UIStyle::ThemeColor::TextMuted().d2d);
            if (txtBrush) rt->DrawTextW(text, (UINT32)wcslen(text), m_tfBtn.Get(), rect, txtBrush.Get());
        }
        return;
    }

    if (accent)
    {
        D2D1_COLOR_F bg = UIStyle::ThemeColor::Accent().d2d;
        bg.a = hovered ? 0.85f : 0.70f;
        auto bgBrush = GetOrCreateDialogBrush(rt, bg);
        if (bgBrush) rt->FillRoundedRectangle(rr, bgBrush.Get());

        if (m_tfBtn)
        {
            auto txtBrush = GetOrCreateDialogBrush(rt, UIStyle::ThemeColor::TextOnAccent().d2d);
            if (txtBrush) rt->DrawTextW(text, (UINT32)wcslen(text), m_tfBtn.Get(), rect, txtBrush.Get());
        }
    }
    else
    {
        float bgA = hovered ? 0.08f : 0.03f;
        float borderA = hovered ? 0.15f : 0.08f;

        auto bgBrush = GetOrCreateDialogBrush(rt, D2D1::ColorF(base.r, base.g, base.b, bgA));
        if (bgBrush) rt->FillRoundedRectangle(rr, bgBrush.Get());

        auto borderBrush = GetOrCreateDialogBrush(rt, D2D1::ColorF(base.r, base.g, base.b, borderA));
        if (borderBrush) rt->DrawRoundedRectangle(rr, borderBrush.Get(), UIStyle::Metrics::ControlStroke());

        if (m_tfBtn)
        {
            auto txtBrush = GetOrCreateDialogBrush(rt, UIStyle::ThemeColor::TextNormal().d2d);
            if (txtBrush) rt->DrawTextW(text, (UINT32)wcslen(text), m_tfBtn.Get(), rect, txtBrush.Get());
        }
    }
}
