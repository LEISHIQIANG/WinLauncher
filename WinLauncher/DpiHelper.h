#pragma once
#include "Config/UIStyle.h"
#include <Windows.h>

static void SetWindowDisplayAffinitySafe(HWND hwnd)
{
    wchar_t className[128]{};
    if (GetClassNameW(hwnd, className, 128) > 0)
    {
        if (wcscmp(className, L"WinLauncherConfig") == 0 ||
            wcscmp(className, L"WinLauncherBatchDialog") == 0 ||
            wcscmp(className, L"WinLauncherBuiltinIconDialog") == 0 ||
            wcscmp(className, L"WinLauncherCommandDialog") == 0 ||
            wcscmp(className, L"WinLauncherConfirm") == 0 ||
            wcscmp(className, L"WinLauncherContextMenu") == 0 ||
            wcscmp(className, L"WinLauncherDropDown") == 0 ||
            wcscmp(className, L"WinLauncherHotkeyDialog") == 0 ||
            wcscmp(className, L"WinLauncherMacroDialog") == 0 ||
            wcscmp(className, L"WinLauncherPrompt") == 0 ||
            wcscmp(className, L"WinLauncherShortcutDialog") == 0 ||
            wcscmp(className, L"WinLauncherSystemIconDialog") == 0 ||
            wcscmp(className, L"WinLauncherUrlDialog") == 0 ||
            wcscmp(className, L"WinLauncherWait") == 0 ||
            wcscmp(className, L"WinLauncherPopup") == 0)
        {
            return;
        }
    }

    if (!SetWindowDisplayAffinity(hwnd, WDA_MONITOR | 0x10))
    {
        SetWindowDisplayAffinity(hwnd, WDA_MONITOR);
    }
}

class DpiHelper
{
public:
    static float GetSystemDpiScaleForMonitor(HMONITOR hMonitor)
    {
        if (hMonitor)
        {
            auto shcore = GetModuleHandleW(L"shcore.dll");
            if (!shcore) shcore = LoadLibraryW(L"shcore.dll");
            if (shcore)
            {
                auto fn = (HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*))GetProcAddress(shcore, "GetDpiForMonitor");
                if (fn)
                {
                    UINT dpiX = 96, dpiY = 96;
                    if (SUCCEEDED(fn(hMonitor, 0, &dpiX, &dpiY))) // 0 is MDT_EFFECTIVE_DPI
                    {
                        return dpiX / 96.0f;
                    }
                }
            }
        }
        HDC hdc = GetDC(nullptr);
        int dpiX = 96;
        if (hdc)
        {
            dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
            ReleaseDC(nullptr, hdc);
        }
        return dpiX / 96.0f;
    }

    static float GetDpiScaleForMonitor(HMONITOR hMonitor)
    {
        float systemScale = GetSystemDpiScaleForMonitor(hMonitor);
        return UIStyle::Scaling::EffectiveScaleFactor(systemScale);
    }

    static float GetSystemWindowScale(HWND hwnd)
    {
        if (hwnd)
        {
            auto user32 = GetModuleHandleW(L"user32.dll");
            if (user32)
            {
                auto fn = (UINT(WINAPI*)(HWND))GetProcAddress(user32, "GetDpiForWindow");
                if (fn)
                {
                    return fn(hwnd) / 96.0f;
                }
            }
            HMONITOR hm = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            return GetSystemDpiScaleForMonitor(hm);
        }
        return 1.0f;
    }

    static float GetWindowScale(HWND hwnd)
    {
        float systemScale = GetSystemWindowScale(hwnd);
        return UIStyle::Scaling::EffectiveScaleFactor(systemScale);
    }

    static int GetPrimaryDisplayScalePercent()
    {
        POINT origin{ 0, 0 };
        HMONITOR hm = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
        int percent = (int)(GetSystemDpiScaleForMonitor(hm) * 100.0f + 0.5f);
        return UIStyle::Scaling::ClampPercent(percent);
    }

    static float GetDpiScaleForPoint(POINT pt)
    {
        HMONITOR hm = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
        return GetDpiScaleForMonitor(hm);
    }

    // Convert physical pixels to logical DIPs
    static float ToLogical(int physicalVal, float scale)
    {
        return (float)physicalVal / scale;
    }

    static POINT ToLogical(POINT physicalPt, float scale)
    {
        POINT logicalPt;
        logicalPt.x = (int)(physicalPt.x / scale);
        logicalPt.y = (int)(physicalPt.y / scale);
        return logicalPt;
    }

    // Convert logical DIPs to physical pixels
    static int ToPhysical(float logicalVal, float scale)
    {
        return (int)(logicalVal * scale);
    }

    static POINT ToPhysical(POINT logicalPt, float scale)
    {
        POINT physicalPt;
        physicalPt.x = (int)(logicalPt.x * scale);
        physicalPt.y = (int)(logicalPt.y * scale);
        return physicalPt;
    }

    static POINT LogicalClientToScreen(HWND hwnd, POINT logicalPt)
    {
        float scale = GetWindowScale(hwnd);
        POINT physicalPt = ToPhysical(logicalPt, scale);
        ClientToScreen(hwnd, &physicalPt);
        return physicalPt;
    }
};
