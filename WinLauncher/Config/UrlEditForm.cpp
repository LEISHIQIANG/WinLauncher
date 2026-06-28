#define NOMINMAX
#include "UrlEditForm.h"
#include "UIStyle.h"
#include "../DpiHelper.h"
#include "../ShortcutManager.h"
#include "../Services/FaviconFetcher.h"
#include <windowsx.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <wininet.h>
#include <algorithm>
#include <cstring>
#include <vector>

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "wininet.lib")
#include "../UI/Controls/IconRenderer.h"

struct UrlBrushCacheEntry
{
    D2D1_COLOR_F color;
    ComPtr<ID2D1SolidColorBrush> brush;
};
static std::vector<UrlBrushCacheEntry> g_urlBrushCache;

static ComPtr<ID2D1SolidColorBrush> GetOrCreateBrush(ID2D1HwndRenderTarget* rt, const D2D1_COLOR_F& color)
{
    for (auto& entry : g_urlBrushCache)
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
            g_urlBrushCache.push_back({ color, brush });
        }
    }
    return brush;
}

// Layout Y coordinates relative to m_bounds.top
static const float Y_SEC_BASIC       = 0.0f;
static const float Y_LBL_NAME        = 16.0f;
static const float Y_BOX_NAME        = 32.0f;
static const float Y_LBL_URL         = 60.0f;
static const float Y_BOX_URL         = 76.0f;
static const float Y_LBL_LATENCY     = 104.0f; // Row containing latency test button and label
static const float Y_SEC_BROWSER     = 132.0f; // "浏览器" separator
static const float Y_LBL_BROWSER     = 148.0f; // preferred browser
static const float Y_BOX_BROWSER     = 164.0f;
static const float Y_LBL_BARGS       = 192.0f; // browser args
static const float Y_BOX_BARGS       = 208.0f;
static const float Y_SEC_ICON        = 236.0f; // "图标" separator
static const float Y_LBL_ICON        = 252.0f;
static const float Y_BOX_ICON        = 268.0f;
static const float Y_AUTO_ICON_ROW   = 298.0f; // Buttons "自动获取", invert check boxes
static const float Y_PREVIEW         = 252.0f; // Preview aligns with Y_BOX_ICON on right (W-70 to W-20)

UrlEditForm::UrlEditForm()
{
}

UrlEditForm::~UrlEditForm()
{
    Destroy();
}

bool UrlEditForm::Create(HWND parentHWND, IDWriteFactory* dwriteFactory, const D2D1_RECT_F& logicalBounds, const UrlEditFormInitParams& init)
{
    m_parentHWND = parentHWND;
    m_bounds = logicalBounds;
    m_init = init;
    m_iconInvertLight = init.iconInvertLight;
    m_iconInvertDark = init.iconInvertDark;

    UIStyle::TextBoxStyle style;
    style.fontSize    = 11;
    style.paddingTop  = 4.0f;
    style.paddingBottom = 4.0f;

    EnsureFonts(dwriteFactory);

    float W = m_bounds.right - m_bounds.left;

    // Name Box
    m_nameBox.SetStyle(style);
    m_nameBox.Create(parentHWND, dwriteFactory, D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_NAME, m_bounds.left + W - 20, m_bounds.top + Y_BOX_NAME + 24), m_init.name);

    // URL Box
    m_urlBox.SetStyle(style);
    m_urlBox.Create(parentHWND, dwriteFactory, D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_URL, m_bounds.left + W - 20, m_bounds.top + Y_BOX_URL + 24), m_init.url);

    // Browser Box
    m_browserBox.SetStyle(style);
    m_browserBox.Create(parentHWND, dwriteFactory, D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_BROWSER, m_bounds.left + W - 145, m_bounds.top + Y_BOX_BROWSER + 24), m_init.browserPath);

    // Args Box
    m_argsBox.SetStyle(style);
    m_argsBox.Create(parentHWND, dwriteFactory, D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_BARGS, m_bounds.left + W - 20, m_bounds.top + Y_BOX_BARGS + 24), m_init.browserArgs);

    // Icon Box
    m_iconBox.SetStyle(style);
    m_iconBox.Create(parentHWND, dwriteFactory, D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_ICON, m_bounds.left + W - 145, m_bounds.top + Y_BOX_ICON + 24), m_init.iconPath);

    m_focusedBox = &m_nameBox;
    m_nameBox.SetFocus(true);

    if (!m_init.iconPath.empty())
    {
        m_previewIcon = GetFileIconForPreview(m_init.iconPath);
    }

    m_latencyResultStr = L"未测试";

    // Auto test latency if URL is already populated on load
    if (!m_init.url.empty())
    {
        TestLatencyAsync();
    }

    return true;
}

