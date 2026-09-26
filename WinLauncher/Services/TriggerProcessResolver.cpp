#include "TriggerProcessResolver.h"

#include "../App/BackgroundTaskService.h"
#include "../App/Logger.h"
#include <algorithm>

namespace
{
    constexpr size_t ProcessCacheCapacity = 128;
    constexpr ULONGLONG NegativeCacheDurationMs = 30000;
    constexpr size_t MaximumProcessPathCapacity = 32768;
}

TriggerProcessResolver::TriggerProcessResolver(
    std::shared_ptr<BackgroundTaskService> backgroundTasks,
    std::shared_ptr<Logger> logger)
    : m_backgroundTasks(std::move(backgroundTasks))
    , m_logger(std::move(logger))
    , m_snapshot(std::make_shared<const Snapshot>())
    , m_blacklist(std::make_shared<const TriggerBlacklistPolicy::Matcher>())
{
}

bool TriggerProcessResolver::IsAlive(const Entry& entry) noexcept
{
    return entry.process && entry.process->handle &&
        WaitForSingleObject(entry.process->handle, 0) == WAIT_TIMEOUT;
}

TriggerProcessResolver::Decision TriggerProcessResolver::Classify(DWORD pid) const noexcept
{
    if (pid == 0)
        return Decision::Unknown;

    auto snapshot = std::atomic_load_explicit(&m_snapshot, std::memory_order_acquire);
    if (snapshot)
    {
        for (const auto& entry : snapshot->entries)
        {
            if (entry.identity.pid != pid)
                continue;
            if (!IsAlive(entry))
                break;
            m_cacheHits.fetch_add(1, std::memory_order_relaxed);
            return entry.blacklisted ? Decision::Blacklisted : Decision::Allowed;
        }
    }

    m_cacheMisses.fetch_add(1, std::memory_order_relaxed);
    return Decision::Unknown;
}

bool TriggerProcessResolver::TryGetIdentity(DWORD pid, Identity& identity) const
{
    auto snapshot = std::atomic_load_explicit(&m_snapshot, std::memory_order_acquire);
    if (!snapshot)
        return false;

    for (const auto& entry : snapshot->entries)
    {
        if (entry.identity.pid == pid && IsAlive(entry))
        {
            identity = entry.identity;
            return true;
        }
    }
    return false;
}

bool TriggerProcessResolver::IsKnown(DWORD pid) const noexcept
{
    if (pid == 0)
        return false;
    auto snapshot = std::atomic_load_explicit(&m_snapshot, std::memory_order_acquire);
    if (!snapshot)
        return false;
    for (const auto& entry : snapshot->entries)
    {
        if (entry.identity.pid == pid)
            return IsAlive(entry);
    }
    return false;
}

void TriggerProcessResolver::Prefetch(DWORD pid)
{
    if (pid == 0 || pid == GetCurrentProcessId() || IsKnown(pid) || !m_backgroundTasks)
        return;

    {
        std::lock_guard<std::mutex> lock(m_updateMutex);
        const ULONGLONG now = GetTickCount64();
        auto negative = m_negativeUntil.find(pid);
        if (negative != m_negativeUntil.end())
        {
            if (negative->second > now)
                return;
            m_negativeUntil.erase(negative);
        }
        if (!m_pending.insert(pid).second)
            return;
    }

    std::weak_ptr<TriggerProcessResolver> weak = weak_from_this();
    auto task = m_backgroundTasks->Submit(
        L"trigger.process_identity",
        BackgroundTaskService::Priority::Interactive,
        [weak, pid](const std::shared_ptr<BackgroundTaskService::CancellationToken>& cancellation) {
            if (auto resolver = weak.lock())
                resolver->Resolve(pid, cancellation);
        });
    if (!task)
    {
        std::lock_guard<std::mutex> lock(m_updateMutex);
        m_pending.erase(pid);
    }
}

void TriggerProcessResolver::Resolve(
    DWORD pid,
    const std::shared_ptr<BackgroundTaskService::CancellationToken>& cancellation)
{
    Entry entry;
    entry.identity.pid = pid;

    HANDLE rawProcess = nullptr;
    if (!cancellation || !cancellation->IsCancellationRequested())
    {
        rawProcess = OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
            FALSE,
            pid);
    }

    if (rawProcess)
    {
        std::vector<wchar_t> path(MaximumProcessPathCapacity);
        DWORD length = static_cast<DWORD>(path.size());
        if (QueryFullProcessImageNameW(rawProcess, 0, path.data(), &length) && length > 0)
        {
            entry.identity.exePath.assign(path.data(), length);
            entry.identity.exeName = FileNameOf(entry.identity.exePath);
            std::wstring normalizedName = entry.identity.exeName;
            TriggerBlacklistPolicy::NormalizeProcessNameInPlace(normalizedName);
            const std::wstring stem = TriggerBlacklistPolicy::StemOf(normalizedName);
            auto blacklist = std::atomic_load_explicit(&m_blacklist, std::memory_order_acquire);
            entry.blacklisted = blacklist && blacklist->MatchesNormalized(normalizedName, stem);
            entry.process = std::make_shared<ProcessHandle>(rawProcess);
            rawProcess = nullptr;
        }
    }
    if (rawProcess)
        CloseHandle(rawProcess);

    {
        std::lock_guard<std::mutex> lock(m_updateMutex);
        m_pending.erase(pid);
        if (!entry.process)
            m_negativeUntil[pid] = GetTickCount64() + NegativeCacheDurationMs;
    }

    if (!entry.process || (cancellation && cancellation->IsCancellationRequested()))
    {
        m_resolveFailed.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    Publish(std::move(entry));
    m_resolveSucceeded.fetch_add(1, std::memory_order_relaxed);
}

