#pragma once

#include <Windows.h>

// Shared shutdown primitive for low-level input hook threads.  The caller owns
// its state reset and supplies the hook cleanup that must happen before the
// explicitly requested TerminateThread fallback.
namespace InputHookThreadStop
{
    struct Result
    {
        DWORD waitResult = WAIT_OBJECT_0;
        DWORD exitCode = STILL_ACTIVE;
        bool quitPosted = false;
        bool forceTerminated = false;
    };

    template <typename TimeoutCleanup>
    Result RequestStopAndClose(HANDLE thread, DWORD threadId, DWORD timeoutMs, TimeoutCleanup&& cleanupBeforeForce)
    {
        Result result{};
        if (!thread)
            return result;

        const ULONGLONG deadline = GetTickCount64() + timeoutMs;
        while (true)
        {
            if (threadId == 0)
                threadId = GetThreadId(thread);
            if (threadId != 0 && PostThreadMessageW(threadId, WM_QUIT, 0, 0))
            {
                result.quitPosted = true;
                break;
            }

            const ULONGLONG now = GetTickCount64();
            if (now >= deadline)
                break;
            const ULONGLONG remaining = deadline - now;
            const DWORD sliceMs = static_cast<DWORD>(remaining > 10 ? 10 : remaining);
            if (WaitForSingleObject(thread, sliceMs) == WAIT_OBJECT_0)
            {
                result.waitResult = WAIT_OBJECT_0;
                GetExitCodeThread(thread, &result.exitCode);
                CloseHandle(thread);
                return result;
            }
        }

        const ULONGLONG now = GetTickCount64();
        const DWORD remainingMs = now >= deadline ? 0 : static_cast<DWORD>(deadline - now);
        result.waitResult = WaitForSingleObject(thread, remainingMs);
        if (result.waitResult == WAIT_TIMEOUT)
        {
            cleanupBeforeForce();
            result.forceTerminated = TerminateThread(thread, 0) != FALSE;
            if (result.forceTerminated)
                WaitForSingleObject(thread, 250);
        }

        GetExitCodeThread(thread, &result.exitCode);
        CloseHandle(thread);
        return result;
    }
}
