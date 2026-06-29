#pragma once

#include "Config/TextBox.h"
#include <windows.h>
#include <cwchar>

namespace InputFocusGuard
{
    inline bool IsLikelyNativeTextInput(HWND hWnd)
    {
        if (!hWnd) return false;

        wchar_t className[128]{};
        if (GetClassNameW(hWnd, className, (int)(sizeof(className) / sizeof(className[0]))) <= 0)
            return false;

        return lstrcmpiW(className, L"Edit") == 0 ||
               lstrcmpiW(className, L"Scintilla") == 0 ||
               _wcsnicmp(className, L"RichEdit", 8) == 0 ||
               _wcsnicmp(className, L"RICHEDIT", 8) == 0;
    }

    inline bool IsTextInputContextActive()
    {
        HWND foreground = GetForegroundWindow();
        if (!foreground) return false;

        DWORD foregroundPid = 0;
        DWORD foregroundTid = GetWindowThreadProcessId(foreground, &foregroundPid);
        if (foregroundPid == GetCurrentProcessId() && TextBox::IsAnyTextBoxFocused())
            return true;

        GUITHREADINFO gui{};
        gui.cbSize = sizeof(gui);
        if (foregroundTid != 0 && GetGUIThreadInfo(foregroundTid, &gui))
        {
            if (gui.hwndCaret)
                return true;

            if (IsLikelyNativeTextInput(gui.hwndFocus))
                return true;
        }

        HWND focus = GetFocus();
        return IsLikelyNativeTextInput(focus);
    }
}
