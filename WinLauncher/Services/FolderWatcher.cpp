#include "FolderWatcher.h"
#include "../App/Logger.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <limits>
#include <thread>
#include <unordered_map>
#include <vector>

namespace
{
    constexpr DWORD kNotifyFilter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
        FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_CREATION;
    constexpr DWORD kChangeDebounceMs = 350;
    constexpr DWORD kRecoveryDelaysMs[] = { 5000, 15000, 60000 };

    struct DirectoryWatch
    {
        std::wstring path;
        HANDLE directory = INVALID_HANDLE_VALUE;
        HANDLE event = nullptr;
        OVERLAPPED overlapped{};
        std::vector<BYTE> buffer = std::vector<BYTE>(16 * 1024);

        ~DirectoryWatch() { if (directory != INVALID_HANDLE_VALUE) CloseHandle(directory); if (event) CloseHandle(event); }
        DirectoryWatch() = default;
        DirectoryWatch(DirectoryWatch&& other) noexcept { *this = std::move(other); }
        DirectoryWatch& operator=(DirectoryWatch&& other) noexcept
        {
            if (this == &other) return *this;
            if (directory != INVALID_HANDLE_VALUE) CloseHandle(directory);
            if (event) CloseHandle(event);
            path = std::move(other.path); directory = other.directory; event = other.event;
            overlapped = other.overlapped; buffer = std::move(other.buffer);
            other.directory = INVALID_HANDLE_VALUE; other.event = nullptr;
            return *this;
        }
        DirectoryWatch(const DirectoryWatch&) = delete;
        DirectoryWatch& operator=(const DirectoryWatch&) = delete;
    };

    struct UnavailableFolder
    {
        unsigned int retryIndex = 0;
        ULONGLONG nextAttempt = 0;
    };
}

struct FolderWatcher::Impl
{
    bool Arm(DirectoryWatch& watch)
    {
        ResetEvent(watch.event);
        ZeroMemory(&watch.overlapped, sizeof(watch.overlapped));
        watch.overlapped.hEvent = watch.event;
        DWORD ignored = 0;
        return ReadDirectoryChangesW(watch.directory, watch.buffer.data(), static_cast<DWORD>(watch.buffer.size()), FALSE,
            kNotifyFilter, &ignored, &watch.overlapped, nullptr) != FALSE;
    }

    bool IsActive(const std::vector<DirectoryWatch>& watches, const std::wstring& path) const
    {
        return std::any_of(watches.begin(), watches.end(), [&](const DirectoryWatch& watch) { return watch.path == path; });
    }

    void MarkUnavailable(const std::wstring& path, DWORD error, ULONGLONG now)
    {
        auto [it, firstFailure] = m_unavailable.emplace(path, UnavailableFolder{});
        const DWORD delay = kRecoveryDelaysMs[(std::min)(it->second.retryIndex, static_cast<unsigned int>(_countof(kRecoveryDelaysMs) - 1))];
        it->second.nextAttempt = now + delay;
        if (it->second.retryIndex + 1 < _countof(kRecoveryDelaysMs)) ++it->second.retryIndex;
        if (firstFailure)
            LOG_G_WARNING_NODE(L"storage.folder_watcher", L"folder_unavailable", L"error=%lu retry_ms=%lu", error, delay);
    }

    bool TryOpenFolder(const std::wstring& path, std::vector<DirectoryWatch>& watches, ULONGLONG now)
    {
        if (IsActive(watches, path)) return false;
        const auto unavailable = m_unavailable.find(path);
        if (unavailable != m_unavailable.end() && now < unavailable->second.nextAttempt) return false;

        DirectoryWatch watch;
        watch.path = path;
        watch.directory = CreateFileW(path.c_str(), FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
        if (watch.directory == INVALID_HANDLE_VALUE)
        {
            MarkUnavailable(path, GetLastError(), now);
            return false;
        }
        watch.event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!watch.event || !Arm(watch))
        {
            MarkUnavailable(path, watch.event ? GetLastError() : ERROR_NOT_ENOUGH_MEMORY, now);
            return false;
        }
        const bool recovered = m_unavailable.erase(path) != 0;
        watches.push_back(std::move(watch));
        if (recovered) LOG_G_INFO_NODE(L"storage.folder_watcher", L"folder_recovered", L"ok=%d", 1);
        return recovered;
    }

