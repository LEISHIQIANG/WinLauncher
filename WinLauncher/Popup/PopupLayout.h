#pragma once

#include <Windows.h>

// Pure geometry shared by painting and input routing. It deliberately knows
// nothing about HWND, D2D or shortcut data so DPI/layout behavior is testable.
namespace PopupLayout
{
    struct GridMetrics
    {
        int columns = 1;
        int rows = 1;
        int cellWidth = 1;
        int cellHeight = 1;
        int padding = 0;
        int gap = 0;
        int headerHeight = 0;
    };

    int HitTestGrid(const GridMetrics& metrics, int itemCount, POINT point, int top = 0);
    int DockTop(const GridMetrics& metrics, int dockRows);

    // Raw layout inputs gathered from configuration getters by the window.
    struct WindowMetricsInputs
    {
        int columns = 1;
        int rows = 1;
        int dockRows = 1;
        int cellWidth = 1;
        int cellHeight = 1;
        int wndPadding = 0;
        int iconGap = 0;
        int topBarHeight = 0;
    };

    // Window size and dock band positions in DIPs, all derived from the same
    // formula so sizing, DPI changes and painting cannot drift apart.
    struct WindowMetrics
    {
        int width = 0;     // capped at 900
        int height = 0;    // capped at 900
        int lineY = 0;     // dock separator line y
        int dockTopY = 0;  // dock grid top y
    };

    WindowMetrics ComputeWindowMetrics(const WindowMetricsInputs& inputs);
}
