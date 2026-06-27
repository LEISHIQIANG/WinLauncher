#pragma once
#include "../GlassWindow.h"
#include <vector>
#include <string>
#include <functional>
#include <wrl.h>

using Microsoft::WRL::ComPtr;

class ContextMenu : public GlassWindow
{
public:
    struct Item
    {
        std::wstring text;
        std::function<void()> callback;
    };

    static void Show(HWND parent, POINT pt, const std::vector<Item>& items, AppContext* ctx = nullptr);
    static void Hide();
    static bool IsVisible();

protected:
    virtual const wchar_t* ClassName() const override { return L"WinLauncherContextMenu"; }
    virtual LRESULT HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override;
    virtual void OnPaintContent(ID2D1HwndRenderTarget* rt) override;

private:
    ContextMenu(AppContext* ctx, const std::vector<Item>& items);
    virtual ~ContextMenu() override;

    int HitTest(POINT pt);

    static ContextMenu* s_instance;
    static HWND s_hMainWnd;
    static AppContext* s_ctx;

    std::vector<Item> m_items;
    int m_hovered;
    ComPtr<IDWriteTextFormat> m_tfMenu;
};
