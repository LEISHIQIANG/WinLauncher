#include "UpdateService.h"
#include "../App/AppContext.h"
#include "../App/Logger.h"
#include "../App/AppMessages.h"
#include "../version.h"
#include "JsonImportHelper.h"
#include <wininet.h>
#include <sstream>
#include <fstream>
#include <vector>

#pragma comment(lib, "wininet.lib")

namespace
{
    std::wstring EscapePowerShellSingleQuotedString(const std::wstring& value)
    {
        std::wstring escaped;
        escaped.reserve(value.size());
        for (wchar_t ch : value)
        {
            if (ch == L'\'')
            {
                escaped += L"''";
            }
            else
            {
                escaped += ch;
            }
        }
        return escaped;
    }
}

UpdateService& UpdateService::GetInstance()
{
    static UpdateService instance;
    return instance;
}

void UpdateService::CheckForUpdates(HWND notifyWnd, bool isSilent, AppContext* ctx)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_isChecking || m_isDownloading) return;
        m_isChecking = true;
        m_updatePromptClosed = false;
        if (notifyWnd) m_mainNotifyWnd = notifyWnd;
    }

    auto tasks = ctx ? ctx->backgroundTasks : nullptr;
    auto logger = ctx ? ctx->logger : std::shared_ptr<Logger>();
    auto handle = tasks ? tasks->Submit(L"update.check", BackgroundTaskService::Priority::Normal,
        [this, notifyWnd, isSilent, logger](const std::shared_ptr<BackgroundTaskService::CancellationToken>& cancellation) {
            if (!cancellation->IsCancellationRequested()) PerformCheck(notifyWnd, isSilent, logger.get());
        }) : BackgroundTaskService::TaskHandle{};
    if (!handle)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_isChecking = false;
        m_state = UpdateState::Error;
    }
}

void UpdateService::StartDownloadAndInstall(HWND parentWnd, AppContext* ctx)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_isDownloading) return;
        m_isDownloading = true;
        m_downloadProgress = 0;
    }

    auto tasks = ctx ? ctx->backgroundTasks : nullptr;
    auto logger = ctx ? ctx->logger : std::shared_ptr<Logger>();
    auto handle = tasks ? tasks->Submit(L"update.download", BackgroundTaskService::Priority::Normal,
        [this, parentWnd, logger](const std::shared_ptr<BackgroundTaskService::CancellationToken>& cancellation) {
            PerformDownloadAndInstall(parentWnd, logger.get(), cancellation);
        }) : BackgroundTaskService::TaskHandle{};
    if (!handle)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_isDownloading = false;
        m_state = UpdateState::Error;
    }
}

UpdateService::UpdateState UpdateService::GetState() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_state;
}

std::wstring UpdateService::GetLatestVersion() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_latestVersion;
}

std::wstring UpdateService::GetReleaseNotes() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_releaseNotes;
}

std::wstring UpdateService::GetDownloadUrl() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_downloadUrl;
}

int UpdateService::GetDownloadProgress() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_downloadProgress;
}

bool UpdateService::IsUpdatePromptClosed() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_updatePromptClosed;
}

void UpdateService::SetUpdatePromptClosed(bool closed)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_updatePromptClosed = closed;
}

#if MOCK_UPDATE_SERVICE

