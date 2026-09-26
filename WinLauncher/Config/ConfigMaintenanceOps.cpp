#define NOMINMAX
#include "ConfigMaintenanceOps.h"
#include "ConfirmWindow.h"
#include "WaitWindow.h"
#include "../App/AppContext.h"
#include "../Services/ConfigPath.h"
#include "../Services/MigrationBackupService.h"
#include <shlobj.h>
#include <shellapi.h>
#include <commdlg.h>
#include <algorithm>
#include <cwctype>

namespace fs = std::filesystem;

namespace ConfigMaintenanceOps
{
    namespace
    {
        bool IsPreservedFile(const std::wstring& normalizedPath, const std::set<std::wstring>& preservedFiles)
        {
            return preservedFiles.find(normalizedPath) != preservedFiles.end();
        }

        bool IsPreservedRuntimeDirectory(const std::wstring& normalizedPath, const std::vector<std::wstring>& preservedDirectories)
        {
            return std::any_of(preservedDirectories.begin(), preservedDirectories.end(), [&](const std::wstring& directory) {
                return IsPathWithinDirectory(normalizedPath, directory);
            });
        }
    }

    std::wstring FormatHistoryTime(unsigned long long fileTimeValue)
    {
        if (fileTimeValue == 0)
            return L"未知时间";

        FILETIME ft{};
        ULARGE_INTEGER value{};
        value.QuadPart = fileTimeValue;
        ft.dwLowDateTime = value.LowPart;
        ft.dwHighDateTime = value.HighPart;

        FILETIME localFt{};
        SYSTEMTIME st{};
        if (!FileTimeToLocalFileTime(&ft, &localFt) || !FileTimeToSystemTime(&localFt, &st))
            return L"未知时间";

        wchar_t buf[32]{};
        swprintf_s(buf, L"%04u-%02u-%02u %02u:%02u:%02u",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        return buf;
    }

    std::wstring FormatReleasedSize(uintmax_t bytes)
    {
        if (bytes < 1024)
            return std::to_wstring(bytes) + L" B";
        if (bytes < 1024 * 1024)
            return std::to_wstring((bytes + 512) / 1024) + L" KB";
        return std::to_wstring((bytes + 512 * 1024) / (1024 * 1024)) + L" MB";
    }

    std::wstring NormalizePathForComparison(const fs::path& path)
    {
        std::error_code ec;
        fs::path normalized = fs::weakly_canonical(path, ec);
        if (ec)
        {
            ec.clear();
            normalized = fs::absolute(path, ec);
            if (ec)
                normalized = path;
        }

        std::wstring value = normalized.lexically_normal().wstring();
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
            return static_cast<wchar_t>(std::towlower(ch));
        });
        return value;
    }

    bool IsPathWithinDirectory(const std::wstring& path, const std::wstring& directory)
    {
        if (path == directory)
            return true;
        if (path.size() <= directory.size() || path.compare(0, directory.size(), directory) != 0)
            return false;
        return directory.back() == L'\\' || path[directory.size()] == L'\\';
    }

    CacheCleanupResult CleanupUserDataDirectory(const fs::path& root, const std::set<std::wstring>& preservedFiles)
    {
        CacheCleanupResult result;
        const std::vector<std::wstring> preservedDirectories = {
            NormalizePathForComparison(root / L"plugins" / L"installed"),
            NormalizePathForComparison(root / L"plugins" / L"state")
        };
        std::vector<fs::path> emptyDirectories;
        std::error_code ec;
        fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
        for (; !ec && it != end; it.increment(ec))
        {
            const fs::directory_entry& entry = *it;
            const std::wstring normalizedPath = NormalizePathForComparison(entry.path());
            if (IsPreservedRuntimeDirectory(normalizedPath, preservedDirectories))
            {
                if (entry.is_directory(ec))
                    it.disable_recursion_pending();
                ec.clear();
                continue;
            }

            if (entry.is_directory(ec))
            {
                emptyDirectories.push_back(entry.path());
                continue;
            }

            if (!entry.is_regular_file(ec) && !entry.is_symlink(ec))
            {
                ec.clear();
                continue;
            }
            if (IsPreservedFile(normalizedPath, preservedFiles))
                continue;

            std::error_code sizeError;
            const uintmax_t size = entry.is_regular_file(sizeError) ? entry.file_size(sizeError) : 0;
            DWORD attributes = GetFileAttributesW(entry.path().c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_READONLY))
                SetFileAttributesW(entry.path().c_str(), attributes & ~FILE_ATTRIBUTE_READONLY);

            ec.clear();
            if (fs::remove(entry.path(), ec))
            {
                result.deletedFiles++;
                result.releasedBytes += size;
            }
            else
            {
                result.failedItems++;
            }
            ec.clear();
        }
        if (ec)
        {
            result.failedItems++;
            ec.clear();
        }

        for (auto dir = emptyDirectories.rbegin(); dir != emptyDirectories.rend(); ++dir)
        {
            const std::wstring normalizedPath = NormalizePathForComparison(*dir);
            if (IsPreservedRuntimeDirectory(normalizedPath, preservedDirectories))
                continue;
            ec.clear();
            fs::remove(*dir, ec);
        }
        return result;
    }

    void OpenLogFile(HWND hwnd)
    {
        std::wstring path = ConfigPath::GetUserLogDirectory() + L"\\current.jsonl";
        if (!path.empty())
            ShellExecuteW(hwnd, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    void OpenConfigDir(HWND hwnd)
    {
        std::wstring dir = ConfigPath::GetUserDataDirectory();
        if (!dir.empty())
            ShellExecuteW(hwnd, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    void OpenConfigHistoryDir(HWND hwnd, const std::wstring& dir)
    {
        if (!dir.empty())
            ShellExecuteW(hwnd, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    void CreateDiagnosticPackage(HWND hwnd, AppContext* appCtx)
    {
        if (!appCtx || !appCtx->diagnostics) return;
        // Build default filename with timestamp
        wchar_t stamp[32]{}; SYSTEMTIME st{}; GetLocalTime(&st);
        swprintf_s(stamp, L"%04u%02u%02u-%02u%02u%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        std::wstring defaultName = std::wstring(L"WinLauncher-diagnostic-") + stamp + L".zip";
        // Resolve Desktop as initial directory
        wchar_t desktopPath[MAX_PATH]{};
        SHGetFolderPathW(nullptr, CSIDL_DESKTOP, nullptr, SHGFP_TYPE_CURRENT, desktopPath);
        // Show Save dialog
        wchar_t filePath[MAX_PATH]{};
        wcsncpy_s(filePath, defaultName.c_str(), _TRUNCATE);
        OPENFILENAMEW ofn{};
        ofn.lStructSize   = sizeof(ofn);
        ofn.hwndOwner     = hwnd;
        ofn.lpstrFilter   = L"ZIP 文件 (*.zip)\0*.zip\0";
        ofn.lpstrFile     = filePath;
        ofn.nMaxFile      = MAX_PATH;
        ofn.lpstrInitialDir = desktopPath;
        ofn.lpstrTitle    = L"保存诊断包";
        ofn.lpstrDefExt   = L"zip";
        ofn.Flags         = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        if (!GetSaveFileNameW(&ofn)) return; // user cancelled
        std::wstring path = filePath, error;
        bool ok = appCtx->diagnostics->CreatePackage(path, error);
        std::wstring message = ok ? std::wstring(L"已在本地生成脱敏诊断包：\n") + path + L"\n不会自动上传。" : error;
        ConfirmWindow::Show(hwnd, ok ? L"诊断包已生成" : L"诊断包生成失败", message.c_str(), appCtx, false);
    }

    void ExportMigrationBackup(HWND hwnd, AppContext* appCtx)
    {
        // Build default filename with timestamp
        wchar_t stamp[32]{}; SYSTEMTIME st{}; GetLocalTime(&st);
        swprintf_s(stamp, L"%04u%02u%02u-%02u%02u%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        std::wstring defaultName = std::wstring(L"WinLauncher-migration-") + stamp + L".zip";
        // Resolve Desktop as initial directory
        wchar_t desktopPath[MAX_PATH]{};
        SHGetFolderPathW(nullptr, CSIDL_DESKTOP, nullptr, SHGFP_TYPE_CURRENT, desktopPath);
        // Show Save dialog
        wchar_t filePath[MAX_PATH]{};
        wcsncpy_s(filePath, defaultName.c_str(), _TRUNCATE);
        OPENFILENAMEW ofn{};
        ofn.lStructSize   = sizeof(ofn);
        ofn.hwndOwner     = hwnd;
        ofn.lpstrFilter   = L"ZIP 文件 (*.zip)\0*.zip\0";
        ofn.lpstrFile     = filePath;
        ofn.nMaxFile      = MAX_PATH;
        ofn.lpstrInitialDir = desktopPath;
        ofn.lpstrTitle    = L"保存迁移备份";
        ofn.lpstrDefExt   = L"zip";
        ofn.Flags         = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        if (!GetSaveFileNameW(&ofn)) return; // user cancelled
        std::wstring path = filePath;
        MigrationBackupService service; auto result = service.Export(path);
        std::wstring message = result.ok ? result.message + L"：\n" + path : result.message;
        ConfirmWindow::Show(hwnd, result.ok ? L"迁移备份已导出" : L"迁移备份失败", message.c_str(), appCtx, false);
    }

    bool ImportMigrationBackup(HWND hwnd, AppContext* appCtx)
    {
        wchar_t path[MAX_PATH]{}; OPENFILENAMEW ofn{}; ofn.lStructSize=sizeof(ofn); ofn.hwndOwner=hwnd; ofn.lpstrFilter=L"WinLauncher migration (*.zip)\0*.zip\0"; ofn.lpstrFile=path; ofn.nMaxFile=MAX_PATH; ofn.Flags=OFN_FILEMUSTEXIST;
        if (!GetOpenFileNameW(&ofn)) return false;
        MigrationBackupService service; auto check=service.Preflight(path);
        if (!check.ok) { ConfirmWindow::Show(hwnd, L"迁移包无效", check.message.c_str(), appCtx, false); return false; }
        auto result=service.Restore(path);
        ConfirmWindow::Show(hwnd, result.ok ? L"迁移恢复完成" : L"迁移恢复失败", result.message.c_str(), appCtx, false);
        return result.ok;
    }

    void ClearUsageHistory(HWND hwnd, AppContext* appCtx)
    {
        bool ok=appCtx && appCtx->usageHistory && appCtx->usageHistory->Clear();
        ConfirmWindow::Show(hwnd, ok ? L"使用记录已清除" : L"清除失败", ok ? L"智能图标排序记录已清除。" : L"无法清除本地使用记录。", appCtx, false);
    }

    void ClearCache(HWND hwnd, AppContext* appCtx, const std::function<void()>& onFlushConfig)
    {
        if (!appCtx || !appCtx->configService)
            return;

        if (!ConfirmWindow::Show(hwnd, L"清理缓存",
            L"清理未使用图标、日志及临时文件？\n"
            L"配置、在用图标和已装插件会保留。",
            appCtx))
        {
            return;
        }

        // Flush the current edits first, then derive the protected icon list from exactly that file.
        if (onFlushConfig)
        {
            onFlushConfig();
        }

        const fs::path dataRoot = ConfigPath::GetUserDataDirectory();
        std::set<std::wstring> preservedFiles = {
            NormalizePathForComparison(appCtx->configService->GetConfigFilePath())
        };
        for (const auto& page : appCtx->configService->LoadConfig())
        {
            for (const auto& shortcut : page.shortcuts)
            {
                if (shortcut.iconPath.empty())
                    continue;

                std::error_code iconError;
                const fs::path iconPath(shortcut.iconPath);
                if (fs::is_regular_file(iconPath, iconError) &&
                    IsPathWithinDirectory(NormalizePathForComparison(iconPath), NormalizePathForComparison(dataRoot)))
                {
                    preservedFiles.insert(NormalizePathForComparison(iconPath));
                }
            }
        }

        CacheCleanupResult result;
        WaitWindow::Show(hwnd, L"正在清理", L"正在清理未使用的缓存文件，请稍候...",
            [&]() {
                result = CleanupUserDataDirectory(dataRoot, preservedFiles);
            }, appCtx);

        const bool complete = result.failedItems == 0;
        std::wstring message;
        if (complete)
        {
            message = L"已删除 " + std::to_wstring(result.deletedFiles) + L" 个未使用文件，释放 " +
                FormatReleasedSize(result.releasedBytes) + L"。\n配置、在用图标和已安装插件均已保留。";
        }
        else
        {
            message = L"已删除 " + std::to_wstring(result.deletedFiles) + L" 个文件，释放 " +
                FormatReleasedSize(result.releasedBytes) + L"。\n另有 " + std::to_wstring(result.failedItems) + L" 项未清理，关闭程序后重试。";
        }
        ConfirmWindow::Show(hwnd, complete ? L"缓存清理完成" : L"缓存已部分清理", message.c_str(), appCtx, false);
    }

    void CreateConfigBackupNow(HWND hwnd, AppContext* appCtx, const std::function<void()>& onSaveConfig)
    {
        if (!appCtx || !appCtx->configService)
            return;

        if (onSaveConfig) onSaveConfig();
        bool ok = appCtx->configService->CreateConfigBackup(L"manual");
        ConfirmWindow::Show(hwnd, ok ? L"备份完成" : L"备份失败",
            ok ? L"已创建当前配置的手动备份，可在配置历史中回滚找回。"
               : L"未能创建配置备份，请检查配置目录权限或磁盘状态。",
            appCtx, false);
    }

    bool RestoreLatestConfigBackup(HWND hwnd, AppContext* appCtx, const std::function<void()>& onSaveConfig)
    {
        if (!appCtx || !appCtx->configService)
            return false;

        if (onSaveConfig) onSaveConfig();
        auto history = appCtx->configService->GetConfigHistory();
        if (history.empty())
        {
            ConfirmWindow::Show(hwnd, L"无法回滚", L"当前没有可用的配置历史。", appCtx, false);
            return false;
        }

        std::wstring prompt = L"将回滚到最近历史:\n" + FormatHistoryTime(history.front().lastWriteTime) +
            L"\n当前配置会先自动备份。是否继续？";
        if (!ConfirmWindow::Show(hwnd, L"回滚配置", prompt.c_str(), appCtx))
            return false;

        bool ok = appCtx->configService->RestoreConfigBackup(history.front().filePath);
        ConfirmWindow::Show(hwnd, ok ? L"回滚完成" : L"回滚失败",
            ok ? L"配置已恢复到最近历史，并已刷新当前窗口。"
               : L"未能恢复配置历史，请检查历史文件是否仍存在。",
            appCtx, false);
        return ok;
    }

    bool ClearConfigData(HWND hwnd, AppContext* appCtx, const std::function<void()>& onSaveConfig)
    {
        if (!appCtx || !appCtx->configService)
            return false;

        if (!ConfirmWindow::Show(hwnd, L"清除配置",
            L"只清空当前快捷方式和设置，不删除配置历史。\n清除前会立即自动备份当前配置。是否继续？",
            appCtx))
        {
            return false;
        }

        if (onSaveConfig) onSaveConfig();
        bool ok = appCtx->configService->ClearConfig();
        ConfirmWindow::Show(hwnd, ok ? L"配置已清除" : L"清除失败",
            ok ? L"已恢复默认配置，原配置已保存在配置历史中。"
               : L"未能清除配置，当前配置可能未完成备份或文件权限异常。",
            appCtx, false);
        return ok;
    }

    void ClearConfigHistoryData(HWND hwnd, AppContext* appCtx)
    {
        if (!appCtx || !appCtx->configService)
            return;

        if (!ConfirmWindow::Show(hwnd, L"清除配置历史",
            L"将删除所有配置历史备份。\n删除后无法从历史中回滚找回。是否继续？",
            appCtx))
        {
            return;
        }

        bool ok = appCtx->configService->ClearConfigHistory();
        ConfirmWindow::Show(hwnd, ok ? L"历史已清除" : L"清除失败",
            ok ? L"配置历史已清空。"
               : L"部分历史文件未能删除，请检查配置历史目录。",
            appCtx, false);
    }
}
