#include "MouseHook.h"
#include "App/AppMessages.h"
#include "App/Logger.h"
#include "App/MouseButtonPairs.h"
#include "App/InputHookThreadStop.h"
#include "InputFocusGuard.h"
#include "Services/MacroService.h"
#include "TriggerBlacklistPolicy.h"
#include "TriggerPolicy.h"
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

std::atomic<int>    MouseHook::s_triggerType(0);
std::atomic<HHOOK>  MouseHook::s_hHook       = nullptr;
std::atomic<HWND>   MouseHook::s_hTargetWnd  = nullptr;
HANDLE              MouseHook::s_hThread     = nullptr;
std::atomic<DWORD>  MouseHook::s_hookThreadId = 0;
HANDLE              MouseHook::s_hReadyEvent = nullptr;
std::atomic<bool>   MouseHook::s_running(false);
std::atomic<bool>   MouseHook::s_triggerEnabled(true);
std::atomic<bool>   MouseHook::s_popupRequestPending(false);
std::atomic<ULONG_PTR> MouseHook::s_triggerGeneration(1);
HMODULE             MouseHook::s_hModule     = nullptr;

namespace
{
    constexpr DWORD SuppressMiddleUp  = 0x01;
    constexpr DWORD SuppressXButton1Up = 0x02;
    constexpr DWORD SuppressXButton2Up = 0x04;
    constexpr size_t ProcessIdentityCacheCapacity = 16;
    constexpr size_t CommonProcessPathCapacity = 1024;
    constexpr size_t MaximumProcessPathCapacity = 32768;

    MouseButtonPairs g_buttonPairs;
    struct HookDiagnostic { WPARAM message; DWORD eventTime; ULONGLONG elapsed; DWORD pairs; bool consumed; };
    std::array<HookDiagnostic, 64> g_diagnostics{};
    std::atomic_size_t g_diagnosticWrite{0}, g_diagnosticRead{0};
    std::atomic_uint g_diagnosticDropped{0};
    // Single hook producer, UI heartbeat consumer. Never allocate or log here.
    void RecordHook(WPARAM message, DWORD eventTime, ULONGLONG started, bool consumed)
    {
        const size_t write = g_diagnosticWrite.load(std::memory_order_relaxed);
        if (write - g_diagnosticRead.load(std::memory_order_acquire) >= g_diagnostics.size())
        { ++g_diagnosticDropped; return; }
        g_diagnostics[write % g_diagnostics.size()] = {message, eventTime, GetTickCount64() - started, g_buttonPairs.Snapshot(), consumed};
        g_diagnosticWrite.store(write + 1, std::memory_order_release);
    }

    std::shared_ptr<const TriggerBlacklistPolicy::Matcher> g_triggerBlacklist =
        std::make_shared<const TriggerBlacklistPolicy::Matcher>();

    struct ProcessIdentity
    {
        DWORD pid = 0;
        HANDLE process = nullptr;
        bool lifetimeCheckAvailable = false;
        uint64_t lastUse = 0;
        std::wstring processName;
        std::wstring processStem;
    };

    class ProcessIdentityCache
    {
    public:
        ~ProcessIdentityCache()
        {
            for (auto& entry : m_entries)
                Reset(entry);
        }

        const ProcessIdentity* Resolve(HWND hwnd)
        {
            DWORD pid = 0;
            if (!hwnd || GetWindowThreadProcessId(hwnd, &pid) == 0 || pid == 0)
                return nullptr;

            const uint64_t access = ++m_accessSerial;
            for (auto& entry : m_entries)
            {
                if (entry.pid != pid)
                    continue;

                // Holding a SYNCHRONIZE handle prevents PID reuse from making a
                // stale cache entry look valid. This zero-time wait never
                // blocks the low-level hook.
                if (entry.process &&
                    entry.lifetimeCheckAvailable &&
                    WaitForSingleObject(entry.process, 0) == WAIT_TIMEOUT)
                {
                    entry.lastUse = access;
                    return &entry;
                }

                Reset(entry);
                break;
            }

            // These process queries do not synchronously call the target GUI
            // thread, so an unresponsive application cannot stall us in the
            // same way as GetGUIThreadInfo/GetClassNameW on a foreign window.
            // A successful lookup is cached and lifetime-checked thereafter.
            HANDLE process = OpenProcess(
                PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
                FALSE,
                pid);
            bool lifetimeCheckAvailable = process != nullptr;
            if (!process)
            {
                // A protected process may permit image-name queries while
                // denying SYNCHRONIZE. Preserve correct one-shot matching in
                // that case; only lifetime-validated handles enter the hot
                // cache path.
                process = OpenProcess(
                    PROCESS_QUERY_LIMITED_INFORMATION,
                    FALSE,
                    pid);
            }
            if (!process)
                return nullptr;

            std::wstring processName;
            if (!QueryProcessName(process, processName))
            {
                CloseHandle(process);
                return nullptr;
            }

            ProcessIdentity* slot = FindReplacementSlot();
            Reset(*slot);
            slot->pid = pid;
            slot->process = process;
            slot->lifetimeCheckAvailable = lifetimeCheckAvailable;
            slot->lastUse = access;
            slot->processName = std::move(processName);
            slot->processStem = TriggerBlacklistPolicy::StemOf(slot->processName);
            return slot;
        }

