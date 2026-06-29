#pragma once
#include "../GlassWindow.h"
#include "../ShortcutManager.h"
#include "../ViewModel/ConfigViewModel.h"
#include "../Services/IIconService.h"
#include "../Services/IIconService.h"
#include "IConfigWindow.h"
#include "CategoryList.h"
#include "ShortcutPage.h"
#include "SettingsPage.h"
#include "DropDownMenu.h"
#include "UIStyle.h"
#include <vector>
#include <string>
#include <wrl.h>

using Microsoft::WRL::ComPtr;

class ConfigWindow : public GlassWindow, public IConfigWindow
{
public:
    ConfigWindow(AppContext* ctx);
    virtual ~ConfigWindow() override;

    static void Show(HWND parent, AppContext* ctx = nullptr);
    static void ShowConfig(HWND parent, AppContext* ctx = nullptr);
    static void ShowSettings(HWND parent, AppContext* ctx = nullptr);
    static void Hide();
    static void Release();
    static bool IsVisible();

    // IConfigWindow implementation
    virtual void NotifyConfigChanged() override;
    virtual HWND GetWindowHWND() override;
    virtual std::wstring GetConfigDir() override;
    virtual std::wstring GetConfigFilePath() override;
    virtual void OpenConfigFile() override;
    virtual void OpenLogFile() override;
    virtual void OpenConfigDir() override;
    virtual void StartAnimation() override;
    virtual IDWriteTextFormat* GetLeftFont() override { return m_tfLeft.Get(); }
    virtual IDWriteTextFormat* GetTitleFont() override { return m_tfTitle.Get(); }
    virtual IDWriteTextFormat* GetHeaderFont() override { return m_tfHeader.Get(); }
    virtual IDWriteTextFormat* GetDefaultFont() override { return m_tf.Get(); }
    virtual ID2D1Bitmap* CreateD2DBitmapFromHicon(HICON hIcon, const std::wstring& name = L"", bool invert = false) override;

    virtual size_t GetCategoryCount() override;
    virtual std::wstring GetCategoryName(size_t index) override;
    virtual int GetCurrentCategoryIndex() override;
    virtual void SetCurrentCategoryIndex(int index) override;
    virtual void AddCategory(const std::wstring& name) override;
    virtual void DeleteCategory(int index) override;
    virtual void RenameCategory(int index, const std::wstring& newName) override;
    virtual void ReorderCategories(int fromIndex, int toIndex) override;
    virtual RendPopupPage* GetPageByIndex(int index) override;

    virtual AppContext* GetAppContext() override { return m_appCtx; }
    virtual bool IsSettingsMode() override { return m_showSettings; }
    virtual bool IsDraggingShortcut() override { return m_shortcutPage.IsDragging(); }
    virtual int GetTriggerType() override;
    virtual void SetTriggerType(int type) override;
    virtual bool GetAutoStart() override;
    virtual void SetAutoStart(bool enable) override;
    virtual bool GetHideTrayIcon() override;
    virtual void SetHideTrayIcon(bool hide) override;

    virtual int GetPopupColumns() override;
    virtual void SetPopupColumns(int columns) override;
    virtual int GetPopupRows() override;
    virtual void SetPopupRows(int rows) override;
    virtual int GetPopupIconSize() override;
    virtual void SetPopupIconSize(int size) override;
    virtual int GetPopupIconGap() override;
    virtual void SetPopupIconGap(int gap) override;
    virtual int GetPopupIconRadius() override;
    virtual void SetPopupIconRadius(int radius) override;
    virtual int GetPopupWndPadding() override;
    virtual void SetPopupWndPadding(int padding) override;
    virtual int GetTheme() override;
    virtual void SetTheme(int theme, POINT clickPt = { -1, -1 }) override;
    virtual int GetThemeColor() override;
    virtual void SetThemeColor(int colorIndex, POINT clickPt = { -1, -1 }) override;
    virtual int GetWindowMode() override;
    virtual void SetWindowMode(int mode, POINT clickPt = { -1, -1 }) override;
    virtual int GetGlobalScalePercent() override;
    virtual void SetGlobalScalePercent(int percent) override;
    virtual int GetDockHeight() override;
    virtual void SetDockHeight(int height) override;
    virtual bool GetAnimationEnabled() override;
    virtual void SetAnimationEnabled(bool enabled) override;
    virtual int GetAnimationDuration() override;
    virtual void SetAnimationDuration(int duration) override;
    virtual bool GetHardwareAccelerationEnabled() override;
    virtual void SetHardwareAccelerationEnabled(bool enabled) override;
    void ImportJsonConfig();

protected:
    virtual const wchar_t* ClassName() const override { return L"WinLauncherConfig"; }
    virtual LRESULT HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override;
    virtual void OnPaintContent(ID2D1HwndRenderTarget* rt) override;

private:
    void EnsureIcons();
    void ClearPages();
    void LoadConfig();
    void SaveConfig();
    static void ShowMode(HWND parent, AppContext* ctx, bool settingsMode);
    void SetSettingsMode(bool settingsMode);
    void ResizeToCurrentScale();
    bool HitTestCloseButton(POINT pt);
    bool HitTestSettingsButton(POINT pt);
    bool HitTestAddButton(POINT pt);
    void DrawAddButton(ID2D1HwndRenderTarget* rt);
    void HandleCategoryDrop(HDROP hDrop, bool& repaint);
    void AddSyncCategory(const std::wstring& name, const std::wstring& folderPath);

    static ConfigWindow* s_instance;
    static AppContext* s_ctx;

    std::unique_ptr<ConfigViewModel> m_viewModel;
    std::wstring m_configDir;
    std::vector<RendPopupPage> m_pages;
    int m_currentCategory;

    // Text formats
    ComPtr<IDWriteTextFormat> m_tfLeft;
    ComPtr<IDWriteTextFormat> m_tfTitle;
    ComPtr<IDWriteTextFormat> m_tfHeader;

    // Sub-components
    CategoryList m_categoryList;
    ShortcutPage m_shortcutPage;
    SettingsPage m_settingsPage;
    ConfigPage* m_currentPage;

    // View state
    bool m_showSettings;
    int m_currentSettingsCategory;

    // Hover states
    bool m_hoveredClose;
    bool m_hoveredSettingsBtn;
    bool m_hoveredAddBtn;
    bool m_trackMouse;

    ID2D1HwndRenderTarget* m_lastRt;
    EventBus::Token m_themeChangedToken = 0;
    EventBus::Token m_configChangedToken = 0;

    // Animation tracking state
    bool m_animating = false;
    double m_animLastTime = 0.0;
    double GetTimeInSeconds();

    int m_ignoreConfigChangedCount = 0;
};
