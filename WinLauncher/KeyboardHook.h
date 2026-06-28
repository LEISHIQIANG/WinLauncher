#pragma once
/**
 * KeyboardHook.h
 *
 * Low-level keyboard hook for WinLauncher.
 *
 * Provides two independent use-cases sharing the same WH_KEYBOARD_LL hook:
 *   1. Hotkey-recording mode  – captures the next key-chord and reports it
 *      through a callback.  Automatically stops when the chord is fully
 *      released (all keys up), or after a configurable timeout.
 *   2. Global hotkey trigger  – can be extended later to trigger shortcuts.
 *
 * Architecture mirrors MouseHook.cpp:
 *   - A dedicated background thread owns the hook and runs a Win32 message loop.
 *   - The hook callback collects press/release events and dispatches results
 *     via PostMessageW to the supplied HWND so processing happens on the UI
 *     thread (thread-safe by design – no mutex needed for the UI path).
 */

#include <windows.h>
#include <atomic>
#include <functional>
#include <string>
#include "App/AppMessages.h"

// Custom window messages posted to the owner HWND during recording.
// wParam = VK code of the main key (0 = chord-release notification).
// lParam = packed modifier flags (see RecordModifiers below).
namespace KeyboardHookMsg
{
    // Posted when a non-modifier key is pressed during recording.
    constexpr UINT KeyCaptured = AppMessages::KeyboardHookKeyCaptured;
    // Posted when all keys in the chord are released (recording complete).
    constexpr UINT ChordComplete = AppMessages::KeyboardHookChordComplete;
}

// Modifier bitmask packed into lParam of KeyboardHookMsg messages.
namespace RecordModifiers
{
    constexpr DWORD Ctrl  = 0x01;
    constexpr DWORD Alt   = 0x02;
    constexpr DWORD Shift = 0x04;
    constexpr DWORD Win   = 0x08;
}

class KeyboardHook
{
public:
    // -----------------------------------------------------------------------
    // Lifecycle – call Install once on startup, Uninstall on exit.
    // -----------------------------------------------------------------------
    static bool Install();
    static void Uninstall();
    static bool IsInstalled();

    // -----------------------------------------------------------------------
    // Recording API
    // -----------------------------------------------------------------------

    /**
     * Start recording the next key-chord.
     *
     * @param hTargetWnd  Window that will receive KeyboardHookMsg::KeyCaptured
     *                    and KeyboardHookMsg::ChordComplete messages.
     * @param timeoutMs   Milliseconds before recording auto-stops (0 = no timeout).
     *
     * While recording is active, every non-modifier key-down event posts
     *   KeyCaptured
     *     wParam = VK code
     *     lParam = modifier flags (Ctrl | Alt | Shift | Win bitmask)
     *
     * When all captured keys are released, or on timeout:
     *   ChordComplete
     *     wParam = 0
     *     lParam = accumulated modifier flags
     */
    static bool StartRecording(HWND hTargetWnd, DWORD timeoutMs = 10000);

    /**
     * Stop recording immediately (no ChordComplete message is sent).
     */
    static void StopRecording();

    static bool IsRecording();

    // -----------------------------------------------------------------------
    // Double-Alt hotkey API
    // Detects two rapid Alt key taps (both pressed+released within doubleClickMs).
    // Posts AppMessages::DoubleAltPressed to hTargetWnd when triggered.
    // -----------------------------------------------------------------------
    static void SetDoubleAltTarget(HWND hTargetWnd, DWORD doubleClickMs = 400);
    static void ClearDoubleAltTarget();

private:
    static DWORD WINAPI ThreadProc(LPVOID);
    static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
    static void CALLBACK TimerCallback(HWND, UINT, UINT_PTR, DWORD);

    static std::atomic<HHOOK>  s_hHook;
    static std::atomic<HANDLE> s_hThread;
    static HANDLE              s_hReadyEvent;
    static std::atomic<bool>   s_running;
    static HMODULE             s_hModule;

    // Recording state (only touched from the hook thread or under the lock)
    static std::atomic<bool>   s_recording;
    static std::atomic<HWND>   s_hRecordWnd;
    static std::atomic<DWORD>  s_timeoutMs;
    static UINT_PTR            s_timerId;          // hook-thread-local timer ID

    // Key state tracking for chord detection (hook thread only)
    static DWORD               s_recordModifiers;  // accumulated mods
    static bool                s_hadNonModifier;   // saw at least one non-mod key
    static int                 s_pressedCount;     // # of captured keys currently held

    // Double-Alt detection state (hook thread only)
    static std::atomic<HWND>   s_hDoubleAltWnd;    // target window (nullptr = disabled)
    static std::atomic<DWORD>  s_doubleAltMs;      // max interval in ms
    static ULONGLONG           s_lastAltUpTime;    // tick of last Alt key-up
    static bool                s_altDown;          // is Alt currently pressed
};
