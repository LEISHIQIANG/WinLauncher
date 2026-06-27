#pragma once
#include "../GlassWindow.h"
#include "TextBox.h"
#include <string>

class PromptWindow : public GlassWindow
{
public:
    PromptWindow(const wchar_t* title, const wchar_t* prompt, const wchar_t* defaultText = L"", AppContext* ctx = nullptr);
    virtual ~PromptWindow() override;

    static bool Show(HWND parent, const wchar_t* title, const wchar_t* prompt,
                     std::wstring& outResult, const wchar_t* defaultText = L"",
                     AppContext* ctx = nullptr);

protected:
    virtual const wchar_t* ClassName() const override { return L"WinLauncherPrompt"; }
    virtual LRESULT HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override;
    virtual void OnPaintContent(ID2D1HwndRenderTarget* rt) override;

private:
    void EnsureFonts();
    void UpdateChildLayout();

    bool HitTestRect(POINT pt, const D2D1_RECT_F& rect);
    bool HitTestCloseButton(POINT pt);
    bool HitTestOkButton(POINT pt);
    bool HitTestCancelButton(POINT pt);

    std::wstring m_title;
    std::wstring m_prompt;
    std::wstring m_defaultText;
    std::wstring m_result;
    bool m_okPressed;

    TextBox m_textBox;

    bool m_hoveredOk;
    bool m_hoveredCancel;
    bool m_hoveredClose;
    bool m_trackMouse;

    ComPtr<IDWriteTextFormat> m_tfTitle;
    ComPtr<IDWriteTextFormat> m_tfPrompt;
    ComPtr<IDWriteTextFormat> m_tfBtn;
};
