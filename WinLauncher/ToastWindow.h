#pragma once
/**
 * ToastWindow.h
 *
 * 屏幕中央短暂显示的纯文字提示窗口，继承 GlassWindow，
 * 风格与主弹窗完全一致，窗口大小刚好包裹文字加少量边距，
 * durationMs 后自动关闭，不抢焦点。
 */
#include "GlassWindow.h"
#include <string>

class ToastWindow : public GlassWindow
{
public:
    /**
     * 在屏幕中央显示一个提示。
     * @param message    纯文字内容（短句）
     * @param durationMs 自动关闭延迟，默认 500ms
     */
    static void Show(const std::wstring& message, DWORD durationMs = 500);

    /** 立即关闭（若存在）。 */
    static void Hide();

protected:
    virtual const wchar_t* ClassName() const override { return L"WinLauncherToast"; }
    virtual LRESULT HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override;
    virtual void OnPaintContent(ID2D1HwndRenderTarget* rt) override;

private:
    explicit ToastWindow(std::wstring msg) : m_message(std::move(msg)) {}

    std::wstring m_message;

    static ToastWindow* s_instance;

    static constexpr UINT TIMER_CLOSE = 1;
};