void UpdateService::PerformCheck(HWND notifyWnd, bool isSilent, Logger* logger)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_state = UpdateState::Checking;
        m_downloadProgress = 0;
        if (notifyWnd) m_mainNotifyWnd = notifyWnd;
    }

    if (notifyWnd && IsWindow(notifyWnd))
    {
        PostMessageW(notifyWnd, AppMessages::UpdateCheckCompleted, 0, 0);
    }
    else if (m_mainNotifyWnd && IsWindow(m_mainNotifyWnd))
    {
        PostMessageW(m_mainNotifyWnd, AppMessages::UpdateCheckCompleted, 0, 0);
    }

    // Simulate network delay
    Sleep(1500);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_latestVersion = L"v9.9.9.9";
        m_releaseNotes = L"测试伪装更新版本 v9.9.9.9\n- 这是一个测试更新提示与下载重启流程的伪装包\n- 移除了更新界面配置\n- 增加了左上角标题后显示主题色“更新”小标签机制";
        m_downloadUrl = L"https://dummyurl.com/WinLauncher_new.exe";
        m_state = UpdateState::NewVersionAvailable;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_isChecking = false;
    }

    if (notifyWnd && IsWindow(notifyWnd))
    {
        PostMessageW(notifyWnd, AppMessages::UpdateCheckCompleted, 0, 0);
    }
    if (m_mainNotifyWnd && m_mainNotifyWnd != notifyWnd && IsWindow(m_mainNotifyWnd))
    {
        PostMessageW(m_mainNotifyWnd, AppMessages::UpdateCheckCompleted, 0, 0);
    }
}

void UpdateService::PerformDownloadAndInstall(HWND parentWnd, Logger* logger, const std::shared_ptr<BackgroundTaskService::CancellationToken>& cancellation)
{
    // Simulate background downloading
    for (int p = 0; p <= 100; p += 10)
    {
        if (cancellation && cancellation->IsCancellationRequested())
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_state = UpdateState::Error;
            m_isDownloading = false;
            m_downloadProgress = 0;
            return;
        }
        Sleep(300);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_downloadProgress = p;
        }
        if (parentWnd && IsWindow(parentWnd))
        {
            PostMessageW(parentWnd, AppMessages::UpdateDownloadProgress, 0, 0);
        }
        if (m_mainNotifyWnd && m_mainNotifyWnd != parentWnd && IsWindow(m_mainNotifyWnd))
        {
            PostMessageW(m_mainNotifyWnd, AppMessages::UpdateDownloadProgress, 0, 0);
        }
    }

    // Copy current executable to system temp directory as WinLauncher.exe so cmd replacement will succeed
    wchar_t currentExePath[MAX_PATH];
    GetModuleFileNameW(nullptr, currentExePath, MAX_PATH);
    wchar_t tempDir[MAX_PATH];
    GetTempPathW(MAX_PATH, tempDir);
    std::wstring targetPath = std::wstring(tempDir) + L"WinLauncher.exe";
    if (CopyFileW(currentExePath, targetPath.c_str(), FALSE))
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_isDownloading = false;
        }

        if (parentWnd && IsWindow(parentWnd))
        {
            PostMessageW(parentWnd, AppMessages::UpdateDownloadCompleted, 0, 0);
        }
        if (m_mainNotifyWnd && m_mainNotifyWnd != parentWnd && IsWindow(m_mainNotifyWnd))
        {
            PostMessageW(m_mainNotifyWnd, AppMessages::UpdateDownloadCompleted, 0, 0);
        }
    }
    else
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_state = UpdateState::Error;
        m_isDownloading = false;
        if (parentWnd && IsWindow(parentWnd))
        {
            PostMessageW(parentWnd, AppMessages::UpdateCheckCompleted, 0, 0);
        }
        if (m_mainNotifyWnd && m_mainNotifyWnd != parentWnd && IsWindow(m_mainNotifyWnd))
        {
            PostMessageW(m_mainNotifyWnd, AppMessages::UpdateCheckCompleted, 0, 0);
        }
    }
}

#else

