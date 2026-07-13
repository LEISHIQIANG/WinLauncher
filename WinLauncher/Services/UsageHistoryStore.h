#pragma once
#include <string>
#include <map>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>

struct UsageHistoryEntry
{
    unsigned long long launchCount = 0;
    unsigned long long lastUsedUtc = 0;
};

// Local-only ranking metadata. Queries, arguments and paths are never persisted.
class UsageHistoryStore
{
public:
    explicit UsageHistoryStore(const std::wstring& filePath);
    ~UsageHistoryStore();
    void RecordAccepted(const std::wstring& key);
    UsageHistoryEntry Get(const std::wstring& key) const;
    bool Clear();
    bool Flush();
    std::wstring GetFilePath() const { return m_filePath; }
private:
    void LoadLocked();
    bool SaveLocked();
    void SaveWorkerLoop();
    std::wstring m_filePath;
    mutable std::mutex m_mutex;
    std::map<std::wstring, UsageHistoryEntry> m_entries;
    bool m_loaded = false;
    bool m_dirty = false;
    size_t m_pendingAccepted = 0;
    unsigned long long m_firstDirtyTick = 0;
    std::condition_variable m_saveCv;
    std::thread m_saveThread;
    bool m_saveThreadStarted = false;
    bool m_stopSaveThread = false;
    std::chrono::steady_clock::time_point m_saveDue{};
};