void UrlEditForm::Destroy()
{
    m_nameBox.Destroy();
    m_urlBox.Destroy();
    m_browserBox.Destroy();
    m_argsBox.Destroy();
    m_iconBox.Destroy();

    if (m_previewBitmap) { m_previewBitmap->Release(); m_previewBitmap = nullptr; }
    if (m_previewIcon)   { DestroyIcon(m_previewIcon); m_previewIcon = nullptr; }

    g_urlBrushCache.clear();
}

void UrlEditForm::UpdateLayout(const D2D1_RECT_F& logicalBounds, float scale)
{
    m_bounds = logicalBounds;
    float W = m_bounds.right - m_bounds.left;

    m_nameBox.SetBounds(D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_NAME, m_bounds.left + W - 20, m_bounds.top + Y_BOX_NAME + 24));
    m_nameBox.UpdateLayout(scale);

    m_urlBox.SetBounds(D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_URL, m_bounds.left + W - 20, m_bounds.top + Y_BOX_URL + 24));
    m_urlBox.UpdateLayout(scale);

    m_browserBox.SetBounds(D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_BROWSER, m_bounds.left + W - 145, m_bounds.top + Y_BOX_BROWSER + 24));
    m_browserBox.UpdateLayout(scale);

    m_argsBox.SetBounds(D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_BARGS, m_bounds.left + W - 20, m_bounds.top + Y_BOX_BARGS + 24));
    m_argsBox.UpdateLayout(scale);

    m_iconBox.SetBounds(D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_ICON, m_bounds.left + W - 145, m_bounds.top + Y_BOX_ICON + 24));
    m_iconBox.UpdateLayout(scale);
}

