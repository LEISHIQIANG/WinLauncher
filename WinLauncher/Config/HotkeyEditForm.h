#pragma once
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl.h>
#include <string>
#include "TextBox.h"
#include "../Model/ShortcutInfo.h"
#include "../KeyboardHook.h"

using Microsoft::WRL::ComPtr;

struct HotkeyEditFormResult
{
    std::wstring name;
    std::wstring hotkey; // e.g. "Ctrl + Shift + A"
    std::wstring iconPath;
    bool         afterClose = true;
    bool         iconInvertLight = false;
    bool         iconInvertDark = false;
};

struct HotkeyEditFormInitParams
{
    std::wstring name;
    std::wstring hotkey;
    std::wstring iconPath;
    bool         afterClose = true;
    bool         iconInvertLight = false;
    bool         iconInvertDark = false;
};

class HotkeyEditForm
{
public:
    HotkeyEditForm();
    ~HotkeyEditForm();

    static constexpr float PreferredContentHeight() { return 222.0f; }

    bool Create(HWND parentHWND, IDWriteFactory* dwriteFactory, const D2D1_RECT_F& logicalBounds, const HotkeyEditFormInitParams& init);
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

    // Called by HotkeyDialog for hook messages from KeyboardHook
    bool HandleHookMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, bool& repaint);

    void BlinkCaret();

    // Focus helpers
    bool IsInputFocused() const;
    void ResetFocus();

    // Data exchange
    HotkeyEditFormResult GetResult() const;
    bool Validate(HWND hWnd);

    void StartRecording(HWND hWnd);
    void StopRecording(HWND hWnd);
    void ClearHotkey();

private:
    void EnsureFonts(IDWriteFactory* dwriteFactory);
    void BrowseIconFile(HWND hWnd);

    // Hit-testing helpers (logical coordinates)
    bool HitTestRect(POINT pt, const D2D1_RECT_F& rect);
    bool HitTestRecordButton(POINT pt);
    bool HitTestClearButton(POINT pt);
    bool HitTestBrowseIconButton(POINT pt);
    bool HitTestAfterCloseCheckbox(POINT pt);
    bool HitTestInvertLightCheckbox(POINT pt);
    bool HitTestInvertDarkCheckbox(POINT pt);

    // Drawing helpers
    void DrawSectionLabel(ID2D1HwndRenderTarget* rt, const wchar_t* text, const D2D1_RECT_F& rect);
    void DrawButton(ID2D1HwndRenderTarget* rt, const wchar_t* text, const D2D1_RECT_F& rect, bool hovered);
    void DrawCheckbox(ID2D1HwndRenderTarget* rt, const D2D1_RECT_F& rect, bool checked, bool hovered);
    void DrawIconPreview(ID2D1HwndRenderTarget* rt);
    HICON GetFileIconForPreview(const std::wstring& path);

    HWND            m_parentHWND = nullptr;
    D2D1_RECT_F     m_bounds = {};
    HotkeyEditFormInitParams m_init;

    // UI Controls
    TextBox m_nameBox;
    TextBox m_hotkeyBox;
    TextBox m_iconBox;
    TextBox* m_focusedBox = nullptr;

    bool m_recording = false;
    bool m_recordingCapturedHotkey = false;
    std::wstring m_hotkeyBeforeRecording;
    bool m_afterClose = true;
    bool m_iconInvertLight = false;
    bool m_iconInvertDark = false;

    // Hover states
    bool m_hoveredRecord      = false;
    bool m_hoveredClear       = false;
    bool m_hoveredBrowseIcon  = false;
    bool m_hoveredAfterClose  = false;
    bool m_hoveredInvertLight = false;
    bool m_hoveredInvertDark  = false;

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
