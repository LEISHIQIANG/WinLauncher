#include "MouseHook.h"
#include "App/AppMessages.h"
#include "App/Logger.h"
#include "App/MouseButtonPairs.h"
#include "App/InputHookThreadStop.h"
#include "InputFocusGuard.h"
#include "Services/MacroService.h"
#include "Services/TriggerProcessResolver.h"
#include "TriggerPolicy.h"
#include <array>
#include <cstdint>

std::atomic<int>    MouseHook::s_triggerType(0);
std::atomic<HHOOK>  MouseHook::s_hHook       = nullptr;
std::atomic<HWND>   MouseHook::s_hTargetWnd  = nullptr;
HANDLE              MouseHook::s_hThread     = nullptr;
std::atomic<DWORD>  MouseHook::s_hookThreadId = 0;
HANDLE              MouseHook::s_hReadyEvent = nullptr;
std::atomic<bool>   MouseHook::s_running(false);
std::atomic<bool>   MouseHook::s_triggerEnabled(true);
std::atomic<ULONG_PTR> MouseHook::s_triggerGeneration(1);
HMODULE             MouseHook::s_hModule     = nullptr;

namespace
{
    constexpr DWORD SuppressMiddleUp  = 0x01;
    constexpr DWORD SuppressXButton1Up = 0x02;
    constexpr DWORD SuppressXButton2Up = 0x04;
    constexpr ULONGLONG ProcessPrefetchSampleMs = 200;

    MouseButtonPairs g_buttonPairs;
    struct HookDiagnostic { WPARAM message; DWORD eventTime; ULONGLONG elapsed; DWORD pairs; bool consumed; };
    std::array<HookDiagnostic, 64> g_diagnostics{};
    std::atomic_size_t g_diagnosticWrite{0}, g_diagnosticRead{0};
    std::atomic_uint g_diagnosticDropped{0};
    std::atomic_uint64_t g_triggerAccepted{0};
    std::atomic_uint64_t g_triggerBlacklisted{0};
    std::atomic_uint64_t g_triggerUnknownFailOpen{0};
    std::atomic_uint64_t g_triggerPostFailed{0};
    std::atomic<ULONGLONG> g_lastPrefetchSampleTick{0};
    std::atomic<DWORD> g_lastPrefetchPid{0};
    std::atomic<TriggerProcessResolver*> g_processResolver{nullptr};
    // Single hook producer, UI heartbeat consumer. Never allocate or log here.
    void RecordHook(WPARAM message, DWORD eventTime, ULONGLONG started, bool consumed)
    {
        const size_t write = g_diagnosticWrite.load(std::memory_order_relaxed);
        if (write - g_diagnosticRead.load(std::memory_order_acquire) >= g_diagnostics.size())
        { ++g_diagnosticDropped; return; }
        g_diagnostics[write % g_diagnostics.size()] = {message, eventTime, GetTickCount64() - started, g_buttonPairs.Snapshot(), consumed};
        g_diagnosticWrite.store(write + 1, std::memory_order_release);
    }

    DWORD ProcessIdAtPoint(POINT point)
    {
        HWND window = WindowFromPoint(point);
        if (!window)
            return 0;
        HWND root = GetAncestor(window, GA_ROOT);
        if (root)
            window = root;
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        return pid;
    }

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

    return true;
}

bool MouseHook::Install(HWND hTargetWnd, TriggerProcessResolver* processResolver)
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
    g_processResolver.store(processResolver, std::memory_order_release);
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
    g_processResolver.store(nullptr, std::memory_order_release);
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
    g_processResolver.store(nullptr, std::memory_order_release);
    s_hookThreadId.store(0);
    LOG_G_INFO(L"MouseHook::Uninstall: uninstalled successfully");
}

bool MouseHook::IsInstalled()
{
    return s_hHook != nullptr;
}

