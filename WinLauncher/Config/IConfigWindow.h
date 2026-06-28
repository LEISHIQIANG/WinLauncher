#pragma once
#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string>

struct AppContext;
struct RendPopupPage;

class IConfigWindow
{
public:
    virtual ~IConfigWindow() = default;

    virtual void NotifyConfigChanged() = 0;
    virtual HWND GetWindowHWND() = 0;
    virtual std::wstring GetConfigDir() = 0;
    virtual std::wstring GetConfigFilePath() = 0;
    virtual void OpenConfigFile() = 0;
    virtual void OpenLogFile() = 0;
    virtual void OpenConfigDir() = 0;
    virtual void StartAnimation() = 0;
    virtual IDWriteTextFormat* GetLeftFont() = 0;
    virtual IDWriteTextFormat* GetTitleFont() = 0;
    virtual IDWriteTextFormat* GetHeaderFont() = 0;
    virtual IDWriteTextFormat* GetDefaultFont() = 0;

    virtual ID2D1Bitmap* CreateD2DBitmapFromHicon(HICON hIcon, const std::wstring& name = L"") = 0;

    virtual size_t GetCategoryCount() = 0;
    virtual std::wstring GetCategoryName(size_t index) = 0;
    virtual int GetCurrentCategoryIndex() = 0;
    virtual void SetCurrentCategoryIndex(int index) = 0;
    virtual void AddCategory(const std::wstring& name) = 0;
    virtual void DeleteCategory(int index) = 0;
    virtual void RenameCategory(int index, const std::wstring& newName) = 0;
    virtual void ReorderCategories(int fromIndex, int toIndex) = 0;
    virtual RendPopupPage* GetPageByIndex(int index) = 0;

    virtual bool IsSettingsMode() = 0;
    virtual bool IsDraggingShortcut() = 0;
    virtual int GetTriggerType() = 0;
    virtual void SetTriggerType(int type) = 0;
    virtual bool GetAutoStart() = 0;
    virtual void SetAutoStart(bool enable) = 0;

    virtual int GetPopupColumns() = 0;
    virtual void SetPopupColumns(int columns) = 0;
    virtual int GetPopupRows() = 0;
    virtual void SetPopupRows(int rows) = 0;
    virtual int GetPopupIconSize() = 0;
    virtual void SetPopupIconSize(int size) = 0;
    virtual int GetPopupIconGap() = 0;
    virtual void SetPopupIconGap(int gap) = 0;
    virtual int GetPopupIconRadius() = 0;
    virtual void SetPopupIconRadius(int radius) = 0;
    virtual int GetPopupWndPadding() = 0;
    virtual void SetPopupWndPadding(int padding) = 0;

    virtual int GetTheme() = 0;
    virtual void SetTheme(int theme) = 0;
    virtual int GetThemeColor() = 0;
    virtual void SetThemeColor(int colorIndex) = 0;
    virtual int GetWindowMode() = 0;
    virtual void SetWindowMode(int mode) = 0;
    virtual AppContext* GetAppContext() = 0;
    virtual int GetDockHeight() = 0;
    virtual void SetDockHeight(int height) = 0;
};
