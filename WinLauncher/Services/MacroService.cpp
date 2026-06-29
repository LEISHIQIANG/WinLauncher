#define NOMINMAX
#include "MacroService.h"
#include "../App/Logger.h"
#include <sstream>
#include <iomanip>
#include <algorithm>

#ifndef MOUSEEVENTF_VIRTUALDESKTOP
#define MOUSEEVENTF_VIRTUALDESKTOP 0x4000
#endif

// Static members initialization
std::atomic<HWND> MacroRecorder::s_hNotifyWnd = nullptr;
std::atomic<bool> MacroRecorder::s_recording = false;
std::vector<MacroEvent> MacroRecorder::s_events;
std::mutex MacroRecorder::s_eventsMutex;
HHOOK MacroRecorder::s_hKeyHook = nullptr;
HHOOK MacroRecorder::s_hMouseHook = nullptr;
HANDLE MacroRecorder::s_hThread = nullptr;
std::atomic<uint64_t> MacroRecorder::s_lastTimeUs = 0;
HANDLE MacroRecorder::s_hReadyEvent = nullptr;
std::atomic<bool> MacroRecorder::s_hooksInstalled = false;
std::atomic<bool> MacroRecorder::s_ignoreMouseUntilReleased = false;

std::atomic<bool> MacroPlayer::s_playing = false;
HANDLE MacroPlayer::s_hPlayThread = nullptr;

// Time helper
static uint64_t GetCurrentTimeUs()
{
    LARGE_INTEGER pc, pf;
    QueryPerformanceCounter(&pc);
    QueryPerformanceFrequency(&pf);
    return (pc.QuadPart * 1000000ULL) / pf.QuadPart;
}

// ============================================================================
// MacroHelper Implementation
// ============================================================================

std::wstring MacroHelper::Serialize(double speed, const std::wstring& triggerMode, const std::vector<MacroEvent>& events)
{
    std::wstringstream wss;
    wss << speed << L"|" << triggerMode << L"|";
    for (size_t i = 0; i < events.size(); ++i)
    {
        const auto& ev = events[i];
        wss << ev.type << L","
            << ev.flags << L","
            << ev.delayUs << L","
            << ev.x << L","
            << ev.y << L","
            << ev.data << L","
            << ev.vkCode << L","
            << ev.scanCode;
        if (i + 1 < events.size())
        {
            wss << L";";
        }
    }
    return wss.str();
}

bool MacroHelper::Parse(const std::wstring& arguments, double& speed, std::wstring& triggerMode, std::vector<MacroEvent>& events)
{
    events.clear();
    speed = 1.0;
    triggerMode = L"immediate";

    if (arguments.empty()) return false;

    size_t pipe1 = arguments.find(L'|');
    if (pipe1 == std::wstring::npos) return false;

    std::wstring speedStr = arguments.substr(0, pipe1);
    try { speed = std::stod(speedStr); } catch(...) { speed = 1.0; }

    size_t pipe2 = arguments.find(L'|', pipe1 + 1);
    if (pipe2 == std::wstring::npos) return false;

    triggerMode = arguments.substr(pipe1 + 1, pipe2 - pipe1 - 1);

    std::wstring eventsStr = arguments.substr(pipe2 + 1);
    if (eventsStr.empty()) return true;

    std::wstringstream wss(eventsStr);
    std::wstring eventToken;
    while (std::getline(wss, eventToken, L';'))
    {
        if (eventToken.empty()) continue;
        std::wstringstream evss(eventToken);
        std::wstring field;
        MacroEvent ev;
        int fieldIndex = 0;
        while (std::getline(evss, field, L','))
        {
            try
            {
                switch (fieldIndex)
                {
                case 0: ev.type = std::stoul(field); break;
                case 1: ev.flags = std::stoul(field); break;
                case 2: ev.delayUs = std::stoul(field); break;
                case 3: ev.x = std::stol(field); break;
                case 4: ev.y = std::stol(field); break;
                case 5: ev.data = std::stol(field); break;
                case 6: ev.vkCode = std::stoul(field); break;
                case 7: ev.scanCode = std::stoul(field); break;
                }
            }
            catch(...) {}
            fieldIndex++;
        }
        if (fieldIndex >= 8)
        {
            events.push_back(ev);
        }
    }
    return true;
}

