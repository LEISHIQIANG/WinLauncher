#include "UsageHistoryStore.h"
#include "ConfigPath.h"
#include <Windows.h>
#include <fstream>
#include <regex>
#include <algorithm>

UsageHistoryStore::UsageHistoryStore(const std::wstring& filePath) : m_filePath(filePath) {}

void UsageHistoryStore::LoadLocked()
{
    if (m_loaded) return;
    m_loaded = true;
    std::wifstream in(m_filePath);
    std::wstring text((std::istreambuf_iterator<wchar_t>(in)), {});
    std::wregex pattern(LR"(\{\"key\":\"([^\"]+)\",\"count\":([0-9]+),\"lastUsedUtc\":([0-9]+)\})");
    for (std::wsregex_iterator it(text.begin(), text.end(), pattern), end; it != end; ++it)
        m_entries[(*it)[1].str()] = { _wcstoui64((*it)[2].str().c_str(), nullptr, 10), _wcstoui64((*it)[3].str().c_str(), nullptr, 10) };
}

void UsageHistoryStore::SaveLocked()
{
    std::wstring dir = m_filePath.substr(0, m_filePath.find_last_of(L"\\/"));
    ConfigPath::EnsureDirectoryExists(dir);
    std::wofstream out(m_filePath, std::ios::trunc);
    if (!out) return;
    out << L"{\"schemaVersion\":1,\"entries\":[";
    bool first = true;
    for (const auto& pair : m_entries) {
        if (!first) out << L","; first = false;
        out << L"{\"key\":\"" << pair.first << L"\",\"count\":" << pair.second.launchCount << L",\"lastUsedUtc\":" << pair.second.lastUsedUtc << L"}";
    }
    out << L"]}";
}

void UsageHistoryStore::RecordAccepted(const std::wstring& key)
{
    if (key.empty()) return;
    std::lock_guard<std::mutex> lock(m_mutex); LoadLocked();
    auto& entry = m_entries[key]; ++entry.launchCount; entry.lastUsedUtc = GetSystemTimeAsFileTime ? [] { FILETIME ft{}; GetSystemTimeAsFileTime(&ft); ULARGE_INTEGER value{}; value.LowPart=ft.dwLowDateTime; value.HighPart=ft.dwHighDateTime; return value.QuadPart; }() : 0;
    if (m_entries.size() > 500) {
        auto oldest = std::min_element(m_entries.begin(), m_entries.end(), [](const auto& a, const auto& b) { return a.second.lastUsedUtc < b.second.lastUsedUtc; });
        if (oldest != m_entries.end()) m_entries.erase(oldest);
    }
    SaveLocked();
}

UsageHistoryEntry UsageHistoryStore::Get(const std::wstring& key) const { std::lock_guard<std::mutex> lock(m_mutex); const_cast<UsageHistoryStore*>(this)->LoadLocked(); auto it=m_entries.find(key); return it==m_entries.end()?UsageHistoryEntry{}:it->second; }
bool UsageHistoryStore::Clear() { std::lock_guard<std::mutex> lock(m_mutex); m_entries.clear(); m_loaded=true; SaveLocked(); return true; }
