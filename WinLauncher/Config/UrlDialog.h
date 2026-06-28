#pragma once
#include "../GlassWindow.h"
#include "UrlEditForm.h"
#include <string>

// UrlDialogResult
struct UrlDialogResult
{
    std::wstring name;
    std::wstring url;
    std::wstring browserPath;
    std::wstring browserArgs;
    std::wstring iconPath;
    bool         iconInvertLight = false;
    bool         iconInvertDark = false;
};

// UrlDialog - Dialog wrapper hosting UrlEditForm
class UrlDialog : public GlassWindow
{
public:
    using InitParams = UrlEditFormInitParams;

    UrlDialog(const wchar_t* title, const InitParams& init, AppContext* ctx = nullptr);
    virtual ~UrlDialog() override;

    static bool Show(HWND parent, const wchar_t* title,
                     UrlDialogResult& result,
                     const InitParams* init = nullptr,
                     AppContext* ctx = nullptr);

protected:
    virtual const wchar_t* ClassName() const override { return L"WinLauncherUrlDialog"; }
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

    UrlEditForm      m_form;

    // Hover states
    bool m_hoveredOk      = false;
    bool m_hoveredCancel  = false;
    bool m_hoveredClose   = false;
    bool m_trackMouse     = false;

    // Fonts
    ComPtr<IDWriteTextFormat> m_tfTitle;
    ComPtr<IDWriteTextFormat> m_tfBtn;
};
