#pragma once
#include "../GlassWindow.h"
#include <string>
#include <functional>
#include <wrl.h>

using Microsoft::WRL::ComPtr;

class WaitWindow : public GlassWindow
{
public:
    WaitWindow(const wchar_t* title, const wchar_t* prompt, AppContext* ctx = nullptr);
    virtual ~WaitWindow() override;

    // Static helper to display the modal waiting dialog.
    // Starts the worker on a background thread and automatically closes once it is finished.
    static void Show(HWND parent, const wchar_t* title, const wchar_t* prompt, std::function<void()> worker, AppContext* ctx = nullptr);

protected:
    virtual const wchar_t* ClassName() const override { return L"WinLauncherWait"; }
    virtual LRESULT HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override;
    virtual void OnPaintContent(ID2D1HwndRenderTarget* rt) override;

private:
    void EnsureFonts();

    std::wstring m_title;
    std::wstring m_prompt;
    float m_angle;
    UINT_PTR m_timerId;
    bool m_finished;

    ComPtr<IDWriteTextFormat> m_tfTitle;
    ComPtr<IDWriteTextFormat> m_tfPrompt;
};
