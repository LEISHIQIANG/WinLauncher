#pragma once

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../App/BackgroundTaskService.h"
#include "../TriggerBlacklistPolicy.h"

class Logger;

// Resolves process identity away from WH_MOUSE_LL. Readers only consume an
// immutable snapshot, while all OpenProcess/path work runs in BackgroundTaskService.
class TriggerProcessResolver final : public std::enable_shared_from_this<TriggerProcessResolver>
{
public:
    enum class Decision
    {
        Unknown,
        Allowed,
        Blacklisted
    };

    struct Identity
    {
        DWORD pid = 0;
        std::wstring exePath;
        std::wstring exeName;
    };

    TriggerProcessResolver(
        std::shared_ptr<BackgroundTaskService> backgroundTasks,
        std::shared_ptr<Logger> logger);

    Decision Classify(DWORD pid) const noexcept;
    bool TryGetIdentity(DWORD pid, Identity& identity) const;
    bool IsKnown(DWORD pid) const noexcept;
    void Prefetch(DWORD pid);
    void SetBlacklist(const std::vector<std::wstring>& processNames);
    void FlushDiagnostics();

private:
    struct ProcessHandle
    {
        explicit ProcessHandle(HANDLE value) : handle(value) {}
        ~ProcessHandle() { if (handle) CloseHandle(handle); }
        ProcessHandle(const ProcessHandle&) = delete;
        ProcessHandle& operator=(const ProcessHandle&) = delete;
        HANDLE handle = nullptr;
    };

    struct Entry
    {
        Identity identity;
        std::shared_ptr<ProcessHandle> process;
        bool blacklisted = false;
        uint64_t serial = 0;
    };

    struct Snapshot
    {
        std::vector<Entry> entries;
    };

    void Resolve(DWORD pid, const std::shared_ptr<BackgroundTaskService::CancellationToken>& cancellation);
    void Publish(Entry entry);
    static bool IsAlive(const Entry& entry) noexcept;
    static std::wstring FileNameOf(const std::wstring& path);

    std::shared_ptr<BackgroundTaskService> m_backgroundTasks;
    std::shared_ptr<Logger> m_logger;
    std::shared_ptr<const Snapshot> m_snapshot;
    std::shared_ptr<const TriggerBlacklistPolicy::Matcher> m_blacklist;
    mutable std::mutex m_updateMutex;
    std::unordered_set<DWORD> m_pending;
    std::unordered_map<DWORD, ULONGLONG> m_negativeUntil;
    uint64_t m_nextSerial = 1;
    mutable std::atomic_uint64_t m_cacheHits{ 0 };
    mutable std::atomic_uint64_t m_cacheMisses{ 0 };
    std::atomic_uint64_t m_resolveSucceeded{ 0 };
    std::atomic_uint64_t m_resolveFailed{ 0 };
    ULONGLONG m_lastDiagnosticsFlush = 0;
};
