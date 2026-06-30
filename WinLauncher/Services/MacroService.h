#pragma once
#include <Windows.h>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include "../App/AppMessages.h"

struct MacroEvent
{
    uint32_t type = 0;      // 1=mouse_move, 2=mouse_down, 3=mouse_up, 4=wheel, 6=key_down, 7=key_up
    uint32_t flags = 0;     // extended, absolute, etc.
    uint32_t delayUs = 0;   // delay since last event
    int32_t x = 0;
    int32_t y = 0;
    int32_t data = 0;       // wheel delta or button code (1=L, 2=R, 4=M)
    uint32_t vkCode = 0;
    uint32_t scanCode = 0;
};

class MacroHelper
{
public:
    static std::wstring Serialize(double speed, const std::wstring& triggerMode, const std::vector<MacroEvent>& events);
    static bool Parse(const std::wstring& arguments, double& speed, std::wstring& triggerMode, std::vector<MacroEvent>& events);
    static std::wstring ToScript(const std::vector<MacroEvent>& events);
    static std::vector<MacroEvent> FromScript(const std::wstring& script);
    
private:
    static std::wstring GetKeyName(uint32_t vk);
    static uint32_t GetVkFromName(const std::wstring& name);
    static void ParseCoords(const std::wstring& target, int32_t& x, int32_t& y);
};

class MacroRecorder
{
public:
    static bool Start(HWND hNotifyWnd);
    static void Stop(bool discardTrailingMouseClick = false);
    static void Clear();
    static bool IsRecording();
    static std::vector<MacroEvent> GetEvents();
    static void AddEvent(const MacroEvent& ev);

private:
    static DWORD WINAPI ThreadProc(LPVOID lpParam);
    static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam);
    static void DiscardTrailingMouseClick();

    static std::atomic<HWND> s_hNotifyWnd;
    static std::atomic<bool> s_recording;
    static std::vector<MacroEvent> s_events;
    static std::mutex s_eventsMutex;
    static HHOOK s_hKeyHook;
    static HHOOK s_hMouseHook;
    static HANDLE s_hThread;
    static std::atomic<DWORD> s_hookThreadId;
    static std::atomic<uint64_t> s_lastTimeUs;
    static std::atomic<bool> s_hooksInstalled;
    static std::atomic<bool> s_ignoreMouseUntilReleased;
    static std::atomic<int32_t> s_lastMouseMoveX;
    static std::atomic<int32_t> s_lastMouseMoveY;
};

class MacroPlayer
{
public:
    static bool Play(const std::vector<MacroEvent>& events, double speed, const std::wstring& triggerMode, HWND hParentWnd, HWND hRestoreForegroundWnd = nullptr);
    static void Cancel();
    static bool IsPlaying();

private:
    static DWORD WINAPI PlayThreadProc(LPVOID lpParam);
    static void MicroSleep(uint64_t microseconds);

    struct PlayParams
    {
        std::vector<MacroEvent> events;
        double speed;
        std::wstring triggerMode;
        HWND hParentWnd;
        HWND hRestoreForegroundWnd;
    };

    static std::atomic<bool> s_playing;
    static HANDLE s_hPlayThread;
    static std::mutex s_playMutex;
};