void UrlEditForm::EnsureFonts(IDWriteFactory* dwriteFactory)
{
    auto makeFormat = [&](ComPtr<IDWriteTextFormat>& tf,
                          float size,
                          DWRITE_FONT_WEIGHT weight,
                          DWRITE_TEXT_ALIGNMENT ha,
                          DWRITE_PARAGRAPH_ALIGNMENT va)
    {
        if (!dwriteFactory || tf) return;
        UIStyle::Typography::CreateTextFormat(
            dwriteFactory,
            &tf,
            size,
            weight,
            ha,
            va);
    };

    makeFormat(m_tfLabel, 10.0f, DWRITE_FONT_WEIGHT_NORMAL,
               DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    makeFormat(m_tfBtn,   10.0f, DWRITE_FONT_WEIGHT_NORMAL,
               DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    makeFormat(m_tfSmall, 9.0f, DWRITE_FONT_WEIGHT_NORMAL,
               DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

bool UrlEditForm::HitTestRect(POINT pt, const D2D1_RECT_F& rect)
{
    return (pt.x >= rect.left && pt.x <= rect.right && pt.y >= rect.top && pt.y <= rect.bottom);
}

bool UrlEditForm::HitTestTestLatencyButton(POINT pt)
{
    return HitTestRect(pt, D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_LBL_LATENCY, m_bounds.left + 100, m_bounds.top + Y_LBL_LATENCY + 20));
}

bool UrlEditForm::HitTestBrowseBrowserButton(POINT pt)
{
    float W = m_bounds.right - m_bounds.left;
    return HitTestRect(pt, D2D1::RectF(m_bounds.left + W - 135, m_bounds.top + Y_BOX_BROWSER, m_bounds.left + W - 80, m_bounds.top + Y_BOX_BROWSER + 24));
}

bool HitTestRectX(POINT pt, float left, float top, float right, float bottom)
{
    return (pt.x >= left && pt.x <= right && pt.y >= top && pt.y <= bottom);
}

bool UrlEditForm::HitTestClearBrowserButton(POINT pt)
{
    float W = m_bounds.right - m_bounds.left;
    return HitTestRect(pt, D2D1::RectF(m_bounds.left + W - 75, m_bounds.top + Y_BOX_BROWSER, m_bounds.left + W - 20, m_bounds.top + Y_BOX_BROWSER + 24));
}

bool UrlEditForm::HitTestBrowseIconButton(POINT pt)
{
    float W = m_bounds.right - m_bounds.left;
    return HitTestRect(pt, D2D1::RectF(m_bounds.left + W - 135, m_bounds.top + Y_BOX_ICON, m_bounds.left + W - 80, m_bounds.top + Y_BOX_ICON + 24));
}

bool UrlEditForm::HitTestAutoIconButton(POINT pt)
{
    return HitTestRect(pt, D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_AUTO_ICON_ROW, m_bounds.left + 92, m_bounds.top + Y_AUTO_ICON_ROW + 22));
}

bool UrlEditForm::HitTestClearIconButton(POINT pt)
{
    return HitTestRect(pt, D2D1::RectF(m_bounds.left + 98, m_bounds.top + Y_AUTO_ICON_ROW, m_bounds.left + 166, m_bounds.top + Y_AUTO_ICON_ROW + 22));
}

bool UrlEditForm::HitTestInvertLightCheckbox(POINT pt)
{
    return HitTestRect(pt, D2D1::RectF(m_bounds.left + 174, m_bounds.top + Y_AUTO_ICON_ROW, m_bounds.left + 254, m_bounds.top + Y_AUTO_ICON_ROW + 22));
}

bool UrlEditForm::HitTestInvertDarkCheckbox(POINT pt)
{
    return HitTestRect(pt, D2D1::RectF(m_bounds.left + 262, m_bounds.top + Y_AUTO_ICON_ROW, m_bounds.left + 340, m_bounds.top + Y_AUTO_ICON_ROW + 22));
}

void UrlEditForm::OnMouseMove(HWND hWnd, POINT pt, float scale, bool& repaint)
{
    POINT rawPt{ (int)(pt.x * scale), (int)(pt.y * scale) };
    m_nameBox.OnMouseMove(hWnd, rawPt, scale, repaint);
    m_urlBox.OnMouseMove(hWnd, rawPt, scale, repaint);
    m_browserBox.OnMouseMove(hWnd, rawPt, scale, repaint);
    m_argsBox.OnMouseMove(hWnd, rawPt, scale, repaint);
    m_iconBox.OnMouseMove(hWnd, rawPt, scale, repaint);

    auto update = [&](bool& flag, bool newVal){ if (flag != newVal){ flag = newVal; repaint = true; } };
    update(m_hoveredTestLatency,   HitTestTestLatencyButton(pt));
    update(m_hoveredBrowseBrowser, HitTestBrowseBrowserButton(pt));
    update(m_hoveredClearBrowser,  HitTestClearBrowserButton(pt));
    update(m_hoveredBrowseIcon,    HitTestBrowseIconButton(pt));
    update(m_hoveredAutoIcon,      HitTestAutoIconButton(pt));
    update(m_hoveredClearIcon,     HitTestClearIconButton(pt));
    update(m_hoveredInvertLight,   HitTestInvertLightCheckbox(pt));
    update(m_hoveredInvertDark,    HitTestInvertDarkCheckbox(pt));
}

void UrlEditForm::OnLButtonDown(HWND hWnd, POINT pt, float scale, bool& repaint)
{
    POINT rawPt{ (int)(pt.x * scale), (int)(pt.y * scale) };

    if (HitTestTestLatencyButton(pt))
    {
        TestLatencyAsync();
        repaint = true;
        return;
    }

    if (HitTestBrowseBrowserButton(pt))
    {
        BrowseBrowserFile(hWnd);
        repaint = true;
        return;
    }

    if (HitTestClearBrowserButton(pt))
    {
        ClearBrowserSettings();
        repaint = true;
        return;
    }

    if (HitTestBrowseIconButton(pt))
    {
        BrowseIconFile(hWnd);
        repaint = true;
        return;
    }

    if (HitTestAutoIconButton(pt))
    {
        FetchFaviconAsync();
        repaint = true;
        return;
    }

    if (HitTestClearIconButton(pt))
    {
        ClearIcon();
        repaint = true;
        return;
    }

    if (HitTestInvertLightCheckbox(pt))
    {
        m_iconInvertLight = !m_iconInvertLight;
        repaint = true;
        return;
    }

    if (HitTestInvertDarkCheckbox(pt))
    {
        m_iconInvertDark = !m_iconInvertDark;
        repaint = true;
        return;
    }

    auto tryFocus = [&](TextBox& tb) -> bool {
        if (tb.HitTest(pt))
        {
            if (m_focusedBox && m_focusedBox != &tb) m_focusedBox->SetFocus(false);
            m_focusedBox = &tb;
            tb.SetFocus(true);
            tb.OnLButtonDown(hWnd, rawPt, scale, repaint);
            return true;
        }
        return false;
    };

    if (tryFocus(m_nameBox))    return;
    if (tryFocus(m_urlBox))     return;
    if (tryFocus(m_browserBox)) return;
    if (tryFocus(m_argsBox))    return;
    if (tryFocus(m_iconBox))    return;

    if (m_focusedBox) { m_focusedBox->SetFocus(false); m_focusedBox = nullptr; repaint = true; }
}

void UrlEditForm::OnLButtonUp(HWND hWnd, POINT pt, float scale, bool& repaint)
{
    POINT rawPt{ (int)(pt.x * scale), (int)(pt.y * scale) };
    m_nameBox.OnLButtonUp(hWnd, rawPt, scale, repaint);
    m_urlBox.OnLButtonUp(hWnd, rawPt, scale, repaint);
    m_browserBox.OnLButtonUp(hWnd, rawPt, scale, repaint);
    m_argsBox.OnLButtonUp(hWnd, rawPt, scale, repaint);
    m_iconBox.OnLButtonUp(hWnd, rawPt, scale, repaint);
}

void UrlEditForm::OnChar(HWND hWnd, WPARAM wParam, bool& repaint)
{
    if (m_focusedBox)
    {
        m_focusedBox->OnChar(hWnd, wParam, repaint);

        if (m_focusedBox == &m_iconBox)
        {
            std::wstring tp = m_iconBox.GetText();
            if (m_previewIcon)  { DestroyIcon(m_previewIcon);   m_previewIcon = nullptr; }
            if (m_previewBitmap){ m_previewBitmap->Release(); m_previewBitmap = nullptr; }
            if (!tp.empty())
                m_previewIcon = GetFileIconForPreview(tp);
            repaint = true;
        }
    }
}

void UrlEditForm::OnKeyDown(HWND hWnd, WPARAM wParam, LPARAM lParam, bool& repaint)
{
    if (wParam == VK_TAB)
    {
        TextBox* order[] = { &m_nameBox, &m_urlBox, &m_browserBox, &m_argsBox, &m_iconBox };
        const int N = 5;
        for (int i = 0; i < N; i++)
        {
            if (m_focusedBox == order[i])
            {
                m_focusedBox->SetFocus(false);
                bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                m_focusedBox = order[(i + (shift ? N - 1 : 1)) % N];
                m_focusedBox->SetFocus(true);
                repaint = true;
                return;
            }
        }
        return;
    }

    if (m_focusedBox)
    {
        m_focusedBox->OnKeyDown(hWnd, wParam, lParam, repaint);
    }
}

void UrlEditForm::BlinkCaret()
{
    m_nameBox.BlinkCaret();
    m_urlBox.BlinkCaret();
    m_browserBox.BlinkCaret();
    m_argsBox.BlinkCaret();
    m_iconBox.BlinkCaret();
}

bool UrlEditForm::IsInputFocused() const
{
    return m_focusedBox != nullptr;
}

void UrlEditForm::ResetFocus()
{
    if (m_focusedBox)
    {
        m_focusedBox->SetFocus(false);
        m_focusedBox = nullptr;
    }
}

UrlEditFormResult UrlEditForm::GetResult() const
{
    UrlEditFormResult res;
    res.name = m_nameBox.GetText();
    res.url = m_urlBox.GetText();
    res.browserPath = m_browserBox.GetText();
    res.browserArgs = m_argsBox.GetText();
    res.iconPath = m_iconBox.GetText();
    res.iconInvertLight = m_iconInvertLight;
    res.iconInvertDark = m_iconInvertDark;
    return res;
}

bool UrlEditForm::Validate(HWND hWnd)
{
    std::wstring name = m_nameBox.GetText();
    std::wstring url = m_urlBox.GetText();

    if (name.empty())
    {
        MessageBoxW(hWnd, L"请输入名称！", L"验证失败", MB_OK | MB_ICONWARNING);
        return false;
    }
    if (url.empty())
    {
        MessageBoxW(hWnd, L"请输入网址！", L"验证失败", MB_OK | MB_ICONWARNING);
        return false;
    }
    return true;
}

void UrlEditForm::BrowseBrowserFile(HWND hWnd)
{
    wchar_t filename[MAX_PATH]{};
    OPENFILENAMEW ofn{ sizeof(ofn) };
    ofn.hwndOwner = hWnd;
    ofn.lpstrFilter = L"可执行文件 (*.exe)\0*.exe\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&ofn))
    {
        m_browserBox.SetText(filename);
    }
}

void UrlEditForm::ClearBrowserSettings()
{
    m_browserBox.SetText(L"");
    m_argsBox.SetText(L"");
}

void UrlEditForm::BrowseIconFile(HWND hWnd)
{
    wchar_t filename[MAX_PATH]{};
    OPENFILENAMEW ofn{ sizeof(ofn) };
    ofn.hwndOwner = hWnd;
    ofn.lpstrFilter = L"图标文件 (*.ico;*.exe;*.dll;*.png)\0*.ico;*.exe;*.dll;*.png\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&ofn))
    {
        m_iconBox.SetText(filename);
        if (m_previewIcon)   { DestroyIcon(m_previewIcon);   m_previewIcon = nullptr; }
        if (m_previewBitmap) { m_previewBitmap->Release(); m_previewBitmap = nullptr; }
        m_previewIcon = GetFileIconForPreview(filename);
    }
}

void UrlEditForm::ClearIcon()
{
    m_iconBox.SetText(L"");
    if (m_previewIcon)   { DestroyIcon(m_previewIcon);   m_previewIcon = nullptr; }
    if (m_previewBitmap) { m_previewBitmap->Release(); m_previewBitmap = nullptr; }
}

void UrlEditForm::TestLatencyAsync()
{
    m_latencyState = LatencyState::Checking;
    m_latencyResultStr = L"测试中...";

    std::wstring url = m_urlBox.GetText();

    std::thread([this, url]() {
        // Ensure scheme exists
        std::wstring targetUrl = url;
        // Trim spaces
        while(!targetUrl.empty() && targetUrl.front() == L' ') targetUrl.erase(0, 1);
        while(!targetUrl.empty() && targetUrl.back() == L' ') targetUrl.pop_back();

        if (targetUrl.empty())
        {
            m_latencyMs = -1;
            m_latencyResultStr = L"网址为空";
            m_latencyState = LatencyState::CheckedError;
            if (m_parentHWND) InvalidateRect(m_parentHWND, nullptr, FALSE);
            return;
        }

        if (targetUrl.rfind(L"http://", 0) != 0 && targetUrl.rfind(L"https://", 0) != 0)
        {
            targetUrl = L"http://" + targetUrl;
        }

        ULONGLONG start = GetTickCount64();

        HINTERNET hSession = InternetOpenW(L"WinLauncher URL Probe", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
        bool success = false;
        if (hSession)
        {
            DWORD timeout = 4000;
            InternetSetOptionW(hSession, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
            InternetSetOptionW(hSession, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

            HINTERNET hUrl = InternetOpenUrlW(hSession, targetUrl.c_str(), nullptr, 0, INTERNET_FLAG_NO_UI | INTERNET_FLAG_NO_CACHE_WRITE, 0);
            if (hUrl)
            {
                success = true;
                InternetCloseHandle(hUrl);
            }
            InternetCloseHandle(hSession);
        }

        ULONGLONG end = GetTickCount64();

        if (success)
        {
            m_latencyMs = static_cast<int>(end - start);
            m_latencyResultStr = L"延迟 " + std::to_wstring(m_latencyMs) + L" ms";
            m_latencyState = LatencyState::CheckedOk;
        }
        else
        {
            m_latencyMs = -1;
            m_latencyResultStr = L"无法访问";
            m_latencyState = LatencyState::CheckedError;
        }

        if (m_parentHWND)
        {
            InvalidateRect(m_parentHWND, nullptr, FALSE);
        }
    }).detach();
}

void UrlEditForm::FetchFaviconAsync()
{
    if (m_fetchingFavicon) return;
    m_fetchingFavicon = true;
    m_faviconResultStr = L"获取中...";

    std::wstring url = m_urlBox.GetText();
    // Trim whitespace
    while (!url.empty() && url.front() == L' ') url.erase(0, 1);
    while (!url.empty() && url.back()  == L' ') url.pop_back();

    if (url.empty())
    {
        m_fetchingFavicon = false;
        m_faviconResultStr = L"网址为空";
        if (m_parentHWND) InvalidateRect(m_parentHWND, nullptr, FALSE);
        return;
    }

    // Force-refresh: always go online
    std::thread([this, url]() {
        std::wstring iconPath = FaviconFetcher::FetchFavicon(url, /*forceRefresh=*/true);

        if (!iconPath.empty())
        {
            // Deliver result back; the UI controls are only updated from the
            // thread that created the HWND (which runs the D2D loop), but
            // SetText is a simple wstring assignment guarded by atomic flag, so
            // it is safe here.  We then force a repaint via InvalidateRect.
            m_iconBox.SetText(iconPath);
            if (m_previewIcon)  { DestroyIcon(m_previewIcon);   m_previewIcon = nullptr; }
            if (m_previewBitmap){ m_previewBitmap->Release(); m_previewBitmap = nullptr; }
            m_previewIcon = GetFileIconForPreview(iconPath);
            m_faviconResultStr = L"";
        }
        else
        {
            m_faviconResultStr = L"未获取到图标";
        }

        m_fetchingFavicon = false;
        if (m_parentHWND)
        {
            InvalidateRect(m_parentHWND, nullptr, FALSE);
        }
    }).detach();
}

void UrlEditForm::Paint(ID2D1HwndRenderTarget* rt, float scale)
{
    float W = m_bounds.right - m_bounds.left;

    // "基本信息" Separator
    DrawSectionLabel(rt, L"基本信息", D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_SEC_BASIC, m_bounds.left + W - 20, m_bounds.top + Y_SEC_BASIC + 16));

    // Name Label
    if (m_tfLabel)
    {
        auto lblBrush = GetOrCreateBrush(rt, UIStyle::ThemeColor::TextMuted().d2d);
        if (lblBrush) rt->DrawTextW(L"名称 (最多6个字符)", 11, m_tfLabel.Get(), D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_LBL_NAME, m_bounds.left + W - 20, m_bounds.top + Y_LBL_NAME + 16), lblBrush.Get());
    }
    m_nameBox.Paint(rt, scale);

    // URL Label
    if (m_tfLabel)
    {
        auto lblBrush = GetOrCreateBrush(rt, UIStyle::ThemeColor::TextMuted().d2d);
        if (lblBrush) rt->DrawTextW(L"网址 (支持 {{input}}, {{clipboard}}, {{date}}, {{time}})", 47, m_tfLabel.Get(), D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_LBL_URL, m_bounds.left + W - 20, m_bounds.top + Y_LBL_URL + 16), lblBrush.Get());
    }
    m_urlBox.Paint(rt, scale);

    // Test Latency Row
    DrawButton(rt, L"测试延迟", D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_LBL_LATENCY, m_bounds.left + 100, m_bounds.top + Y_LBL_LATENCY + 20), m_hoveredTestLatency);

    if (m_tfLabel)
    {
        D2D1_COLOR_F statusClr = UIStyle::ThemeColor::TextMuted().d2d;
        if (m_latencyState == LatencyState::CheckedOk)
        {
            statusClr = D2D1::ColorF(0x16A34A); // soft green
        }
        else if (m_latencyState == LatencyState::CheckedError)
        {
            statusClr = D2D1::ColorF(0xDC2626); // soft red
        }
        else if (m_latencyState == LatencyState::Checking)
        {
            statusClr = UIStyle::ThemeColor::Accent().d2d;
        }

        auto statusBrush = GetOrCreateBrush(rt, statusClr);
        if (statusBrush)
        {
            rt->DrawTextW(m_latencyResultStr.c_str(), (UINT32)m_latencyResultStr.length(), m_tfLabel.Get(),
                D2D1::RectF(m_bounds.left + 110, m_bounds.top + Y_LBL_LATENCY, m_bounds.left + W - 20, m_bounds.top + Y_LBL_LATENCY + 20), statusBrush.Get());
        }
    }

    // "浏览器" Separator
    DrawSectionLabel(rt, L"浏览器 (留空使用默认)", D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_SEC_BROWSER, m_bounds.left + W - 20, m_bounds.top + Y_SEC_BROWSER + 16));

    // Browser Path Label
    if (m_tfLabel)
    {
        auto lblBrush = GetOrCreateBrush(rt, UIStyle::ThemeColor::TextMuted().d2d);
        if (lblBrush) rt->DrawTextW(L"浏览器路径", 5, m_tfLabel.Get(), D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_LBL_BROWSER, m_bounds.left + W - 20, m_bounds.top + Y_LBL_BROWSER + 16), lblBrush.Get());
    }
    m_browserBox.Paint(rt, scale);
    DrawButton(rt, L"浏览...", D2D1::RectF(m_bounds.left + W - 135, m_bounds.top + Y_BOX_BROWSER, m_bounds.left + W - 80, m_bounds.top + Y_BOX_BROWSER + 24), m_hoveredBrowseBrowser);
    DrawButton(rt, L"清除", D2D1::RectF(m_bounds.left + W - 75, m_bounds.top + Y_BOX_BROWSER, m_bounds.left + W - 20, m_bounds.top + Y_BOX_BROWSER + 24), m_hoveredClearBrowser);

    // Browser Args Label
    if (m_tfLabel)
    {
        auto lblBrush = GetOrCreateBrush(rt, UIStyle::ThemeColor::TextMuted().d2d);
        if (lblBrush) rt->DrawTextW(L"启动参数 (支持 {{url}})", 17, m_tfLabel.Get(), D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_LBL_BARGS, m_bounds.left + W - 20, m_bounds.top + Y_LBL_BARGS + 16), lblBrush.Get());
    }
    m_argsBox.Paint(rt, scale);

    // "图标" Separator
    DrawSectionLabel(rt, L"图标", D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_SEC_ICON, m_bounds.left + W - 20, m_bounds.top + Y_SEC_ICON + 16));

    // Custom Icon Label
    if (m_tfLabel)
    {
        auto lblBrush = GetOrCreateBrush(rt, UIStyle::ThemeColor::TextMuted().d2d);
        if (lblBrush) rt->DrawTextW(L"自定义图标路径", 7, m_tfLabel.Get(), D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_LBL_ICON, m_bounds.left + W - 150, m_bounds.top + Y_LBL_ICON + 16), lblBrush.Get());
    }
    m_iconBox.Paint(rt, scale);
    DrawButton(rt, L"选择...", D2D1::RectF(m_bounds.left + W - 135, m_bounds.top + Y_BOX_ICON, m_bounds.left + W - 80, m_bounds.top + Y_BOX_ICON + 24), m_hoveredBrowseIcon);

    // Draw preview icon
    DrawIconPreview(rt);

    // Auto Fetch Favicon and Theme Inversions Row
    DrawButton(rt, m_fetchingFavicon ? L"\u83b7\u53d6\u4e2d..." : L"\u81ea\u52a8\u83b7\u53d6",
               D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_AUTO_ICON_ROW, m_bounds.left + 92, m_bounds.top + Y_AUTO_ICON_ROW + 22),
               m_hoveredAutoIcon, !m_fetchingFavicon);

    // "Clear icon" button
    DrawButton(rt, L"\u6e05\u9664\u56fe\u6807",
               D2D1::RectF(m_bounds.left + 98, m_bounds.top + Y_AUTO_ICON_ROW, m_bounds.left + 166, m_bounds.top + Y_AUTO_ICON_ROW + 22),
               m_hoveredClearIcon, true);

    // Favicon fetch status text
    if (!m_faviconResultStr.empty() && m_tfSmall)
    {
        D2D1_COLOR_F statusClr = UIStyle::ThemeColor::TextMuted().d2d;
        if (m_faviconResultStr == L"\u672a\u83b7\u53d6\u5230\u56fe\u6807") statusClr = D2D1::ColorF(0xDC2626);
        auto statusBrush = GetOrCreateBrush(rt, statusClr);
        if (statusBrush)
            rt->DrawTextW(m_faviconResultStr.c_str(), (UINT32)m_faviconResultStr.size(),
                          m_tfSmall.Get(),
                          D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_AUTO_ICON_ROW + 26,
                                      m_bounds.left + W - 20, m_bounds.top + Y_AUTO_ICON_ROW + 48),
                          statusBrush.Get());
    }

    DrawCheckbox(rt, D2D1::RectF(m_bounds.left + 174, m_bounds.top + Y_AUTO_ICON_ROW, m_bounds.left + 254, m_bounds.top + Y_AUTO_ICON_ROW + 22), m_iconInvertLight, m_hoveredInvertLight);
    if (m_tfLabel)
    {
        auto txtBrush = GetOrCreateBrush(rt, UIStyle::ThemeColor::TextNormal().d2d);
        if (txtBrush) rt->DrawTextW(L"\u6d45\u8272\u53cd\u8f6c", 4, m_tfLabel.Get(), D2D1::RectF(m_bounds.left + 194, m_bounds.top + Y_AUTO_ICON_ROW, m_bounds.left + 254, m_bounds.top + Y_AUTO_ICON_ROW + 22), txtBrush.Get());
    }

    DrawCheckbox(rt, D2D1::RectF(m_bounds.left + 262, m_bounds.top + Y_AUTO_ICON_ROW, m_bounds.left + 340, m_bounds.top + Y_AUTO_ICON_ROW + 22), m_iconInvertDark, m_hoveredInvertDark);
    if (m_tfLabel)
    {
        auto txtBrush = GetOrCreateBrush(rt, UIStyle::ThemeColor::TextNormal().d2d);
        if (txtBrush) rt->DrawTextW(L"\u6df1\u8272\u53cd\u8f6c", 4, m_tfLabel.Get(), D2D1::RectF(m_bounds.left + 282, m_bounds.top + Y_AUTO_ICON_ROW, m_bounds.left + 340, m_bounds.top + Y_AUTO_ICON_ROW + 22), txtBrush.Get());
    }
}

