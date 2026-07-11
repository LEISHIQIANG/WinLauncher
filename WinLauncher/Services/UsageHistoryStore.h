#pragma once
#include <string>
#include <map>
#include <mutex>

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
    void RecordAccepted(const std::wstring& key);
    UsageHistoryEntry Get(const std::wstring& key) const;
    bool Clear();
    std::wstring GetFilePath() const { return m_filePath; }
private:
    void LoadLocked();
    void SaveLocked();
    std::wstring m_filePath;
    mutable std::mutex m_mutex;
    std::map<std::wstring, UsageHistoryEntry> m_entries;
    bool m_loaded = false;
};
