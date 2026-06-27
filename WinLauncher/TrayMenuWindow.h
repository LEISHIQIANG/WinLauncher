#pragma once
#include "GlassWindow.h"

class PopupWindow;
class ConfigWindow;

class TrayMenuWindow : public GlassWindow
{
public:
    TrayMenuWindow(AppContext* ctx);
    virtual ~TrayMenuWindow() override;

    static void Init(HWND hMainWnd, AppContext* ctx = nullptr);
    static void Show(POINT pt);
    static void Hide();
    static void Release();

protected:
    virtual const wchar_t* ClassName() const override { return L"WinLauncherTrayMenu"; }
    virtual LRESULT HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override;
    virtual void OnPaintContent(ID2D1HwndRenderTarget* rt) override;

private:
    int HitTest(POINT pt);

    static TrayMenuWindow* s_instance;
    static HWND s_hMainWnd;
    static AppContext* s_ctx;

    int m_hovered;
};