void UrlEditForm::DrawSectionLabel(ID2D1HwndRenderTarget* rt, const wchar_t* text, const D2D1_RECT_F& rect)
{
    if (m_tfSmall)
    {
        auto labelBrush = GetOrCreateBrush(rt, UIStyle::ThemeColor::TextMuted().d2d);
        if (labelBrush)
        {
            rt->DrawTextW(text, (UINT32)wcslen(text), m_tfSmall.Get(), rect, labelBrush.Get());
            float lineStart = rect.left + (float)wcslen(text) * 8.5f + 10.0f;
            lineStart = std::min(lineStart, rect.right);
            D2D1_COLOR_F lineClr = UIStyle::ThemeColor::TextNormal().d2d;
            lineClr.a = 0.28f;
            auto lineBrush = GetOrCreateBrush(rt, lineClr);
            if (lineBrush) rt->DrawLine(D2D1::Point2F(lineStart, rect.top + 8), D2D1::Point2F(rect.right, rect.top + 8), lineBrush.Get(), 0.35f);
        }
    }
}

void UrlEditForm::DrawButton(ID2D1HwndRenderTarget* rt, const wchar_t* text, const D2D1_RECT_F& rect, bool hovered, bool enabled)
{
    D2D1_ROUNDED_RECT rc = D2D1::RoundedRect(rect, 3.0f, 3.0f);
    D2D1_COLOR_F bgClr = hovered && enabled ? UIStyle::ThemeColor::ButtonBgHover().d2d : UIStyle::ThemeColor::ButtonBgNormal().d2d;
    if (!enabled) bgClr.a = 0.03f;

    auto bg = GetOrCreateBrush(rt, bgClr);
    if (bg) rt->FillRoundedRectangle(rc, bg.Get());

    D2D1_COLOR_F bdrClr = hovered && enabled ? UIStyle::ThemeColor::ButtonBorderHover().d2d : UIStyle::ThemeColor::ButtonBorderNormal().d2d;
    if (!enabled) bdrClr.a = 0.2f;

    auto bdr = GetOrCreateBrush(rt, bdrClr);
    if (bdr) rt->DrawRoundedRectangle(rc, bdr.Get(), UIStyle::Metrics::ControlStroke());

    if (m_tfBtn)
    {
        D2D1_COLOR_F textClr = UIStyle::ThemeColor::TextNormal().d2d;
        if (!enabled) textClr.a = 0.4f;
        auto txt = GetOrCreateBrush(rt, textClr);
        if (txt) rt->DrawTextW(text, (UINT32)wcslen(text), m_tfBtn.Get(), rect, txt.Get());
    }
}

