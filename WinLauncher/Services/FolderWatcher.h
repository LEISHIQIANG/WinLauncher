#pragma once
#include <Windows.h>
#include <cstdint>
#include <vector>
#include <string>
#include <memory>

struct FolderAutoPauseRequest
{
    std::wstring folderPath;
    DWORD errorCode = ERROR_SUCCESS;
    uint64_t generation = 0;
};

class FolderWatcher
{
public:
    static constexpr unsigned int AutoPauseFailureCount = 3;
    static constexpr bool ShouldAutoPause(unsigned int consecutiveFailures) noexcept
    {
        return consecutiveFailures >= AutoPauseFailureCount;
    }

    FolderWatcher();
    ~FolderWatcher();

    // After three consecutive failures, autoPauseMessage receives an owned
    // FolderAutoPauseRequest through lParam. The receiver must delete it.
    void UpdateFolders(const std::vector<std::wstring>& folders, HWND hWndNotify, UINT msgNotify,
        UINT autoPauseMessage = 0);
    void Start();
    void Stop();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