std::wstring MacroHelper::ToScript(const std::vector<MacroEvent>& events)
{
    std::wstringstream wss;
    uint64_t elapsedUs = 0;
    for (const auto& ev : events)
    {
        elapsedUs += ev.delayUs;
        double elapsedS = (double)elapsedUs / 1000000.0;

        std::wstringstream line;

        if (ev.type == 6 || ev.type == 7) // key_down, key_up
        {
            line << (ev.type == 6 ? L"按下 " : L"抬起 ");
            std::wstring keyName = GetKeyName(ev.vkCode);
            if (keyName.empty())
            {
                wchar_t vkBuf[32]{};
                swprintf_s(vkBuf, L"vk_%02x", ev.vkCode);
                keyName = vkBuf;
            }
            line << keyName;
        }
        else if (ev.type == 2 || ev.type == 3) // mouse_down, mouse_up
        {
            line << (ev.type == 2 ? L"按下 " : L"抬起 ");
            std::wstring btnName = L"鼠标未知键";
            if (ev.data == 1) btnName = L"鼠标左键";
            else if (ev.data == 2) btnName = L"鼠标右键";
            else if (ev.data == 4) btnName = L"鼠标中键";
            line << btnName;
            if (ev.flags & 0x0040) // absolute
            {
                line << L" @ " << ev.x << L", " << ev.y;
            }
        }
        else if (ev.type == 4) // wheel
        {
            line << L"滚轮 Delta " << ev.data;
        }
        else if (ev.type == 1) // mouse_move
        {
            line << L"移动鼠标 @ " << ev.x << L", " << ev.y;
        }

        std::wstring desc = line.str();
        if (!desc.empty())
        {
            wchar_t timeBuf[32]{};
            swprintf_s(timeBuf, L"[%07.3fs] ", elapsedS);
            wss << timeBuf << desc << L"\r\n";
        }
    }
    return wss.str();
}

