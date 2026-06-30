#pragma once
#include <Windows.h>
#include <atomic>

class MouseHook
{
public:
    static bool Install(HWND hTargetWnd);
    static void Uninstall();
    static bool IsInstalled();
    static void SetTriggerType(int type);

private:
    static std::atomic<int>    s_triggerType;
    static std::atomic<HHOOK>  s_hHook;
    static std::atomic<HWND>   s_hTargetWnd;
    static HANDLE              s_hThread;
    static std::atomic<DWORD>  s_hookThreadId;
    static HANDLE              s_hReadyEvent;
    static std::atomic<bool>   s_running;
    static std::atomic<DWORD>  s_suppressButtonUpMask;
    static HMODULE             s_hModule;

    static DWORD WINAPI ThreadProc(LPVOID lpParam);
    static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam);
};
