#pragma once
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl.h>
#include <string>
#include <vector>
#include "TextBox.h"
#include "../Model/ShortcutInfo.h"
#include "../Services/MacroService.h"

using Microsoft::WRL::ComPtr;

struct AppContext;

struct MacroEditFormResult
{
    std::wstring name;
    std::wstring arguments; // speed|trigger_mode|events_serialized
    std::wstring iconPath;
    bool         iconInvertLight = false;
    bool         iconInvertDark = false;
};

struct MacroEditFormInitParams
{
    std::wstring name;
    std::wstring arguments;
    std::wstring iconPath;
    bool         iconInvertLight = false;
    bool         iconInvertDark = false;
};

class MacroEditForm
{
public:
    MacroEditForm();
    ~MacroEditForm();

    static constexpr float PreferredContentHeight() { return 378.0f; }

    bool Create(HWND parentHWND, IDWriteFactory* dwriteFactory, const D2D1_RECT_F& logicalBounds, const MacroEditFormInitParams& init, AppContext* ctx = nullptr);
    void Destroy();

    void Paint(ID2D1HwndRenderTarget* rt, float scale);
    void UpdateLayout(const D2D1_RECT_F& logicalBounds, float scale);

    // Event handlers
    void OnMouseMove(HWND hWnd, POINT pt, float scale, bool& repaint);
    void OnLButtonDown(HWND hWnd, POINT pt, float scale, bool& repaint);
    void OnLButtonDblClk(HWND hWnd, POINT pt, float scale, bool& repaint);
    void OnLButtonUp(HWND hWnd, POINT pt, float scale, bool& repaint);
    void OnChar(HWND hWnd, WPARAM wParam, bool& repaint);
    void OnKeyDown(HWND hWnd, WPARAM wParam, LPARAM lParam, bool& repaint);

    // Callbacks from Dialog
    bool HandleHookMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, bool& repaint);

    void BlinkCaret();

    // Focus helpers
    bool IsInputFocused() const;
    void ResetFocus();
    bool HandleImeMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, bool& repaint);
    void OnMouseWheel(HWND hWnd, short zDelta, POINT pt, float scale, bool& repaint);

    // Data validation and results
    MacroEditFormResult GetResult() const;
    bool Validate(HWND hWnd);

private:
    void EnsureFonts(IDWriteFactory* dwriteFactory);
    void BrowseIconFile(HWND hWnd);
    void RefreshScriptText(HWND hWnd);

    bool HitTestRect(POINT pt, const D2D1_RECT_F& rect);
    bool HitTestRecordButton(POINT pt);
    bool HitTestStopButton(POINT pt);
    bool HitTestClearButton(POINT pt);
    bool HitTestPlayButton(POINT pt);
    bool HitTestBrowseIconButton(POINT pt);
    bool HitTestAfterCloseCheckbox(POINT pt);
    int  HitTestSpeedButton(POINT pt); // returns index 0-3 or -1
    bool HitTestInvertLightCheckbox(POINT pt);
    bool HitTestInvertDarkCheckbox(POINT pt);

    void DrawSectionLabel(ID2D1HwndRenderTarget* rt, const wchar_t* text, const D2D1_RECT_F& rect);
    void DrawButton(ID2D1HwndRenderTarget* rt, const wchar_t* text, const D2D1_RECT_F& rect, bool hovered, bool disabled = false);
    void DrawCheckbox(ID2D1HwndRenderTarget* rt, const D2D1_RECT_F& rect, bool checked, bool hovered, const wchar_t* labelText = nullptr);
    void DrawIconPreview(ID2D1HwndRenderTarget* rt);
    HICON GetFileIconForPreview(const std::wstring& path);

    AppContext* m_ctx = nullptr;
    HWND m_parentHWND = nullptr;
    D2D1_RECT_F m_bounds = {};
    MacroEditFormInitParams m_init;

    // UI Controls
    TextBox m_nameBox;
    TextBox m_iconBox;
    TextBox m_eventsBox; // Multi-line script text box
    TextBox* m_focusedBox = nullptr;

    int m_speedIdx = 0; // 0=1x, 1=2x, 2=4x, 3=8x
    std::wstring m_triggerMode = L"immediate";
    bool m_iconInvertLight = false;
    bool m_iconInvertDark = false;

    // Hover states
    bool m_hoveredRecord = false;
    bool m_hoveredStop = false;
    bool m_hoveredClear = false;
    bool m_hoveredPlay = false;
    bool m_hoveredBrowseIcon = false;
    bool m_hoveredAfterClose = false;
    int  m_hoveredSpeedIdx = -1; // -1 = none
    bool m_hoveredInvertLight = false;
    bool m_hoveredInvertDark = false;

    // Icon Preview cache
    HICON m_previewIcon = nullptr;
    ID2D1Bitmap* m_previewBitmap = nullptr;
    std::wstring m_lastPreviewPath;
    std::wstring m_lastPreviewText;
    ID2D1Factory* m_d2dFactoryCache = nullptr;

    // Fonts
    ComPtr<IDWriteTextFormat> m_tfLabel;
    ComPtr<IDWriteTextFormat> m_tfBtn;
    ComPtr<IDWriteTextFormat> m_tfSmall;
};