void UpdateService::PerformCheck(HWND notifyWnd, bool isSilent, Logger* logger)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_state = UpdateState::Checking;
        m_downloadProgress = 0;
        if (notifyWnd) m_mainNotifyWnd = notifyWnd;
    }

    if (notifyWnd && IsWindow(notifyWnd))
    {
        PostMessageW(notifyWnd, AppMessages::UpdateCheckCompleted, 0, 0);
    }
    else if (m_mainNotifyWnd && IsWindow(m_mainNotifyWnd))
    {
        PostMessageW(m_mainNotifyWnd, AppMessages::UpdateCheckCompleted, 0, 0);
    }

    if (logger)
    {
        LOG_INFO(logger, L"UpdateService: Starting update check...");
    }

    std::string response;
    std::wstring apiUrl = L"https://api.github.com/repos/LEISHIQIANG/WinLauncher/releases/latest";

    HINTERNET hSession = InternetOpenW(
        L"WinLauncher-Updater",
        INTERNET_OPEN_TYPE_PRECONFIG,
        nullptr, nullptr,
        INTERNET_FLAG_NO_UI);

    bool requestSuccess = false;
    if (hSession)
    {
        DWORD connectTimeout = 8000;
        DWORD recvTimeout = 12000;
        InternetSetOptionW(hSession, INTERNET_OPTION_CONNECT_TIMEOUT, (LPVOID)&connectTimeout, sizeof(connectTimeout));
        InternetSetOptionW(hSession, INTERNET_OPTION_RECEIVE_TIMEOUT, (LPVOID)&recvTimeout, sizeof(recvTimeout));

        std::wstring headers = L"Accept: application/vnd.github.v3+json\r\n";
        HINTERNET hUrl = InternetOpenUrlW(
            hSession,
            apiUrl.c_str(),
            headers.c_str(), (DWORD)headers.size(),
            INTERNET_FLAG_NO_UI | INTERNET_FLAG_NO_CACHE_WRITE |
            INTERNET_FLAG_PRAGMA_NOCACHE | INTERNET_FLAG_RELOAD |
            INTERNET_FLAG_SECURE,
            0);

        if (hUrl)
        {
            DWORD statusCode = 0;
            DWORD bufLen = sizeof(statusCode);
            HttpQueryInfoW(hUrl, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &statusCode, &bufLen, nullptr);

            if (statusCode == 200)
            {
                char buf[4096];
                DWORD bytesRead = 0;
                while (InternetReadFile(hUrl, buf, sizeof(buf), &bytesRead) && bytesRead > 0)
                {
                    response.append(buf, bytesRead);
                }
                requestSuccess = true;
            }
            else
            {
                if (logger)
                {
                    LOG_ERROR(logger, L"UpdateService: GitHub API request returned status code %d", statusCode);
                }
            }
            InternetCloseHandle(hUrl);
        }
        else
        {
            if (logger)
            {
                LOG_ERROR(logger, L"UpdateService: InternetOpenUrlW failed. Error: %d", GetLastError());
            }
        }
        InternetCloseHandle(hSession);
    }
    else
    {
        if (logger)
        {
            LOG_ERROR(logger, L"UpdateService: InternetOpenW failed.");
        }
    }

    UpdateState finalState = UpdateState::Error;
    std::wstring tag, body, downloadUrl;

    if (requestSuccess && !response.empty())
    {
        if (ParseReleaseJson(response, tag, body, downloadUrl))
        {
            if (IsNewer(tag))
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_latestVersion = tag;
                m_releaseNotes = body;
                m_downloadUrl = downloadUrl;
                m_state = UpdateState::NewVersionAvailable;
                finalState = UpdateState::NewVersionAvailable;
            }
            else
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_state = UpdateState::UpToDate;
                finalState = UpdateState::UpToDate;
            }
        }
        else
        {
            if (logger)
            {
                LOG_ERROR(logger, L"UpdateService: Failed to parse release JSON.");
            }
            std::lock_guard<std::mutex> lock(m_mutex);
            m_state = UpdateState::Error;
        }
    }
    else
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_state = UpdateState::Error;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_isChecking = false;
    }

    if (notifyWnd && IsWindow(notifyWnd))
    {
        PostMessageW(notifyWnd, AppMessages::UpdateCheckCompleted, 0, 0);
    }
    if (m_mainNotifyWnd && m_mainNotifyWnd != notifyWnd && IsWindow(m_mainNotifyWnd))
    {
        PostMessageW(m_mainNotifyWnd, AppMessages::UpdateCheckCompleted, 0, 0);
    }

    if (logger)
    {
        if (finalState == UpdateState::NewVersionAvailable)
        {
            LOG_INFO(logger, L"UpdateService: New version available: %s", tag.c_str());
        }
        else if (finalState == UpdateState::UpToDate)
        {
            LOG_INFO(logger, L"UpdateService: Application is up to date.");
        }
        else
        {
            LOG_ERROR(logger, L"UpdateService: Update check failed or finished with error.");
        }
    }
}

