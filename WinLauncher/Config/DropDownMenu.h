#pragma once
#include "../GlassWindow.h"
#include <vector>
#include <string>
#include <functional>
#include <wrl.h>

using Microsoft::WRL::ComPtr;

class DropDownMenu : public GlassWindow
{
public:
    struct Item
    {
        std::wstring text;
        std::function<void()> callback;
        bool disabled = false;
    };

    static void Show(HWND parent, POINT pt, const std::vector<Item>& items, AppContext* ctx = nullptr, float minWidth = 0.0f, bool fixedWidth = false, float fontSize = 12.0f);
    static void Hide();
    static bool IsVisible();

protected:
    virtual const wchar_t* ClassName() const override { return L"WinLauncherDropDown"; }
    virtual LRESULT HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override;
    virtual void OnPaintContent(ID2D1HwndRenderTarget* rt) override;
    virtual void GetAnimationTransform(float w, float h, float progress, AnimState state, D2D1_MATRIX_3X2_F& transform) override;

private:
    DropDownMenu(AppContext* ctx, const std::vector<Item>& items, float fontSize);
    virtual ~DropDownMenu() override;

    int HitTest(POINT pt);

    static DropDownMenu* s_instance;
    static HWND s_hMainWnd;
    static AppContext* s_ctx;

    std::vector<Item> m_items;
    int m_hovered;
    float m_fontSize;
    ComPtr<IDWriteTextFormat> m_tfMenu;
};
