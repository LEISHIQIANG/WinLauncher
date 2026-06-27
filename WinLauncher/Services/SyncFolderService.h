#pragma once
#include <vector>
#include <string>
#include "../Model/ShortcutInfo.h"
#include "../ShortcutManager.h"

class SyncFolderService
{
public:
    static std::vector<Model::ShortcutInfo> LoadShortcuts(const std::wstring& folderPath);
    static std::vector<RendShortcutInfo> LoadRendShortcuts(const std::wstring& folderPath);
    static bool ShouldIgnoreFile(const WIN32_FIND_DATAW& ffd);
};
