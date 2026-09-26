#pragma once
#include <d2d1.h>
#include <dwrite.h>

class SettingsAboutView
{
public:
    static void RenderAboutSection(
        ID2D1HwndRenderTarget* rt,
        IDWriteTextFormat* tfDefault,
        IDWriteTextFormat* tfTitle,
        ID2D1SolidColorBrush* tbNormal,
        ID2D1SolidColorBrush* tbMuted,
        D2D1_COLOR_F baseClr,
        bool hoveredOpenSourceUrl);
};
