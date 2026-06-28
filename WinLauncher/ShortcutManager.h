#pragma once
#include <Windows.h>
#include <string>
#include <vector>
#include "Model/ShortcutInfo.h"

struct ID2D1Bitmap;

// Legacy rendering-extended types (used by UI layer for bitmap management)
// These coexist alongside Model:: types via different namespaces
struct RendShortcutInfo
{
    std::wstring name;
    std::wstring targetPath;
    std::wstring arguments;
    std::wstring iconPath;
    HICON        hIcon = nullptr;
    bool         runAsAdmin = false;
    Model::ShortcutType type = Model::ShortcutType::File;
    Model::ShortcutTargetKind targetKind = Model::ShortcutTargetKind::Unknown;
    Model::IconSource iconSource = Model::IconSource::Auto;
    std::wstring builtinIconId;
    bool         iconInvertLight = false;
    bool         iconInvertDark = false;
};

struct RendPopupPage
{
    std::wstring name;
    std::vector<RendShortcutInfo> shortcuts;
    std::vector<ID2D1Bitmap*> iconBitmaps;
    bool isSyncFolder = false;
    std::wstring folderPath;
};

// Legacy facade for config loading/icon extraction
class ShortcutManager
{
public:
    static std::vector<RendShortcutInfo> LoadShortcuts(const std::wstring& configDir);
    static void FreeShortcuts(std::vector<RendShortcutInfo>& shortcuts);
    static std::wstring FindConfigDir();
    static HICON GetShortcutIcon(const std::wstring& targetPath);
    static HICON GetShortcutIcon(const RendShortcutInfo& shortcut);
    static HICON GetShortcutIcon(const Model::ShortcutInfo& shortcut);
    static Model::ShortcutTargetKind InferTargetKind(const std::wstring& path);

    static std::vector<RendPopupPage> LoadConfig(const std::wstring& configDir);
    static void SaveConfig(const std::wstring& configDir, const std::vector<RendPopupPage>& pages);

    // Re-extract icons for all shortcuts (destroys existing HICONs and reloads)
    static void RefreshShortcutIcons(std::vector<RendPopupPage>& pages);
    static void RefreshShortcutIcon(RendShortcutInfo& shortcut);
};