std::vector<MacroEvent> MacroHelper::FromScript(const std::wstring& script)
{
    std::vector<MacroEvent> events;
    std::wstringstream wss(script);
    std::wstring line;
    uint64_t lastTimeUs = 0;

    while (std::getline(wss, line))
    {
        while (!line.empty() && (line.back() == L'\r' || line.back() == L'\n' || line.back() == L' ' || line.back() == L'\t'))
            line.pop_back();
        size_t start = 0;
        while (start < line.size() && (line[start] == L' ' || line[start] == L'\t'))
            start++;
        if (start > 0) line = line.substr(start);
        if (line.empty()) continue;

        size_t openBracket = line.find(L'[');
        size_t closeBracket = line.find(L"s]");
        if (openBracket == std::wstring::npos || closeBracket == std::wstring::npos || closeBracket < openBracket)
            continue;

        std::wstring timeStr = line.substr(openBracket + 1, closeBracket - openBracket - 1);
        double s = 0.0;
        try { s = std::stod(timeStr); } catch(...) { continue; }
        uint64_t timeUs = (uint64_t)(s * 1000000.0);

        std::wstring rest = line.substr(closeBracket + 2);
        while (!rest.empty() && (rest.front() == L' ' || rest.front() == L'\t'))
            rest.erase(0, 1);

        MacroEvent ev;
        ev.delayUs = (uint32_t)(timeUs > lastTimeUs ? timeUs - lastTimeUs : 0);
        lastTimeUs = timeUs;

        if (rest.rfind(L"按下 ", 0) == 0)
        {
            std::wstring target = rest.substr(3);
            if (target == L"鼠标左键" || target.rfind(L"鼠标左键 @", 0) == 0)
            {
                ev.type = 2; // mouse_down
                ev.data = 1;
                ev.flags = 0x0040; // absolute
                ParseCoords(target, ev.x, ev.y);
            }
            else if (target == L"鼠标右键" || target.rfind(L"鼠标右键 @", 0) == 0)
            {
                ev.type = 2;
                ev.data = 2;
                ev.flags = 0x0040;
                ParseCoords(target, ev.x, ev.y);
            }
            else if (target == L"鼠标中键" || target.rfind(L"鼠标中键 @", 0) == 0)
            {
                ev.type = 2;
                ev.data = 4;
                ev.flags = 0x0040;
                ParseCoords(target, ev.x, ev.y);
            }
            else
            {
                ev.type = 6; // key_down
                ev.vkCode = GetVkFromName(target);
                ev.scanCode = MapVirtualKeyW(ev.vkCode, MAPVK_VK_TO_VSC);
            }
            events.push_back(ev);
        }
        else if (rest.rfind(L"抬起 ", 0) == 0)
        {
            std::wstring target = rest.substr(3);
            if (target == L"鼠标左键" || target.rfind(L"鼠标左键 @", 0) == 0)
            {
                ev.type = 3; // mouse_up
                ev.data = 1;
                ev.flags = 0x0040;
                ParseCoords(target, ev.x, ev.y);
            }
            else if (target == L"鼠标右键" || target.rfind(L"鼠标右键 @", 0) == 0)
            {
                ev.type = 3;
                ev.data = 2;
                ev.flags = 0x0040;
                ParseCoords(target, ev.x, ev.y);
            }
            else if (target == L"鼠标中键" || target.rfind(L"鼠标中键 @", 0) == 0)
            {
                ev.type = 3;
                ev.data = 4;
                ev.flags = 0x0040;
                ParseCoords(target, ev.x, ev.y);
            }
            else
            {
                ev.type = 7; // key_up
                ev.vkCode = GetVkFromName(target);
                ev.scanCode = MapVirtualKeyW(ev.vkCode, MAPVK_VK_TO_VSC);
            }
            events.push_back(ev);
        }
        else if (rest.rfind(L"滚轮 Delta ", 0) == 0)
        {
            std::wstring valStr = rest.substr(10);
            ev.type = 4; // wheel
            try { ev.data = std::stoi(valStr); } catch(...) { ev.data = 120; }
            events.push_back(ev);
        }
        else if (rest.rfind(L"移动鼠标 @", 0) == 0)
        {
            ev.type = 1; // mouse_move
            ev.flags = 0x0040;
            ParseCoords(rest, ev.x, ev.y);
            events.push_back(ev);
        }
    }
    return events;
}

void MacroHelper::ParseCoords(const std::wstring& target, int32_t& x, int32_t& y)
{
    x = 0; y = 0;
    size_t at = target.find(L'@');
    if (at == std::wstring::npos) return;
    std::wstring coordPart = target.substr(at + 1);
    size_t comma = coordPart.find(L',');
    if (comma == std::wstring::npos) return;
    try {
        x = std::stoi(coordPart.substr(0, comma));
        y = std::stoi(coordPart.substr(comma + 1));
    } catch(...) {}
}

std::wstring MacroHelper::GetKeyName(uint32_t vk)
{
    if (vk >= 'A' && vk <= 'Z') return std::wstring(1, (wchar_t)vk);
    if (vk >= '0' && vk <= '9') return std::wstring(1, (wchar_t)vk);
    if (vk >= VK_F1 && vk <= VK_F24) return L"F" + std::to_wstring(vk - VK_F1 + 1);

    switch (vk)
    {
    case VK_SPACE:    return L"Space";
    case VK_RETURN:   return L"Enter";
    case VK_TAB:      return L"Tab";
    case VK_ESCAPE:   return L"Esc";
    case VK_BACK:     return L"Backspace";
    case VK_INSERT:   return L"Insert";
    case VK_DELETE:   return L"Delete";
    case VK_HOME:     return L"Home";
    case VK_END:      return L"End";
    case VK_PRIOR:    return L"PageUp";
    case VK_NEXT:     return L"PageDown";
    case VK_UP:       return L"Up";
    case VK_DOWN:     return L"Down";
    case VK_LEFT:     return L"Left";
    case VK_RIGHT:    return L"Right";
    case VK_CONTROL:  return L"Ctrl";
    case VK_MENU:     return L"Alt";
    case VK_SHIFT:    return L"Shift";
    case VK_LWIN:     return L"Win";
    case VK_RWIN:     return L"Win";
    }
    return L"";
}

