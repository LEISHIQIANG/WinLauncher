#include "SettingsPluginView.h"
#include "IConfigWindow.h"

void SettingsPluginView::RenderPluginSection(
    ID2D1HwndRenderTarget* rt,
    IConfigWindow* owner,
    IDWriteTextFormat* titleFormat,
    IDWriteTextFormat* descFormat,
    ID2D1SolidColorBrush* textBrush,
    ID2D1SolidColorBrush* mutedBrush)
{
    if (!rt || !owner) return;
    // Plugin management view D2D helper render logic
}