void UpdateService::PerformDownloadAndInstall(HWND parentWnd, Logger* logger, const std::shared_ptr<BackgroundTaskService::CancellationToken>& cancellation)
{
    std::wstring downloadUrl;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        downloadUrl = m_downloadUrl;
        m_downloadProgress = 0;
    }

    if (logger)
    {
        LOG_INFO(logger, L"UpdateService: Starting update download from: %s", downloadUrl.c_str());
    }

    if (downloadUrl.empty())
    {
        ShellExecuteW(nullptr, L"open", L"https://github.com/LEISHIQIANG/WinLauncher/releases", nullptr, nullptr, SW_SHOWNORMAL);
        std::lock_guard<std::mutex> lock(m_mutex);
        m_state = UpdateState::Idle;
        m_isDownloading = false;
        if (parentWnd && IsWindow(parentWnd))
        {
            PostMessageW(parentWnd, AppMessages::UpdateCheckCompleted, 0, 0);
        }
        if (m_mainNotifyWnd && m_mainNotifyWnd != parentWnd && IsWindow(m_mainNotifyWnd))
        {
            PostMessageW(m_mainNotifyWnd, AppMessages::UpdateCheckCompleted, 0, 0);
        }
        return;
    }

    wchar_t tempDir[MAX_PATH];
    GetTempPathW(MAX_PATH, tempDir);

    std::wstring targetPath = std::wstring(tempDir) + L"WinLauncher.exe";

    HINTERNET hSession = InternetOpenW(
        L"WinLauncher-Updater",
        INTERNET_OPEN_TYPE_PRECONFIG,
        nullptr, nullptr,
        INTERNET_FLAG_NO_UI);

    bool downloadSuccess = false;
    if (hSession)
    {
        HINTERNET hUrl = InternetOpenUrlW(
            hSession,
            downloadUrl.c_str(),
            nullptr, 0,
            INTERNET_FLAG_NO_UI | INTERNET_FLAG_NO_CACHE_WRITE |
            INTERNET_FLAG_PRAGMA_NOCACHE | INTERNET_FLAG_RELOAD |
            INTERNET_FLAG_SECURE,
            0);

        if (hUrl)
        {
            DWORD statusCode = 0;
            DWORD statusLen = sizeof(statusCode);
            const bool httpSuccess = HttpQueryInfoW(hUrl, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                                                    &statusCode, &statusLen, nullptr) &&
                                     statusCode >= 200 && statusCode < 300;
            DWORD contentLength = 0;
            DWORD bufLen = sizeof(contentLength);
            HttpQueryInfoW(hUrl, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER, &contentLength, &bufLen, nullptr);

            std::ofstream outFile(targetPath, std::ios::binary | std::ios::trunc);
            if (httpSuccess && outFile.is_open())
            {
                char buf[8192];
                DWORD bytesRead = 0;
                DWORD totalBytesRead = 0;
                bool readOk = true;
                bool writeOk = true;

                while (true)
                {
                    if (cancellation && cancellation->IsCancellationRequested())
                    {
                        readOk = false;
                        break;
                    }
                    if (!InternetReadFile(hUrl, buf, sizeof(buf), &bytesRead))
                    {
                        readOk = false;
                        break;
                    }
                    if (bytesRead == 0)
                    {
                        break;
                    }

                    outFile.write(buf, bytesRead);
                    if (!outFile.good())
                    {
                        writeOk = false;
                        break;
                    }
                    totalBytesRead += bytesRead;

                    if (contentLength > 0)
                    {
                        int progress = (int)((double)totalBytesRead * 100 / contentLength);
                        if (progress > 100) progress = 100;

                        {
                            std::lock_guard<std::mutex> lock(m_mutex);
                            m_downloadProgress = progress;
                        }

                        if (parentWnd && IsWindow(parentWnd))
                        {
                            PostMessageW(parentWnd, AppMessages::UpdateDownloadProgress, 0, 0);
                        }
                        if (m_mainNotifyWnd && m_mainNotifyWnd != parentWnd && IsWindow(m_mainNotifyWnd))
                        {
                            PostMessageW(m_mainNotifyWnd, AppMessages::UpdateDownloadProgress, 0, 0);
                        }
                    }
                }
                outFile.close();
                downloadSuccess = readOk && writeOk && (contentLength == 0 || totalBytesRead == contentLength);
                if (!downloadSuccess)
                {
                    DeleteFileW(targetPath.c_str());
                    if (logger)
                    {
                        LOG_ERROR(logger, L"UpdateService: Download incomplete. readOk=%d writeOk=%d bytes=%lu expected=%lu",
                                  readOk ? 1 : 0, writeOk ? 1 : 0, totalBytesRead, contentLength);
                    }
                }
            }
            else if (logger)
            {
                LOG_ERROR(logger, L"UpdateService: Download request rejected. status=%lu targetOpen=%d",
                          statusCode, outFile.is_open() ? 1 : 0);
            }
            InternetCloseHandle(hUrl);
        }
        InternetCloseHandle(hSession);
    }

    if (!downloadSuccess)
        DeleteFileW(targetPath.c_str());

    if (downloadSuccess)
    {
        if (parentWnd && IsWindow(parentWnd))
        {
            PostMessageW(parentWnd, AppMessages::UpdateDownloadProgress, 0, 0);
        }
        if (m_mainNotifyWnd && m_mainNotifyWnd != parentWnd && IsWindow(m_mainNotifyWnd))
        {
            PostMessageW(m_mainNotifyWnd, AppMessages::UpdateDownloadProgress, 0, 0);
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_isDownloading = false;
        }

        if (parentWnd && IsWindow(parentWnd))
        {
            PostMessageW(parentWnd, AppMessages::UpdateDownloadCompleted, 0, 0);
        }
        if (m_mainNotifyWnd && m_mainNotifyWnd != parentWnd && IsWindow(m_mainNotifyWnd))
        {
            PostMessageW(m_mainNotifyWnd, AppMessages::UpdateDownloadCompleted, 0, 0);
        }
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_state = UpdateState::Error;
        m_isDownloading = false;
    }

    if (parentWnd && IsWindow(parentWnd))
    {
        PostMessageW(parentWnd, AppMessages::UpdateCheckCompleted, 0, 0);
    }
    if (m_mainNotifyWnd && m_mainNotifyWnd != parentWnd && IsWindow(m_mainNotifyWnd))
    {
        PostMessageW(m_mainNotifyWnd, AppMessages::UpdateCheckCompleted, 0, 0);
    }
}

