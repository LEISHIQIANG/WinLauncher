#include "Logger.h"
#include <cwchar>
#include <cstdarg>
#include <sstream>

// Define static members
Logger* Logger::s_defaultLogger = nullptr;

Logger::Logger(const std::wstring& logFile)
    : m_logFilePath(logFile)
{
    GetInstanceRef() = this;
    if (s_defaultLogger == nullptr)
    {
        s_defaultLogger = this;
    }

    if (!m_logFilePath.empty())
    {
        m_file.open(m_logFilePath, std::ios::app);
        m_cleanupThread = std::thread(&Logger::CleanupLoop, this);
    }
    m_prevFilter = SetUnhandledExceptionFilter(UnhandledCrashHandler);
}

Logger::~Logger()
{
    if (m_cleanupThread.joinable())
    {
        {
            std::lock_guard<std::mutex> lock(m_cleanupMutex);
            m_stopCleanup = true;
        }
        m_cv.notify_all();
        m_cleanupThread.join();
    }

    if (m_prevFilter)
    {
        SetUnhandledExceptionFilter(m_prevFilter);
    }
    if (m_file.is_open())
    {
        m_file.close();
    }
    if (GetInstanceRef() == this)
    {
        GetInstanceRef() = nullptr;
    }
    if (s_defaultLogger == this)
    {
        s_defaultLogger = nullptr;
    }
}

Logger* Logger::GetDefault()
{
    return s_defaultLogger;
}

void Logger::SetDefault(Logger* logger)
{
    s_defaultLogger = logger;
}

Logger*& Logger::GetInstanceRef()
{
    static Logger* s_instance = nullptr;
    return s_instance;
}

namespace {
    const char* GetFileName(const char* path)
    {
        if (!path) return "";
        const char* lastSlash = strrchr(path, '\\');
        if (!lastSlash) lastSlash = strrchr(path, '/');
        return lastSlash ? lastSlash + 1 : path;
    }
}

void Logger::Log(Level level, const char* file, int line, const char* func, const wchar_t* format, ...)
{
    va_list args;
    va_start(args, format);
    LogV(level, file, line, func, format, args);
    va_end(args);
}

void Logger::LogV(Level level, const char* file, int line, const char* func, const wchar_t* format, va_list args)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    va_list argsCopy;
    va_copy(argsCopy, args);
    int len = _vsnwprintf(nullptr, 0, format, argsCopy);
    va_end(argsCopy);
    if (len <= 0) return;

    std::wstring msg(len, L'\0');
    _vsnwprintf(&msg[0], len + 1, format, args);

    auto now = std::chrono::system_clock::now();
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    auto tt = std::chrono::system_clock::to_time_t(now);
    tm tm{};
    localtime_s(&tm, &tt);

    const wchar_t* levelStr = L"INFO";
    switch (level)
    {
    case INFO:    levelStr = L"INFO"; break;
    case WORNING: levelStr = L"WORNING"; break;
    case DEBUG:   levelStr = L"DEBUG"; break;
    case ERRA:    levelStr = L"ERRA"; break;
    }

    wchar_t prefix[256];
    if (file && func)
    {
        swprintf_s(prefix, L"[%04d-%02d-%02d %02d:%02d:%02d.%03d][%s][%hs:%d (%hs)] ",
            tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
            tm.tm_hour, tm.tm_min, tm.tm_sec, (int)now_ms.count(),
            levelStr, GetFileName(file), line, func);
    }
    else
    {
        swprintf_s(prefix, L"[%04d-%02d-%02d %02d:%02d:%02d.%03d][%s] ",
            tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
            tm.tm_hour, tm.tm_min, tm.tm_sec, (int)now_ms.count(),
            levelStr);
    }

    std::wstring output = prefix + msg + L"\n";
    OutputDebugStringW(output.c_str());

    if (m_file.is_open())
    {
        int clen = WideCharToMultiByte(CP_UTF8, 0, output.c_str(), (int)output.size(), nullptr, 0, nullptr, nullptr);
        if (clen > 0)
        {
            std::string utf8(clen, '\0');
            WideCharToMultiByte(CP_UTF8, 0, output.c_str(), (int)output.size(), &utf8[0], clen, nullptr, nullptr);
            m_file.write(utf8.data(), utf8.size());
            m_file.flush();
        }
    }
}

// Backward compatibility methods
void Logger::LogInfo(const wchar_t* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    LogV(INFO, nullptr, 0, nullptr, fmt, args);
    va_end(args);
}

