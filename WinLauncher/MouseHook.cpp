#include "MouseHook.h"
#include "App/AppMessages.h"

std::atomic<int>    MouseHook::s_triggerType(0);
std::atomic<HHOOK>  MouseHook::s_hHook       = nullptr;
std::atomic<HWND>   MouseHook::s_hTargetWnd  = nullptr;
HANDLE              MouseHook::s_hThread     = nullptr;
HANDLE              MouseHook::s_hReadyEvent = nullptr;
std::atomic<bool>   MouseHook::s_running(false);
HMODULE             MouseHook::s_hModule     = nullptr;

void MouseHook::SetTriggerType(int type)
{
    s_triggerType.store(type);
}

bool MouseHook::Install(HWND hTargetWnd)
{
    if (s_running.load()) return true;

    s_hTargetWnd = hTargetWnd;

    s_hReadyEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!s_hReadyEvent) return false;

    s_hModule = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&LowLevelMouseProc),
        &s_hModule);

    s_running.store(true);
    s_hThread = CreateThread(nullptr, 0, ThreadProc, nullptr, 0, nullptr);
    if (!s_hThread)
    {
        s_running.store(false);
        return false;
    }

    DWORD wait = WaitForSingleObject(s_hReadyEvent, 3000);
    return wait == WAIT_OBJECT_0;
}

void MouseHook::Uninstall()
{
    if (!s_running.load()) return;

    s_running.store(false);

    if (s_hThread)
    {
        if (s_hHook)
            PostThreadMessageW(GetThreadId(s_hThread), WM_QUIT, 0, 0);

        if (WaitForSingleObject(s_hThread, 2000) == WAIT_TIMEOUT)
            TerminateThread(s_hThread, 0);

        CloseHandle(s_hThread);
        s_hThread = nullptr;
    }

    if (s_hReadyEvent) { CloseHandle(s_hReadyEvent); s_hReadyEvent = nullptr; }

    s_hHook      = nullptr;
    s_hTargetWnd = nullptr;
}

bool MouseHook::IsInstalled()
{
    return s_hHook != nullptr;
}

DWORD WINAPI MouseHook::ThreadProc(LPVOID)
{
    s_hHook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, s_hModule, 0);

    if (s_hReadyEvent) SetEvent(s_hReadyEvent);

    if (!s_hHook)
    {
        s_running.store(false);
        return 1;
    }

    MSG msg;
    while (s_running.load() && GetMessage(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (s_hHook)
    {
        UnhookWindowsHookEx(s_hHook);
        s_hHook = nullptr;
    }

    s_running.store(false);
    return 0;
}

LRESULT CALLBACK MouseHook::LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION && s_hTargetWnd)
    {
        int trigger = s_triggerType.load();
        bool activated = false;

        if (trigger == 0) // Middle Click
        {
            if (wParam == WM_MBUTTONDOWN) activated = true;
        }
        else if (trigger == 1 || trigger == 2) // Side button 4 or 5
        {
            if (wParam == WM_XBUTTONDOWN)
            {
                MSLLHOOKSTRUCT* pMsh = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
                WORD btn = HIWORD(pMsh->mouseData);
                if (trigger == 1 && btn == XBUTTON1) activated = true;
                if (trigger == 2 && btn == XBUTTON2) activated = true;
            }
        }

        if (activated)
        {
            PostMessage(s_hTargetWnd, AppMessages::ShowPopup, 0, 0);
        }
    }

    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}