#endif

void UpdateService::ApplyUpdate(AppContext* ctx)
{
    wchar_t currentExePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, currentExePath, MAX_PATH);

    wchar_t tempDir[MAX_PATH];
    GetTempPathW(MAX_PATH, tempDir);
    std::wstring targetPath = std::wstring(tempDir) + L"WinLauncher.exe";
    WIN32_FILE_ATTRIBUTE_DATA updateFile{};
    if (!GetFileAttributesExW(targetPath.c_str(), GetFileExInfoStandard, &updateFile) ||
        (static_cast<ULONGLONG>(updateFile.nFileSizeHigh) << 32 | updateFile.nFileSizeLow) == 0)
    {
        if (ctx && ctx->logger)
            LOG_ERROR(ctx->logger, L"UpdateService: replacement skipped because downloaded EXE is missing or empty.");
        MessageBoxW(nullptr, L"更新文件无效或已丢失，请重新下载后再试。", L"WinLauncher", MB_OK | MB_ICONWARNING);
        return;
    }
    std::wstring currentExePathEscaped = EscapePowerShellSingleQuotedString(currentExePath);
    std::wstring targetPathEscaped = EscapePowerShellSingleQuotedString(targetPath);

    std::wstring params = L"-WindowStyle Hidden -Command \"Stop-Process -Id " + std::to_wstring(GetCurrentProcessId()) +
                          L" -Force -ErrorAction SilentlyContinue; Start-Sleep -Seconds 1; for ($i=0; $i -lt 10; $i++) { try { Remove-Item -LiteralPath '" + currentExePathEscaped +
                          L"' -Force -ErrorAction Stop; Move-Item -LiteralPath '" + targetPathEscaped +
                          L"' -Destination '" + currentExePathEscaped + L"' -Force -ErrorAction Stop; Start-Process -FilePath '" + currentExePathEscaped +
                          L"' -ArgumentList '--updated'; break; } catch { Start-Sleep -Seconds 1; } }\"";

    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = L"runas";
    sei.lpFile = L"powershell.exe";
    sei.lpParameters = params.c_str();
    sei.nShow = SW_HIDE;
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;

    if (ctx && ctx->logger)
    {
        LOG_INFO(ctx->logger, L"UpdateService: Launching elevated PowerShell for replacement.");
    }

    if (ShellExecuteExW(&sei))
    {
        PostQuitMessage(0);
        ExitProcess(0);
    }
    else
    {
        if (ctx && ctx->logger)
        {
            LOG_ERROR(ctx->logger, L"UpdateService: ShellExecuteExW with runas verb failed or UAC rejected.");
        }
        DeleteFileW(targetPath.c_str());
    }
}

