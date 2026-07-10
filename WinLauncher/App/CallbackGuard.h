#pragma once

#include "Logger.h"
#include "CrashReporter.h"
#include <chrono>
#include <exception>
#include <functional>
#include <string>

namespace CallbackGuard
{
    template<typename Callback>
    bool Invoke(Logger* logger, const wchar_t* scope, Callback&& callback) noexcept
    {
        const auto started = std::chrono::steady_clock::now();
        CrashReporter::RecordBreadcrumb(L"callback", scope ? scope : L"unknown");
        bool ok = true;
        try
        {
            callback();
        }
        catch (const std::exception& ex)
        {
            ok = false;
            LOG_ERROR_NODE(logger, L"callback", L"exception", L"scope=%ls what=%hs", scope ? scope : L"unknown", ex.what());
        }
        catch (...)
        {
            ok = false;
            LOG_ERROR_NODE(logger, L"callback", L"exception_unknown", L"scope=%ls", scope ? scope : L"unknown");
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
        if (elapsed > 100)
            LOG_WARNING_NODE(logger, L"callback", L"slow", L"scope=%ls elapsed_ms=%lld", scope ? scope : L"unknown", static_cast<long long>(elapsed));
        return ok;
    }
}
