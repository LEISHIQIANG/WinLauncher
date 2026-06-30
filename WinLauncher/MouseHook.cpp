#include "MouseHook.h"
#include "App/AppMessages.h"
#include "App/Logger.h"
#include "InputFocusGuard.h"
#include "Services/MacroService.h"

std::atomic<int>    MouseHook::s_triggerType(0);
std::atomic<HHOOK>  MouseHook::s_hHook       = nullptr;
std::atomic<HWND>   MouseHook::s_hTargetWnd  = nullptr;
HANDLE              MouseHook::s_hThread     = nullptr;
std::atomic<DWORD>  MouseHook::s_hookThreadId = 0;
HANDLE              MouseHook::s_hReadyEvent = nullptr;
std::atomic<bool>   MouseHook::s_running(false);
std::atomic<DWORD>  MouseHook::s_suppressButtonUpMask(0);
HMODULE             MouseHook::s_hModule     = nullptr;

namespace
{
    constexpr DWORD SuppressMiddleUp  = 0x01;
    constexpr DWORD SuppressXButton1Up = 0x02;
    constexpr DWORD SuppressXButton2Up = 0x04;
}

void MouseHook::SetTriggerType(int type)
{
    s_triggerType.store(type);
}

bool MouseHook::Install(HWND hTargetWnd)
{
    LOG_G_INFO(L"MouseHook::Install called");
    if (s_running.load()) return IsInstalled();

    s_hTargetWnd = hTargetWnd;
    s_suppressButtonUpMask.store(0);
    s_hookThreadId.store(0);

    s_hReadyEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!s_hReadyEvent)
    {
        LOG_G_ERRA(L"MouseHook::Install: CreateEventW failed (error=%d)", GetLastError());
        return false;
    }

    s_hModule = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&LowLevelMouseProc),
        &s_hModule);

    s_running.store(true);
    s_hThread = CreateThread(nullptr, 0, ThreadProc, nullptr, 0, nullptr);
    if (!s_hThread)
    {
        LOG_G_ERRA(L"MouseHook::Install: CreateThread failed (error=%d)", GetLastError());
        s_running.store(false);
        CloseHandle(s_hReadyEvent);
        s_hReadyEvent = nullptr;
        return false;
    }

    DWORD wait = WaitForSingleObject(s_hReadyEvent, 3000);
    if (wait == WAIT_OBJECT_0 && IsInstalled())
    {
        LOG_G_INFO(L"MouseHook::Install: installation completed successfully");
        return true;
    }

    if (wait == WAIT_OBJECT_0)
    {
        LOG_G_ERRA(L"MouseHook::Install: hook thread reported ready but hook is not installed");
    }
    else
    {
        LOG_G_ERRA(L"MouseHook::Install: wait for ready event timed out");
    }

    s_running.store(false);
    DWORD tid = s_hookThreadId.load();
    if (tid != 0)
        PostThreadMessageW(tid, WM_QUIT, 0, 0);
    if (s_hThread)
    {
        if (WaitForSingleObject(s_hThread, 1000) == WAIT_TIMEOUT)
        {
            LOG_G_WORNING(L"MouseHook::Install: hook thread did not exit after failed install, terminating");
            TerminateThread(s_hThread, 0);
        }
        CloseHandle(s_hThread);
        s_hThread = nullptr;
    }
    if (s_hReadyEvent) { CloseHandle(s_hReadyEvent); s_hReadyEvent = nullptr; }
    s_hTargetWnd = nullptr;
    s_hookThreadId.store(0);
    return false;
}

void MouseHook::Uninstall()
{
    LOG_G_INFO(L"MouseHook::Uninstall called");
    if (!s_running.load()) return;

    s_running.store(false);

    if (s_hThread)
    {
        DWORD tid = s_hookThreadId.load();
        if (tid == 0)
            tid = GetThreadId(s_hThread);
        if (tid != 0)
            PostThreadMessageW(tid, WM_QUIT, 0, 0);

        if (WaitForSingleObject(s_hThread, 2000) == WAIT_TIMEOUT)
        {
            LOG_G_WORNING(L"MouseHook::Uninstall: thread did not exit in time, terminating");
            TerminateThread(s_hThread, 0);
        }

        CloseHandle(s_hThread);
        s_hThread = nullptr;
    }

    if (s_hReadyEvent) { CloseHandle(s_hReadyEvent); s_hReadyEvent = nullptr; }

    s_hHook      = nullptr;
    s_hTargetWnd = nullptr;
    s_hookThreadId.store(0);
    s_suppressButtonUpMask.store(0);
    LOG_G_INFO(L"MouseHook::Uninstall: uninstalled successfully");
}

