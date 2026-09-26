#pragma once
#include <windows.h>
#include <shellapi.h>
#include <string>

class IConfigWindow;
class SettingsPage;

class SettingsPluginActions
{
public:
    static bool IsPluginPackagePath(const std::wstring& filePath);
    static bool InstallPluginPackageFromPath(IConfigWindow* owner, const std::wstring& filePath, bool showSuccessMessage, std::wstring* errorMessage = nullptr);
    static bool HandleLButtonDown(SettingsPage* page, IConfigWindow* owner, POINT pt, bool& repaint);
    void static HandleDropFiles(SettingsPage* page, IConfigWindow* owner, HDROP hDrop, bool& repaint);
};
