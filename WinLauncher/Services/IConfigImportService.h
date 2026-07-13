#pragma once
#include <string>
#include <vector>
#include "../Model/ShortcutInfo.h"

class IConfigImportService
{
public:
    virtual ~IConfigImportService() = default;

    struct ImportResult
    {
        std::vector<Model::PopupPage> pages;
        bool hasAutoStartSetting = false;
        bool autoStart = false;
        int popupColumns = 0;
        int popupRows = 0;
        int dockHeight = -1;
        int popupIconSize = 0;
        int globalScalePercent = 0;
        int theme = -1;
        int sortMode = -1;
        int popupAlignMode = -1;
        bool hasHideTrayIcon = false;
        bool hideTrayIcon = false;
        bool hasHardwareAcceleration = false;
        bool hardwareAcceleration = false;
        bool hasSearchMode = false;
        bool searchMode = false;
        bool hasPopupAutoClose = false;
        bool popupAutoClose = true;
        bool hasPopupMultiOpenWhenPinned = false;
        bool popupMultiOpenWhenPinned = false;
        int hoverLeaveDelay = -1;
        int importedItems = 0;
        int skippedItems = 0;
        int copiedIcons = 0;
        std::wstring errorMsg;
        bool success = false;
    };

    virtual ImportResult Import(const std::wstring& filePath, const std::wstring& configDir) = 0;
};
