/**
 * KeyboardHook.cpp
 *
 * Low-level keyboard hook for WinLauncher.
 * See KeyboardHook.h for the full design description.
 */

#include "KeyboardHook.h"
#include "App/Logger.h"

// ============================================================
// Static member definitions
// ============================================================

std::atomic<HHOOK>  KeyboardHook::s_hHook         = nullptr;
std::atomic<HANDLE> KeyboardHook::s_hThread        = nullptr;
HANDLE              KeyboardHook::s_hReadyEvent     = nullptr;
std::atomic<bool>   KeyboardHook::s_running(false);
HMODULE             KeyboardHook::s_hModule         = nullptr;

std::atomic<bool>   KeyboardHook::s_recording(false);
std::atomic<HWND>   KeyboardHook::s_hRecordWnd      = nullptr;
std::atomic<DWORD>  KeyboardHook::s_timeoutMs(10000);
UINT_PTR            KeyboardHook::s_timerId         = 0;

DWORD               KeyboardHook::s_recordModifiers = 0;
bool                KeyboardHook::s_hadNonModifier  = false;
int                 KeyboardHook::s_pressedCount    = 0;

// ============================================================
// Helpers
// ============================================================

static inline bool IsModifierVk(DWORD vk)
{
    switch (vk)
    {
    case VK_SHIFT:    case VK_LSHIFT:    case VK_RSHIFT:
    case VK_CONTROL:  case VK_LCONTROL:  case VK_RCONTROL:
    case VK_MENU:     case VK_LMENU:     case VK_RMENU:
    case VK_LWIN:     case VK_RWIN:
        return true;
    default:
        return false;
    }
}

// Build the modifier bitmask from current low-level key state.
// Called from inside the hook callback so GetAsyncKeyState is safe.
static DWORD CurrentModifiers()
{
    DWORD mods = 0;
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) mods |= RecordModifiers::Ctrl;
    if (GetAsyncKeyState(VK_MENU)    & 0x8000) mods |= RecordModifiers::Alt;
    if (GetAsyncKeyState(VK_SHIFT)   & 0x8000) mods |= RecordModifiers::Shift;
    if ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000)
        mods |= RecordModifiers::Win;
    return mods;
}

// ============================================================
// Public API
// ============================================================

bool KeyboardHook::Install()
{
    LOG_G_INFO(L"KeyboardHook::Install called");
    if (s_running.load()) return true;

    s_hReadyEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!s_hReadyEvent)
    {
        LOG_G_ERRA(L"KeyboardHook::Install: CreateEventW failed (error=%d)", GetLastError());
        return false;
    }

    // Keep a reference to our own module so the hook DLL stays loaded.
    s_hModule = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&LowLevelKeyboardProc),
        &s_hModule);

    s_running.store(true);
    HANDLE hThread = CreateThread(nullptr, 0, ThreadProc, nullptr, 0, nullptr);
    if (!hThread)
    {
        LOG_G_ERRA(L"KeyboardHook::Install: CreateThread failed (error=%d)", GetLastError());
        s_running.store(false);
        CloseHandle(s_hReadyEvent);
        s_hReadyEvent = nullptr;
        return false;
    }
    s_hThread.store(hThread);

    DWORD wait = WaitForSingleObject(s_hReadyEvent, 3000);
    if (wait == WAIT_OBJECT_0)
    {
        LOG_G_INFO(L"KeyboardHook::Install: low-level keyboard hook installed");
        return true;
    }
    LOG_G_ERRA(L"KeyboardHook::Install: hook-thread ready event timed out");
    return false;
}

void KeyboardHook::Uninstall()
{
    LOG_G_INFO(L"KeyboardHook::Uninstall called");
    if (!s_running.load()) return;

    s_running.store(false);
    s_recording.store(false);

    HANDLE hThread = s_hThread.load();
    if (hThread)
    {
        // Wake the hook thread's message loop
        PostThreadMessageW(GetThreadId(hThread), WM_QUIT, 0, 0);
        if (WaitForSingleObject(hThread, 2000) == WAIT_TIMEOUT)
        {
            LOG_G_WORNING(L"KeyboardHook::Uninstall: thread did not exit in time, terminating");
            TerminateThread(hThread, 0);
        }
        CloseHandle(hThread);
        s_hThread.store(nullptr);
    }

    if (s_hReadyEvent) { CloseHandle(s_hReadyEvent); s_hReadyEvent = nullptr; }
    s_hHook.store(nullptr);
    LOG_G_INFO(L"KeyboardHook::Uninstall: done");
}

bool KeyboardHook::IsInstalled()
{
    return s_hHook.load() != nullptr;
}