void Logger::LogError(const wchar_t* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    LogV(ERRA, nullptr, 0, nullptr, fmt, args);
    va_end(args);
}

void Logger::LogWorning(const wchar_t* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    LogV(WORNING, nullptr, 0, nullptr, fmt, args);
    va_end(args);
}

void Logger::LogDebug(const wchar_t* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    LogV(DEBUG, nullptr, 0, nullptr, fmt, args);
    va_end(args);
}

std::wstring Logger::HrToString(HRESULT hr)
{
    wchar_t buf[256];
    swprintf_s(buf, L"HRESULT=0x%08X", hr);
    return buf;
}

LONG WINAPI Logger::UnhandledCrashHandler(EXCEPTION_POINTERS* exceptionInfo)
{
    Logger* logger = GetInstanceRef();
    if (logger)
    {
        logger->Log(Logger::ERRA, __FILE__, __LINE__, __FUNCTION__, L"==================================================");
        logger->Log(Logger::ERRA, __FILE__, __LINE__, __FUNCTION__, L"!!! CRASH DETECTED !!!");
        
        DWORD code = exceptionInfo->ExceptionRecord->ExceptionCode;
        void* address = exceptionInfo->ExceptionRecord->ExceptionAddress;
        
        const wchar_t* codeStr = L"Unknown Exception";
        switch (code)
        {
        case EXCEPTION_ACCESS_VIOLATION:          codeStr = L"Access Violation (0xC0000005)"; break;
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:     codeStr = L"Array Bounds Exceeded (0xC000008C)"; break;
        case EXCEPTION_BREAKPOINT:                codeStr = L"Breakpoint (0x80000003)"; break;
        case EXCEPTION_DATATYPE_MISALIGNMENT:     codeStr = L"Datatype Misalignment (0x80000002)"; break;
        case EXCEPTION_FLT_DENORMAL_OPERAND:      codeStr = L"Float Denormal Operand (0xC000008D)"; break;
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:       codeStr = L"Float Divide by Zero (0xC000008E)"; break;
        case EXCEPTION_FLT_INEXACT_RESULT:       codeStr = L"Float Inexact Result (0xC000008F)"; break;
        case EXCEPTION_FLT_INVALID_OPERATION:     codeStr = L"Float Invalid Operation (0xC0000090)"; break;
        case EXCEPTION_FLT_OVERFLOW:              codeStr = L"Float Overflow (0xC0000091)"; break;
        case EXCEPTION_FLT_STACK_CHECK:           codeStr = L"Float Stack Check (0xC0000092)"; break;
        case EXCEPTION_FLT_UNDERFLOW:             codeStr = L"Float Underflow (0xC0000093)"; break;
        case EXCEPTION_INT_DIVIDE_BY_ZERO:        codeStr = L"Integer Divide by Zero (0xC0000094)"; break;
        case EXCEPTION_INT_OVERFLOW:               codeStr = L"Integer Overflow (0xC0000095)"; break;
        case EXCEPTION_PRIV_INSTRUCTION:          codeStr = L"Privileged Instruction (0xC0000096)"; break;
        case EXCEPTION_IN_PAGE_ERROR:             codeStr = L"In Page Error (0xC0000006)"; break;
        case EXCEPTION_ILLEGAL_INSTRUCTION:       codeStr = L"Illegal Instruction (0xC000001D)"; break;
        case EXCEPTION_NONCONTINUABLE_EXCEPTION:  codeStr = L"Noncontinuable Exception (0xC0000025)"; break;
        case EXCEPTION_STACK_OVERFLOW:            codeStr = L"Stack Overflow (0xC0000FD)"; break;
        case EXCEPTION_INVALID_DISPOSITION:       codeStr = L"Invalid Disposition (0xC0000026)"; break;
        case 0xC0000374:                          codeStr = L"Heap Corruption (0xC0000374)"; break;
        }

        logger->Log(Logger::ERRA, __FILE__, __LINE__, __FUNCTION__, L"Exception Code: %s", codeStr);
        logger->Log(Logger::ERRA, __FILE__, __LINE__, __FUNCTION__, L"Exception Address: 0x%p", address);

        if (code == EXCEPTION_ACCESS_VIOLATION && exceptionInfo->ExceptionRecord->NumberParameters >= 2)
        {
            ULONG_PTR type = exceptionInfo->ExceptionRecord->ExceptionInformation[0];
            ULONG_PTR faultAddr = exceptionInfo->ExceptionRecord->ExceptionInformation[1];
            logger->Log(Logger::ERRA, __FILE__, __LINE__, __FUNCTION__, L"Details: %s location 0x%p",
                type == 0 ? L"Read from" : (type == 1 ? L"Write to" : L"Execute at"), (void*)faultAddr);
        }

        logger->Log(Logger::ERRA, __FILE__, __LINE__, __FUNCTION__, L"Stack Backtrace:");
        void* stack[32];
        USHORT frames = CaptureStackBackTrace(0, 32, stack, nullptr);
        for (USHORT i = 0; i < frames; i++)
        {
            HMODULE hMod = nullptr;
            wchar_t modPath[MAX_PATH] = L"unknown";
            if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                (LPCWSTR)stack[i], &hMod))
            {
                GetModuleFileNameW(hMod, modPath, MAX_PATH);
                wchar_t* lastSlash = wcsrchr(modPath, L'\\');
                if (lastSlash)
                {
                    wcscpy_s(modPath, lastSlash + 1);
                }
            }
            logger->Log(Logger::ERRA, __FILE__, __LINE__, __FUNCTION__, L"  [%d] %s (0x%p)", i, modPath, stack[i]);
        }
        logger->Log(Logger::ERRA, __FILE__, __LINE__, __FUNCTION__, L"==================================================");
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// Threaded Cleanup Loop
void Logger::CleanupLoop()
{
    // Run cleanup immediately at startup
    PruneLogFile();

    while (!m_stopCleanup)
    {
        std::unique_lock<std::mutex> lock(m_cleanupMutex);
        m_cv.wait_for(lock, std::chrono::hours(1), [this]() { return m_stopCleanup.load(); });
        if (m_stopCleanup) break;

        PruneLogFile();
    }
}