bool UpdateService::ParseReleaseJson(const std::string& json, std::wstring& tag, std::wstring& body, std::wstring& downloadUrl)
{
    std::wstring wjson = Utf8ToWstr(json);
    JsonImport::JsonValue root = JsonImport::ParseJson(wjson);
    if (root.type != JsonImport::JsonValue::Object) return false;

    tag = root.GetString(L"tag_name");
    body = root.GetString(L"body");

    auto* assetsVal = root.Get(L"assets");
    if (assetsVal && assetsVal->type == JsonImport::JsonValue::Array)
    {
        for (size_t i = 0; i < assetsVal->Size(); ++i)
        {
            auto* asset = (*assetsVal)[i];
            if (asset && asset->type == JsonImport::JsonValue::Object)
            {
                std::wstring name = asset->GetString(L"name");
                std::wstring url = asset->GetString(L"browser_download_url");

                if (name.size() >= 4 && name.compare(name.size() - 4, 4, L".exe") == 0)
                {
                    downloadUrl = url;
                    return true;
                }
            }
        }

    }

    return false;
}

bool UpdateService::IsNewer(const std::wstring& latestTag)
{
    std::wstring latest = latestTag;
    if (!latest.empty() && (latest[0] == L'v' || latest[0] == L'V'))
    {
        latest = latest.substr(1);
    }

    std::vector<int> latestParts;
    std::wstringstream ss(latest);
    std::wstring part;
    while (std::getline(ss, part, L'.'))
    {
        try
        {
            latestParts.push_back(std::stoi(part));
        }
        catch (...)
        {
            latestParts.push_back(0);
        }
    }
    while (latestParts.size() < 4) latestParts.push_back(0);

    int currentParts[4] = {
        WINLAUNCHER_VERSION_MAJOR,
        WINLAUNCHER_VERSION_MINOR,
        WINLAUNCHER_VERSION_PATCH,
        WINLAUNCHER_VERSION_BUILD
    };

    for (int i = 0; i < 4; ++i)
    {
        if (latestParts[i] > currentParts[i]) return true;
        if (latestParts[i] < currentParts[i]) return false;
    }
    return false;
}

std::wstring UpdateService::Utf8ToWstr(const std::string& s)
{
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 0) return L"";
    std::wstring w(n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}
