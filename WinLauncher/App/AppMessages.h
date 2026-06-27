#pragma once
#include <Windows.h>

namespace AppMessages
{
    constexpr UINT ShowPopup = WM_APP + 1;
    constexpr UINT TrayIcon = WM_APP + 2;
    constexpr UINT ConfigChanged = WM_APP + 3;
    constexpr UINT ShowConfigWindow = WM_APP + 4;
    constexpr UINT ShowSettingsWindow = WM_APP + 5;
}
