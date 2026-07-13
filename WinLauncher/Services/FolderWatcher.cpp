#include "FolderWatcher.h"
#include "../App/Logger.h"
#include <thread>
#include <mutex>
#include <chrono>
#include <atomic>
#include <vector>
#include <algorithm>

namespace
{
    constexpr DWORD kNotifyFilter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
        FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_CREATION;
    constexpr DWORD kChangeDebounceMs = 350;
    constexpr DWORD kRecoveryDelayMs = 5000;

    struct DirectoryWatch
    {
        std::wstring path;
        HANDLE directory = INVALID_HANDLE_VALUE;
        HANDLE event = nullptr;
        OVERLAPPED overlapped{};
        std::vector<BYTE> buffer = std::vector<BYTE>(16 * 1024);
        bool active = false;

        ~DirectoryWatch()
        {
            if (directory != INVALID_HANDLE_VALUE) CloseHandle(directory);
            if (event) CloseHandle(event);
        }

        DirectoryWatch() = default;
        DirectoryWatch(DirectoryWatch&& other) noexcept { *this = std::move(other); }
        DirectoryWatch& operator=(DirectoryWatch&& other) noexcept
        {
            if (this == &other) return *this;
            if (directory != INVALID_HANDLE_VALUE) CloseHandle(directory);
            if (event) CloseHandle(event);
            path = std::move(other.path); directory = other.directory; event = other.event;
            overlapped = other.overlapped; buffer = std::move(other.buffer); active = other.active;
            other.directory = INVALID_HANDLE_VALUE; other.event = nullptr; other.active = false;
            return *this;
        }
        DirectoryWatch(const DirectoryWatch&) = delete;
        DirectoryWatch& operator=(const DirectoryWatch&) = delete;
    };
}

struct FolderWatcher::Impl
{
    bool Arm(DirectoryWatch& watch)
    {
        if (watch.directory == INVALID_HANDLE_VALUE) return false;
        ResetEvent(watch.event);
        ZeroMemory(&watch.overlapped, sizeof(watch.overlapped));
        watch.overlapped.hEvent = watch.event;
        DWORD ignored = 0;
        const BOOL ok = ReadDirectoryChangesW(watch.directory, watch.buffer.data(), static_cast<DWORD>(watch.buffer.size()), FALSE,
            kNotifyFilter, &ignored, &watch.overlapped, nullptr);
        watch.active = ok != FALSE;
        if (!watch.active)
            LOG_G_WARNING_NODE(L"storage.folder_watcher", L"arm_failed", L"error=%lu", GetLastError());
        return watch.active;
    }

    std::vector<DirectoryWatch> BuildWatches(const std::vector<std::wstring>& folders)
    {
        std::vector<DirectoryWatch> watches;
        watches.reserve(std::min<size_t>(folders.size(), MAXIMUM_WAIT_OBJECTS - 1));
        for (const auto& folder : folders)
        {
            if (watches.size() >= MAXIMUM_WAIT_OBJECTS - 1)
            {
                LOG_G_WARNING_NODE(L"storage.folder_watcher", L"folder_limit_reached", L"count=%zu", folders.size());
                break;
            }
            DirectoryWatch watch;
            watch.path = folder;
            watch.directory = CreateFileW(folder.c_str(), FILE_LIST_DIRECTORY,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
            if (watch.directory == INVALID_HANDLE_VALUE)
            {
                LOG_G_WARNING_NODE(L"storage.folder_watcher", L"open_failed", L"error=%lu", GetLastError());
                continue;
            }
            watch.event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!watch.event || !Arm(watch)) continue;
            watches.push_back(std::move(watch));
        }
        return watches;
    }

