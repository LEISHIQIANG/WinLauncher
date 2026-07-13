#pragma once

#include <Windows.h>

// Shared shutdown primitive for low-level input hook threads.  A low-level
// hook can hold Win32/CRT state while it is inside a callback, so terminating
// its thread is unsafe.  Callers retain a timed-out handle, disable their
// input path, and reap it once the thread exits naturally.
namespace InputHookThreadStop
{
    struct Result
    {
        DWORD waitResult = WAIT_OBJECT_0;
        DWORD exitCode = STILL_ACTIVE;
        bool quitPosted = false;
        bool timedOut = false;
    };

    inline bool ReapIfExited(HANDLE& thread)
    {
        if (!thread)
            return true;
        if (WaitForSingleObject(thread, 0) != WAIT_OBJECT_0)
            return false;
        CloseHandle(thread);
        thread = nullptr;
        return true;
    }

    inline Result RequestStop(HANDLE thread, DWORD threadId, DWORD timeoutMs)
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
                return result;
            }
        }

        const ULONGLONG now = GetTickCount64();
        const DWORD remainingMs = now >= deadline ? 0 : static_cast<DWORD>(deadline - now);
        result.waitResult = WaitForSingleObject(thread, remainingMs);
        result.timedOut = result.waitResult == WAIT_TIMEOUT;

        GetExitCodeThread(thread, &result.exitCode);
        return result;
    }
}
