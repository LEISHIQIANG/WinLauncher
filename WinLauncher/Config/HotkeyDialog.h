#pragma once
#include "../GlassWindow.h"
#include "HotkeyEditForm.h"
#include <string>

// HotkeyDialogResult
struct HotkeyDialogResult
{
    std::wstring name;
    std::wstring hotkey;
    std::wstring iconPath;
    bool         afterClose = true;
    bool         iconInvertLight = false;
    bool         iconInvertDark = false;
};

// HotkeyDialog - Dialog wrapper hosting HotkeyEditForm
class HotkeyDialog : public GlassWindow
{
public:
    using InitParams = HotkeyEditFormInitParams;

    HotkeyDialog(const wchar_t* title, const InitParams& init, AppContext* ctx = nullptr);
    virtual ~HotkeyDialog() override;

    static bool Show(HWND parent, const wchar_t* title,
                     HotkeyDialogResult& result,
                     const InitParams* init = nullptr,
                     AppContext* ctx = nullptr);

protected:
    virtual const wchar_t* ClassName() const override { return L"WinLauncherHotkeyDialog"; }
    virtual LRESULT HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override;
    virtual void OnPaintContent(ID2D1HwndRenderTarget* rt) override;

private:
    void EnsureFonts();
    void UpdateChildLayout();

    bool HitTestRect(POINT pt, const D2D1_RECT_F& rect);
    bool HitTestCloseButton(POINT pt);
    bool HitTestOkButton(POINT pt);
    bool HitTestCancelButton(POINT pt);

    void DrawButton(ID2D1HwndRenderTarget* rt, const wchar_t* text,
                    const D2D1_RECT_F& rect, bool hovered, bool accent = false);

    std::wstring     m_title;
    InitParams       m_init;
    bool             m_okPressed;

    HotkeyEditForm   m_form;

    // Hover states
    bool m_hoveredOk      = false;
    bool m_hoveredCancel  = false;
    bool m_hoveredClose   = false;
    bool m_trackMouse     = false;

    // Fonts
    ComPtr<IDWriteTextFormat> m_tfTitle;
    ComPtr<IDWriteTextFormat> m_tfBtn;
};