    private:
        static bool QueryProcessName(HANDLE process, std::wstring& processName)
        {
            wchar_t commonPath[CommonProcessPathCapacity]{};
            DWORD length = static_cast<DWORD>(CommonProcessPathCapacity);
            if (QueryFullProcessImageNameW(process, 0, commonPath, &length) && length > 0)
            {
                processName.assign(commonPath, length);
            }
            else
            {
                if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
                    return false;

                // Paths above 1023 characters are exceptional. Keep the common
                // hook path stack-only and allocate the maximum buffer only for
                // that rare case.
                std::vector<wchar_t> extendedPath(MaximumProcessPathCapacity);
                length = static_cast<DWORD>(extendedPath.size());
                if (!QueryFullProcessImageNameW(process, 0, extendedPath.data(), &length) ||
                    length == 0)
                {
                    return false;
                }
                processName.assign(extendedPath.data(), length);
            }

            TriggerBlacklistPolicy::NormalizeProcessNameInPlace(processName);
            return !processName.empty();
        }

        ProcessIdentity* FindReplacementSlot()
        {
            ProcessIdentity* replacement = &m_entries[0];
            for (auto& entry : m_entries)
            {
                if (entry.pid == 0)
                    return &entry;
                if (entry.lastUse < replacement->lastUse)
                    replacement = &entry;
            }
            return replacement;
        }

        static void Reset(ProcessIdentity& entry)
        {
            if (entry.process)
                CloseHandle(entry.process);
            entry = {};
        }

        std::array<ProcessIdentity, ProcessIdentityCacheCapacity> m_entries{};
        uint64_t m_accessSerial = 0;
    };

    // The hook callback always runs on its dedicated message-loop thread.
    // Thread-local storage therefore needs no lock and releases cached process
    // handles automatically whenever that hook thread exits.
    thread_local ProcessIdentityCache g_processIdentityCache;

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

    bool IsTriggerBlacklistedAtPoint(POINT triggerPoint)
    {
        const auto blacklist = std::atomic_load_explicit(&g_triggerBlacklist, std::memory_order_acquire);

        if (!blacklist || blacklist->empty())
            return false;

        // Resolve a first-seen PID here so blacklist enforcement cannot fail
        // open forever. The cache keeps later triggers on a lifetime-checked
        // fast path; Resolve itself performs no synchronous cross-window calls.
        HWND windowAtPoint = WindowFromPoint(triggerPoint);
        const ProcessIdentity* identity = g_processIdentityCache.Resolve(windowAtPoint);
        if (!identity)
        {
            // A window can disappear between hit testing and PID lookup.
            // Retry only when the hit result actually changed, keeping the
            // normal path to one WindowFromPoint call.
            HWND retryWindow = WindowFromPoint(triggerPoint);
            if (retryWindow && retryWindow != windowAtPoint)
                identity = g_processIdentityCache.Resolve(retryWindow);
        }
        return identity &&
            blacklist->MatchesNormalized(identity->processName, identity->processStem);
    }

    void LogHookThreadStopResult(const wchar_t* operation, const InputHookThreadStop::Result& result)
    {
        LOG_G_INFO(L"MouseHook::%ls: quitPosted=%d wait=%lu timedOut=%d exitCode=%lu",
            operation, result.quitPosted, result.waitResult, result.timedOut, result.exitCode);
    }

    DWORD SuppressionMaskFor(TriggerPolicy::Button button)
    {
        switch (button)
        {
        case TriggerPolicy::Button::Middle: return SuppressMiddleUp;
        case TriggerPolicy::Button::XButton1: return SuppressXButton1Up;
        case TriggerPolicy::Button::XButton2: return SuppressXButton2Up;
        default: return 0;
        }
    }
}

