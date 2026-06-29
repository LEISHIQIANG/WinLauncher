#pragma once
#include "GlassWindow.h"
#include "ShortcutManager.h"
#include "ViewModel/PopupViewModel.h"
#include "Services/IIconService.h"
#include <vector>
#include <unordered_map>
#include <wrl.h>
#include "Config/TextBox.h"
#include "Services/FileSelectionService.h"
#include <mutex>

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
    static bool ExecuteShortcut(const RendShortcutInfo& sc, HWND parent, AppContext* ctx);
    static HWND GetRestoreForegroundWindow();

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
    virtual bool ShouldAutoResizeOnDpiChange() const override { return false; }

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
    void UpdateImeWindowPosition();
    void SavePopupConfig();
    void DrawTopBar(ID2D1HwndRenderTarget* rt);
    void DrawSearchResults(ID2D1HwndRenderTarget* rt);
    void DrawDock(ID2D1HwndRenderTarget* rt);
    void LaunchShortcut(const RendShortcutInfo& sc);
    void StartPageAnimationLoop();
    void StepPageAnimationFrame(HWND hWnd);
    void ResetPressedShortcut();

    enum class PressedShortcutKind
    {
        None,
        Page,
        Dock,
        SearchResult
    };

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
    float m_lastDpi;
    int m_lastIconBitmapSize = 0;

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
    HWND m_restoreForegroundWnd = nullptr;
    PressedShortcutKind m_pressedShortcutKind = PressedShortcutKind::None;
    int m_pressedShortcutIndex = -1;
    int m_pressedShortcutPage = -1;

    TextBox m_searchTextBox;
    bool m_searchTextBoxCreated = false;
    // EventBus subscription token (for cleanup)
    EventBus::Token m_configChangedToken = 0;
    EventBus::Token m_themeChangedToken = 0;
    EventBus::Token m_bgStyleChangedToken = 0;
    EventBus::Token m_uiScaleChangedToken = 0;
    bool m_refreshingIcons = false;

    std::mutex m_selectedFilesMutex;
    Services::SelectionContext m_selectedFilesCtx;
    void StartFileSelectionQuery(HWND activeHwnd, POINT clickPt, POINT popupCenter);
};
