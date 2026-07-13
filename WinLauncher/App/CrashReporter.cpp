#include "CrashReporter.h"
#include "../Services/ConfigPath.h"
#include "../version.h"
#include <DbgHelp.h>
#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <vector>

#pragma comment(lib, "Dbghelp.lib")

CrashReporter* CrashReporter::s_instance = nullptr;
CrashReporter::Breadcrumb CrashReporter::s_breadcrumbs[CrashReporter::BreadcrumbCount];
std::atomic<unsigned long long> CrashReporter::s_breadcrumbSequence{ 0 };

namespace
{
    std::wstring BuildTimestamp()
    {
        SYSTEMTIME st{};
        GetLocalTime(&st);
        wchar_t value[64]{};
        swprintf_s(value, L"%04u%02u%02u_%02u%02u%02u_%03u", st.wYear, st.wMonth, st.wDay,
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        return value;
    }

    bool WriteUtf8File(const std::wstring& path, const std::wstring& text)
    {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        std::vector<char> bytes(size > 0 ? size : 0);
        if (size > 0) WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), bytes.data(), size, nullptr, nullptr);
        DWORD written = 0;
        bool ok = size == 0 || (WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) && written == bytes.size());
        FlushFileBuffers(file);
        CloseHandle(file);
        return ok;
    }
}

CrashReporter::CrashReporter(const std::wstring& crashDirectory)
    : m_crashDirectory(crashDirectory)
    , m_processStartTick(GetTickCount64())
{
    ConfigPath::EnsureDirectoryExists(m_crashDirectory);
    m_dumpEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    m_dumpCompletedEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    s_instance = this;
    m_dumpThread = std::thread(&CrashReporter::DumpThreadLoop, this);
    m_previousFilter = SetUnhandledExceptionFilter(UnhandledExceptionFilter);
    m_previousTerminate = std::set_terminate(TerminateHandler);
    PruneOldReports();
}

CrashReporter::~CrashReporter()
{
    if (s_instance == this)
    {
        SetUnhandledExceptionFilter(m_previousFilter);
        std::set_terminate(m_previousTerminate);
        s_instance = nullptr;
    }
    if (m_stopEvent) SetEvent(m_stopEvent);
    if (m_dumpThread.joinable()) m_dumpThread.join();
    if (m_dumpEvent) CloseHandle(m_dumpEvent);
    if (m_dumpCompletedEvent) CloseHandle(m_dumpCompletedEvent);
    if (m_stopEvent) CloseHandle(m_stopEvent);
}

void CrashReporter::RecordBreadcrumb(const wchar_t* category, const std::wstring& detail) noexcept
{
    unsigned long long sequence = s_breadcrumbSequence.fetch_add(1) + 1;
    Breadcrumb& entry = s_breadcrumbs[sequence % BreadcrumbCount];
    entry.sequence.store(0, std::memory_order_release);
    entry.threadId = GetCurrentThreadId();
    entry.tick = GetTickCount64();
    wcsncpy_s(entry.category, category ? category : L"unknown", _TRUNCATE);
    wcsncpy_s(entry.detail, detail.c_str(), _TRUNCATE);
    entry.sequence.store(sequence, std::memory_order_release);
}

LONG WINAPI CrashReporter::UnhandledExceptionFilter(EXCEPTION_POINTERS* exceptionInfo)
{
    CrashReporter* reporter = s_instance;
    if (!reporter || !exceptionInfo || reporter->m_crashStarted.exchange(1) != 0)
        return EXCEPTION_CONTINUE_SEARCH;

    reporter->m_exceptionRecord = *exceptionInfo->ExceptionRecord;
    reporter->m_context = *exceptionInfo->ContextRecord;
    reporter->m_crashingThreadId = GetCurrentThreadId();
    ResetEvent(reporter->m_dumpCompletedEvent);
    SetEvent(reporter->m_dumpEvent);
    WaitForSingleObject(reporter->m_dumpCompletedEvent, 4000);
    return EXCEPTION_EXECUTE_HANDLER;
}

