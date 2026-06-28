#pragma once
#include "../GlassWindow.h"
#include "CommandEditForm.h"
#include <string>

// CommandDialogResult
struct CommandDialogResult
{
    std::wstring name;
    std::wstring command;
    std::wstring iconPath;
    bool         runAsAdmin = false;
    bool         iconInvertLight = false;
    bool         iconInvertDark = false;
    std::wstring commandType;
    std::wstring builtinCmd;
    bool         showWindow = false;
    bool         captureOutput = false;
    int          timeoutSeconds = 300;
    int          maxChars = 2000;
};

// CommandDialog - Dialog wrapper hosting CommandEditForm
class CommandDialog : public GlassWindow
{
public:
    using InitParams = CommandEditFormInitParams;

    CommandDialog(const wchar_t* title, const InitParams& init, AppContext* ctx = nullptr);
    virtual ~CommandDialog() override;

    static bool Show(HWND parent, const wchar_t* title,
                     CommandDialogResult& result,
                     const InitParams* init = nullptr,
                     AppContext* ctx = nullptr);

protected:
    virtual const wchar_t* ClassName() const override { return L"WinLauncherCommandDialog"; }
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

    CommandEditForm  m_form;

    // Hover states
    bool m_hoveredOk      = false;
    bool m_hoveredCancel  = false;
    bool m_hoveredClose   = false;
    bool m_trackMouse     = false;

    // Fonts
    ComPtr<IDWriteTextFormat> m_tfTitle;
    ComPtr<IDWriteTextFormat> m_tfBtn;
};
