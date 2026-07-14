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
}
