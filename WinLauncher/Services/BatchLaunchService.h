#pragma once
#include <Windows.h>
#include <string>
#include <vector>
#include <atomic>
#include "../App/AppContext.h"

struct BatchStep
{
    std::wstring shortcutId;
    uint32_t delayMs = 0;
    bool stopOnError = true;
    bool enabled = true;
};

class BatchHelper
{
public:
    static std::wstring Serialize(const std::vector<BatchStep>& steps);
    static std::vector<BatchStep> Parse(const std::wstring& arguments);
};

class BatchLaunchService
{
public:
    static bool Execute(const std::wstring& arguments, HWND parent, AppContext* ctx);
    static void Cancel();
    static bool IsRunning();

private:
    static DWORD WINAPI ThreadProc(LPVOID lpParam);

    struct ExecParams
    {
        std::vector<BatchStep> steps;
        HWND parent;
        AppContext* ctx;
    };

    static std::atomic<bool> s_running;
    static HANDLE s_hThread;
};
