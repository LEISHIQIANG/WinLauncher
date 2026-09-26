#pragma once
#include <Windows.h>
#include <atomic>

class TriggerProcessResolver;

class MouseHook
{
public:
    static bool Install(HWND hTargetWnd, TriggerProcessResolver* processResolver);
    static void Uninstall();
    static bool IsInstalled();
    static bool IsHealthy();
    static void FlushDiagnostics();
    static void SetTriggerType(int type);
    static void SetTriggerEnabled(bool enabled);
    // Returns false when a queued request became stale because triggers were
    // paused or reconfigured before the UI thread could handle it.
    static bool AcknowledgePopupRequest(ULONG_PTR requestGeneration);

private:
    static std::atomic<int>    s_triggerType;
    static std::atomic<HHOOK>  s_hHook;
    static std::atomic<HWND>   s_hTargetWnd;
    static HANDLE              s_hThread;
    static std::atomic<DWORD>  s_hookThreadId;
    static HANDLE              s_hReadyEvent;
    static std::atomic<bool>   s_running;
    static std::atomic<bool>   s_triggerEnabled;
    static std::atomic<ULONG_PTR> s_triggerGeneration;
    static HMODULE             s_hModule;

    static DWORD WINAPI ThreadProc(LPVOID lpParam);
    static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam);
};
