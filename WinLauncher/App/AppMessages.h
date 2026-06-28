#pragma once
#include <Windows.h>

namespace AppMessages
{
    constexpr UINT ShowPopup              = WM_APP + 1;
    constexpr UINT TrayIcon               = WM_APP + 2;
    constexpr UINT ConfigChanged          = WM_APP + 3;
    constexpr UINT ShowConfigWindow       = WM_APP + 4;
    constexpr UINT ShowSettingsWindow     = WM_APP + 5;
    constexpr UINT TogglePopupPause       = WM_APP + 6;   // 暂停/启用弹窗
    constexpr UINT RestartApp             = WM_APP + 7;   // 重启应用
    constexpr UINT RestartHook            = WM_APP + 8;   // 重启钩子
    constexpr UINT KeyboardHookKeyCaptured  = WM_APP + 0x80;
    constexpr UINT KeyboardHookChordComplete = WM_APP + 0x81;
    constexpr UINT DoubleAltPressed       = WM_APP + 0x82; // 双击Alt热键触发
}
