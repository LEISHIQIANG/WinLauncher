#pragma once
#include "../GlassWindow.h"
#include "TextBox.h"
#include <string>
#include <wrl.h>

using Microsoft::WRL::ComPtr;

class CommandPanelWindow : public GlassWindow
{
public:
    CommandPanelWindow(const wchar_t* title, const wchar_t* outputText, AppContext* ctx = nullptr);
    virtual ~CommandPanelWindow() override;

    static void Show(HWND parent, const wchar_t* title, const wchar_t* outputText, AppContext* ctx = nullptr);

protected:
    virtual const wchar_t* ClassName() const override { return L"WinLauncherCommandPanel"; }
    virtual LRESULT HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override;
    virtual void OnPaintContent(ID2D1HwndRenderTarget* rt) override;

private:
    void EnsureFonts();
    void UpdateChildLayout();

    bool HitTestRect(POINT pt, const D2D1_RECT_F& rect);
    bool HitTestCloseButton(POINT pt);
    bool HitTestOkButton(POINT pt);

    std::wstring m_title;
    std::wstring m_outputText;

    TextBox m_textBox;

    bool m_hoveredOk;
    bool m_hoveredClose;
    bool m_trackMouse;

    ComPtr<IDWriteTextFormat> m_tfTitle;
    ComPtr<IDWriteTextFormat> m_tfBtn;
};
