#pragma once
#include <string>
#include <vector>

namespace Model
{
    struct ShortcutInfo
    {
        std::wstring name;
        std::wstring targetPath;
        std::wstring arguments;
        std::wstring iconPath;

        bool IsValid() const { return !targetPath.empty(); }
    };

    struct PopupPage
    {
        std::wstring name;
        std::vector<ShortcutInfo> shortcuts;
        bool isSyncFolder = false;
        std::wstring folderPath;
    };
}
