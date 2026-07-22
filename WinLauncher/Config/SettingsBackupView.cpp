#include "SettingsBackupView.h"
#include "IConfigWindow.h"

void SettingsBackupView::RenderBackupSection(
    ID2D1HwndRenderTarget* rt,
    IConfigWindow* owner,
    IDWriteTextFormat* titleFormat,
    IDWriteTextFormat* descFormat,
    ID2D1SolidColorBrush* textBrush,
    ID2D1SolidColorBrush* mutedBrush)
{
    if (!rt || !owner) return;
    // Backup and sync view D2D helper render logic
}