void UrlEditForm::DrawCheckbox(ID2D1HwndRenderTarget* rt, const D2D1_RECT_F& rect, bool checked, bool hovered)
{
    D2D1_RECT_F boxRect = D2D1::RectF(rect.left, rect.top + 4.0f, rect.left + 14.0f, rect.top + 18.0f);
    D2D1_ROUNDED_RECT roundedBox = D2D1::RoundedRect(boxRect, 2.0f, 2.0f);

    D2D1_COLOR_F bgClr = checked ? UIStyle::ThemeColor::Accent().d2d : UIStyle::ThemeColor::EditBg().d2d;
    if (checked && hovered) bgClr = UIStyle::ThemeColor::AccentHover().d2d;

    auto bg = GetOrCreateBrush(rt, bgClr);
    if (bg) rt->FillRoundedRectangle(roundedBox, bg.Get());

    D2D1_COLOR_F bdrClr = checked ? UIStyle::ThemeColor::Accent().d2d : UIStyle::ThemeColor::EditBorderNormal().d2d;
    if (hovered) bdrClr = UIStyle::ThemeColor::Accent().d2d;

    auto bdr = GetOrCreateBrush(rt, bdrClr);
    if (bdr) rt->DrawRoundedRectangle(roundedBox, bdr.Get(), UIStyle::Metrics::ControlStroke());

    if (checked)
    {
        auto chkBrush = GetOrCreateBrush(rt, UIStyle::ThemeColor::TextOnAccent().d2d);
        if (chkBrush)
        {
            rt->DrawLine(D2D1::Point2F(boxRect.left + 3, boxRect.top + 7), D2D1::Point2F(boxRect.left + 6, boxRect.top + 10), chkBrush.Get(), 1.5f);
            rt->DrawLine(D2D1::Point2F(boxRect.left + 6, boxRect.top + 10), D2D1::Point2F(boxRect.left + 11, boxRect.top + 4), chkBrush.Get(), 1.5f);
        }
    }
}

