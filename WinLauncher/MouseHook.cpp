#include "MouseHook.h"
#include "App/AppMessages.h"
#include "App/Logger.h"
#include "App/InputHookThreadStop.h"
#include "InputFocusGuard.h"
#include "Services/MacroService.h"
#include <algorithm>
#include <cwctype>
#include <mutex>
#include <string>
#include <vector>

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
    std::mutex g_triggerBlacklistMutex;
    std::vector<std::wstring> g_triggerBlacklist;

    bool IsCtrlDown()
    {
        return (GetAsyncKeyState(VK_CONTROL) & 0x8000) ||
            (GetAsyncKeyState(VK_LCONTROL) & 0x8000) ||
            (GetAsyncKeyState(VK_RCONTROL) & 0x8000);
    }

    bool IsShiftDown()
    {
        return (GetAsyncKeyState(VK_SHIFT) & 0x8000) ||
            (GetAsyncKeyState(VK_LSHIFT) & 0x8000) ||
            (GetAsyncKeyState(VK_RSHIFT) & 0x8000);
    }

    bool IsAltDown()
    {
        return (GetAsyncKeyState(VK_MENU) & 0x8000) ||
            (GetAsyncKeyState(VK_LMENU) & 0x8000) ||
            (GetAsyncKeyState(VK_RMENU) & 0x8000);
    }

    void TrimInPlace(std::wstring& value)
    {
        while (!value.empty() && iswspace(value.back()))
            value.pop_back();
        size_t start = 0;
        while (start < value.size() && iswspace(value[start]))
            ++start;
        if (start > 0)
            value.erase(0, start);
    }

    std::wstring ToLowerCopy(std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
            return static_cast<wchar_t>(towlower(ch));
        });
        return value;
    }

    std::wstring FileNameFromPath(const std::wstring& path)
    {
        size_t slash = path.find_last_of(L"\\/");
        if (slash == std::wstring::npos)
            return path;
        return path.substr(slash + 1);
    }

    std::wstring StripExeExtension(std::wstring value)
    {
        const std::wstring suffix = L".exe";
        if (value.size() >= suffix.size() &&
            value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0)
        {
            value.resize(value.size() - suffix.size());
        }
        return value;
    }

    std::wstring GetForegroundProcessName()
    {
        HWND hwnd = GetForegroundWindow();
        if (!hwnd)
            return L"";

        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == 0)
            return L"";

        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!process)
            return L"";

        std::vector<wchar_t> path(32768);
        DWORD len = static_cast<DWORD>(path.size());
        std::wstring processName;
        if (QueryFullProcessImageNameW(process, 0, path.data(), &len) && len > 0)
            processName = FileNameFromPath(std::wstring(path.data(), len));
        CloseHandle(process);
        return ToLowerCopy(processName);
    }

    bool IsTriggerBlacklistedForForegroundApp()
    {
        std::vector<std::wstring> blacklist;
        {
            std::lock_guard<std::mutex> lock(g_triggerBlacklistMutex);
            blacklist = g_triggerBlacklist;
        }

        if (blacklist.empty())
            return false;

        std::wstring processName = GetForegroundProcessName();
        if (processName.empty())
            return false;

        std::wstring processStem = StripExeExtension(processName);
        for (const auto& entry : blacklist)
        {
            std::wstring item = ToLowerCopy(FileNameFromPath(entry));
            TrimInPlace(item);
            if (item.empty())
                continue;

            std::wstring itemStem = StripExeExtension(item);
            if (item == processName ||
                itemStem == processStem ||
                processName.find(item) != std::wstring::npos ||
                processStem.find(itemStem) != std::wstring::npos)
            {
                return true;
            }
        }
        return false;
    }

    void LogHookThreadStopResult(const wchar_t* operation, const InputHookThreadStop::Result& result)
    {
        LOG_G_INFO(L"MouseHook::%ls: quitPosted=%d wait=%lu forceTerminated=%d exitCode=%lu",
            operation, result.quitPosted, result.waitResult, result.forceTerminated, result.exitCode);
    }
}

void MouseHook::SetTriggerType(int type)
{
    s_triggerType.store(type);
}

void MouseHook::SetTriggerBlacklist(const std::vector<std::wstring>& processNames)
{
    std::vector<std::wstring> normalized;
    normalized.reserve(processNames.size());
    for (auto item : processNames)
    {
        item = ToLowerCopy(FileNameFromPath(item));
        TrimInPlace(item);
        if (item.empty())
            continue;

        bool exists = false;
        for (const auto& existing : normalized)
        {
            if (existing == item || StripExeExtension(existing) == StripExeExtension(item))
            {
                exists = true;
                break;
            }
        }
        if (!exists)
            normalized.push_back(std::move(item));
    }

    std::lock_guard<std::mutex> lock(g_triggerBlacklistMutex);
    g_triggerBlacklist = std::move(normalized);
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
    const auto stopResult = InputHookThreadStop::RequestStopAndClose(
        s_hThread, s_hookThreadId.load(), 1000, []() {
            HHOOK hook = MouseHook::s_hHook.exchange(nullptr);
            if (hook) UnhookWindowsHookEx(hook);
        });
    LogHookThreadStopResult(L"InstallFailureCleanup", stopResult);
    s_hThread = nullptr;
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

    const auto stopResult = InputHookThreadStop::RequestStopAndClose(
        s_hThread, s_hookThreadId.load(), 2000, []() {
            HHOOK hook = MouseHook::s_hHook.exchange(nullptr);
            if (hook) UnhookWindowsHookEx(hook);
        });
    LogHookThreadStopResult(L"Uninstall", stopResult);
    s_hThread = nullptr;

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
        else if (trigger == 3 || trigger == 4 || trigger == 5) // Modifier + Middle Click
        {
            if (wParam == WM_MBUTTONDOWN)
            {
                bool modifierMatched =
                    (trigger == 3 && IsCtrlDown()) ||
                    (trigger == 4 && IsShiftDown()) ||
                    (trigger == 5 && IsAltDown());
                if (modifierMatched)
                {
                    activated = true;
                    suppressUpMask = SuppressMiddleUp;
                }
            }
        }
        else if (trigger == 6 || trigger == 7) // Ctrl + Side button 4 or 5
        {
            if (wParam == WM_XBUTTONDOWN && IsCtrlDown())
            {
                WORD btn = HIWORD(pMsh->mouseData);
                if (trigger == 6 && btn == XBUTTON1)
                {
                    activated = true;
                    suppressUpMask = SuppressXButton1Up;
                }
                if (trigger == 7 && btn == XBUTTON2)
                {
                    activated = true;
                    suppressUpMask = SuppressXButton2Up;
                }
            }
        }

        if (activated)
        {
            if (IsTriggerBlacklistedForForegroundApp())
            {
                LOG_G_INFO(L"MouseHook::LowLevelMouseProc: trigger passed through because foreground process is blacklisted");
                return CallNextHookEx(nullptr, nCode, wParam, lParam);
            }

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
