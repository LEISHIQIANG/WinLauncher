#define NOMINMAX
#include "BatchLaunchService.h"
#include "../App/Logger.h"
#include "../App/AppMessages.h"
#include <sstream>

std::atomic<bool> BatchLaunchService::s_running = false;
HANDLE BatchLaunchService::s_hThread = nullptr;

// ============================================================================
// BatchHelper Implementation
// ============================================================================

std::wstring BatchHelper::Serialize(const std::vector<BatchStep>& steps)
{
    std::wstringstream wss;
    for (size_t i = 0; i < steps.size(); ++i)
    {
        const auto& step = steps[i];
        wss << step.shortcutId << L","
            << step.delayMs << L","
            << (step.stopOnError ? L"1" : L"0") << L","
            << (step.enabled ? L"1" : L"0");
        if (i + 1 < steps.size())
        {
            wss << L"|||";
        }
    }
    return wss.str();
}

std::vector<BatchStep> BatchHelper::Parse(const std::wstring& arguments)
{
    std::vector<BatchStep> steps;
    if (arguments.empty()) return steps;

    std::wstring s = arguments;
    size_t pos = 0;
    std::vector<std::wstring> tokens;
    while ((pos = s.find(L"|||")) != std::wstring::npos)
    {
        tokens.push_back(s.substr(0, pos));
        s.erase(0, pos + 3);
    }
    tokens.push_back(s);

    for (const auto& token : tokens)
    {
        if (token.empty()) continue;
        std::wstringstream tss(token);
        std::wstring field;
        BatchStep step;
        int idx = 0;
        while (std::getline(tss, field, L','))
        {
            if (idx == 0) step.shortcutId = field;
            else if (idx == 1) { try { step.delayMs = std::stoul(field); } catch(...) { step.delayMs = 0; } }
            else if (idx == 2) { step.stopOnError = (field == L"1"); }
            else if (idx == 3) { step.enabled = (field == L"1"); }
            idx++;
        }
        if (idx >= 3)
        {
            steps.push_back(step);
        }
    }
    return steps;
}

// ============================================================================
// BatchLaunchService Implementation
// ============================================================================

bool BatchLaunchService::Execute(const std::wstring& arguments, HWND parent, AppContext* ctx)
{
    if (s_running.load())
    {
        LOG_G_WORNING(L"BatchLaunchService::Execute: ignored because another batch is already running.");
        return false;
    }

    auto steps = BatchHelper::Parse(arguments);
    if (steps.empty())
    {
        LOG_G_WORNING(L"BatchLaunchService::Execute: parsed zero steps. arguments=%s", arguments.c_str());
        return false;
    }

    LOG_G_INFO(
        L"BatchLaunchService::Execute: starting batch with %zu steps. parent=%p main=%p",
        steps.size(),
        parent,
        ctx ? ctx->hMainWnd : nullptr);

    s_running.store(true);

    ExecParams* params = new ExecParams();
    params->steps = steps;
    params->parent = parent;
    params->ctx = ctx;

    s_hThread = CreateThread(nullptr, 0, ThreadProc, params, 0, nullptr);
    if (!s_hThread)
    {
        s_running.store(false);
        delete params;
        LOG_G_ERRA(L"BatchLaunchService::Execute: CreateThread failed. error=%lu", GetLastError());
        return false;
    }
    return true;
}

void BatchLaunchService::Cancel()
{
    s_running.store(false);
    if (s_hThread)
    {
        WaitForSingleObject(s_hThread, 2000);
        CloseHandle(s_hThread);
        s_hThread = nullptr;
    }
}

bool BatchLaunchService::IsRunning()
{
    return s_running.load();
}

DWORD WINAPI BatchLaunchService::ThreadProc(LPVOID lpParam)
{
    ExecParams* params = reinterpret_cast<ExecParams*>(lpParam);
    if (!params) return 1;

    HWND hMainWnd = params->ctx ? params->ctx->hMainWnd : nullptr;
    if (!hMainWnd || !IsWindow(hMainWnd))
    {
        LOG_G_ERRA(L"BatchLaunchService::ThreadProc: invalid main window. main=%p", hMainWnd);
        s_running.store(false);
        delete params;
        return 1;
    }

    for (const auto& step : params->steps)
    {
        if (!s_running.load()) break;
        if (!step.enabled)
        {
            LOG_G_INFO(L"BatchLaunchService::ThreadProc: skipping disabled step id=%s", step.shortcutId.c_str());
            continue;
        }

        // Cancellable sleep
        DWORD delayed = 0;
        while (delayed < step.delayMs)
        {
            if (!s_running.load()) break;
            DWORD sleepTime = (step.delayMs - delayed > 50) ? 50 : (step.delayMs - delayed);
            Sleep(sleepTime);
            delayed += sleepTime;
        }

        if (!s_running.load()) break;

        LOG_G_INFO(L"BatchLaunchService::ThreadProc: launching step id=%s delayMs=%lu", step.shortcutId.c_str(), step.delayMs);

        // Send synchronously to the UI thread, returns 1 for success, 0 for failure
        LRESULT res = SendMessageW(hMainWnd, AppMessages::LaunchShortcutById, 0, reinterpret_cast<LPARAM>(&step.shortcutId));
        bool ok = (res != 0);
        LOG_G_INFO(L"BatchLaunchService::ThreadProc: step id=%s result=%d", step.shortcutId.c_str(), ok ? 1 : 0);

        if (!ok && step.stopOnError)
        {
            LOG_G_WORNING(L"BatchLaunchService::ThreadProc: step failed and stopOnError is set, halting batch execution. id=%s", step.shortcutId.c_str());
            break;
        }
    }

    s_running.store(false);
    LOG_G_INFO(L"BatchLaunchService::ThreadProc: batch finished.");
    delete params;
    return 0;
}