    void WatchThread()
    {
        std::vector<DirectoryWatch> watches;
        unsigned long observedGeneration = 0;
        bool changePending = false;
        ULONGLONG changeDue = 0;
        ULONGLONG nextRecovery = 0;

        while (m_running)
        {
            std::vector<std::wstring> folders;
            unsigned long generation = 0;
            HWND notifyHwnd = nullptr;
            UINT notifyMessage = 0;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                folders = m_folders;
                generation = m_generation;
                notifyHwnd = m_hWndNotify;
                notifyMessage = m_msgNotify;
            }
            const ULONGLONG now = GetTickCount64();
            if (generation != observedGeneration || (watches.empty() && now >= nextRecovery && !folders.empty()))
            {
                watches = BuildWatches(folders);
                observedGeneration = generation;
                nextRecovery = now + kRecoveryDelayMs;
            }

            std::vector<HANDLE> events;
            events.reserve(watches.size() + 1);
            events.push_back(m_wakeEvent);
            for (const auto& watch : watches) events.push_back(watch.event);
            DWORD timeout = INFINITE;
            if (changePending) timeout = static_cast<DWORD>(std::min<ULONGLONG>(kChangeDebounceMs, changeDue > now ? changeDue - now : 0));
            else if (watches.empty()) timeout = kRecoveryDelayMs;
            const DWORD result = WaitForMultipleObjects(static_cast<DWORD>(events.size()), events.data(), FALSE, timeout);
            if (!m_running) break;
            if (result == WAIT_OBJECT_0)
            {
                ResetEvent(m_wakeEvent);
                continue;
            }
            if (result >= WAIT_OBJECT_0 + 1 && result < WAIT_OBJECT_0 + events.size())
            {
                DirectoryWatch& watch = watches[result - WAIT_OBJECT_0 - 1];
                DWORD bytes = 0;
                if (!GetOverlappedResult(watch.directory, &watch.overlapped, &bytes, FALSE))
                {
                    LOG_G_WARNING_NODE(L"storage.folder_watcher", L"read_failed", L"error=%lu", GetLastError());
                    watches.clear();
                    nextRecovery = GetTickCount64() + kRecoveryDelayMs;
                    continue;
                }
                Arm(watch);
                changePending = true;
                changeDue = GetTickCount64() + kChangeDebounceMs;
                continue;
            }
            if (changePending && GetTickCount64() >= changeDue)
            {
                changePending = false;
                if (notifyHwnd && IsWindow(notifyHwnd))
                {
                    LOG_G_INFO_NODE(L"storage.folder_watcher", L"changes_coalesced", L"folders=%zu", watches.size());
                    PostMessageW(notifyHwnd, notifyMessage, 0, 0);
                }
            }
        }
    }

    std::thread m_thread;
    std::mutex m_mutex;
    std::vector<std::wstring> m_folders;
    HWND m_hWndNotify = nullptr;
    UINT m_msgNotify = 0;
    std::atomic<bool> m_running = false;
    unsigned long m_generation = 1;
    HANDLE m_wakeEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    ~Impl() { if (m_wakeEvent) CloseHandle(m_wakeEvent); }
};

FolderWatcher::FolderWatcher() : m_impl(std::make_unique<Impl>()) {}
FolderWatcher::~FolderWatcher() { Stop(); }

void FolderWatcher::UpdateFolders(const std::vector<std::wstring>& folders, HWND hWndNotify, UINT msgNotify)
{
    {
        std::lock_guard<std::mutex> lock(m_impl->m_mutex);
        m_impl->m_folders = folders;
        m_impl->m_hWndNotify = hWndNotify;
        m_impl->m_msgNotify = msgNotify;
        ++m_impl->m_generation;
    }
    SetEvent(m_impl->m_wakeEvent);
    if (folders.empty())
    {
        Stop();
        return;
    }
    if (!m_impl->m_running && !folders.empty()) Start();
}

void FolderWatcher::Start()
{
    if (m_impl->m_running.exchange(true)) return;
    LOG_G_INFO_NODE(L"storage.folder_watcher", L"started", L"folders=%zu", m_impl->m_folders.size());
    m_impl->m_thread = std::thread(&FolderWatcher::Impl::WatchThread, m_impl.get());
}

void FolderWatcher::Stop()
{
    if (!m_impl || !m_impl->m_running.exchange(false)) return;
    SetEvent(m_impl->m_wakeEvent);
    if (m_impl->m_thread.joinable()) m_impl->m_thread.join();
    LOG_G_INFO_NODE(L"storage.folder_watcher", L"stopped", L"ok=%d", 1);
}
