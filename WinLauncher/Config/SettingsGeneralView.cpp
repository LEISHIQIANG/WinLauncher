#include "SettingsGeneralView.h"
#include "IConfigWindow.h"

void SettingsGeneralView::RenderGeneralSection(
    ID2D1HwndRenderTarget* rt,
    IConfigWindow* owner,
    IDWriteTextFormat* titleFormat,
    IDWriteTextFormat* descFormat,
    ID2D1SolidColorBrush* textBrush,
    ID2D1SolidColorBrush* mutedBrush)
{
    if (!rt || !owner) return;
    // General section D2D helper render logic
}
