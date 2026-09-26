#pragma once

// Monotonic QPC clock in seconds shared by the popup window's orchestration,
// input and render translation units (perf logging, validity windows,
// animation timestamps).
namespace PopupClock
{
    inline double NowSeconds()
    {
        static double freq = 0.0;
        if (freq == 0.0)
        {
            LARGE_INTEGER li;
            QueryPerformanceFrequency(&li);
            freq = (double)li.QuadPart;
        }
        LARGE_INTEGER li;
        QueryPerformanceCounter(&li);
        return (double)li.QuadPart / freq;
    }
}