void TriggerProcessResolver::Publish(Entry entry)
{
    std::lock_guard<std::mutex> lock(m_updateMutex);
    std::wstring normalizedName = entry.identity.exeName;
    TriggerBlacklistPolicy::NormalizeProcessNameInPlace(normalizedName);
    auto blacklist = std::atomic_load_explicit(&m_blacklist, std::memory_order_acquire);
    entry.blacklisted = blacklist && blacklist->MatchesNormalized(
        normalizedName,
        TriggerBlacklistPolicy::StemOf(normalizedName));
    auto current = std::atomic_load_explicit(&m_snapshot, std::memory_order_acquire);
    auto next = std::make_shared<Snapshot>();
    if (current)
    {
        next->entries.reserve((std::min)(ProcessCacheCapacity, current->entries.size() + 1));
        for (const auto& existing : current->entries)
        {
            if (existing.identity.pid != entry.identity.pid && IsAlive(existing))
                next->entries.push_back(existing);
        }
    }

    entry.serial = m_nextSerial++;
    next->entries.push_back(std::move(entry));
    if (next->entries.size() > ProcessCacheCapacity)
    {
        auto oldest = std::min_element(next->entries.begin(), next->entries.end(),
            [](const Entry& left, const Entry& right) { return left.serial < right.serial; });
        next->entries.erase(oldest);
    }
    std::atomic_store_explicit(
        &m_snapshot,
        std::static_pointer_cast<const Snapshot>(next),
        std::memory_order_release);
}

void TriggerProcessResolver::SetBlacklist(const std::vector<std::wstring>& processNames)
{
    auto matcher = std::make_shared<const TriggerBlacklistPolicy::Matcher>(
        TriggerBlacklistPolicy::Matcher::Compile(processNames));
    std::atomic_store_explicit(&m_blacklist, matcher, std::memory_order_release);

    std::lock_guard<std::mutex> lock(m_updateMutex);
    auto current = std::atomic_load_explicit(&m_snapshot, std::memory_order_acquire);
    auto next = std::make_shared<Snapshot>();
    if (current)
    {
        next->entries = current->entries;
        for (auto& entry : next->entries)
        {
            std::wstring normalizedName = entry.identity.exeName;
            TriggerBlacklistPolicy::NormalizeProcessNameInPlace(normalizedName);
            entry.blacklisted = matcher->MatchesNormalized(
                normalizedName,
                TriggerBlacklistPolicy::StemOf(normalizedName));
        }
    }
    std::atomic_store_explicit(
        &m_snapshot,
        std::static_pointer_cast<const Snapshot>(next),
        std::memory_order_release);
}

std::wstring TriggerProcessResolver::FileNameOf(const std::wstring& path)
{
    const size_t separator = path.find_last_of(L"\\/");
    return separator == std::wstring::npos ? path : path.substr(separator + 1);
}

void TriggerProcessResolver::FlushDiagnostics()
{
    const ULONGLONG now = GetTickCount64();
    if (m_lastDiagnosticsFlush != 0 && now - m_lastDiagnosticsFlush < 5000)
        return;
    m_lastDiagnosticsFlush = now;
    const uint64_t hits = m_cacheHits.exchange(0, std::memory_order_relaxed);
    const uint64_t misses = m_cacheMisses.exchange(0, std::memory_order_relaxed);
    const uint64_t succeeded = m_resolveSucceeded.exchange(0, std::memory_order_relaxed);
    const uint64_t failed = m_resolveFailed.exchange(0, std::memory_order_relaxed);
    if (hits || misses || succeeded || failed)
    {
        LOG_INFO_NODE(m_logger, L"input.process_resolver", L"summary",
            L"cache_hits=%llu cache_misses=%llu resolved=%llu failed=%llu",
            static_cast<unsigned long long>(hits),
            static_cast<unsigned long long>(misses),
            static_cast<unsigned long long>(succeeded),
            static_cast<unsigned long long>(failed));
    }
}
