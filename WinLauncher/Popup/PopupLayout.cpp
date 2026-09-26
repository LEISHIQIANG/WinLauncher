#include "PopupLayout.h"
#include <algorithm>

int PopupLayout::HitTestGrid(const GridMetrics& metrics, int itemCount, POINT point, int top)
{
    const int columns = (std::max)(1, metrics.columns);
    const int capacity = columns * (std::max)(0, metrics.rows);
    const int count = (std::min)((std::max)(0, itemCount), capacity);
    for (int index = 0; index < count; ++index)
    {
        const int column = index % columns;
        const int row = index / columns;
        const RECT rect{
            metrics.padding + column * metrics.cellWidth,
            top + row * metrics.cellHeight,
            metrics.padding + column * metrics.cellWidth + metrics.cellWidth - metrics.gap,
            top + row * metrics.cellHeight + metrics.cellHeight - metrics.gap
        };
        if (PtInRect(&rect, point)) return index;
    }
    return -1;
}

int PopupLayout::DockTop(const GridMetrics& metrics, int)
{
    const int mainGridBottom = metrics.padding + metrics.rows * metrics.cellHeight - metrics.gap + metrics.headerHeight;
    return mainGridBottom + metrics.padding * 2;
}

PopupLayout::WindowMetrics PopupLayout::ComputeWindowMetrics(const WindowMetricsInputs& in)
{
    WindowMetrics out;
    out.width = in.columns * in.cellWidth + in.wndPadding * 2 - in.iconGap;
    const int mainGridCardBottom = in.wndPadding + in.rows * in.cellHeight - in.iconGap + in.topBarHeight;
    out.lineY = mainGridCardBottom + in.wndPadding;
    out.dockTopY = out.lineY + in.wndPadding;
    out.height = out.dockTopY + in.dockRows * in.cellHeight - in.iconGap + in.wndPadding;
    if (out.width > 900) out.width = 900;
    if (out.height > 900) out.height = 900;
    return out;
}
