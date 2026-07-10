#pragma once
#include "../GlassWindow.h"
#include "TextBox.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <wrl.h>

using Microsoft::WRL::ComPtr;

class CommandPanelWindow : public GlassWindow
{
public:
    CommandPanelWindow(const wchar_t* title, const wchar_t* outputText, AppContext* ctx = nullptr, std::function<void(HWND)> refreshWorker = nullptr);
    virtual ~CommandPanelWindow() override;

    static void Show(HWND parent, const wchar_t* title, const wchar_t* outputText, AppContext* ctx = nullptr, std::function<void(HWND)> refreshWorker = nullptr);
    static void ShowLive(HWND parent, const wchar_t* title, const wchar_t* initialText, std::function<void(HWND)> worker, AppContext* ctx = nullptr, std::function<void(HWND)> refreshWorker = nullptr);
    static bool PostAppend(HWND hwnd, const std::wstring& text);

    // Register a built-in action that appears in the "More" dropdown
    void RegisterBuiltinPopupAction(const std::wstring& title, std::function<void()> callback);

protected:
    virtual const wchar_t* ClassName() const override { return L"WinLauncherCommandPanel"; }
    virtual LRESULT HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override;
    virtual void OnPaintContent(ID2D1HwndRenderTarget* rt) override;

private:
    void EnsureFonts();
    void UpdateChildLayout();

    bool HitTestRect(POINT pt, const D2D1_RECT_F& rect);
    bool HitTestCloseButton(POINT pt);
    bool HitTestRefreshButton(POINT pt);
    bool HitTestCopyButton(POINT pt);
    bool HitTestOkButton(POINT pt);
    bool HitTestMoreButton(POINT pt);
    D2D1_RECT_F GetFooterButtonRect(int index) const;
    void AppendOutput(const std::wstring& text);
    void FlushPendingOutput();
    void RunRefresh();
    void CopyOutputToClipboard();
    void ClearOutput(const wchar_t* initialText);
    void ShowMoreDropdown();
    void RegisterBuiltinActions();

    std::wstring m_title;
    std::wstring m_outputText;
    std::wstring m_initialText;
    std::wstring m_pendingOutput;
    std::function<void(HWND)> m_refreshWorker;

    TextBox m_textBox;

    bool m_hoveredOk;
    bool m_hoveredCopy;
    bool m_hoveredRefresh;
    bool m_hoveredMore;
    bool m_hoveredClose;
    bool m_trackMouse;
    bool m_refreshRunning;
    int m_loadingFrame;
    ULONGLONG m_loadingStartedTick;
    uint64_t m_workerGeneration;
    uint64_t m_instanceToken;

    ComPtr<IDWriteTextFormat> m_tfTitle;
    ComPtr<IDWriteTextFormat> m_tfBtn;

    // Built-in popup actions for the "More" dropdown (merged with plugin actions)
    struct BuiltinPopupAction
    {
        std::wstring title;
        std::function<void()> callback;
    };
    std::vector<BuiltinPopupAction> m_builtinActions;
};
