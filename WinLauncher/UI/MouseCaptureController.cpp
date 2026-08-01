#include "MouseCaptureController.h"
#include "../App/Logger.h"

namespace
{
    struct CaptureState
    {
        HWND owner = nullptr;
        MouseCaptureController::Mode mode = MouseCaptureController::Mode::None;
        int virtualKey = 0;
        DWORD ownerThreadId = 0;
        ULONGLONG startedAt = 0;
        void* context = nullptr;
        MouseCaptureController::CancelCallback cancel = nullptr;
    };

    CaptureState g_capture;

    CaptureState TakeCaptureState()
    {
        CaptureState previous = g_capture;
        g_capture = {};
        return previous;
    }

    void NotifyCancelled(CaptureState state)
    {
        if (state.cancel)
            state.cancel(state.context);
    }

    void ClearTrackedCapture(bool notify)
    {
        CaptureState previous = TakeCaptureState();
        if (notify)
            NotifyCancelled(previous);
    }

    bool IsOwnedByCurrentThread(HWND owner, DWORD& threadId)
    {
        threadId = 0;
        if (!owner || !IsWindow(owner))
            return false;
        threadId = GetWindowThreadProcessId(owner, nullptr);
        return threadId != 0 && threadId == GetCurrentThreadId();
    }

    const wchar_t* ModeName(MouseCaptureController::Mode mode)
    {
        switch (mode)
        {
        case MouseCaptureController::Mode::Gesture: return L"gesture";
        case MouseCaptureController::Mode::PersistentPopup: return L"persistent_popup";
        default: return L"none";
        }
    }

    void LogRecovery(const CaptureState& state, const wchar_t* reason, bool released)
    {
        wchar_t className[96]{};
        if (state.owner && IsWindow(state.owner))
            GetClassNameW(state.owner, className, static_cast<int>(sizeof(className) / sizeof(className[0])));

        const ULONGLONG now = GetTickCount64();
        const ULONGLONG heldMs = state.startedAt != 0 && now >= state.startedAt ? now - state.startedAt : 0;
        LOG_G_INFO(
            L"MouseCaptureController: recovered mode=%ls class=%ls heldMs=%llu reason=%ls released=%d",
            ModeName(state.mode), className[0] ? className : L"unknown", heldMs,
            reason ? reason : L"unknown", released ? 1 : 0);
    }

    bool Capture(HWND owner, MouseCaptureController::Mode mode, int virtualKey,
                 void* context, MouseCaptureController::CancelCallback cancel)
    {
        DWORD ownerThreadId = 0;
        if (!IsOwnedByCurrentThread(owner, ownerThreadId))
            return false;

        // SetCapture does not emit WM_CAPTURECHANGED when a different control
        // inside the same top-level HWND takes over. Cancel the previous local
        // interaction explicitly before replacing its context.
        if (g_capture.owner == owner &&
            (g_capture.context != context || g_capture.mode != mode))
        {
            ClearTrackedCapture(true);
        }

        SetCapture(owner);
        if (GetCapture() != owner)
            return false;

        g_capture.owner = owner;
        g_capture.mode = mode;
        g_capture.virtualKey = virtualKey;
        g_capture.ownerThreadId = ownerThreadId;
        g_capture.startedAt = GetTickCount64();
        g_capture.context = context;
        g_capture.cancel = cancel;
        return true;
    }
}

namespace MouseCaptureController
{
    bool CaptureGesture(HWND owner, int virtualKey, void* context, CancelCallback cancel)
    {
        return Capture(owner, Mode::Gesture, virtualKey, context, cancel);
    }

    bool CapturePersistent(HWND owner)
    {
        return Capture(owner, Mode::PersistentPopup, 0, nullptr, nullptr);
    }

    bool Complete(HWND expectedOwner)
    {
        HWND captured = GetCapture();
        if (expectedOwner && captured && captured != expectedOwner)
            return false;

        if (!expectedOwner || g_capture.owner == expectedOwner)
            ClearTrackedCapture(false);

        if (captured && (!expectedOwner || captured == expectedOwner))
            return ReleaseCapture() != FALSE;
        return captured == nullptr;
    }

    bool Release(HWND expectedOwner, const wchar_t*)
    {
        HWND captured = GetCapture();
        if (expectedOwner && captured && captured != expectedOwner)
        {
            if (g_capture.owner == expectedOwner)
                ClearTrackedCapture(true);
            return false;
        }

        const bool tracked = !expectedOwner || g_capture.owner == expectedOwner;
        if (captured && (!expectedOwner || captured == expectedOwner))
        {
            const BOOL released = ReleaseCapture();
            // ReleaseCapture normally sends WM_CAPTURECHANGED synchronously.
            // Retain a fallback for teardown paths where no message is sent.
            if (tracked && g_capture.owner && GetCapture() != g_capture.owner)
                ClearTrackedCapture(true);
            return released != FALSE;
        }

        if (tracked && g_capture.owner)
            ClearTrackedCapture(true);
        return captured == nullptr;
    }

    bool ReleaseCurrent(const wchar_t* reason)
    {
        return Release(GetCapture(), reason);
    }

    bool ReleaseForContext(void* context, const wchar_t* reason)
    {
        if (!context || g_capture.context != context)
            return false;
        return Release(g_capture.owner, reason);
    }

    void OnCaptureChanged(HWND losingOwner, HWND gainingOwner)
    {
        if (g_capture.owner == losingOwner && gainingOwner != losingOwner)
            ClearTrackedCapture(true);
    }

    bool RecoverStaleGestureCapture(const wchar_t* reason)
    {
        if (g_capture.mode != Mode::Gesture || !g_capture.owner)
            return false;

        const CaptureState snapshot = g_capture;
        const bool ownsCapture = GetCapture() == snapshot.owner;
        const bool ownerUsable = IsWindow(snapshot.owner) && IsWindowVisible(snapshot.owner) &&
            snapshot.ownerThreadId == GetCurrentThreadId();
        const bool buttonDown = snapshot.virtualKey != 0 &&
            (GetAsyncKeyState(snapshot.virtualKey) & 0x8000) != 0;

        if (!ShouldRecoverGesture(ownsCapture, ownerUsable, buttonDown))
            return false;

        bool released = false;
        if (ownsCapture)
            released = Release(snapshot.owner, reason);
        else
            ClearTrackedCapture(true);
        LogRecovery(snapshot, reason, released || !ownsCapture);
        return true;
    }

    bool ForceReleaseGestureCapture(const wchar_t* reason)
    {
        if (g_capture.mode != Mode::Gesture || !g_capture.owner)
            return false;
        const CaptureState snapshot = g_capture;
        const bool ownsCapture = GetCapture() == snapshot.owner;
        const bool released = ownsCapture ? Release(snapshot.owner, reason) : false;
        if (!ownsCapture)
            ClearTrackedCapture(true);
        LogRecovery(snapshot, reason, released || !ownsCapture);
        return true;
    }

    Mode CurrentMode()
    {
        return g_capture.mode;
    }

    HWND CurrentOwner()
    {
        return g_capture.owner;
    }
}
