#pragma once
#include <string>
#include <vector>
#include <windows.h>
#include <functional>
#include <memory>
#include <mutex>
#include "../App/BackgroundTaskService.h"

namespace Services
{
    struct SelectionContext
    {
        std::vector<std::wstring> filePaths;
        HWND sourceHwnd = nullptr;
        double capturedTime = 0.0;
        bool isPending = false;
    };

    class SelectionRequest
    {
    public:
        void Cancel() noexcept { m_cancelled.store(true); if (m_task) m_task.Cancel(); }
        bool IsCancelled() const noexcept { return m_cancelled.load(); }
        bool IsCompleted() const noexcept { return m_completed.load(); }
        bool TryGetResult(SelectionContext& result) const
        {
            if (!m_completed.load()) return false;
            std::lock_guard<std::mutex> lock(m_mutex);
            result = m_result;
            return true;
        }

    private:
        friend class FileSelectionService;
        mutable std::mutex m_mutex;
        SelectionContext m_result;
        std::atomic_bool m_completed{ false };
        std::atomic_bool m_cancelled{ false };
        BackgroundTaskService::TaskHandle m_task;
    };

    class FileSelectionService
    {
    public:
        // Asynchronously captures selected files from the specified foreground window.
        // Runs COM operations in a background thread to prevent UI thread blocking.
        static std::shared_ptr<SelectionRequest> CaptureSelectedFilesAsync(
            HWND activeHwnd,
            POINT clickPt,
            POINT popupCenter,
            const std::shared_ptr<BackgroundTaskService>& tasks);

        // Synchronously retrieves selected files from the specified window.
        static std::vector<std::wstring> GetSelectedFiles(HWND hwnd, POINT clickPt, POINT popupCenter);
    };
}
