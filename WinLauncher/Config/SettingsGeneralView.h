#pragma once
#include <d2d1.h>
#include <dwrite.h>
#include <wrl.h>

class IConfigWindow;

class SettingsGeneralView
{
public:
    static void RenderGeneralSection(
        ID2D1HwndRenderTarget* rt,
        IConfigWindow* owner,
        IDWriteTextFormat* titleFormat,
        IDWriteTextFormat* descFormat,
        ID2D1SolidColorBrush* textBrush,
        ID2D1SolidColorBrush* mutedBrush);
};
