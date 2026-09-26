#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <set>
#include <filesystem>
#include <functional>

struct AppContext;

namespace ConfigMaintenanceOps
{
    struct CacheCleanupResult
    {
        size_t deletedFiles = 0;
        uintmax_t releasedBytes = 0;
        size_t failedItems = 0;
    };

    std::wstring FormatHistoryTime(unsigned long long fileTimeValue);
    std::wstring FormatReleasedSize(uintmax_t bytes);
    std::wstring NormalizePathForComparison(const std::filesystem::path& path);
    bool IsPathWithinDirectory(const std::wstring& path, const std::wstring& directory);
    CacheCleanupResult CleanupUserDataDirectory(const std::filesystem::path& root, const std::set<std::wstring>& preservedFiles);

    void OpenLogFile(HWND hwnd);
    void OpenConfigDir(HWND hwnd);
    void OpenConfigHistoryDir(HWND hwnd, const std::wstring& dir);
    void CreateDiagnosticPackage(HWND hwnd, AppContext* appCtx);
    void ExportMigrationBackup(HWND hwnd, AppContext* appCtx);
    bool ImportMigrationBackup(HWND hwnd, AppContext* appCtx);
    void ClearUsageHistory(HWND hwnd, AppContext* appCtx);
    void ClearCache(HWND hwnd, AppContext* appCtx, const std::function<void()>& onFlushConfig);
    void CreateConfigBackupNow(HWND hwnd, AppContext* appCtx, const std::function<void()>& onSaveConfig);
    bool RestoreLatestConfigBackup(HWND hwnd, AppContext* appCtx, const std::function<void()>& onSaveConfig);
    bool ClearConfigData(HWND hwnd, AppContext* appCtx, const std::function<void()>& onSaveConfig);
    void ClearConfigHistoryData(HWND hwnd, AppContext* appCtx);
}
