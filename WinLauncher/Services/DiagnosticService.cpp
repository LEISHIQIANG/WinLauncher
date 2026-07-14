#include "DiagnosticService.h"
#include "ArchiveUtility.h"
#include "ConfigPath.h"
#include "../App/Logger.h"
#include "../version.h"

#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <map>

namespace
{
    std::wstring Timestamp()
    {
        SYSTEMTIME now{};
        GetLocalTime(&now);
        wchar_t value[32]{};
        swprintf_s(value, L"%04u%02u%02u-%02u%02u%02u", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
        return value;
    }

    // Diagnostic archives deliberately expose only stable identifiers and counts.
    // Message text may contain paths, commands, URLs, selected files, or user input.
    std::map<std::wstring, unsigned int> ReadEventCounts()
    {
        std::map<std::wstring, unsigned int> counts;
        const std::filesystem::path log = std::filesystem::path(ConfigPath::GetUserLogDirectory()) / L"current.jsonl";
        std::wifstream input(log);
        std::wstring line;
        while (std::getline(input, line))
        {
            const auto componentAt = line.find(L"\"component\":\"");
            const auto eventAt = line.find(L"\"event\":\"");
            const auto levelAt = line.find(L"\"level\":\"");
            if (componentAt == std::wstring::npos || eventAt == std::wstring::npos || levelAt == std::wstring::npos)
                continue;

            const auto readValue = [&](size_t start) {
                start = line.find(L'\"', start) + 1;
                const size_t end = line.find(L'\"', start);
                return end == std::wstring::npos ? std::wstring{} : line.substr(start, end - start);
            };
            const std::wstring component = readValue(componentAt + 12);
            const std::wstring event = readValue(eventAt + 8);
            const std::wstring level = readValue(levelAt + 8);
            if (!component.empty() && !event.empty() && !level.empty())
                ++counts[level + L":" + component + L":" + event];
        }
        return counts;
    }

    std::wstring JsonEscape(const std::wstring& value)
    {
        std::wstring escaped;
        for (wchar_t ch : value)
        {
            if (ch == L'\\' || ch == L'\"') escaped += L'\\';
            escaped += ch;
        }
        return escaped;
    }
}

DiagnosticService::DiagnosticService(Logger* logger) : m_logger(logger) {}

std::wstring DiagnosticService::GetDirectory() const
{
    const auto directory = ConfigPath::GetUserDataDirectory() + L"\\diagnostics";
    ConfigPath::EnsureDirectoryExists(directory);
    return directory;
}

bool DiagnosticService::CreatePackage(const std::wstring& destPath, std::wstring& outError) const
{
    outError.clear();
    const std::wstring staging = GetDirectory() + L"\\package-" + Timestamp();
    const std::filesystem::path manifestPath = std::filesystem::path(staging) / L"diagnostic.json";
    auto cleanup = [&]() { std::error_code ignored; std::filesystem::remove_all(staging, ignored); };
    if (!ConfigPath::EnsureDirectoryExists(staging))
    {
        outError = L"无法创建诊断临时目录";
        return false;
    }

    std::wofstream manifest(manifestPath);
    if (!manifest)
    {
        cleanup();
        outError = L"无法创建诊断文件";
        return false;
    }
    SYSTEM_INFO systemInfo{};
    GetNativeSystemInfo(&systemInfo);
    manifest << L"{\"schemaVersion\":2,\"app\":{\"version\":\"" << WINLAUNCHER_VERSION_WSTR
             << L"\"},\"environment\":{\"architecture\":" << systemInfo.wProcessorArchitecture
             << L"},\"eventCounts\":[";
    bool first = true;
    for (const auto& [key, count] : ReadEventCounts())
    {
        if (!first) manifest << L",";
        first = false;
        manifest << L"{\"key\":\"" << JsonEscape(key) << L"\",\"count\":" << count << L"}";
    }
    manifest << L"],\"privacy\":\"metadata-only; no raw logs or user content\"}";
    manifest.close();

    if (!ArchiveUtility::CompressDirectoryContents(staging, destPath, 30000, outError))
    {
        cleanup();
        return false;
    }
    cleanup();
    LOG_INFO(m_logger, L"Diagnostic metadata package created.");
    return true;
}
