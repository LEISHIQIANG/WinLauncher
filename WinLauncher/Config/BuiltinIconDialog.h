#pragma once
#include "../GlassWindow.h"
#include "../ShortcutManager.h"
#include <string>
#include <vector>

struct BuiltinIconPreset
{
    std::wstring name;
    std::wstring targetPath;
    std::wstring arguments;
    Model::ShortcutType type;
    int category; // 0:系统, 1:网站, 2:网络, 3:其他
    std::wstring builtinIconId;
};

class BuiltinIconDialog : public GlassWindow
{
public:
    BuiltinIconDialog(AppContext* ctx = nullptr);
    virtual ~BuiltinIconDialog() override;

    static bool Show(HWND parent, std::vector<RendShortcutInfo>& selectedIcons, AppContext* ctx = nullptr);

protected:
    virtual const wchar_t* ClassName() const override { return L"WinLauncherBuiltinIconDialog"; }
    virtual LRESULT HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override;
    virtual void OnPaintContent(ID2D1HwndRenderTarget* rt) override;

private:
    void EnsureFonts();
    void EnsurePresetBitmaps(ID2D1HwndRenderTarget* rt);
    void ReleasePresetBitmaps();
    void UpdateFilteredItems();

    bool HitTestRect(POINT pt, const D2D1_RECT_F& rect);
    bool HitTestCloseButton(POINT pt);
    bool HitTestOkButton(POINT pt);
    bool HitTestCancelButton(POINT pt);
    int HitTestTab(POINT pt);
    int HitTestIconCard(POINT pt);

    void DrawButton(ID2D1HwndRenderTarget* rt, const wchar_t* text,
                    const D2D1_RECT_F& rect, bool hovered, bool enabled = true, bool accent = false);
    
    ID2D1Bitmap* CreateD2DBitmapFromHicon(HICON hIcon, const std::wstring& name, bool invert);

    bool m_okPressed = false;
    std::vector<RendShortcutInfo> m_selectedIcons;

    // Tabs
    int m_activeTab = 0; // 0:系统, 1:网站, 2:网络, 3:其他
    int m_hoveredTab = -1;

    // Filtered items in the current active tab
    struct FilteredItem
    {
        size_t originalPresetIndex;
        RendShortcutInfo info;
    };
    std::vector<FilteredItem> m_filteredItems;

    // Icons grid state
    float m_scrollY = 0.0f;
    float m_targetScrollY = 0.0f;
    float m_scrollVelocity = 0.0f;
    bool m_animating = false;

    struct PresetVisualState
    {
        bool selected = false;
    };
    std::vector<PresetVisualState> m_presetStates; // parallel to m_filteredItems
    int m_hoveredPreset = -1;
    int m_selectionAnchorIndex = -1;

    // Hover states for dialog controls
    bool m_hoveredOk = false;
    bool m_hoveredCancel = false;
    bool m_hoveredClose = false;
    bool m_trackMouse = false;

    // Fonts
    ComPtr<IDWriteTextFormat> m_tfTitle;
    ComPtr<IDWriteTextFormat> m_tfBtn;
    ComPtr<IDWriteTextFormat> m_tfTab;
    ComPtr<IDWriteTextFormat> m_tfCardLabel;

    // Bitmaps cache for presets
    std::vector<ID2D1Bitmap*> m_presetBitmaps; // stores bitmaps loaded for all presets
    bool m_bitmapsLoaded = false;
};