uint32_t MacroHelper::GetVkFromName(const std::wstring& name)
{
    if (name.size() == 1)
    {
        wchar_t ch = name[0];
        if (ch >= L'a' && ch <= L'z') return 'A' + (ch - L'a');
        if (ch >= L'A' && ch <= L'Z') return ch;
        if (ch >= L'0' && ch <= L'9') return ch;
    }

    if (name.rfind(L"F", 0) == 0 && name.size() > 1)
    {
        try {
            int num = std::stoi(name.substr(1));
            if (num >= 1 && num <= 24)
                return VK_F1 + (num - 1);
        } catch(...) {}
    }

    if (name == L"Space") return VK_SPACE;
    if (name == L"Enter") return VK_RETURN;
    if (name == L"Tab") return VK_TAB;
    if (name == L"Esc") return VK_ESCAPE;
    if (name == L"Backspace") return VK_BACK;
    if (name == L"Insert") return VK_INSERT;
    if (name == L"Delete") return VK_DELETE;
    if (name == L"Home") return VK_HOME;
    if (name == L"End") return VK_END;
    if (name == L"PageUp") return VK_PRIOR;
    if (name == L"PageDown") return VK_NEXT;
    if (name == L"Up") return VK_UP;
    if (name == L"Down") return VK_DOWN;
    if (name == L"Left") return VK_LEFT;
    if (name == L"Right") return VK_RIGHT;
    if (name == L"Ctrl") return VK_CONTROL;
    if (name == L"Alt") return VK_MENU;
    if (name == L"Shift") return VK_SHIFT;
    if (name == L"Win") return VK_LWIN;

    if (name.rfind(L"vk_", 0) == 0 && name.size() == 5)
    {
        try {
            return std::stoul(name.substr(3), nullptr, 16);
        } catch(...) {}
    }

    return 0;
}

// ============================================================================
// MacroRecorder Implementation
// ============================================================================

bool MacroRecorder::Start(HWND hNotifyWnd)
{
    if (s_recording.load()) return true;

    s_hNotifyWnd = hNotifyWnd;
    s_hooksInstalled.store(false);
    s_hReadyEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!s_hReadyEvent) return false;

    Clear();
    s_recording.store(true);
    s_lastTimeUs.store(GetCurrentTimeUs());
    bool mouseDown =
        ((GetAsyncKeyState(VK_LBUTTON) | GetAsyncKeyState(VK_RBUTTON) | GetAsyncKeyState(VK_MBUTTON)) & 0x8000) != 0;
    s_ignoreMouseUntilReleased.store(mouseDown);

    s_hThread = CreateThread(nullptr, 0, ThreadProc, nullptr, 0, nullptr);
    if (!s_hThread)
    {
        s_recording.store(false);
        CloseHandle(s_hReadyEvent);
        s_hReadyEvent = nullptr;
        return false;
    }

    WaitForSingleObject(s_hReadyEvent, 2000);
    CloseHandle(s_hReadyEvent);
    s_hReadyEvent = nullptr;

    if (!s_hooksInstalled.load())
    {
        s_recording.store(false);
        if (s_hThread)
        {
            PostThreadMessageW(GetThreadId(s_hThread), WM_QUIT, 0, 0);
            WaitForSingleObject(s_hThread, 2000);
            CloseHandle(s_hThread);
            s_hThread = nullptr;
        }
        LOG_G_ERRA(L"MacroRecorder::Start: failed to install low-level input hooks");
        return false;
    }
    return true;
}

void MacroRecorder::Stop(bool discardTrailingMouseClick)
{
    if (!s_recording.load()) return;
    if (discardTrailingMouseClick)
    {
        DiscardTrailingMouseClick();
    }
    s_recording.store(false);
    s_hooksInstalled.store(false);

    if (s_hThread)
    {
        PostThreadMessageW(GetThreadId(s_hThread), WM_QUIT, 0, 0);
        if (WaitForSingleObject(s_hThread, 2000) == WAIT_TIMEOUT)
        {
            TerminateThread(s_hThread, 0);
        }
        CloseHandle(s_hThread);
        s_hThread = nullptr;
    }
}

