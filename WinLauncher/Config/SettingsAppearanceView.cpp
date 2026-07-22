#include "SettingsAppearanceView.h"
#include "IConfigWindow.h"

void SettingsAppearanceView::RenderAppearanceSection(
    ID2D1HwndRenderTarget* rt,
    IConfigWindow* owner,
    IDWriteTextFormat* titleFormat,
    IDWriteTextFormat* descFormat,
    ID2D1SolidColorBrush* textBrush,
    ID2D1SolidColorBrush* mutedBrush)
{
    if (!rt || !owner) return;
    // Appearance and theme view D2D helper render logic
}
