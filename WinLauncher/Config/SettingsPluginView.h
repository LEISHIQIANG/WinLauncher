#pragma once
#include <d2d1.h>
#include <dwrite.h>

class IConfigWindow;

struct SettingsPluginHoverState
{
    bool install = false;
    bool openDir = false;
    bool refresh = false;
    int configure = -1;
    int toggle = -1;
    int uninstall = -1;
};

class SettingsPluginView
{
public:
    static void RenderPluginSection(
        ID2D1HwndRenderTarget* rt,
        IConfigWindow* owner,
        IDWriteTextFormat* tfDefault,
        ID2D1SolidColorBrush* tbNormal,
        ID2D1SolidColorBrush* tbMuted,
        D2D1_COLOR_F baseClr,
        const SettingsPluginHoverState& hover);
};