bool MouseHook::IsHealthy()
{
    return IsInstalled() && s_running.load(std::memory_order_acquire) && s_hThread &&
        WaitForSingleObject(s_hThread, 0) == WAIT_TIMEOUT;
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

    static ULONGLONG lastSummaryTick = 0;
    const ULONGLONG now = GetTickCount64();
    const uint64_t postFailed = g_triggerPostFailed.load(std::memory_order_relaxed);
    if (postFailed || lastSummaryTick == 0 || now - lastSummaryTick >= 5000)
    {
        const uint64_t accepted = g_triggerAccepted.exchange(0, std::memory_order_relaxed);
        const uint64_t blacklisted = g_triggerBlacklisted.exchange(0, std::memory_order_relaxed);
        const uint64_t unknown = g_triggerUnknownFailOpen.exchange(0, std::memory_order_relaxed);
        const uint64_t failed = g_triggerPostFailed.exchange(0, std::memory_order_relaxed);
        if (accepted || blacklisted || unknown || failed)
        {
            LOG_G_INFO_NODE(L"input.mouse_hook", L"trigger_summary",
                L"accepted=%llu blacklisted=%llu unknown_fail_open=%llu post_failed=%llu",
                static_cast<unsigned long long>(accepted),
                static_cast<unsigned long long>(blacklisted),
                static_cast<unsigned long long>(unknown),
                static_cast<unsigned long long>(failed));
        }
        lastSummaryTick = now;
    }
}

LRESULT CALLBACK MouseHook::LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION)
    {
        auto* pMsh = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
        if (!pMsh) return CallNextHookEx(nullptr, nCode, wParam, lParam);
        const ULONGLONG callbackTick = GetTickCount64();
        if (wParam == WM_MOUSEMOVE && !(pMsh->flags & LLMHF_INJECTED))
        {
            ULONGLONG lastSample = g_lastPrefetchSampleTick.load(std::memory_order_relaxed);
            if (callbackTick - lastSample >= ProcessPrefetchSampleMs &&
                g_lastPrefetchSampleTick.compare_exchange_strong(lastSample, callbackTick, std::memory_order_relaxed))
            {
                const DWORD pid = ProcessIdAtPoint(pMsh->pt);
                auto* resolver = g_processResolver.load(std::memory_order_acquire);
                HWND target = s_hTargetWnd.load(std::memory_order_acquire);
                const DWORD previousPid = g_lastPrefetchPid.exchange(pid, std::memory_order_relaxed);
                if (pid && pid != previousPid && resolver && !resolver->IsKnown(pid) && target)
                    PostMessageW(target, AppMessages::PrefetchTriggerProcess, pid, 0);
            }
        }
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
            const DWORD pid = ProcessIdAtPoint(pMsh->pt);
            auto* resolver = g_processResolver.load(std::memory_order_acquire);
            const auto processDecision = resolver
                ? resolver->Classify(pid)
                : TriggerProcessResolver::Decision::Unknown;
            if (processDecision == TriggerProcessResolver::Decision::Blacklisted)
            {
                g_triggerBlacklisted.fetch_add(1, std::memory_order_relaxed);
                return CallNextHookEx(nullptr, nCode, wParam, lParam);
            }
            if (processDecision == TriggerProcessResolver::Decision::Unknown)
            {
                g_triggerUnknownFailOpen.fetch_add(1, std::memory_order_relaxed);
                HWND prefetchTarget = s_hTargetWnd.load(std::memory_order_acquire);
                if (pid && prefetchTarget)
                    PostMessageW(prefetchTarget, AppMessages::PrefetchTriggerProcess, pid, 0);
            }

            // Only check whether a WinLauncher-owned text box has focus.
            // Cross-process checks (GetGUIThreadInfo, GetClassNameW) are
            // removed to avoid blocking the hook callback beyond the 200ms
            // LowLevelHooksTimeout when the foreground app is unresponsive.
            if (InputFocusGuard::IsOwnProcessTextInputActive())
            {
                return CallNextHookEx(nullptr, nCode, wParam, lParam);
            }

            HWND target = s_hTargetWnd.load();
            if (!target || !IsWindow(target))
            {
                return CallNextHookEx(nullptr, nCode, wParam, lParam);
            }

            const ULONG_PTR requestGeneration = s_triggerGeneration.load(std::memory_order_acquire);
            // Settings changes run on the UI thread, while this callback is on
            // the hook thread. Do not post a request that crossed that boundary.
            if (requestGeneration != s_triggerGeneration.load(std::memory_order_acquire))
            {
                return CallNextHookEx(nullptr, nCode, wParam, lParam);
            }
            if (!PostMessageW(target, AppMessages::ShowPopup, requestGeneration, static_cast<LPARAM>(pMsh->time)))
            {
                g_triggerPostFailed.fetch_add(1, std::memory_order_relaxed);
                return CallNextHookEx(nullptr, nCode, wParam, lParam);
            }
            g_triggerAccepted.fetch_add(1, std::memory_order_relaxed);
            g_buttonPairs.Down(suppressUpMask, true);
            diagnostic.consumed = true;
            return 1;
        }
    }

    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}
