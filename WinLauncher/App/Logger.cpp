#include "Logger.h"
#include <cwchar>
#include <cstdarg>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <shlobj.h>
#include <cstring>

Logger* Logger::s_defaultLogger = nullptr;

namespace
{
    constexpr size_t kMaxPendingLogEntries = 4096;
    constexpr size_t kMaxDebugRecords = 2048;
    constexpr size_t kFlushBytes = 64 * 1024;
    constexpr ULONGLONG kRotateBytes = 2ULL * 1024ULL * 1024ULL;
    constexpr ULONGLONG kArchiveBytes = 20ULL * 1024ULL * 1024ULL;
    constexpr ULONGLONG kArchiveAge100ns = 14ULL * 24ULL * 60ULL * 60ULL * 10000000ULL;

    thread_local uint64_t t_logTaskId = 0;
    thread_local std::wstring t_logTaskName;

    const char* GetFileName(const char* path)
    {
        if (!path) return "";
        const char* slash = strrchr(path, '\\');
        if (!slash) slash = strrchr(path, '/');
        return slash ? slash + 1 : path;
    }

    std::wstring ParentDirectory(const std::wstring& path)
    {
        const size_t at = path.find_last_of(L"\\/");
        return at == std::wstring::npos ? L"" : path.substr(0, at);
    }

    void EnsureDirectory(const std::wstring& path)
    {
        if (!path.empty()) SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
    }

    std::string ToUtf8(const std::wstring& value)
    {
        if (value.empty()) return {};
        const int length = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        std::string result(length, '\0');
        if (length > 0) WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), length, nullptr, nullptr);
        return result;
    }

    std::wstring FromAnsi(const char* value)
    {
        if (!value || !*value) return L"";
        const int length = MultiByteToWideChar(CP_ACP, 0, value, -1, nullptr, 0);
        std::wstring result(length > 0 ? length : 0, L'\0');
        if (length > 1)
        {
            MultiByteToWideChar(CP_ACP, 0, value, -1, result.data(), length);
            result.pop_back();
        }
        return result;
    }

    std::wstring JsonEscape(const std::wstring& value)
    {
        std::wstring result;
        result.reserve(value.size() + 16);
        for (const wchar_t ch : value)
        {
            switch (ch)
            {
            case L'\\': result += L"\\\\"; break;
            case L'\"': result += L"\\\""; break;
            case L'\n': result += L"\\n"; break;
            case L'\r': result += L"\\r"; break;
            case L'\t': result += L"\\t"; break;
            default:
                if (ch < 0x20) { wchar_t escaped[8]{}; swprintf_s(escaped, L"\\u%04x", static_cast<unsigned>(ch)); result += escaped; }
                else result += ch;
                break;
            }
        }
        return result;
    }

    std::wstring SanitizeMessage(std::wstring value)
    {
        wchar_t userProfile[MAX_PATH]{};
        if (GetEnvironmentVariableW(L"USERPROFILE", userProfile, MAX_PATH) > 0)
        {
            const std::wstring needle(userProfile);
            size_t position = 0;
            while ((position = value.find(needle, position)) != std::wstring::npos)
            {
                value.replace(position, needle.size(), L"<user-path>");
                position += 11;
            }
        }
        return value;
    }

    std::wstring UtcTimestamp()
    {
        SYSTEMTIME now{};
        GetSystemTime(&now);
        wchar_t buffer[40]{};
        swprintf_s(buffer, L"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
        return buffer;
    }

    std::wstring RotationTimestamp()
    {
        SYSTEMTIME now{};
        GetLocalTime(&now);
        wchar_t buffer[40]{};
        swprintf_s(buffer, L"%04u%02u%02u-%02u%02u%02u-%03u", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
        return buffer;
    }

    const wchar_t* LevelName(Logger::Level level)
    {
        switch (level) { case Logger::DEBUG: return L"debug"; case Logger::WORNING: return L"warn"; case Logger::ERRA: return L"error"; default: return L"info"; }
    }
}

Logger::Logger(const std::wstring& logFile) : m_logFilePath(logFile)
{
    GetInstanceRef() = this;
    if (!s_defaultLogger) s_defaultLogger = this;
    if (!m_logFilePath.empty())
    {
        EnsureDirectory(ParentDirectory(m_logFilePath));
        m_archiveDirectory = ParentDirectory(m_logFilePath) + L"\\archive";
        EnsureDirectory(m_archiveDirectory);
        m_file.open(m_logFilePath, std::ios::app | std::ios::binary);
        m_sessionId = std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64());
        m_cleanupThread = std::thread(&Logger::CleanupLoop, this);
    }
}

Logger::~Logger()
{
    {
        std::lock_guard<std::mutex> lock(m_cleanupMutex);
        m_stopCleanup = true;
    }
    m_cv.notify_all();
    if (m_cleanupThread.joinable()) m_cleanupThread.join();
    if (m_file.is_open()) m_file.close();
    if (GetInstanceRef() == this) GetInstanceRef() = nullptr;
    if (s_defaultLogger == this) s_defaultLogger = nullptr;
}

