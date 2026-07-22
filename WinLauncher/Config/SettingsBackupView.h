#pragma once
#include <d2d1.h>
#include <dwrite.h>
#include <wrl.h>

class IConfigWindow;

class SettingsBackupView
{
public:
    static void RenderBackupSection(
        ID2D1HwndRenderTarget* rt,
        IConfigWindow* owner,
        IDWriteTextFormat* titleFormat,
        IDWriteTextFormat* descFormat,
        ID2D1SolidColorBrush* textBrush,
        ID2D1SolidColorBrush* mutedBrush);
};
