#include "UsageHistoryStore.h"
#include "ConfigPath.h"
#include <Windows.h>
#include <fstream>
#include <algorithm>
#include <list>

UsageHistoryStore::UsageHistoryStore(const std::wstring& filePath) : m_filePath(filePath) {}

UsageHistoryStore::~UsageHistoryStore()
{
    Flush();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopSaveThread = true;
    }
    m_saveCv.notify_one();
    if (m_saveThread.joinable()) m_saveThread.join();
}

void UsageHistoryStore::LoadLocked()
{
    if (m_loaded) return;
    m_loaded = true;
    std::wifstream in(m_filePath);
    if (!in) return;
    std::wstring text((std::istreambuf_iterator<wchar_t>(in)), {});
    in.close();

    size_t pos = 0;
    while ((pos = text.find(L"\"key\":", pos)) != std::wstring::npos)
    {
        pos += 6;
        while (pos < text.size() && (text[pos] == L' ' || text[pos] == L'\t' || text[pos] == L'\r' || text[pos] == L'\n'))
            ++pos;
        if (pos >= text.size() || text[pos] != L'"') continue;
        ++pos;
        std::wstring key;
        bool escaped = false;
        while (pos < text.size())
        {
            if (escaped)
            {
                key += text[pos];
                escaped = false;
            }
            else if (text[pos] == L'\\')
            {
                escaped = true;
            }
            else if (text[pos] == L'"')
            {
                break;
            }
            else
            {
                key += text[pos];
            }
            ++pos;
        }
        if (pos >= text.size()) break;
        ++pos;

        size_t objEnd = text.find(L'}', pos);
        if (objEnd == std::wstring::npos) break;

        size_t countPos = text.find(L"\"count\":", pos);
        if (countPos == std::wstring::npos || countPos > objEnd) { pos = objEnd + 1; continue; }
        countPos += 8;
        wchar_t* endPtr = nullptr;
        unsigned long long count = wcstoull(text.c_str() + countPos, &endPtr, 10);

        size_t timePos = text.find(L"\"lastUsedUtc\":", countPos);
        if (timePos == std::wstring::npos || timePos > objEnd) { pos = objEnd + 1; continue; }
        timePos += 14;
        unsigned long long lastUsedUtc = wcstoull(text.c_str() + timePos, &endPtr, 10);

        if (!key.empty())
        {
            m_entries[key] = { count, lastUsedUtc };
        }
        pos = objEnd + 1;
    }
}


bool UsageHistoryStore::SaveLocked()
{
    if (!m_dirty) return true;
    std::wstring dir = m_filePath.substr(0, m_filePath.find_last_of(L"\\/"));
    ConfigPath::EnsureDirectoryExists(dir);
    const std::wstring tempPath = m_filePath + L".tmp";
    DeleteFileW(tempPath.c_str());
    std::wofstream out(tempPath, std::ios::trunc | std::ios::binary);
    if (!out) return false;
    out << L"{\"schemaVersion\":1,\"entries\":[";
    bool first = true;
    for (const auto& pair : m_entries) {
        if (!first) out << L","; first = false;
        out << L"{\"key\":\"";
        for (wchar_t ch : pair.first)
        {
            if (ch == L'"' || ch == L'\\') out << L'\\';
            out << ch;
        }
        out << L"\",\"count\":" << pair.second.launchCount << L",\"lastUsedUtc\":" << pair.second.lastUsedUtc << L"}";
    }
    out << L"]}";
    out.close();
    if (!MoveFileExW(tempPath.c_str(), m_filePath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        DeleteFileW(tempPath.c_str());
        return false;
    }
    m_dirty = false;
    m_pendingAccepted = 0;
    m_firstDirtyTick = 0;
    return true;
}

void UsageHistoryStore::RecordAccepted(const std::wstring& key)
{
    if (key.empty()) return;
    std::lock_guard<std::mutex> lock(m_mutex); LoadLocked();
    auto& entry = m_entries[key]; ++entry.launchCount; FILETIME ft{}; GetSystemTimeAsFileTime(&ft); ULARGE_INTEGER value{}; value.LowPart=ft.dwLowDateTime; value.HighPart=ft.dwHighDateTime; entry.lastUsedUtc = value.QuadPart;
    if (m_entries.size() > 500) {
        auto oldest = std::min_element(m_entries.begin(), m_entries.end(), [](const auto& a, const auto& b) { return a.second.lastUsedUtc < b.second.lastUsedUtc; });
        if (oldest != m_entries.end()) m_entries.erase(oldest);
    }
    m_dirty = true;
    ++m_pendingAccepted;
    if (m_firstDirtyTick == 0) m_firstDirtyTick = GetTickCount64();
    m_saveDue = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    if (!m_saveThreadStarted)
    {
        m_saveThreadStarted = true;
        m_saveThread = std::thread(&UsageHistoryStore::SaveWorkerLoop, this);
    }
    if (m_pendingAccepted >= 20)
        SaveLocked();
    else
        m_saveCv.notify_one();
}

UsageHistoryEntry UsageHistoryStore::Get(const std::wstring& key) const { std::lock_guard<std::mutex> lock(m_mutex); const_cast<UsageHistoryStore*>(this)->LoadLocked(); auto it=m_entries.find(key); return it==m_entries.end()?UsageHistoryEntry{}:it->second; }
bool UsageHistoryStore::Clear() { std::lock_guard<std::mutex> lock(m_mutex); m_entries.clear(); m_loaded=true; m_dirty=true; return SaveLocked(); }

bool UsageHistoryStore::Flush()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    LoadLocked();
    return SaveLocked();
}

void UsageHistoryStore::SaveWorkerLoop()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    while (!m_stopSaveThread)
    {
        if (!m_dirty)
        {
            m_saveCv.wait(lock, [this] { return m_stopSaveThread || m_dirty; });
            continue;
        }
        const auto due = m_saveDue;
        if (m_saveCv.wait_until(lock, due, [this, due] { return m_stopSaveThread || !m_dirty || m_saveDue != due; }))
            continue;
        SaveLocked();
    }
}

