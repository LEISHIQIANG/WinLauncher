#pragma once
#include "../GlassWindow.h"
#include "../ShortcutManager.h"
#include "../Services/AppSceneMatcher.h"
#include <functional>
#include <string>
#include <vector>
#include <wrl.h>

using Microsoft::WRL::ComPtr;

class SceneSettingsWindow : public GlassWindow
{
public:
    SceneSettingsWindow(RendPopupPage* page, AppContext* ctx, std::function<void()> onChanged);
    virtual ~SceneSettingsWindow() override;

    static bool Show(HWND parent, RendPopupPage* page, AppContext* ctx, std::function<void()> onChanged = nullptr);

protected:
    virtual const wchar_t* ClassName() const override { return L"WinLauncherSceneSettings"; }
    virtual LRESULT HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override;
    virtual void OnPaintContent(ID2D1HwndRenderTarget* rt) override;

private:
    enum class ListSide
    {
        None,
        Selected,
        Available
    };

    void EnsureFonts();
    void DrawCloseButton(ID2D1HwndRenderTarget* rt);
    void DrawModeDropDown(ID2D1HwndRenderTarget* rt);
    void DrawList(ID2D1HwndRenderTarget* rt, ListSide side);
    void DrawButton(ID2D1HwndRenderTarget* rt, const wchar_t* text, const D2D1_RECT_F& rect, bool hovered, bool accent);
    void ShowModeMenu();
    void CaptureAvailableScenes();
    void AddAvailableCandidate(const AppScene::AppCandidate& app);
    std::vector<std::wstring> BuildAvailableAppList() const;

    bool HitTestRect(POINT pt, const D2D1_RECT_F& rect) const;
    bool HitTestCloseButton(POINT pt) const;
    bool HitTestModeDropDown(POINT pt) const;
    bool HitTestAddSceneButton(POINT pt) const;
    bool HitTestOkButton(POINT pt) const;
    bool HitTestCancelButton(POINT pt) const;
    bool HitTestList(POINT pt, ListSide side) const;
    int HitTestListItem(POINT pt, ListSide side) const;
    int MaxScroll(ListSide side) const;
    void ClampScrolls();

    bool IsSelected(const std::wstring& exePath) const;
    void AddSelectedApp(const AppScene::AppCandidate& app);
    void RemoveSelectedApp(int index);
    std::wstring SelectedDisplayName(const std::wstring& app) const;

    RendPopupPage* m_page = nullptr;
    std::function<void()> m_onChanged;
    bool m_okPressed = false;
    Model::PageSceneMode m_sceneMode = Model::PageSceneMode::Whitelist;
    std::vector<std::wstring> m_selectedApps;
    std::vector<AppScene::AppCandidate> m_availableApps;

    int m_selectedScroll = 0;
    int m_availableScroll = 0;
    ListSide m_hoveredSide = ListSide::None;
    int m_hoveredItem = -1;
    bool m_hoveredMode = false;
    bool m_hoveredAddScene = false;
    bool m_hoveredOk = false;
    bool m_hoveredCancel = false;
    bool m_hoveredClose = false;
    bool m_trackMouse = false;

    ComPtr<IDWriteTextFormat> m_tfTitle;
    ComPtr<IDWriteTextFormat> m_tfLabel;
    ComPtr<IDWriteTextFormat> m_tfItem;
    ComPtr<IDWriteTextFormat> m_tfSmall;
    ComPtr<IDWriteTextFormat> m_tfButton;
};