void MacroRecorder::Clear()
{
    std::lock_guard<std::mutex> lock(s_eventsMutex);
    s_events.clear();
}

bool MacroRecorder::IsRecording()
{
    return s_recording.load();
}

std::vector<MacroEvent> MacroRecorder::GetEvents()
{
    std::lock_guard<std::mutex> lock(s_eventsMutex);
    return s_events;
}

void MacroRecorder::AddEvent(const MacroEvent& ev)
{
    {
        std::lock_guard<std::mutex> lock(s_eventsMutex);
        s_events.push_back(ev);
    }
    HWND hWnd = s_hNotifyWnd.load();
    if (hWnd)
    {
        PostMessageW(hWnd, AppMessages::MacroRecordingUpdated, 0, 0);
    }
}

void MacroRecorder::DiscardTrailingMouseClick()
{
    std::lock_guard<std::mutex> lock(s_eventsMutex);
    for (size_t i = s_events.size(); i > 0; --i)
    {
        const MacroEvent& ev = s_events[i - 1];
        if (ev.type == 2 && (ev.data == 1 || ev.data == 2 || ev.data == 4))
        {
            s_events.erase(s_events.begin() + static_cast<std::ptrdiff_t>(i - 1), s_events.end());
            return;
        }
    }
}

DWORD WINAPI MacroRecorder::ThreadProc(LPVOID)
{
    HMODULE hModule = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&LowLevelKeyboardProc), &hModule);

    s_hKeyHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, hModule, 0);
    s_hMouseHook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, hModule, 0);

    bool hooksOk = (s_hKeyHook != nullptr && s_hMouseHook != nullptr);
    s_hooksInstalled.store(hooksOk);

    if (s_hReadyEvent) SetEvent(s_hReadyEvent);

    if (!hooksOk)
    {
        DWORD err = GetLastError();
        LOG_G_ERRA(L"MacroRecorder::ThreadProc: SetWindowsHookExW failed, error=%lu, keyHook=%p, mouseHook=%p",
                   err, s_hKeyHook, s_hMouseHook);
        if (s_hKeyHook) { UnhookWindowsHookEx(s_hKeyHook); s_hKeyHook = nullptr; }
        if (s_hMouseHook) { UnhookWindowsHookEx(s_hMouseHook); s_hMouseHook = nullptr; }
        s_recording.store(false);
        return 1;
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (s_hKeyHook) { UnhookWindowsHookEx(s_hKeyHook); s_hKeyHook = nullptr; }
    if (s_hMouseHook) { UnhookWindowsHookEx(s_hMouseHook); s_hMouseHook = nullptr; }
    s_hooksInstalled.store(false);

    return 0;
}