    void SyncWatches(const std::vector<std::wstring>& folders, std::vector<DirectoryWatch>& watches, ULONGLONG now, bool resetRecovery)
    {
        if (resetRecovery) m_unavailable.clear();
        watches.erase(std::remove_if(watches.begin(), watches.end(), [&](const DirectoryWatch& watch) {
            return std::find(folders.begin(), folders.end(), watch.path) == folders.end();
        }), watches.end());
        for (auto it = m_unavailable.begin(); it != m_unavailable.end();)
            it = std::find(folders.begin(), folders.end(), it->first) == folders.end() ? m_unavailable.erase(it) : std::next(it);

        for (const auto& folder : folders)
        {
            if (watches.size() >= MAXIMUM_WAIT_OBJECTS - 1)
            {
                LOG_G_WARNING_NODE(L"storage.folder_watcher", L"folder_limit_reached", L"count=%zu", folders.size());
                break;
            }
            if (TryOpenFolder(folder, watches, now)) m_refreshRequested = true;
        }
    }

    DWORD NextWaitTimeout(ULONGLONG now, bool changePending, ULONGLONG changeDue) const
    {
        ULONGLONG due = changePending ? changeDue : (std::numeric_limits<ULONGLONG>::max)();
        for (const auto& [_, state] : m_unavailable) due = (std::min)(due, state.nextAttempt);
        if (due == (std::numeric_limits<ULONGLONG>::max)()) return INFINITE;
        return static_cast<DWORD>((std::min)(due > now ? due - now : 0ull, static_cast<ULONGLONG>(60000)));
    }

    void WatchThread()
    {
        std::vector<DirectoryWatch> watches;
        unsigned long observedGeneration = 0;
        bool changePending = false;
        ULONGLONG changeDue = 0;

        while (m_running)
        {
            std::vector<std::wstring> folders;
            unsigned long generation = 0;
            HWND notifyHwnd = nullptr;
            UINT notifyMessage = 0;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                folders = m_folders; generation = m_generation; notifyHwnd = m_hWndNotify; notifyMessage = m_msgNotify;
            }
            const ULONGLONG now = GetTickCount64();
            const bool resetRecovery = generation != observedGeneration;
            SyncWatches(folders, watches, now, resetRecovery);
            observedGeneration = generation;
            if (m_refreshRequested) { changePending = true; changeDue = now; m_refreshRequested = false; }

            std::vector<HANDLE> events{ m_wakeEvent };
            for (const auto& watch : watches) events.push_back(watch.event);
            const DWORD result = WaitForMultipleObjects(static_cast<DWORD>(events.size()), events.data(), FALSE,
                NextWaitTimeout(now, changePending, changeDue));
            if (!m_running) break;
            if (result == WAIT_OBJECT_0) { ResetEvent(m_wakeEvent); continue; }
            if (result >= WAIT_OBJECT_0 + 1 && result < WAIT_OBJECT_0 + events.size())
            {
                const size_t index = result - WAIT_OBJECT_0 - 1;
                DWORD bytes = 0;
                if (!GetOverlappedResult(watches[index].directory, &watches[index].overlapped, &bytes, FALSE) || !Arm(watches[index]))
                {
                    const std::wstring path = watches[index].path;
                    const DWORD error = GetLastError();
                    watches.erase(watches.begin() + index);
                    MarkUnavailable(path, error, GetTickCount64());
                    continue;
                }
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
    std::unordered_map<std::wstring, UnavailableFolder> m_unavailable;
    HWND m_hWndNotify = nullptr;
    UINT m_msgNotify = 0;
    std::atomic<bool> m_running = false;
    unsigned long m_generation = 1;
    bool m_refreshRequested = false;
    HANDLE m_wakeEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ~Impl() { if (m_wakeEvent) CloseHandle(m_wakeEvent); }
};

FolderWatcher::FolderWatcher() : m_impl(std::make_unique<Impl>()) {}
FolderWatcher::~FolderWatcher() { Stop(); }

void FolderWatcher::UpdateFolders(const std::vector<std::wstring>& folders, HWND hWndNotify, UINT msgNotify)
{
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(m_impl->m_mutex);
        changed = m_impl->m_folders != folders ||
            m_impl->m_hWndNotify != hWndNotify ||
            m_impl->m_msgNotify != msgNotify;
        if (changed)
        {
            m_impl->m_folders = folders;
            m_impl->m_hWndNotify = hWndNotify;
            m_impl->m_msgNotify = msgNotify;
            ++m_impl->m_generation;
        }
    }

    // Loading the same configuration can happen more than once during startup.
    // Do not reset an unavailable folder's backoff in that case: it would turn
    // a recoverable missing path into repeated 5-second warning bursts.
    if (changed)
        SetEvent(m_impl->m_wakeEvent);
    if (folders.empty()) Stop();
    else if (!m_impl->m_running) Start();
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
