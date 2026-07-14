#pragma once
#include "GlassWindow.h"
#include "ShortcutManager.h"
#include "ViewModel/PopupViewModel.h"
#include "Contracts/IIconService.h"
#include "Services/AppSceneMatcher.h"
#include <vector>
#include <unordered_map>
#include <wrl.h>
#include "Config/TextBox.h"
#include "Services/FileSelectionService.h"
#include "Popup/PopupFileSelectionController.h"
#include "Popup/PopupIconRefreshController.h"
#include "Popup/PopupSearchService.h"
#include <mutex>
#include <memory>
#include <atomic>

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
    static bool ExecuteShortcut(const RendShortcutInfo& sc, HWND parent, AppContext* ctx, const std::vector<std::wstring>& selectedFiles = {});
    static HWND GetRestoreForegroundWindow();
    static HWND GetHWNDStatic();

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
    int GetHeaderSizeLevel() const;

    void UpdateWindowSize();

protected:
    virtual const wchar_t* ClassName() const override { return L"WinLauncherPopup"; }
    virtual LRESULT HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override;
    virtual void OnPaintContent(ID2D1HwndRenderTarget* rt) override;
    virtual bool ShouldAutoResizeOnDpiChange() const override { return false; }

private:
    struct HeaderLayout
    {
        int topBarHeight;
        float controlHeight;
        float textSize;
        float tabHoverRadius;
        float selectionIndicatorWidth;
        float searchTextInset;
    };

    HeaderLayout GetHeaderLayout() const;
    int GetFileSelectionValiditySeconds() const;
    bool IsFileSelectionValid(double elapsedSeconds) const;
    void ClearCapturedFileSelection();
    int HitTest(POINT pt);
    int HitTestDot(POINT pt);
    int HitTestDock(POINT pt);
    void EnsureIcons();
    void RefreshIcons(bool clearExisting = true);
    void ApplyRefreshedIcons();
    void CancelIconRefresh();
    void DrawPage(ID2D1HwndRenderTarget* rt, int pageIndex);
    void ClearPages();
    void OnConfigChanged();
    void StartAutoHideTimer();
    void StopAutoHideTimer();
    float GetFontSize() const;
    float GetSearchFontSize() const;
    int GetLabelHeight() const;
    void UpdateTextFormat();

    void UpdateSearch();
    void UpdateImeWindowPosition();
    void SavePopupConfig();
    void ShowAt(HWND parent, POINT pt);
    void HideSelf();
    void DestroySelf();
    void DrawTopBar(ID2D1HwndRenderTarget* rt);
    void DrawSearchResults(ID2D1HwndRenderTarget* rt);
    void DrawDock(ID2D1HwndRenderTarget* rt);
    void LaunchShortcut(const RendShortcutInfo& sc);
    void ExecuteSearchResult(int index);
    void StartPageAnimationLoop();
    void StepPageAnimationFrame(HWND hWnd);
    void ResetPressedShortcut();
    int ToModelPageIndex(int renderPageIndex) const;

    enum class PressedShortcutKind
    {
        None,
        Page,
        Dock,
        SearchResult
    };

    static PopupWindow* s_instance;
    static std::vector<PopupWindow*> s_extraWindows;
    static void PruneExtraWindows();
    static void RemoveExtraWindow(PopupWindow* window);
    static PopupWindow* FindByHwnd(HWND hwnd);

    std::unique_ptr<PopupViewModel> m_viewModel;
    std::unique_ptr<IIconService> m_iconService; // public for helper function access
    std::wstring m_configDir;

    std::vector<RendPopupPage> m_pages;  // Legacy: pages with icon bitmaps for rendering
    std::vector<int> m_pageModelIndices;
    int m_currentPage;
    RendPopupPage m_dockPage;  // Fixed dock bar (DOCK category)
    AppScene::AppIdentity m_sceneApp;

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
    ComPtr<IDWriteTextFormat> m_tabTextFormat;

    // Search and Tab bar state variables
    bool m_searchActive;
    std::wstring m_searchQuery;

    using SearchResultItem = PopupSearchService::SearchResult;
    std::vector<SearchResultItem> m_searchResults;
    int m_selectedSearchResult;
    int m_hoveredTab;
    int m_hoveredDock;   // index into m_dockPage.shortcuts, -1 if none
    bool m_cursorBlink;
    bool m_destroyOnHide = false;
    double m_showTimeSeconds = 0.0;
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
    BackgroundTaskService::TaskHandle m_iconRefreshTask;
    PopupIconRefreshController m_iconRefresh;

    PopupFileSelectionController m_fileSelection;
    void StartFileSelectionQuery(HWND activeHwnd, POINT clickPt, POINT popupCenter);
    void CancelFileSelectionQuery();
    void PollFileSelectionQuery();

    friend std::wstring ExpandVariables(const std::wstring& inputStr, HWND parent, AppContext* ctx, bool& cancelled);
};