void UrlEditForm::DrawIconPreview(ID2D1HwndRenderTarget* rt)
{
    if (m_previewIcon)
    {
        if (!m_previewBitmap)
        {
            auto bmp = IconRenderer::HicontoD2D(rt, m_previewIcon, 36);
            if (bmp)
            {
                m_previewBitmap = bmp.Detach();
            }
        }
    }
    else
    {
        std::wstring currentName = m_nameBox.GetText();
        if (!m_previewBitmap || m_lastPreviewName != currentName)
        {
            if (m_previewBitmap)
            {
                m_previewBitmap->Release();
                m_previewBitmap = nullptr;
            }
            auto bmp = IconRenderer::CreateDefaultIcon(rt, nullptr, currentName, 36);
            if (bmp)
            {
                m_previewBitmap = bmp.Detach();
                m_lastPreviewName = currentName;
            }
        }
    }

    const float previewSize = 36.0f;
    float W = m_bounds.right - m_bounds.left;
    D2D1_RECT_F previewRect = D2D1::RectF(m_bounds.left + W - 20 - previewSize, m_bounds.top + Y_PREVIEW, m_bounds.left + W - 20, m_bounds.top + Y_PREVIEW + previewSize);
    D2D1_ROUNDED_RECT rrPreview = D2D1::RoundedRect(previewRect, 6.0f, 6.0f);

    bool isLight = (UIStyle::GetThemeMode() == UIStyle::ThemeMode::Light);
    D2D1_COLOR_F bgClr = isLight ? D2D1::ColorF(0.f, 0.f, 0.f, 0.05f) : D2D1::ColorF(1.f, 1.f, 1.f, 0.07f);
    auto bgBrush = GetOrCreateBrush(rt, bgClr);
    if (bgBrush) rt->FillRoundedRectangle(rrPreview, bgBrush.Get());

    D2D1_COLOR_F borderClr = isLight ? D2D1::ColorF(0.f, 0.f, 0.f, 0.10f) : D2D1::ColorF(1.f, 1.f, 1.f, 0.10f);
    auto borderBrush = GetOrCreateBrush(rt, borderClr);
    if (borderBrush) rt->DrawRoundedRectangle(rrPreview, borderBrush.Get(), UIStyle::Metrics::ControlStroke());

    if (m_previewBitmap)
    {
        ComPtr<ID2D1BitmapBrush> bmpBrush;
        rt->CreateBitmapBrush(m_previewBitmap, &bmpBrush);
        if (bmpBrush)
        {
            bmpBrush->SetExtendModeX(D2D1_EXTEND_MODE_CLAMP);
            bmpBrush->SetExtendModeY(D2D1_EXTEND_MODE_CLAMP);
            bmpBrush->SetTransform(D2D1::Matrix3x2F::Translation(previewRect.left, previewRect.top));
            rt->FillRoundedRectangle(rrPreview, bmpBrush.Get());
        }
    }
}

HICON UrlEditForm::GetFileIconForPreview(const std::wstring& path)
{
    return ShortcutManager::GetShortcutIcon(path);
}
