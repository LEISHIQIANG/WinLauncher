#pragma once
#include <string>
#include <vector>
#include <windows.h>
#include <functional>

namespace Services
{
    struct SelectionContext
    {
        std::vector<std::wstring> filePaths;
        HWND sourceHwnd = nullptr;
        double capturedTime = 0.0;
        bool isPending = false;
    };

    class FileSelectionService
    {
    public:
        // Asynchronously captures selected files from the specified foreground window.
        // Runs COM operations in a background thread to prevent UI thread blocking.
        static void CaptureSelectedFilesAsync(HWND activeHwnd, POINT clickPt, POINT popupCenter, std::function<void(const SelectionContext&)> callback);

        // Synchronously retrieves selected files from the specified window.
        static std::vector<std::wstring> GetSelectedFiles(HWND hwnd, POINT clickPt, POINT popupCenter);
    };
}