Logger* Logger::GetDefault() { return s_defaultLogger; }
void Logger::SetDefault(Logger* logger) { s_defaultLogger = logger; }

bool Logger::ShouldLogEvery(ULONGLONG& lastLogTick, DWORD intervalMs)
{
    const ULONGLONG now = GetTickCount64();
    if (lastLogTick != 0 && now - lastLogTick < intervalMs) return false;
    lastLogTick = now;
    return true;
}

bool Logger::ShouldLogElapsed(ULONGLONG& lastLogTick, double elapsedMs, double thresholdMs, DWORD intervalMs)
{
    return elapsedMs >= thresholdMs && ShouldLogEvery(lastLogTick, intervalMs);
}

void Logger::SetThreadTaskContext(uint64_t taskId, const std::wstring& taskName) { t_logTaskId = taskId; t_logTaskName = taskName; }
void Logger::ClearThreadTaskContext() { t_logTaskId = 0; t_logTaskName.clear(); }
Logger*& Logger::GetInstanceRef() { static Logger* instance = nullptr; return instance; }

void Logger::Log(Level level, const char* file, int line, const char* func, const wchar_t* format, ...)
{
    va_list args;
    va_start(args, format);
    LogV(level, file, line, func, format, args);
    va_end(args);
}

void Logger::LogV(Level level, const char* file, int line, const char* func, const wchar_t* format, va_list args)
{
    va_list copy;
    va_copy(copy, args);
    const int length = _vsnwprintf(nullptr, 0, format, copy);
    va_end(copy);
    if (length <= 0) return;

    std::wstring message(length, L'\0');
    _vsnwprintf(message.data(), length + 1, format, args);
    message = SanitizeMessage(std::move(message));

    std::wstring component = FromAnsi(GetFileName(file ? file : "logger"));
    std::wstring event = func ? FromAnsi(func) : L"message";
    constexpr wchar_t nodePrefix[] = L"[node=";
    if (message.rfind(nodePrefix, 0) == 0)
    {
        const size_t nodeEnd = message.find(L"]", 6);
        const size_t eventPrefix = message.find(L"[event=", nodeEnd);
        const size_t eventEnd = eventPrefix == std::wstring::npos ? std::wstring::npos : message.find(L"]", eventPrefix + 7);
        if (nodeEnd != std::wstring::npos && eventEnd != std::wstring::npos)
        {
            component = message.substr(6, nodeEnd - 6);
            event = message.substr(eventPrefix + 7, eventEnd - (eventPrefix + 7));
            message.erase(0, eventEnd + 1);
            if (!message.empty() && message.front() == L' ') message.erase(0, 1);
        }
    }

    std::wostringstream record;
    record << L"{\"ts\":\"" << UtcTimestamp()
           << L"\",\"level\":\"" << LevelName(level)
           << L"\",\"session\":\"" << FromAnsi(m_sessionId.c_str())
           << L"\",\"pid\":" << GetCurrentProcessId()
           << L",\"tid\":" << GetCurrentThreadId()
           << L",\"task\":{\"id\":" << t_logTaskId << L",\"name\":\"" << JsonEscape(t_logTaskName)
           << L"\"},\"component\":\"" << JsonEscape(component)
           << L"\",\"event\":\"" << JsonEscape(event)
           << L"\",\"message\":\"" << JsonEscape(message)
           << L"\",\"source\":{\"file\":\"" << JsonEscape(FromAnsi(GetFileName(file ? file : "")))
           << L"\",\"line\":" << line << L",\"function\":\"" << JsonEscape(func ? FromAnsi(func) : L"") << L"\"}}\n";
    const std::string utf8 = ToUtf8(record.str());
    OutputDebugStringW(record.str().c_str());

    std::lock_guard<std::mutex> lock(m_cleanupMutex);
    if (level == DEBUG)
    {
        m_recentDebugLogs.push_back(utf8);
        if (m_recentDebugLogs.size() > kMaxDebugRecords) m_recentDebugLogs.pop_front();
        return;
    }
    if (m_pendingLogs.size() >= kMaxPendingLogEntries)
    {
        m_pendingBytes -= m_pendingLogs.front().utf8.size();
        m_pendingLogs.pop_front();
        ++m_droppedLogs;
    }
    m_pendingLogs.push_back({ level, utf8 });
    m_pendingBytes += utf8.size();
    if (level == ERRA) m_forceFlush = true;
    if (m_forceFlush || m_pendingBytes >= kFlushBytes) m_cv.notify_one();
}

