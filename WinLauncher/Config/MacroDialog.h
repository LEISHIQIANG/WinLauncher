#pragma once
#include "../GlassWindow.h"
#include "MacroEditForm.h"
#include <string>

struct MacroDialogResult
{
    std::wstring name;
    std::wstring arguments;
    std::wstring iconPath;
    bool         iconInvertLight = false;
    bool         iconInvertDark = false;
};

class MacroDialog : public GlassWindow
{
public:
    using InitParams = MacroEditFormInitParams;

    MacroDialog(const wchar_t* title, const InitParams& init, AppContext* ctx = nullptr);
    virtual ~MacroDialog() override;

    static bool Show(HWND parent, const wchar_t* title,
                     MacroDialogResult& result,
                     const InitParams* init = nullptr,
                     AppContext* ctx = nullptr);

protected:
    virtual const wchar_t* ClassName() const override { return L"WinLauncherMacroDialog"; }
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
    bool             m_okPressed = false;

    MacroEditForm    m_form;

    // Hover states
    bool m_hoveredOk      = false;
    bool m_hoveredCancel  = false;
    bool m_hoveredClose   = false;
    bool m_trackMouse     = false;

    // Fonts
    ComPtr<IDWriteTextFormat> m_tfTitle;
    ComPtr<IDWriteTextFormat> m_tfBtn;
};