bool MouseHook::IsInstalled()
{
    return s_hHook != nullptr;
}

DWORD WINAPI MouseHook::ThreadProc(LPVOID)
{
    s_hookThreadId.store(GetCurrentThreadId());

    MSG dummy{};
    PeekMessageW(&dummy, nullptr, 0, 0, PM_NOREMOVE);

    HHOOK hHook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, s_hModule, 0);
    s_hHook.store(hHook);

    if (s_hReadyEvent) SetEvent(s_hReadyEvent);

    if (!hHook)
    {
        LOG_G_ERRA(L"MouseHook::ThreadProc: SetWindowsHookExW failed (error=%d)", GetLastError());
        s_running.store(false);
        s_hookThreadId.store(0);
        return 1;
    }
    LOG_G_INFO(L"MouseHook::ThreadProc: Low-level mouse hook installed");

    MSG msg;
    while (s_running.load() && GetMessage(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (hHook)
    {
        UnhookWindowsHookEx(hHook);
        s_hHook.store(nullptr);
        LOG_G_INFO(L"MouseHook::ThreadProc: Low-level mouse hook unhooked");
    }

    s_running.store(false);
    s_hookThreadId.store(0);
    return 0;
}

LRESULT CALLBACK MouseHook::LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION && s_hTargetWnd && !MacroRecorder::IsRecording())
    {
        MSLLHOOKSTRUCT* pMsh = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
        DWORD suppressMask = s_suppressButtonUpMask.load();
        if (wParam == WM_MBUTTONUP && (suppressMask & SuppressMiddleUp))
        {
            s_suppressButtonUpMask.fetch_and(~SuppressMiddleUp);
            return 1;
        }
        if (wParam == WM_XBUTTONUP)
        {
            WORD btn = HIWORD(pMsh->mouseData);
            if (btn == XBUTTON1 && (suppressMask & SuppressXButton1Up))
            {
                s_suppressButtonUpMask.fetch_and(~SuppressXButton1Up);
                return 1;
            }
            if (btn == XBUTTON2 && (suppressMask & SuppressXButton2Up))
            {
                s_suppressButtonUpMask.fetch_and(~SuppressXButton2Up);
                return 1;
            }
        }

        if (MacroPlayer::IsPlaying())
        {
            return CallNextHookEx(nullptr, nCode, wParam, lParam);
        }

        int trigger = s_triggerType.load();
        bool activated = false;
        DWORD suppressUpMask = 0;

        if (trigger == 0) // Middle Click
        {
            if (wParam == WM_MBUTTONDOWN)
            {
                activated = true;
                suppressUpMask = SuppressMiddleUp;
            }
        }
        else if (trigger == 1 || trigger == 2) // Side button 4 or 5
        {
            if (wParam == WM_XBUTTONDOWN)
            {
                WORD btn = HIWORD(pMsh->mouseData);
                if (trigger == 1 && btn == XBUTTON1)
                {
                    activated = true;
                    suppressUpMask = SuppressXButton1Up;
                }
                if (trigger == 2 && btn == XBUTTON2)
                {
                    activated = true;
                    suppressUpMask = SuppressXButton2Up;
                }
            }
        }

        if (activated)
        {
            if (InputFocusGuard::IsTextInputContextActive())
            {
                LOG_G_INFO(L"MouseHook::LowLevelMouseProc: trigger ignored because text input is active");
                return CallNextHookEx(nullptr, nCode, wParam, lParam);
            }

            // Debounce: ignore triggers within 300ms of the last one
            static DWORD s_lastTriggerTick = 0;
            DWORD now = GetTickCount();
            if (now - s_lastTriggerTick < 300)
            {
                LOG_G_INFO(L"MouseHook::LowLevelMouseProc: trigger debounced (type=%d), %lums since last", trigger, now - s_lastTriggerTick);
                return CallNextHookEx(nullptr, nCode, wParam, lParam);
            }
            s_lastTriggerTick = now;

            LOG_G_INFO(L"MouseHook::LowLevelMouseProc: trigger detected (type=%d), posting ShowPopup message", trigger);
            PostMessage(s_hTargetWnd, AppMessages::ShowPopup, 0, 0);
            if (suppressUpMask != 0)
            {
                s_suppressButtonUpMask.fetch_or(suppressUpMask);
            }
            return 1;
        }
    }

    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}
