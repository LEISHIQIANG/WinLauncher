#pragma once
#include "GlassWindow.h"
#include "ShortcutManager.h"
#include "ViewModel/PopupViewModel.h"
#include "Services/IIconService.h"
#include <vector>
#include <unordered_map>
#include <wrl.h>

using Microsoft::WRL::ComPtr;

class PopupWindow : public GlassWindow
{
public:
    PopupWindow(AppContext* ctx);
    virtual ~PopupWindow() override;

    static void Init(AppContext* ctx);
    static void Show(HWND parent, POINT pt);
    static void Hide();
    static void Release();
    static bool IsVisible();

    int CellWidth() const;
    int CellHeight() const;

    int GetColumns() const;
    int GetRows() const;
    int GetIconSize() const;
    int GetIconGap() const;
    int GetIconRadius() const;
    int GetWndPadding() const;
    int GetCellMarginX() const;
    int GetCellMarginY() const;
    int GetDockHeight() const;

    void UpdateWindowSize();

protected:
    virtual const wchar_t* ClassName() const override { return L"WinLauncherPopup"; }
    virtual LRESULT HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override;
    virtual void OnPaintContent(ID2D1HwndRenderTarget* rt) override;

private:
    int HitTest(POINT pt);
    int HitTestDot(POINT pt);
    int HitTestDock(POINT pt);
    void EnsureIcons();
    void RefreshIcons();
    void DrawPage(ID2D1HwndRenderTarget* rt, int pageIndex);
    void ClearPages();
    void OnConfigChanged();
    void StartAutoHideTimer();
    void StopAutoHideTimer();
    float GetFontSize() const;
    int GetLabelHeight() const;
    void UpdateTextFormat();

    void UpdateSearch();
    void SavePopupConfig();
    void DrawTopBar(ID2D1HwndRenderTarget* rt);
    void DrawSearchResults(ID2D1HwndRenderTarget* rt);
    void DrawDock(ID2D1HwndRenderTarget* rt);
    void LaunchShortcut(const RendShortcutInfo& sc);

    static PopupWindow* s_instance;

    std::unique_ptr<PopupViewModel> m_viewModel;
    std::unique_ptr<IIconService> m_iconService; // public for helper function access
    std::wstring m_configDir;

    std::vector<RendPopupPage> m_pages;  // Legacy: pages with icon bitmaps for rendering
    int m_currentPage;
    RendPopupPage m_dockPage;  // Fixed dock bar (DOCK category)

    int m_hovered;
    bool m_trackMouse;
    bool m_pinned;

    ID2D1HwndRenderTarget* m_lastRt;

    // Animation states
    bool m_animating;
    double m_animLastTime;
    float m_scrollPosition;
    float m_scrollVelocity;

    // Bitmap brush cache: keyed by ID2D1Bitmap pointer, cleared on EnsureIcons
    std::unordered_map<ID2D1Bitmap*, ComPtr<ID2D1BitmapBrush>> m_bmpBrushCache;

    ComPtr<IDWriteTextFormat> m_popupTextFormat;
    ComPtr<IDWriteTextFormat> m_searchTextFormat;

    // Search and Tab bar state variables
    bool m_searchActive;
    std::wstring m_searchQuery;

    struct SearchResultItem
    {
        RendShortcutInfo shortcut;
        ID2D1Bitmap* bitmap = nullptr;
        int originalPageIndex = -1;
        int originalShortcutIndex = -1;
    };
    std::vector<SearchResultItem> m_searchResults;
    int m_selectedSearchResult;
    int m_hoveredTab;
    int m_hoveredDock;   // index into m_dockPage.shortcuts, -1 if none
    bool m_cursorBlink;

    // EventBus subscription token (for cleanup)
    EventBus::Token m_configChangedToken = 0;
    EventBus::Token m_themeChangedToken = 0;

    bool m_refreshingIcons = false;
};