bool KeyboardHook::StartRecording(HWND hTargetWnd, DWORD timeoutMs)
{
    if (!IsInstalled())
    {
        LOG_G_WORNING(L"KeyboardHook::StartRecording: hook not installed");
        return false;
    }
    s_hRecordWnd.store(hTargetWnd);
    s_timeoutMs.store(timeoutMs);
    s_recordModifiers = 0;
    s_hadNonModifier  = false;
    s_pressedCount    = 0;
    s_recording.store(true);
    LOG_G_INFO(L"KeyboardHook::StartRecording: recording started");
    return true;
}

void KeyboardHook::StopRecording()
{
    s_recording.store(false);
    s_hadNonModifier  = false;
    s_pressedCount    = 0;
    s_recordModifiers = 0;
    LOG_G_INFO(L"KeyboardHook::StopRecording: recording stopped");
}

bool KeyboardHook::IsRecording()
{
    return s_recording.load();
}

// ============================================================
// Hook thread
// ============================================================

DWORD WINAPI KeyboardHook::ThreadProc(LPVOID)
{
    // Trigger a message-queue creation before installing the hook
    MSG dummy{};
    PeekMessageW(&dummy, nullptr, 0, 0, PM_NOREMOVE);

    HHOOK hHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, s_hModule, 0);
    s_hHook.store(hHook);

    if (s_hReadyEvent) SetEvent(s_hReadyEvent);

    if (!hHook)
    {
        LOG_G_ERRA(L"KeyboardHook::ThreadProc: SetWindowsHookExW failed (error=%d)", GetLastError());
        s_running.store(false);
        return 1;
    }
    LOG_G_INFO(L"KeyboardHook::ThreadProc: WH_KEYBOARD_LL hook installed");

    // Message loop – the hook requires this thread to pump messages.
    MSG msg{};
    while (s_running.load() && GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (hHook)
    {
        UnhookWindowsHookEx(hHook);
        s_hHook.store(nullptr);
        LOG_G_INFO(L"KeyboardHook::ThreadProc: hook unhooked, thread exiting");
    }

    s_running.store(false);
    return 0;
}

// ============================================================
// Timeout timer (fired on hook thread via SetTimer)
// ============================================================
void CALLBACK KeyboardHook::TimerCallback(HWND, UINT, UINT_PTR id, DWORD)
{
    KillTimer(nullptr, id);
    s_timerId = 0;
    if (s_recording.load())
    {
        LOG_G_INFO(L"KeyboardHook: recording timeout, sending ChordComplete");
        HWND hWnd = s_hRecordWnd.load();
        s_recording.store(false);
        if (hWnd)
        {
            PostMessageW(hWnd, KeyboardHookMsg::ChordComplete, 0,
                         (LPARAM)s_recordModifiers);
        }
        s_recordModifiers = 0;
        s_hadNonModifier  = false;
        s_pressedCount    = 0;
    }
}

// ============================================================
// Low-level keyboard callback (runs on hook thread)
// ============================================================

LRESULT CALLBACK KeyboardHook::LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    bool suppressKeyEvent = false;
    if (nCode == HC_ACTION && s_recording.load())
    {
        suppressKeyEvent = true;
        auto* kbdll  = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        DWORD vk     = kbdll->vkCode;
        bool  isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        bool  isUp   = (wParam == WM_KEYUP   || wParam == WM_SYSKEYUP);

        HWND hWnd    = s_hRecordWnd.load();

        if (isDown)
        {
            // Arm the timeout timer on first key-down (hook-thread timer)
            if (s_pressedCount == 0 && s_timerId == 0)
            {
                DWORD tms = s_timeoutMs.load();
                if (tms > 0)
                    s_timerId = SetTimer(nullptr, 0, tms, TimerCallback);
            }

            DWORD mods = CurrentModifiers();
            s_recordModifiers |= mods;

            if (IsModifierVk(vk))
            {
                // Modifier-only press: update display via KeyCaptured(vk=0)
                if (hWnd) PostMessageW(hWnd, KeyboardHookMsg::KeyCaptured, 0, (LPARAM)mods);
            }
            else
            {
                s_hadNonModifier = true;
                s_pressedCount++;
                // Post the main captured key
                if (hWnd) PostMessageW(hWnd, KeyboardHookMsg::KeyCaptured, vk, (LPARAM)mods);
            }
        }
        else if (isUp && s_hadNonModifier)
        {
            if (!IsModifierVk(vk))
            {
                s_pressedCount--;
                if (s_pressedCount <= 0)
                {
                    // All captured non-modifier keys released → chord complete
                    s_pressedCount = 0;
                    s_recording.store(false);
                    if (s_timerId) { KillTimer(nullptr, s_timerId); s_timerId = 0; }

                    DWORD finalMods = s_recordModifiers;
                    s_recordModifiers = 0;
                    s_hadNonModifier  = false;

                    if (hWnd) PostMessageW(hWnd, KeyboardHookMsg::ChordComplete, 0, (LPARAM)finalMods);
                    LOG_G_INFO(L"KeyboardHook: chord complete, mods=0x%X", finalMods);
                }
            }
        }
    }

    if (suppressKeyEvent)
    {
        return 1;
    }

    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}
