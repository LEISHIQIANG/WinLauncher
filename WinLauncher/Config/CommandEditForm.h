#pragma once
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl.h>
#include <string>
#include "TextBox.h"
#include "../Model/ShortcutInfo.h"

using Microsoft::WRL::ComPtr;

struct CommandEditFormResult
{
    std::wstring name;
    std::wstring command; // stored in targetPath
    std::wstring iconPath;
    bool         runAsAdmin = false;
    bool         iconInvertLight = false;
    bool         iconInvertDark = false;
    std::wstring commandType; // cmd, powershell, builtin
    std::wstring builtinCmd;  // toggle_topmost, etc.
    bool         showWindow = false;
    bool         captureOutput = false;
    int          timeoutSeconds = 300;
    int          maxChars = 2000;
};

struct CommandEditFormInitParams
{
    std::wstring name;
    std::wstring command;
    std::wstring iconPath;
    bool         runAsAdmin = false;
    bool         iconInvertLight = false;
    bool         iconInvertDark = false;
    std::wstring commandType; // cmd, powershell, builtin
    std::wstring builtinCmd;
    bool         showWindow = false;
    bool         captureOutput = false;
    int          timeoutSeconds = 300;
    int          maxChars = 2000;
};

class CommandEditForm
{
public:
    CommandEditForm();
    ~CommandEditForm();

    static constexpr float PreferredContentHeight() { return 338.0f; }

    bool Create(HWND parentHWND, IDWriteFactory* dwriteFactory, const D2D1_RECT_F& logicalBounds, const CommandEditFormInitParams& init);
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
    CommandEditFormResult GetResult() const;
    bool Validate(HWND hWnd);

private:
    void EnsureFonts(IDWriteFactory* dwriteFactory);
    void SelectCommandType(HWND hWnd);
    void SelectBuiltinCommand(HWND hWnd);
    void BrowseIconFile(HWND hWnd);

    // Hit-testing helpers (logical coordinates)
    bool HitTestRect(POINT pt, const D2D1_RECT_F& rect);
    bool HitTestSelectTypeButton(POINT pt);
    bool HitTestSelectBuiltinButton(POINT pt);
    bool HitTestBrowseIconButton(POINT pt);
    bool HitTestRunAsAdminCheckbox(POINT pt);
    bool HitTestShowWindowCheckbox(POINT pt);
    bool HitTestCaptureOutputCheckbox(POINT pt);
    bool HitTestInvertLightCheckbox(POINT pt);
    bool HitTestInvertDarkCheckbox(POINT pt);

    // Drawing helpers
    void DrawSectionLabel(ID2D1HwndRenderTarget* rt, const wchar_t* text, const D2D1_RECT_F& rect);
    void DrawButton(ID2D1HwndRenderTarget* rt, const wchar_t* text, const D2D1_RECT_F& rect, bool hovered, bool enabled = true);
    void DrawCheckbox(ID2D1HwndRenderTarget* rt, const D2D1_RECT_F& rect, bool checked, bool hovered, bool enabled = true);
    void DrawIconPreview(ID2D1HwndRenderTarget* rt);
    HICON GetFileIconForPreview(const std::wstring& path);

    HWND            m_parentHWND = nullptr;
    D2D1_RECT_F     m_bounds = {};
    CommandEditFormInitParams m_init;

    // UI Controls
    TextBox m_nameBox;
    TextBox m_typeBox;
    TextBox m_builtinBox;
    TextBox m_commandBox;
    TextBox m_iconBox;
    TextBox* m_focusedBox = nullptr;

    std::wstring m_commandType; // cmd, powershell, builtin
    std::wstring m_builtinCmd;
    bool m_runAsAdmin = false;
    bool m_showWindow = false;
    bool m_captureOutput = false;
    bool m_iconInvertLight = false;
    bool m_iconInvertDark = false;
    int  m_timeoutSeconds = 300;
    int  m_maxChars = 2000;

    // Hover states
    bool m_hoveredSelectType    = false;
    bool m_hoveredSelectBuiltin = false;
    bool m_hoveredBrowseIcon    = false;
    bool m_hoveredRunAsAdmin    = false;
    bool m_hoveredShowWindow    = false;
    bool m_hoveredCaptureOutput = false;
    bool m_hoveredInvertLight   = false;
    bool m_hoveredInvertDark    = false;

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
