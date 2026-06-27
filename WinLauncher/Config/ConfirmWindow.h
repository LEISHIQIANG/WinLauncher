#pragma once
#include "../GlassWindow.h"
#include <string>
#include <wrl.h>

using Microsoft::WRL::ComPtr;

class ConfirmWindow : public GlassWindow
{
public:
    ConfirmWindow(const wchar_t* title, const wchar_t* prompt, AppContext* ctx = nullptr);
    virtual ~ConfirmWindow() override;

    static bool Show(HWND parent, const wchar_t* title, const wchar_t* prompt, AppContext* ctx = nullptr);

protected:
    virtual const wchar_t* ClassName() const override { return L"WinLauncherConfirm"; }
    virtual LRESULT HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override;
    virtual void OnPaintContent(ID2D1HwndRenderTarget* rt) override;

private:
    void EnsureFonts();

    bool HitTestRect(POINT pt, const D2D1_RECT_F& rect);
    bool HitTestCloseButton(POINT pt);
    bool HitTestOkButton(POINT pt);
    bool HitTestCancelButton(POINT pt);

    std::wstring m_title;
    std::wstring m_prompt;
    bool m_okPressed;

    bool m_hoveredOk;
    bool m_hoveredCancel;
    bool m_hoveredClose;
    bool m_trackMouse;

    ComPtr<IDWriteTextFormat> m_tfTitle;
    ComPtr<IDWriteTextFormat> m_tfPrompt;
    ComPtr<IDWriteTextFormat> m_tfBtn;
};
