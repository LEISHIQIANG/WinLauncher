#pragma once
#include <string>
#include <fstream>
#include <mutex>
#include <chrono>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

class Logger
{
public:
    enum Level {
        INFO,
        WORNING,
        DEBUG,
        ERRA,
        // Compatibility aliases
        LInfo = INFO,
        LWarn = WORNING,
        LError = ERRA,
        LDebug = DEBUG
    };

    Logger(const std::wstring& logFile = L"");
    ~Logger();

    // Default global logger instance for convenience and global logging
    static Logger* GetDefault();
    static void SetDefault(Logger* logger);
    static bool ShouldLogEvery(ULONGLONG& lastLogTick, DWORD intervalMs = 1000);
    static bool ShouldLogElapsed(ULONGLONG& lastLogTick, double elapsedMs, double thresholdMs, DWORD intervalMs = 1000);

    // Dynamic instance registry for exception callback
    static Logger*& GetInstanceRef();

    // Detailed Log function capturing file, line, function context
    void Log(Level level, const char* file, int line, const char* func, const wchar_t* format, ...);
    void LogV(Level level, const char* file, int line, const char* func, const wchar_t* format, va_list args);

    // Compatibility methods
    void LogInfo(const wchar_t* fmt, ...);
    void LogError(const wchar_t* fmt, ...);
    void LogWorning(const wchar_t* fmt, ...);
    void LogDebug(const wchar_t* fmt, ...);

    static std::wstring HrToString(HRESULT hr);

private:
    static LONG WINAPI UnhandledCrashHandler(EXCEPTION_POINTERS* exceptionInfo);

    void CleanupLoop();
    void PruneLogFile();
    void TrimLogFileBySizeLocked();
    bool ParseLogTime(const std::string& line, std::chrono::system_clock::time_point& outTime);

    std::ofstream m_file;
    std::mutex m_mutex;
    LPTOP_LEVEL_EXCEPTION_FILTER m_prevFilter = nullptr;
    static Logger* s_defaultLogger;

    // Pruning and Thread variables
    std::wstring m_logFilePath;
    std::thread m_cleanupThread;
    std::atomic<bool> m_stopCleanup{false};
    std::condition_variable m_cv;
    std::mutex m_cleanupMutex;
    ULONGLONG m_lastSizeTrimTick = 0;
};

// Logging Macros capturing file, line, and function details
#define LOG_INFO(logger, ...)    if (logger) logger->Log(Logger::INFO, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define LOG_WORNING(logger, ...) if (logger) logger->Log(Logger::WORNING, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define LOG_DEBUG(logger, ...)   if (logger) logger->Log(Logger::DEBUG, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define LOG_ERRA(logger, ...)    if (logger) logger->Log(Logger::ERRA, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)

// Compatibility and helper macros
#define LOG_ERROR(logger, ...)   if (logger) logger->Log(Logger::ERRA, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define LOG_HRESULT(logger, hr)  if (logger) logger->Log(Logger::ERRA, __FILE__, __LINE__, __FUNCTION__, L"%hs:%d - %s", __FILE__, __LINE__, Logger::HrToString(hr).c_str())

// Global helper macros
#define LOG_G_INFO(...)    if (Logger::GetDefault()) Logger::GetDefault()->Log(Logger::INFO, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define LOG_G_WORNING(...) if (Logger::GetDefault()) Logger::GetDefault()->Log(Logger::WORNING, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define LOG_G_DEBUG(...)   if (Logger::GetDefault()) Logger::GetDefault()->Log(Logger::DEBUG, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define LOG_G_ERRA(...)    if (Logger::GetDefault()) Logger::GetDefault()->Log(Logger::ERRA, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)

// Structured node macros. Keep node names stable, for example:
// ui.glass / render.background / render.compositor / app.lifecycle.
#define LOG_INFO_NODE(logger, node, event, fmt, ...)    if (logger) logger->Log(Logger::INFO, __FILE__, __LINE__, __FUNCTION__, L"[node=%ls][event=%ls] " fmt, node, event, __VA_ARGS__)
#define LOG_WARNING_NODE(logger, node, event, fmt, ...) if (logger) logger->Log(Logger::WORNING, __FILE__, __LINE__, __FUNCTION__, L"[node=%ls][event=%ls] " fmt, node, event, __VA_ARGS__)
#define LOG_DEBUG_NODE(logger, node, event, fmt, ...)   if (logger) logger->Log(Logger::DEBUG, __FILE__, __LINE__, __FUNCTION__, L"[node=%ls][event=%ls] " fmt, node, event, __VA_ARGS__)
#define LOG_ERROR_NODE(logger, node, event, fmt, ...)   if (logger) logger->Log(Logger::ERRA, __FILE__, __LINE__, __FUNCTION__, L"[node=%ls][event=%ls] " fmt, node, event, __VA_ARGS__)

#define LOG_G_INFO_NODE(node, event, fmt, ...)    if (Logger::GetDefault()) Logger::GetDefault()->Log(Logger::INFO, __FILE__, __LINE__, __FUNCTION__, L"[node=%ls][event=%ls] " fmt, node, event, __VA_ARGS__)
#define LOG_G_WARNING_NODE(node, event, fmt, ...) if (Logger::GetDefault()) Logger::GetDefault()->Log(Logger::WORNING, __FILE__, __LINE__, __FUNCTION__, L"[node=%ls][event=%ls] " fmt, node, event, __VA_ARGS__)
#define LOG_G_DEBUG_NODE(node, event, fmt, ...)   if (Logger::GetDefault()) Logger::GetDefault()->Log(Logger::DEBUG, __FILE__, __LINE__, __FUNCTION__, L"[node=%ls][event=%ls] " fmt, node, event, __VA_ARGS__)
#define LOG_G_ERROR_NODE(node, event, fmt, ...)   if (Logger::GetDefault()) Logger::GetDefault()->Log(Logger::ERRA, __FILE__, __LINE__, __FUNCTION__, L"[node=%ls][event=%ls] " fmt, node, event, __VA_ARGS__)
