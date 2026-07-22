#pragma once

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <Windows.h>

class CrashReporter
{
public:
    explicit CrashReporter(const std::wstring& crashDirectory);
    ~CrashReporter();

    static void RecordBreadcrumb(const wchar_t* category, const std::wstring& detail) noexcept;
    static std::wstring FormatStackBackTrace();

private:
    struct Breadcrumb
    {
        std::atomic<unsigned long long> sequence{ 0 };
        DWORD threadId = 0;
        ULONGLONG tick = 0;
        wchar_t category[32]{};
        wchar_t detail[160]{};
    };

    static LONG WINAPI UnhandledExceptionFilter(EXCEPTION_POINTERS* exceptionInfo);
    static void TerminateHandler() noexcept;
    static void InvalidParameterHandler(const wchar_t* expression, const wchar_t* function, const wchar_t* file, unsigned int line, uintptr_t pReserved) noexcept;
    static void PureCallHandler() noexcept;
    void DumpThreadLoop();
    void WriteCrashFiles();
    void PruneOldReports();

    static CrashReporter* s_instance;
    static constexpr size_t BreadcrumbCount = 64;
    static Breadcrumb s_breadcrumbs[BreadcrumbCount];
    static std::atomic<unsigned long long> s_breadcrumbSequence;

    std::wstring m_crashDirectory;
    HANDLE m_dumpEvent = nullptr;
    HANDLE m_dumpCompletedEvent = nullptr;
    HANDLE m_stopEvent = nullptr;
    std::thread m_dumpThread;
    std::atomic_long m_crashStarted{ 0 };
    EXCEPTION_RECORD m_exceptionRecord{};
    CONTEXT m_context{};
    DWORD m_crashingThreadId = 0;
    ULONGLONG m_processStartTick = 0;
    LPTOP_LEVEL_EXCEPTION_FILTER m_previousFilter = nullptr;
    std::terminate_handler m_previousTerminate = nullptr;
    _invalid_parameter_handler m_previousInvalidParam = nullptr;
    _purecall_handler m_previousPureCall = nullptr;
};
