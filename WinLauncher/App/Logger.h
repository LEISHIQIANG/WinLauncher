#pragma once
#include <windows.h>
#include <string>
#include <fstream>
#include <mutex>
#include <chrono>
#include <cstdio>

class Logger
{
public:
    enum Level { LInfo, LWarn, LError, LDebug };

    Logger(const std::wstring& logFile = L"")
    {
        if (!logFile.empty())
            m_file.open(logFile, std::ios::app);
    }

    ~Logger()
    {
        if (m_file.is_open()) m_file.close();
    }

    void Log(Level level, const wchar_t* format, ...)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        va_list args;
        va_start(args, format);
        int len = _vsnwprintf(nullptr, 0, format, args);
        va_end(args);
        if (len <= 0) return;

        std::wstring msg(len, L'\0');
        va_start(args, format);
        _vsnwprintf(&msg[0], len, format, args);
        va_end(args);

        wchar_t prefix[32];
        auto now = std::chrono::system_clock::now();
        auto tt = std::chrono::system_clock::to_time_t(now);
        tm tm{};
        localtime_s(&tm, &tt);
        wchar_t lc = level == LInfo ? L'I' : level == LWarn ? L'W' : level == LError ? L'E' : L'D';
        swprintf_s(prefix, L"[%02d:%02d:%02d][%c] ", tm.tm_hour, tm.tm_min, tm.tm_sec, lc);

        std::wstring output = prefix + msg + L"\n";
        OutputDebugStringW(output.c_str());

        if (m_file.is_open())
        {
            int clen = WideCharToMultiByte(CP_UTF8, 0, output.c_str(), (int)output.size(), nullptr, 0, nullptr, nullptr);
            std::string utf8(clen, '\0');
            WideCharToMultiByte(CP_UTF8, 0, output.c_str(), (int)output.size(), &utf8[0], clen, nullptr, nullptr);
            m_file.write(utf8.data(), utf8.size());
            m_file.flush();
        }
    }

    void LogInfo(const wchar_t* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        int len = _vsnwprintf(nullptr, 0, fmt, args);
        va_end(args);
        if (len <= 0) return;
        std::wstring buf(len, L'\0');
        va_start(args, fmt);
        _vsnwprintf(&buf[0], len, fmt, args);
        va_end(args);
        Log(LInfo, L"%s", buf.c_str());
    }

    void LogError(const wchar_t* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        int len = _vsnwprintf(nullptr, 0, fmt, args);
        va_end(args);
        if (len <= 0) return;
        std::wstring buf(len, L'\0');
        va_start(args, fmt);
        _vsnwprintf(&buf[0], len, fmt, args);
        va_end(args);
        Log(LError, L"%s", buf.c_str());
    }

    static std::wstring HrToString(HRESULT hr)
    {
        wchar_t buf[256];
        swprintf_s(buf, L"HRESULT=0x%08X", hr);
        return buf;
    }

private:
    std::ofstream m_file;
    std::mutex m_mutex;
};

#define LOG_INFO(logger, ...) if (logger) logger->LogInfo(__VA_ARGS__)
#define LOG_ERROR(logger, ...) if (logger) logger->LogError(__VA_ARGS__)
#define LOG_HRESULT(logger, hr) if (logger) logger->LogError(L"%hs:%d - %s", __FILE__, __LINE__, Logger::HrToString(hr).c_str())