void CrashReporter::TerminateHandler() noexcept
{
    RaiseException(0xE0000001, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    TerminateProcess(GetCurrentProcess(), 0xE0000001);
}

void CrashReporter::DumpThreadLoop()
{
    HANDLE events[] = { m_stopEvent, m_dumpEvent };
    while (true)
    {
        DWORD wait = WaitForMultipleObjects(2, events, FALSE, INFINITE);
        if (wait == WAIT_OBJECT_0) return;
        if (wait == WAIT_OBJECT_0 + 1)
        {
            WriteCrashFiles();
            ResetEvent(m_dumpEvent);
            SetEvent(m_dumpCompletedEvent);
        }
    }
}

void CrashReporter::WriteCrashFiles()
{
    std::wstring base = m_crashDirectory + L"\\WinLauncher_" + BuildTimestamp() + L"_p" +
        std::to_wstring(GetCurrentProcessId()) + L"_t" + std::to_wstring(m_crashingThreadId);
    std::wstring dumpPath = base + L".dmp";
    std::wstring textPath = base + L".txt";

    bool dumpOk = false;
    HANDLE dumpFile = CreateFileW(dumpPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (dumpFile != INVALID_HANDLE_VALUE)
    {
        EXCEPTION_POINTERS pointers{ &m_exceptionRecord, &m_context };
        MINIDUMP_EXCEPTION_INFORMATION exceptionInfo{};
        exceptionInfo.ThreadId = m_crashingThreadId;
        exceptionInfo.ExceptionPointers = &pointers;
        exceptionInfo.ClientPointers = FALSE;
        MINIDUMP_TYPE type = static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
        dumpOk = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dumpFile, type, &exceptionInfo, nullptr, nullptr) != FALSE;
        FlushFileBuffers(dumpFile);
        CloseHandle(dumpFile);
    }

    wchar_t header[1024]{};
    swprintf_s(header,
        L"WinLauncher crash report\r\nversion=%s\r\npid=%lu\r\ntid=%lu\r\nuptime_ms=%llu\r\nexception_code=0x%08lX\r\nexception_address=%p\r\ndump_ok=%d\r\ndump_path=%s\r\n",
        WINLAUNCHER_VERSION_WSTR, GetCurrentProcessId(), m_crashingThreadId,
        static_cast<unsigned long long>(GetTickCount64() - m_processStartTick),
        m_exceptionRecord.ExceptionCode, m_exceptionRecord.ExceptionAddress, dumpOk ? 1 : 0, dumpPath.c_str());
    std::wstring text = header;
    if (m_exceptionRecord.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && m_exceptionRecord.NumberParameters >= 2)
    {
        text += L"access_type=" + std::to_wstring(m_exceptionRecord.ExceptionInformation[0]) + L"\r\n";
        wchar_t address[64]{};
        swprintf_s(address, L"access_address=%p\r\n", reinterpret_cast<void*>(m_exceptionRecord.ExceptionInformation[1]));
        text += address;
    }
    text += L"breadcrumbs:\r\n";
    std::vector<std::pair<unsigned long long, const Breadcrumb*>> ordered;
    for (const auto& entry : s_breadcrumbs)
    {
        auto sequence = entry.sequence.load(std::memory_order_acquire);
        if (sequence != 0) ordered.push_back({ sequence, &entry });
    }
    std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& item : ordered)
    {
        const Breadcrumb& entry = *item.second;
        text += L"  seq=" + std::to_wstring(item.first) + L" tick=" + std::to_wstring(entry.tick) +
            L" tid=" + std::to_wstring(entry.threadId) + L" [" + entry.category + L"] " + entry.detail + L"\r\n";
    }
    WriteUtf8File(textPath, text);
}

void CrashReporter::PruneOldReports()
{
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW((m_crashDirectory + L"\\WinLauncher_*.*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return;
    struct Entry { std::wstring path; FILETIME time; unsigned long long bytes = 0; };
    std::vector<Entry> entries;
    FILETIME nowFileTime{};
    GetSystemTimeAsFileTime(&nowFileTime);
    ULARGE_INTEGER now{};
    now.LowPart = nowFileTime.dwLowDateTime;
    now.HighPart = nowFileTime.dwHighDateTime;
    constexpr unsigned long long ThirtyDays100ns = 30ULL * 24ULL * 60ULL * 60ULL * 10000000ULL;
    do
    {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            std::wstring path = m_crashDirectory + L"\\" + data.cFileName;
            ULARGE_INTEGER modified{};
            modified.LowPart = data.ftLastWriteTime.dwLowDateTime;
            modified.HighPart = data.ftLastWriteTime.dwHighDateTime;
            if (now.QuadPart > modified.QuadPart && now.QuadPart - modified.QuadPart > ThirtyDays100ns)
                DeleteFileW(path.c_str());
            else
                ULARGE_INTEGER bytes{};
                bytes.LowPart = data.nFileSizeLow;
                bytes.HighPart = data.nFileSizeHigh;
                entries.push_back({ std::move(path), data.ftLastWriteTime, bytes.QuadPart });
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        return CompareFileTime(&a.time, &b.time) > 0;
    });
    // A crash report is a dump plus its companion text file.  Retain at most
    // five recent pairs and cap the folder so repeated crashes cannot consume
    // unbounded disk space.
    constexpr size_t MaxCrashFiles = 10;
    constexpr unsigned long long MaxCrashBytes = 50ULL * 1024ULL * 1024ULL;
    unsigned long long retainedBytes = 0;
    for (size_t i = 0; i < entries.size(); ++i)
    {
        if (i >= MaxCrashFiles || retainedBytes + entries[i].bytes > MaxCrashBytes)
            DeleteFileW(entries[i].path.c_str());
        else
            retainedBytes += entries[i].bytes;
    }
}
