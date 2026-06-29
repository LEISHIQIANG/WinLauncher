#pragma once
#include <string>

// Toggle Mock update engine for testing. Set to 1 to test, 0 for production release.
#define MOCK_UPDATE_SERVICE 0
#include <vector>
#include <functional>
#include <thread>
#include <mutex>
#include <Windows.h>

struct AppContext;

class UpdateService
{
public:
    enum class UpdateState
    {
        Idle,
        Checking,
        NewVersionAvailable,
        UpToDate,
        Error
    };

    static UpdateService& GetInstance();

    // HWND to notify (send message to) on completion or progress, and AppContext for logging/context
    void CheckForUpdates(HWND notifyWnd, bool isSilent, AppContext* ctx = nullptr);
    void StartDownloadAndInstall(HWND parentWnd, AppContext* ctx = nullptr);
    void ApplyUpdate(AppContext* ctx = nullptr);

    UpdateState GetState() const;
    std::wstring GetLatestVersion() const;
    std::wstring GetReleaseNotes() const;
    std::wstring GetDownloadUrl() const;
    int GetDownloadProgress() const; // 0 to 100

    bool IsUpdatePromptClosed() const;
    void SetUpdatePromptClosed(bool closed);

private:
    UpdateService() = default;
    ~UpdateService() = default;

    void PerformCheck(HWND notifyWnd, bool isSilent, AppContext* ctx);
    void PerformDownloadAndInstall(HWND parentWnd, AppContext* ctx);

    bool ParseReleaseJson(const std::string& json, std::wstring& tag, std::wstring& body, std::wstring& downloadUrl);
    bool IsNewer(const std::wstring& latestTag);
    static std::wstring Utf8ToWstr(const std::string& s);

    UpdateState m_state = UpdateState::Idle;
    std::wstring m_latestVersion;
    std::wstring m_releaseNotes;
    std::wstring m_downloadUrl;
    int m_downloadProgress = 0;
    bool m_updatePromptClosed = false;
    HWND m_mainNotifyWnd = nullptr;

    mutable std::mutex m_mutex;
    bool m_isChecking = false;
    bool m_isDownloading = false;
};
