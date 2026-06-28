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
        bool hasAutoStart = false;
        int popupColumns = 0;
        int popupRows = 0;
        int dockHeight = 0;
        std::wstring errorMsg;
        bool success = false;
    };

    virtual ImportResult Import(const std::wstring& filePath, const std::wstring& configDir) = 0;
};
