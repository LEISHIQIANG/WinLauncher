#include "FolderWatcher.h"
#include "SyncFolderService.h"
#include <thread>
#include <mutex>
#include <map>
#include <algorithm>
#include <chrono>
#include <atomic>

struct FolderWatcher::Impl
{
    struct FileState
    {
        std::wstring name;
        ULONGLONG lastWriteTime;
    };

    static std::vector<FileState> GetDirectoryState(const std::wstring& dir)
    {
        std::vector<FileState> state;
        std::wstring searchPath = dir + L"\\*";
        WIN32_FIND_DATAW ffd;
        HANDLE hFind = FindFirstFileW(searchPath.c_str(), &ffd);
        if (hFind != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (!SyncFolderService::ShouldIgnoreFile(ffd))
                {
                    ULARGE_INTEGER uli;
                    uli.LowPart = ffd.ftLastWriteTime.dwLowDateTime;
                    uli.HighPart = ffd.ftLastWriteTime.dwHighDateTime;
                    state.push_back({ ffd.cFileName, uli.QuadPart });
                }
            } while (FindNextFileW(hFind, &ffd));
            FindClose(hFind);
        }
        std::sort(state.begin(), state.end(), [](const FileState& a, const FileState& b) {
            return a.name < b.name;
        });
        return state;
    }

    static bool StateChanged(const std::vector<FileState>& a, const std::vector<FileState>& b)
    {
        if (a.size() != b.size()) return true;
        for (size_t i = 0; i < a.size(); ++i)
        {
            if (a[i].name != b[i].name || a[i].lastWriteTime != b[i].lastWriteTime)
                return true;
        }
        return false;
    }

    void WatchThread()
    {
        std::map<std::wstring, std::vector<FileState>> cachedStates;
        while (m_running)
        {
            std::vector<std::wstring> foldersCopy;
            HWND hWndNotify = nullptr;
            UINT msgNotify = 0;

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                foldersCopy = m_folders;
                hWndNotify = m_hWndNotify;
                msgNotify = m_msgNotify;

                if (m_dirty)
                {
                    cachedStates.clear();
                    for (const auto& f : foldersCopy)
                    {
                        cachedStates[f] = GetDirectoryState(f);
                    }
                    m_dirty = false;
                }
            }

            bool changed = false;
            for (const auto& f : foldersCopy)
            {
                auto currentState = GetDirectoryState(f);
                auto it = cachedStates.find(f);
                if (it == cachedStates.end() || StateChanged(it->second, currentState))
                {
                    cachedStates[f] = currentState;
                    changed = true;
                }
            }

            if (changed && hWndNotify && IsWindow(hWndNotify))
            {
                PostMessageW(hWndNotify, msgNotify, 0, 0);
            }

            for (int i = 0; i < 5 && m_running; ++i)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }

    std::thread m_thread;
    std::mutex m_mutex;
    std::vector<std::wstring> m_folders;
    HWND m_hWndNotify = nullptr;
    UINT m_msgNotify = 0;
    std::atomic<bool> m_running = false;
    bool m_dirty = false;
};

FolderWatcher::FolderWatcher()
    : m_impl(std::make_unique<Impl>())
{
}

FolderWatcher::~FolderWatcher()
{
    Stop();
}

void FolderWatcher::UpdateFolders(const std::vector<std::wstring>& folders, HWND hWndNotify, UINT msgNotify)
{
    std::lock_guard<std::mutex> lock(m_impl->m_mutex);
    m_impl->m_folders = folders;
    m_impl->m_hWndNotify = hWndNotify;
    m_impl->m_msgNotify = msgNotify;
    m_impl->m_dirty = true;

    if (!m_impl->m_running && !folders.empty())
    {
        Start();
    }
}

void FolderWatcher::Start()
{
    if (m_impl->m_running) return;
    m_impl->m_running = true;
    m_impl->m_thread = std::thread(&FolderWatcher::Impl::WatchThread, m_impl.get());
}

void FolderWatcher::Stop()
{
    if (!m_impl || !m_impl->m_running) return;
    m_impl->m_running = false;
    if (m_impl->m_thread.joinable())
    {
        m_impl->m_thread.join();
    }
}
