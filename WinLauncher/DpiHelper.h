#pragma once
#include <Windows.h>

class DpiHelper
{
public:
    static float GetDpiScaleForMonitor(HMONITOR hMonitor)
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

    static float GetWindowScale(HWND hwnd)
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
            return GetDpiScaleForMonitor(hm);
        }
        return 1.0f;
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
};