void Logger::LogInfo(const wchar_t* fmt, ...) { va_list args; va_start(args, fmt); LogV(INFO, nullptr, 0, nullptr, fmt, args); va_end(args); }
void Logger::LogError(const wchar_t* fmt, ...) { va_list args; va_start(args, fmt); LogV(ERRA, nullptr, 0, nullptr, fmt, args); va_end(args); }
void Logger::LogWorning(const wchar_t* fmt, ...) { va_list args; va_start(args, fmt); LogV(WORNING, nullptr, 0, nullptr, fmt, args); va_end(args); }
void Logger::LogDebug(const wchar_t* fmt, ...) { va_list args; va_start(args, fmt); LogV(DEBUG, nullptr, 0, nullptr, fmt, args); va_end(args); }

void Logger::Flush() { WritePending(); }

std::vector<std::string> Logger::GetRecentDebugJsonLines() const
{
    std::lock_guard<std::mutex> lock(m_cleanupMutex);
    return { m_recentDebugLogs.begin(), m_recentDebugLogs.end() };
}

std::wstring Logger::HrToString(HRESULT hr) { wchar_t buffer[64]{}; swprintf_s(buffer, L"HRESULT=0x%08X", hr); return buffer; }

LONG WINAPI Logger::UnhandledCrashHandler(EXCEPTION_POINTERS*) { return EXCEPTION_CONTINUE_SEARCH; }

void Logger::CleanupLoop()
{
    while (true)
    {
        std::unique_lock<std::mutex> lock(m_cleanupMutex);
        m_cv.wait_for(lock, std::chrono::seconds(1), [this] { return m_stopCleanup || m_forceFlush || m_pendingBytes >= kFlushBytes; });
        const bool stopping = m_stopCleanup;
        lock.unlock();
        WritePending();
        if (stopping) return;
    }
}

void Logger::WritePending()
{
    std::deque<PendingLogEntry> pending;
    size_t dropped = 0;
    {
        std::lock_guard<std::mutex> lock(m_cleanupMutex);
        if (m_pendingLogs.empty() && m_droppedLogs == 0) return;
        pending.swap(m_pendingLogs);
        m_pendingBytes = 0;
        m_forceFlush = false;
        dropped = m_droppedLogs;
        m_droppedLogs = 0;
    }

    std::lock_guard<std::mutex> fileLock(m_mutex);
    if (!m_file.is_open()) return;
    for (const auto& entry : pending) m_file.write(entry.utf8.data(), static_cast<std::streamsize>(entry.utf8.size()));
    if (dropped)
    {
        const std::wstring maintenance = L"{\"ts\":\"" + UtcTimestamp() + L"\",\"level\":\"warn\",\"component\":\"logger\",\"event\":\"queue_dropped\",\"message\":\"queued records were discarded\",\"count\":" + std::to_wstring(dropped) + L"}\n";
        const std::string utf8 = ToUtf8(maintenance);
        m_file.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
    }
    m_file.flush();
    RotateIfNeededLocked();
}

void Logger::RotateIfNeededLocked()
{
    HANDLE file = CreateFileW(m_logFilePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER size{};
    GetFileSizeEx(file, &size);
    CloseHandle(file);
    if (size.QuadPart < static_cast<LONGLONG>(kRotateBytes)) return;

    m_file.close();
    const std::wstring archived = m_archiveDirectory + L"\\winlauncher-" + RotationTimestamp() + L"-" + std::to_wstring(GetCurrentProcessId()) + L".jsonl";
    if (!MoveFileExW(m_logFilePath.c_str(), archived.c_str(), MOVEFILE_WRITE_THROUGH))
        m_file.open(m_logFilePath, std::ios::app | std::ios::binary);
    else
    {
        m_file.open(m_logFilePath, std::ios::app | std::ios::binary);
        PruneArchiveLocked();
    }
}

void Logger::PruneArchiveLocked()
{
    struct Entry { std::wstring path; ULONGLONG timestamp; ULONGLONG bytes; };
    std::vector<Entry> entries;
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW((m_archiveDirectory + L"\\*.jsonl").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return;
    do
    {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        ULARGE_INTEGER time{}; time.LowPart = data.ftLastWriteTime.dwLowDateTime; time.HighPart = data.ftLastWriteTime.dwHighDateTime;
        ULARGE_INTEGER bytes{}; bytes.LowPart = data.nFileSizeLow; bytes.HighPart = data.nFileSizeHigh;
        entries.push_back({ m_archiveDirectory + L"\\" + data.cFileName, time.QuadPart, bytes.QuadPart });
    } while (FindNextFileW(find, &data));
    FindClose(find);
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.timestamp < b.timestamp; });
    FILETIME nowFileTime{}; GetSystemTimeAsFileTime(&nowFileTime); ULARGE_INTEGER now{}; now.LowPart = nowFileTime.dwLowDateTime; now.HighPart = nowFileTime.dwHighDateTime;
    ULONGLONG total = 0;
    for (const auto& entry : entries) total += entry.bytes;
    for (const auto& entry : entries)
    {
        if ((now.QuadPart > entry.timestamp && now.QuadPart - entry.timestamp > kArchiveAge100ns) || total > kArchiveBytes)
        {
            if (DeleteFileW(entry.path.c_str())) total -= entry.bytes;
        }
    }
}