void MouseHook::SetTriggerType(int type)
{
    const int normalized = TriggerPolicy::NormalizeTriggerType(type);
    const int previous = s_triggerType.exchange(normalized, std::memory_order_acq_rel);
    if (previous != normalized)
    {
        // A click queued under a previous preset must never open a popup after
        // the user has changed the trigger. Keep an already-consumed button's
        // up event intact so the foreground app never receives an orphaned up.
        s_popupRequestPending.store(false, std::memory_order_release);
        s_triggerGeneration.fetch_add(1, std::memory_order_acq_rel);
    }
}

void MouseHook::SetTriggerEnabled(bool enabled)
{
    const bool wasEnabled = s_triggerEnabled.exchange(enabled, std::memory_order_acq_rel);
    if (!enabled)
    {
        // A pause may occur between a consumed down event and its up event.
        // Preserve the matching up suppression so foreground apps never see an
        // unmatched button-up; all newly arriving input passes through.
        s_popupRequestPending.store(false, std::memory_order_release);
        if (wasEnabled)
            s_triggerGeneration.fetch_add(1, std::memory_order_acq_rel);
    }
}

bool MouseHook::AcknowledgePopupRequest(ULONG_PTR requestGeneration)
{
    if (!s_triggerEnabled.load(std::memory_order_acquire) ||
        requestGeneration != s_triggerGeneration.load(std::memory_order_acquire))
    {
        return false;
    }

    bool expected = true;
    return s_popupRequestPending.compare_exchange_strong(expected, false, std::memory_order_acq_rel);
}

void MouseHook::SetTriggerBlacklist(const std::vector<std::wstring>& processNames)
{
    std::atomic_store_explicit(
        &g_triggerBlacklist,
        std::make_shared<const TriggerBlacklistPolicy::Matcher>(
            TriggerBlacklistPolicy::Matcher::Compile(processNames)),
        std::memory_order_release);
}

bool MouseHook::Install(HWND hTargetWnd)
{
    LOG_G_INFO(L"MouseHook::Install called");
    if (s_running.load()) return IsInstalled();
    if (s_hThread && !InputHookThreadStop::ReapIfExited(s_hThread))
    {
        LOG_G_WORNING(L"MouseHook::Install: previous hook thread is still stopping");
        return false;
    }
    if (s_hReadyEvent) { CloseHandle(s_hReadyEvent); s_hReadyEvent = nullptr; }

    s_hTargetWnd = hTargetWnd;
    s_popupRequestPending.store(false);
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
    const auto stopResult = InputHookThreadStop::RequestStop(s_hThread, s_hookThreadId.load(), 1000);
    LogHookThreadStopResult(L"InstallFailureCleanup", stopResult);
    if (stopResult.waitResult == WAIT_OBJECT_0)
        InputHookThreadStop::ReapIfExited(s_hThread);
    if (!stopResult.timedOut && s_hReadyEvent) { CloseHandle(s_hReadyEvent); s_hReadyEvent = nullptr; }
    s_hTargetWnd = nullptr;
    s_hookThreadId.store(0);
    return false;
}

