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
        bool disabled = false;
    };

    static void Show(HWND parent, POINT pt, const std::vector<Item>& items, AppContext* ctx = nullptr, float minWidth = 0.0f);
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
    bool IsInsideClient(POINT pt) const;
    void CaptureMouse();
    void ReleaseMouseCapture();
    static void DestroyInstance(ContextMenu* inst);
    static void RemoveClosingInstance(ContextMenu* inst);
    static void CloseExisting(bool immediate);

    static ContextMenu* s_instance;
    static std::vector<ContextMenu*> s_closingInstances;
    static HWND s_hMainWnd;
    static AppContext* s_ctx;

    std::vector<Item> m_items;
    int m_hovered;
    bool m_mouseCaptured = false;
    ComPtr<IDWriteTextFormat> m_tfMenu;
};
