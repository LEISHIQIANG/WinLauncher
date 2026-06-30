#pragma once
#include "../GlassWindow.h"
#include "TextBox.h"
#include <string>
#include <vector>

class PromptWindow : public GlassWindow
{
public:
    enum class Mode { Input, Password, Choose, Confirm };

    PromptWindow(Mode mode, const wchar_t* title, const wchar_t* prompt,
                 const std::vector<std::wstring>& chooseOptions = {},
                 const wchar_t* defaultText = L"", AppContext* ctx = nullptr);
    virtual ~PromptWindow() override;

    // Legacy input mode
    static bool Show(HWND parent, const wchar_t* title, const wchar_t* prompt,
                     std::wstring& outResult, const wchar_t* defaultText = L"",
                     AppContext* ctx = nullptr);

    // Password mode: masked input
    static bool ShowPassword(HWND parent, const wchar_t* title, const wchar_t* prompt,
                             std::wstring& outResult, AppContext* ctx = nullptr);

    // Choose mode: select from a list of options
    static bool ShowChoose(HWND parent, const wchar_t* title, const wchar_t* prompt,
                           const std::vector<std::wstring>& options,
                           std::wstring& outResult, AppContext* ctx = nullptr);

    // Confirm mode: OK/Cancel dialog only
    static bool ShowConfirm(HWND parent, const wchar_t* title, const wchar_t* message,
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
    int  HitTestChooseOption(POINT pt);

    // Internal shared Show runner
    static bool ShowInternal(HWND parent, Mode mode, const wchar_t* title, const wchar_t* prompt,
                             std::wstring& outResult, const std::vector<std::wstring>& chooseOptions,
                             const wchar_t* defaultText, AppContext* ctx,
                             int windowWidth, int windowHeight);

    Mode m_mode;
    std::wstring m_title;
    std::wstring m_prompt;
    std::wstring m_defaultText;
    std::wstring m_result;
    bool m_okPressed;

    TextBox m_textBox;

    // Choose mode
    std::vector<std::wstring> m_chooseOptions;
    int m_selectedOption;

    bool m_hoveredOk;
    bool m_hoveredCancel;
    bool m_hoveredClose;
    bool m_trackMouse;

    ComPtr<IDWriteTextFormat> m_tfTitle;
    ComPtr<IDWriteTextFormat> m_tfPrompt;
    ComPtr<IDWriteTextFormat> m_tfBtn;
};