void MouseHook::Uninstall()
{
    LOG_G_INFO(L"MouseHook::Uninstall called");
    SetTriggerEnabled(false);
    if (!s_running.load() && !s_hThread) return;

    s_running.store(false);

    const auto stopResult = InputHookThreadStop::RequestStop(s_hThread, s_hookThreadId.load(), 2000);
    LogHookThreadStopResult(L"Uninstall", stopResult);
    if (stopResult.waitResult == WAIT_OBJECT_0)
        InputHookThreadStop::ReapIfExited(s_hThread);

    if (!stopResult.timedOut && s_hReadyEvent) { CloseHandle(s_hReadyEvent); s_hReadyEvent = nullptr; }

    if (!stopResult.timedOut)
        s_hHook.store(nullptr);
    s_hTargetWnd = nullptr;
    s_hookThreadId.store(0);
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

void MouseHook::FlushDiagnostics()
{
    size_t read = g_diagnosticRead.load(std::memory_order_relaxed);
    const size_t write = g_diagnosticWrite.load(std::memory_order_acquire);
    while (read != write)
    {
        const auto item = g_diagnostics[read % g_diagnostics.size()];
        LOG_G_DEBUG(L"MouseHook event: msg=%u tick=%lu elapsed_ms=%llu pairs=%lu consumed=%d",
                    static_cast<UINT>(item.message), item.eventTime, item.elapsed, item.pairs, item.consumed);
        ++read;
    }
    g_diagnosticRead.store(read, std::memory_order_release);
    const auto dropped = g_diagnosticDropped.exchange(0);
    if (dropped) LOG_G_WORNING(L"MouseHook diagnostics dropped=%u", dropped);
}

LRESULT CALLBACK MouseHook::LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION)
    {
        auto* pMsh = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
        if (!pMsh) return CallNextHookEx(nullptr, nCode, wParam, lParam);
        // Ordinary input and injected recovery events always pass through.
        const bool middle = wParam == WM_MBUTTONDOWN || wParam == WM_MBUTTONUP;
        const bool side = wParam == WM_XBUTTONDOWN || wParam == WM_XBUTTONUP;
        if ((!middle && !side) || (pMsh->flags & LLMHF_INJECTED))
        {
            if (MacroPlayer::IsPlaying()) MacroPlayer::RequestInterruptFromMouse(*pMsh);
            return CallNextHookEx(nullptr, nCode, wParam, lParam);
        }
        const ULONGLONG started = GetTickCount64();
        const DWORD button = middle ? SuppressMiddleUp :
            (HIWORD(pMsh->mouseData) == XBUTTON1 ? SuppressXButton1Up : SuppressXButton2Up);
        const bool up = wParam == WM_MBUTTONUP || wParam == WM_XBUTTONUP;
        // Pair completion precedes every mutable policy, including recording.
        if (up)
        {
            const bool consumed = g_buttonPairs.Up(button);
            RecordHook(wParam, pMsh->time, started, consumed);
            if (consumed) return 1;
            return CallNextHookEx(nullptr, nCode, wParam, lParam);
        }
        g_buttonPairs.Down(button, false);
        struct DiagnosticScope
        {
            WPARAM message; DWORD time; ULONGLONG started; bool consumed = false;
            ~DiagnosticScope() { RecordHook(message, time, started, consumed); }
        } diagnostic{wParam, pMsh->time, started};
        if (MacroPlayer::IsPlaying())
        {
            MacroPlayer::RequestInterruptFromMouse(*pMsh);
            return CallNextHookEx(nullptr, nCode, wParam, lParam);
        }
        if (!s_hTargetWnd || MacroRecorder::IsRecording())
            return CallNextHookEx(nullptr, nCode, wParam, lParam);

        if (!s_triggerEnabled.load(std::memory_order_acquire))
            return CallNextHookEx(nullptr, nCode, wParam, lParam);

        const auto match = TriggerPolicy::Match(
            s_triggerType.load(std::memory_order_acquire), wParam, pMsh->mouseData,
            IsCtrlDown(), IsShiftDown(), IsAltDown());
        const bool activated = match.activated;
        const DWORD suppressUpMask = SuppressionMaskFor(match.button);

        if (activated)
        {
            if (IsTriggerBlacklistedAtPoint(pMsh->pt))
            {
                return CallNextHookEx(nullptr, nCode, wParam, lParam);
            }

            // Only check whether a WinLauncher-owned text box has focus.
            // Cross-process checks (GetGUIThreadInfo, GetClassNameW) are
            // removed to avoid blocking the hook callback beyond the 200ms
            // LowLevelHooksTimeout when the foreground app is unresponsive.
            if (InputFocusGuard::IsOwnProcessTextInputActive())
            {
                return CallNextHookEx(nullptr, nCode, wParam, lParam);
            }

            // A cold process lookup may be slow even without messaging its UI.
            // If our decision budget was exceeded, pass the complete pair on.
            if (GetTickCount64() - started >= 20)
                return CallNextHookEx(nullptr, nCode, wParam, lParam);

            HWND target = s_hTargetWnd.load();
            if (!target || !IsWindow(target))
            {
                return CallNextHookEx(nullptr, nCode, wParam, lParam);
            }

            bool expected = false;
            const ULONG_PTR requestGeneration = s_triggerGeneration.load(std::memory_order_acquire);
            if (!s_popupRequestPending.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                return CallNextHookEx(nullptr, nCode, wParam, lParam);

            // Settings changes run on the UI thread, while this callback is on
            // the hook thread. Do not post a request that crossed that boundary.
            if (requestGeneration != s_triggerGeneration.load(std::memory_order_acquire))
            {
                s_popupRequestPending.store(false, std::memory_order_release);
                return CallNextHookEx(nullptr, nCode, wParam, lParam);
            }
            if (!PostMessageW(target, AppMessages::ShowPopup, requestGeneration, static_cast<LPARAM>(pMsh->time)))
            {
                s_popupRequestPending.store(false, std::memory_order_release);
                return CallNextHookEx(nullptr, nCode, wParam, lParam);
            }
            g_buttonPairs.Down(suppressUpMask, true);
            diagnostic.consumed = true;
            return 1;
        }
    }

    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}
