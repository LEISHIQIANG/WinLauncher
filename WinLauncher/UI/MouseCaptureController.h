#pragma once

#include <Windows.h>

namespace MouseCaptureController
{
    enum class Mode
    {
        None,
        Gesture,
        PersistentPopup,
    };

    using CancelCallback = void (*)(void* context);

    // Gesture capture is valid only while the matching physical button is held.
    // The optional callback is invoked whenever capture is lost or recovered.
    bool CaptureGesture(HWND owner, int virtualKey = VK_LBUTTON,
                        void* context = nullptr, CancelCallback cancel = nullptr);

    // Popup/menu capture intentionally remains active without a pressed button.
    bool CapturePersistent(HWND owner);

    // Completes a normal interaction without invoking its cancellation hook.
    bool Complete(HWND expectedOwner);
    bool Release(HWND expectedOwner, const wchar_t* reason = L"explicit_release");
    bool ReleaseCurrent(const wchar_t* reason = L"explicit_release");
    bool ReleaseForContext(void* context, const wchar_t* reason = L"context_destroyed");

    // Call from the losing window's WM_CAPTURECHANGED handler.
    void OnCaptureChanged(HWND losingOwner, HWND gainingOwner);

    // UI-thread recovery. Returns true only when a stale gesture was cleared.
    bool RecoverStaleGestureCapture(const wchar_t* reason);
    bool ForceReleaseGestureCapture(const wchar_t* reason);

    Mode CurrentMode();
    HWND CurrentOwner();

    // Kept pure so the recovery policy can be covered by the native harness.
    constexpr bool ShouldRecoverGesture(bool ownsCapture, bool ownerUsable, bool buttonDown)
    {
        return !ownsCapture || !ownerUsable || !buttonDown;
    }
}