LRESULT CALLBACK MacroRecorder::LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION && s_recording.load())
    {
        KBDLLHOOKSTRUCT* pKbd = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        if (pKbd)
        {
            bool isKeyDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
            bool isKeyUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);
            if (!isKeyDown && !isKeyUp)
                return CallNextHookEx(nullptr, nCode, wParam, lParam);

            // F8 key stops recording
            if (isKeyDown && pKbd->vkCode == VK_F8)
            {
                s_recording.store(false);
                s_hooksInstalled.store(false);
                if (s_hNotifyWnd)
                {
                    PostMessageW(s_hNotifyWnd, AppMessages::MacroRecordingStopped, 0, 0);
                }
                PostThreadMessageW(GetCurrentThreadId(), WM_QUIT, 0, 0);
                return 1;
            }

            MacroEvent ev;
            ev.type = isKeyUp ? 7 : 6;
            ev.vkCode = pKbd->vkCode;
            ev.scanCode = pKbd->scanCode;
            ev.flags = pKbd->flags;
            
            uint64_t now = GetCurrentTimeUs();
            uint64_t last = s_lastTimeUs.load();
            ev.delayUs = (uint32_t)(now > last ? now - last : 0);
            s_lastTimeUs.store(now);

            AddEvent(ev);
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

LRESULT CALLBACK MacroRecorder::LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION && s_recording.load())
    {
        MSLLHOOKSTRUCT* pMouse = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
        if (pMouse)
        {
            if (s_ignoreMouseUntilReleased.load())
            {
                bool anyMouseDown =
                    ((GetAsyncKeyState(VK_LBUTTON) | GetAsyncKeyState(VK_RBUTTON) | GetAsyncKeyState(VK_MBUTTON)) & 0x8000) != 0;
                if (!anyMouseDown)
                {
                    s_ignoreMouseUntilReleased.store(false);
                }
                return CallNextHookEx(nullptr, nCode, wParam, lParam);
            }

            MacroEvent ev;
            ev.x = pMouse->pt.x;
            ev.y = pMouse->pt.y;
            ev.flags = 0x0040; // absolute coordinates
            
            bool record = false;
            if (wParam == WM_MOUSEMOVE)
            {
                bool dragging =
                    ((GetAsyncKeyState(VK_LBUTTON) | GetAsyncKeyState(VK_RBUTTON) | GetAsyncKeyState(VK_MBUTTON)) & 0x8000) != 0;
                if (!dragging)
                {
                    return CallNextHookEx(nullptr, nCode, wParam, lParam);
                }

                ev.type = 1; // mouse_move
                // Coalesce mouse moves - only record if moved enough or delay elapsed
                static int32_t lastX = -9999, lastY = -9999;
                if (abs(ev.x - lastX) > 8 || abs(ev.y - lastY) > 8)
                {
                    lastX = ev.x; lastY = ev.y;
                    record = true;
                }
            }
            else if (wParam == WM_LBUTTONDOWN) { ev.type = 2; ev.data = 1; record = true; }
            else if (wParam == WM_LBUTTONUP)   { ev.type = 3; ev.data = 1; record = true; }
            else if (wParam == WM_RBUTTONDOWN) { ev.type = 2; ev.data = 2; record = true; }
            else if (wParam == WM_RBUTTONUP)   { ev.type = 3; ev.data = 2; record = true; }
            else if (wParam == WM_MBUTTONDOWN) { ev.type = 2; ev.data = 4; record = true; }
            else if (wParam == WM_MBUTTONUP)   { ev.type = 3; ev.data = 4; record = true; }
            else if (wParam == WM_MOUSEWHEEL)
            {
                ev.type = 4;
                ev.data = (int16_t)HIWORD(pMouse->mouseData);
                record = true;
            }

            if (record)
            {
                uint64_t now = GetCurrentTimeUs();
                uint64_t last = s_lastTimeUs.load();
                ev.delayUs = (uint32_t)(now > last ? now - last : 0);
                s_lastTimeUs.store(now);
                AddEvent(ev);
            }
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

// ============================================================================
// MacroPlayer Implementation
// ============================================================================

bool MacroPlayer::Play(const std::vector<MacroEvent>& events, double speed, const std::wstring& triggerMode, HWND hParentWnd, HWND hRestoreForegroundWnd)
{
    if (s_playing.load()) return false;

    s_playing.store(true);
    
    PlayParams* params = new PlayParams();
    params->events = events;
    params->speed = speed;
    params->triggerMode = triggerMode;
    params->hParentWnd = hParentWnd;
    params->hRestoreForegroundWnd = hRestoreForegroundWnd;

    s_hPlayThread = CreateThread(nullptr, 0, PlayThreadProc, params, 0, nullptr);
    if (!s_hPlayThread)
    {
        s_playing.store(false);
        delete params;
        return false;
    }
    return true;
}

void MacroPlayer::Cancel()
{
    s_playing.store(false);
    if (s_hPlayThread)
    {
        WaitForSingleObject(s_hPlayThread, 2000);
        CloseHandle(s_hPlayThread);
        s_hPlayThread = nullptr;
    }
}

bool MacroPlayer::IsPlaying()
{
    return s_playing.load();
}

void MacroPlayer::MicroSleep(uint64_t microseconds)
{
    if (microseconds == 0) return;
    LARGE_INTEGER pc, pf;
    QueryPerformanceFrequency(&pf);
    double ticksPerMicro = (double)pf.QuadPart / 1000000.0;
    QueryPerformanceCounter(&pc);
    int64_t targetTick = pc.QuadPart + (int64_t)(microseconds * ticksPerMicro);

    if (microseconds > 2000)
    {
        Sleep((DWORD)(microseconds / 1000 - 1));
    }

    do
    {
        if (!s_playing.load()) return;
        QueryPerformanceCounter(&pc);
    } while (pc.QuadPart < targetTick);
}

DWORD WINAPI MacroPlayer::PlayThreadProc(LPVOID lpParam)
{
    PlayParams* params = reinterpret_cast<PlayParams*>(lpParam);
    if (!params) return 1;

    if (params->triggerMode == L"after_close")
    {
        const ULONGLONG deadline = GetTickCount64() + 1200;
        while (params->hParentWnd && IsWindow(params->hParentWnd) && IsWindowVisible(params->hParentWnd) && GetTickCount64() < deadline)
        {
            Sleep(15);
        }

        if (params->hRestoreForegroundWnd && IsWindow(params->hRestoreForegroundWnd))
        {
            ShowWindow(params->hRestoreForegroundWnd, IsIconic(params->hRestoreForegroundWnd) ? SW_RESTORE : SW_SHOW);
            SetForegroundWindow(params->hRestoreForegroundWnd);
            SetFocus(params->hRestoreForegroundWnd);
        }

        Sleep(120);
    }

    double speedMultiplier = params->speed;
    if (speedMultiplier <= 0.0) speedMultiplier = 1.0;

    int screenW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int screenH = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    int screenX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int screenY = GetSystemMetrics(SM_YVIRTUALSCREEN);

    for (const auto& ev : params->events)
    {
        if (!s_playing.load()) break;

        uint64_t scaledDelay = (uint64_t)(ev.delayUs / speedMultiplier);
        MicroSleep(scaledDelay);

        if (!s_playing.load()) break;

        INPUT input{};
        if (ev.type == 6 || ev.type == 7) // key_down, key_up
        {
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = (WORD)ev.vkCode;
            input.ki.wScan = (WORD)ev.scanCode;
            input.ki.dwFlags = 0;
            if (ev.type == 7)
                input.ki.dwFlags |= KEYEVENTF_KEYUP;
            if (ev.flags & LLKHF_EXTENDED)
                input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
            if (ev.scanCode != 0)
                input.ki.dwFlags |= KEYEVENTF_SCANCODE;
        }
        else if (ev.type == 1 || ev.type == 2 || ev.type == 3 || ev.type == 4) // mouse events
        {
            input.type = INPUT_MOUSE;
            input.mi.dx = (long)((ev.x - screenX) * 65535LL / (screenW > 0 ? screenW : 1));
            input.mi.dy = (long)((ev.y - screenY) * 65535LL / (screenH > 0 ? screenH : 1));
            input.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESKTOP;

            if (ev.type == 1) // mouse_move
            {
                input.mi.dwFlags |= MOUSEEVENTF_MOVE;
            }
            else if (ev.type == 2) // mouse_down
            {
                if (ev.data == 1) input.mi.dwFlags |= MOUSEEVENTF_LEFTDOWN;
                else if (ev.data == 2) input.mi.dwFlags |= MOUSEEVENTF_RIGHTDOWN;
                else if (ev.data == 4) input.mi.dwFlags |= MOUSEEVENTF_MIDDLEDOWN;
            }
            else if (ev.type == 3) // mouse_up
            {
                if (ev.data == 1) input.mi.dwFlags |= MOUSEEVENTF_LEFTUP;
                else if (ev.data == 2) input.mi.dwFlags |= MOUSEEVENTF_RIGHTUP;
                else if (ev.data == 4) input.mi.dwFlags |= MOUSEEVENTF_MIDDLEUP;
            }
            else if (ev.type == 4) // wheel
            {
                input.mi.dwFlags |= MOUSEEVENTF_WHEEL;
                input.mi.mouseData = ev.data;
            }
        }

        SendInput(1, &input, sizeof(INPUT));
    }

    s_playing.store(false);
    delete params;
    return 0;
}
