#pragma once

#include <windows.h>

#include <d2d1.h>

#include <dwrite.h>

#include <wrl.h>

#include <string>

#include "TextBox.h"



using Microsoft::WRL::ComPtr;



// Form output parameters

struct ShortcutEditFormResult

{

    std::wstring name;

    std::wstring targetPath;

    std::wstring arguments;

    std::wstring workingDir;

    std::wstring iconPath;

    bool         runAsAdmin = false;

    bool         iconInvertLight = false;

    bool         iconInvertDark = false;

};



// Form initialization parameters

struct ShortcutEditFormInitParams

{

    std::wstring name;

    std::wstring targetPath;

    std::wstring arguments;

    std::wstring workingDir;

    std::wstring iconPath;

    bool         runAsAdmin = false;

    bool         iconInvertLight = false;

    bool         iconInvertDark = false;

};



// ShortcutEditForm - Decoupled form UI component

// Handles all rendering and inputs for the shortcut editing form, independent of the window host.

class ShortcutEditForm

{

public:

    ShortcutEditForm();

    ~ShortcutEditForm();

    static constexpr float PreferredContentHeight() { return 300.0f; }



    bool Create(HWND parentHWND, IDWriteFactory* dwriteFactory, const D2D1_RECT_F& logicalBounds, const ShortcutEditFormInitParams& init);

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

    void BlinkCaret();



    // Focus helpers

    bool IsInputFocused() const;
    bool HandleImeMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, bool& repaint);
    void ResetFocus();



    // Data exchange

    ShortcutEditFormResult GetResult() const;

    bool Validate(HWND hWnd); // Validates basic fields (name, target)



    // Button actions exposed in case parent wants to trigger them

    void BrowseTargetFile(HWND hWnd);

    void BrowseIconFile(HWND hWnd);

    void BrowseWorkingDir(HWND hWnd);



private:

    void EnsureFonts(IDWriteFactory* dwriteFactory);

    void AutoFillNameFromTarget(const std::wstring& targetPath);



    // Hit-testing helpers (logical coordinates)

    bool HitTestRect(POINT pt, const D2D1_RECT_F& rect);

    bool HitTestBrowseTargetButton(POINT pt);

    bool HitTestBrowseIconButton(POINT pt);

    bool HitTestBrowseWorkdirButton(POINT pt);

    bool HitTestRunAsAdminCheckbox(POINT pt);

    bool HitTestInvertLightCheckbox(POINT pt);

    bool HitTestInvertDarkCheckbox(POINT pt);



    // Drawing helpers

    void DrawSectionLabel(ID2D1HwndRenderTarget* rt, const wchar_t* text, const D2D1_RECT_F& rect);

    void DrawButton(ID2D1HwndRenderTarget* rt, const wchar_t* text, const D2D1_RECT_F& rect, bool hovered);

    void DrawCheckbox(ID2D1HwndRenderTarget* rt, const D2D1_RECT_F& rect, bool checked, bool hovered, const wchar_t* labelText);

    void DrawIconPreview(ID2D1HwndRenderTarget* rt);

    HICON GetFileIconForPreview(const std::wstring& path);



    HWND            m_parentHWND = nullptr;

    D2D1_RECT_F     m_bounds = {};

    ShortcutEditFormInitParams m_init;



    // UI Controls

    TextBox m_nameBox;

    TextBox m_targetBox;

    TextBox m_iconBox;

    TextBox m_argsBox;

    TextBox m_workdirBox;

    TextBox* m_focusedBox = nullptr;



    bool m_runAsAdmin = false;

    bool m_iconInvertLight = false;

    bool m_iconInvertDark = false;



    // Hover states

    bool m_hoveredBrowseTarget  = false;

    bool m_hoveredBrowseIcon    = false;

    bool m_hoveredBrowseWorkdir = false;

    bool m_hoveredRunAsAdmin    = false;

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
