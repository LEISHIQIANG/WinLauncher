#pragma once
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl.h>
#include <string>
#include <thread>
#include <atomic>
#include "TextBox.h"
#include "../Model/ShortcutInfo.h"

using Microsoft::WRL::ComPtr;

struct UrlEditFormResult
{
    std::wstring name;
    std::wstring url;
    std::wstring browserPath;
    std::wstring browserArgs;
    std::wstring iconPath;
    bool         iconInvertLight = false;
    bool         iconInvertDark = false;
};

struct UrlEditFormInitParams
{
    std::wstring name;
    std::wstring url;
    std::wstring browserPath;
    std::wstring browserArgs;
    std::wstring iconPath;
    bool         iconInvertLight = false;
    bool         iconInvertDark = false;
};

class UrlEditForm
{
public:
    UrlEditForm();
    ~UrlEditForm();

    static constexpr float PreferredContentHeight() { return 348.0f; }

    bool Create(HWND parentHWND, IDWriteFactory* dwriteFactory, const D2D1_RECT_F& logicalBounds, const UrlEditFormInitParams& init);
    void Destroy();

    void Paint(ID2D1HwndRenderTarget* rt, float scale);
    void UpdateLayout(const D2D1_RECT_F& logicalBounds, float scale);

    // Event handlers
    void OnMouseMove(HWND hWnd, POINT pt, float scale, bool& repaint);
    void OnLButtonDown(HWND hWnd, POINT pt, float scale, bool& repaint);
    void OnLButtonUp(HWND hWnd, POINT pt, float scale, bool& repaint);
    void OnChar(HWND hWnd, WPARAM wParam, bool& repaint);
    void OnKeyDown(HWND hWnd, WPARAM wParam, LPARAM lParam, bool& repaint);
    void BlinkCaret();

    // Focus helpers
    bool IsInputFocused() const;
    void ResetFocus();

    // Data exchange
    UrlEditFormResult GetResult() const;
    bool Validate(HWND hWnd);

private:
    void EnsureFonts(IDWriteFactory* dwriteFactory);
    void BrowseBrowserFile(HWND hWnd);
    void ClearBrowserSettings();
    void BrowseIconFile(HWND hWnd);
    void ClearIcon();

    // Async Latency and Favicon Fetchers
    void TestLatencyAsync();
    void FetchFaviconAsync();

    // Hit-testing helpers (logical coordinates)
    bool HitTestRect(POINT pt, const D2D1_RECT_F& rect);
    bool HitTestTestLatencyButton(POINT pt);
    bool HitTestBrowseBrowserButton(POINT pt);
    bool HitTestClearBrowserButton(POINT pt);
    bool HitTestBrowseIconButton(POINT pt);
    bool HitTestAutoIconButton(POINT pt);
    bool HitTestClearIconButton(POINT pt);
    bool HitTestInvertLightCheckbox(POINT pt);
    bool HitTestInvertDarkCheckbox(POINT pt);

    // Drawing helpers
    void DrawSectionLabel(ID2D1HwndRenderTarget* rt, const wchar_t* text, const D2D1_RECT_F& rect);
    void DrawButton(ID2D1HwndRenderTarget* rt, const wchar_t* text, const D2D1_RECT_F& rect, bool hovered, bool enabled = true);
    void DrawCheckbox(ID2D1HwndRenderTarget* rt, const D2D1_RECT_F& rect, bool checked, bool hovered);
    void DrawIconPreview(ID2D1HwndRenderTarget* rt);
    HICON GetFileIconForPreview(const std::wstring& path);

    HWND            m_parentHWND = nullptr;
    D2D1_RECT_F     m_bounds = {};
    UrlEditFormInitParams m_init;

    // UI Controls
    TextBox m_nameBox;
    TextBox m_urlBox;
    TextBox m_browserBox;
    TextBox m_argsBox;
    TextBox m_iconBox;
    TextBox* m_focusedBox = nullptr;

    bool m_iconInvertLight = false;
    bool m_iconInvertDark = false;

    // Hover states
    bool m_hoveredTestLatency   = false;
    bool m_hoveredBrowseBrowser = false;
    bool m_hoveredClearBrowser  = false;
    bool m_hoveredBrowseIcon    = false;
    bool m_hoveredAutoIcon      = false;
    bool m_hoveredClearIcon     = false;
    bool m_hoveredInvertLight   = false;
    bool m_hoveredInvertDark    = false;

    // Latency status state
    enum class LatencyState { Unchecked, Checking, CheckedOk, CheckedError };
    std::atomic<LatencyState> m_latencyState{ LatencyState::Unchecked };
    std::wstring m_latencyResultStr;
    int m_latencyMs = -1;

    // Auto Favicon state
    std::atomic<bool> m_fetchingFavicon{ false };
    std::wstring m_faviconResultStr;

    // Icon preview cache
    HICON        m_previewIcon = nullptr;
    ID2D1Bitmap* m_previewBitmap = nullptr;
    std::wstring m_lastPreviewName;
    ID2D1Factory* m_d2dFactoryCache = nullptr;

    // Fonts
    ComPtr<IDWriteTextFormat> m_tfLabel;
    ComPtr<IDWriteTextFormat> m_tfBtn;
    ComPtr<IDWriteTextFormat> m_tfSmall;
};