// Helper to parse log timestamps
bool Logger::ParseLogTime(const std::string& line, std::chrono::system_clock::time_point& outTime)
{
    const char* p = line.c_str();
    size_t len = line.size();

    // Skip UTF-8 BOM if present
    if (len >= 3 && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF)
    {
        p += 3;
        len -= 3;
    }

    if (len < 24 || p[0] != '[') return false;

    int year, month, day, hour, minute, second, ms;
    if (sscanf_s(p + 1, "%d-%d-%d %d:%d:%d.%d", 
                 &year, &month, &day, &hour, &minute, &second, &ms) == 7)
    {
        tm t{};
        t.tm_year = year - 1900;
        t.tm_mon = month - 1;
        t.tm_mday = day;
        t.tm_hour = hour;
        t.tm_min = minute;
        t.tm_sec = second;
        t.tm_isdst = -1;

        time_t tt = mktime(&t);
        if (tt != -1)
        {
            outTime = std::chrono::system_clock::from_time_t(tt) + std::chrono::milliseconds(ms);
            return true;
        }
    }
    return false;
}

// Log File Pruning
void Logger::PruneLogFile()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_logFilePath.empty()) return;

    // Close the file stream so we can read and rewrite it
    if (m_file.is_open())
    {
        m_file.close();
    }

    // Read all lines
    std::ifstream inFile(m_logFilePath, std::ios::binary);
    if (!inFile.is_open())
    {
        // Reopen log file for appending
        m_file.open(m_logFilePath, std::ios::app);
        return;
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(inFile, line))
    {
        lines.push_back(line);
    }
    inFile.close();

    // Prune lines older than 6 hours
    auto now = std::chrono::system_clock::now();
    std::vector<std::string> keptLines;
    bool currentKeep = true; // Default to keeping lines if they don't have a timestamp

    for (const auto& l : lines)
    {
        std::chrono::system_clock::time_point logTime;
        if (ParseLogTime(l, logTime))
        {
            auto diff = std::chrono::duration_cast<std::chrono::seconds>(now - logTime).count();
            // 6 hours = 21600 seconds
            currentKeep = (diff >= 0 && diff < 21600);
        }
        if (currentKeep)
        {
            keptLines.push_back(l);
        }
    }

    // Rewrite log file with kept lines
    std::ofstream outFile(m_logFilePath, std::ios::trunc | std::ios::binary);
    if (outFile.is_open())
    {
        for (const auto& l : keptLines)
        {
            outFile.write(l.c_str(), l.size());
            outFile.write("\n", 1);
        }
        outFile.close();
    }

    // Reopen log file for appending
    m_file.open(m_logFilePath, std::ios::app);
}
