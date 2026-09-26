#pragma once

#include <Windows.h>
#include <shlwapi.h>
#include <atomic>
#include <cstdint>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>

// Shared LRU cache for extracted HICONs. One instance is owned by the
// composition root (AppContext::iconService) and reused by the popup, the
// config window, and background icon backfill, so a target path is extracted
// at most once per session.
//
// Ownership: Store() takes ownership of the handed-in HICON (the cache
// destroys it); LookupIconCopy() hands out an owned CopyIcon duplicate the
// caller must DestroyIcon. LookupMaster() exposes the cache-owned master for
// callers with the legacy borrow-only contract (never destroy).
//
// Staleness: entries extracted from fixed local drives are re-validated
// against the file's last-write time at most once per validation interval;
// directories, resource-derived icons, UNC paths, and removable/unknown
// drives never auto-invalidate so a slow or offline share can never stall a
// cache hit.
class IconLruCache final
{
public:
    static constexpr size_t kDefaultCapacity = 512;
    static constexpr ULONGLONG kDefaultValidationIntervalMs = 5 * 60 * 1000;

    struct Stats
    {
        uint64_t hits = 0;
        uint64_t misses = 0;
        uint64_t evictions = 0;
        uint64_t invalidated = 0;
        uint64_t validations = 0;
    };

    explicit IconLruCache(
        size_t capacity = kDefaultCapacity,
        ULONGLONG validationIntervalMs = kDefaultValidationIntervalMs)
        : m_capacity(capacity ? capacity : 1)
        , m_validationIntervalMs(validationIntervalMs)
    {
    }

    ~IconLruCache() { Clear(); }

    IconLruCache(const IconLruCache&) = delete;
    IconLruCache& operator=(const IconLruCache&) = delete;

    // Returns the cache-owned master HICON for key, or nullptr. The returned
    // icon stays valid only until the entry is evicted or the cache cleared;
    // callers must not destroy it. Pure lookups never touch the filesystem.
    HICON LookupMaster(const std::wstring& key)
    {
        if (key.empty()) return nullptr;
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_entries.find(key);
        if (it == m_entries.end())
        {
            ++m_stats.misses;
            return nullptr;
        }
        if (IsStaleLocked(key, it->second))
        {
            DestroyIcon(it->second.master);
            m_lru.erase(it->second.lruIter);
            m_entries.erase(it);
            ++m_stats.invalidated;
            ++m_stats.misses;
            return nullptr;
        }
        TouchLocked(key, it->second);
        ++m_stats.hits;
        return it->second.master;
    }

    // Returns an owned duplicate of the cached master, or nullptr.
    HICON LookupIconCopy(const std::wstring& key)
    {
        HICON master = LookupMaster(key);
        if (!master) return nullptr;
        return CopyIcon(master);
    }

    // Takes ownership of ownedIcon and caches it under key.
    void Store(const std::wstring& key, HICON ownedIcon, bool fromResource = false)
    {
        if (!ownedIcon) return;
        if (key.empty())
        {
            DestroyIcon(ownedIcon);
            return;
        }

        Entry entry;
        entry.master = ownedIcon;
        entry.fromResource = fromResource;
        entry.lastCheckTick = GetTickCount64();
        CaptureFileStamp(key, entry);

        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_entries.find(key);
        if (it != m_entries.end())
        {
            if (it->second.master) DestroyIcon(it->second.master);
            m_lru.erase(it->second.lruIter);
            m_entries.erase(it);
        }
        EvictLocked(1);
        m_lru.push_front(key);
        entry.lruIter = m_lru.begin();
        m_entries.emplace(key, entry);
    }

    void Clear()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& pair : m_entries)
            if (pair.second.master) DestroyIcon(pair.second.master);
        m_entries.clear();
        m_lru.clear();
        m_driveTypes.clear();
    }

    size_t Size() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_entries.size();
    }

    Stats GetStats() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_stats;
    }

    // Policy hook, pure so tests can drive it directly. Validation only runs
    // for regular files on fixed local drives; anything else (network shares,
    // removable media, directories, resource-derived icons) keeps its cached
    // icon to guarantee a hit can never block on the filesystem.
    static bool ShouldValidate(DWORD attributes, bool isUncPath, DWORD driveType)
    {
        if (isUncPath) return false;
        if (driveType != DRIVE_FIXED) return false;
        if (attributes == 0 || attributes == INVALID_FILE_ATTRIBUTES) return false;
        if (attributes & FILE_ATTRIBUTE_DIRECTORY) return false;
        return true;
    }

private:
    struct Entry
    {
        HICON master = nullptr;
        std::list<std::wstring>::iterator lruIter;
        FILETIME lastWrite{};
        DWORD attributes = 0;
        ULONGLONG lastCheckTick = 0;
        bool fromResource = false;
    };

    void TouchLocked(const std::wstring& key, Entry& entry)
    {
        m_lru.erase(entry.lruIter);
        m_lru.push_front(key);
        entry.lruIter = m_lru.begin();
    }

    void EvictLocked(size_t count)
    {
        while (m_entries.size() + count > m_capacity && !m_lru.empty())
        {
            const std::wstring oldest = m_lru.back();
            m_lru.pop_back();
            auto it = m_entries.find(oldest);
            if (it != m_entries.end())
            {
                if (it->second.master) DestroyIcon(it->second.master);
                m_entries.erase(it);
                ++m_stats.evictions;
            }
        }
    }

    static void CaptureFileStamp(const std::wstring& key, Entry& entry)
    {
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (GetFileAttributesExW(key.c_str(), GetFileExInfoStandard, &data))
        {
            entry.lastWrite = data.ftLastWriteTime;
            entry.attributes = data.dwFileAttributes;
        }
    }

    bool IsStaleLocked(const std::wstring& key, Entry& entry)
    {
        if (entry.fromResource) return false;
        if (entry.attributes & FILE_ATTRIBUTE_DIRECTORY) return false;
        if (m_validationIntervalMs == 0 && entry.lastCheckTick != 0)
        {
            // Interval 0 is a test hook: validate on every lookup after the
            // first.
        }
        else if (GetTickCount64() - entry.lastCheckTick < m_validationIntervalMs)
        {
            return false;
        }

        entry.lastCheckTick = GetTickCount64();
        ++m_stats.validations;

        DWORD driveType = ResolveDriveTypeLocked(key);
        bool unc = PathIsUNCW(key.c_str()) == TRUE;
        if (!ShouldValidate(entry.attributes, unc, driveType)) return false;

        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (!GetFileAttributesExW(key.c_str(), GetFileExInfoStandard, &data))
            return true; // Target vanished: the cached icon is stale.
        return CompareFileTime(&data.ftLastWriteTime, &entry.lastWrite) != 0;
    }

    DWORD ResolveDriveTypeLocked(const std::wstring& key)
    {
        if (key.size() < 3 || key[1] != L':' || key[2] != L'\\')
            return DRIVE_NO_ROOT_DIR;
        const wchar_t letter = key[0];
        auto it = m_driveTypes.find(letter);
        if (it != m_driveTypes.end()) return it->second;
        wchar_t root[4] = { letter, L':', L'\\', L'\0' };
        const DWORD type = GetDriveTypeW(root);
        m_driveTypes.emplace(letter, type);
        return type;
    }

    size_t m_capacity;
    ULONGLONG m_validationIntervalMs;
    mutable std::mutex m_mutex;
    std::list<std::wstring> m_lru;
    std::unordered_map<std::wstring, Entry> m_entries;
    std::unordered_map<wchar_t, DWORD> m_driveTypes;
    Stats m_stats;
};
