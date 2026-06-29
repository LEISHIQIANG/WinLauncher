#pragma once

namespace Model
{
    struct ThemeEffectConfig
    {
        float hue;
        float blur;
        float opacity;
        float highlight;
        float brightness;
        float saturation;
    };

    struct AppearanceSettings
    {
        ThemeEffectConfig dark = { 32.0f, 20.0f, 0.50f, 0.90f, 0.11f, 2.5f };
        ThemeEffectConfig light = { 36.0f, 20.0f, 0.30f, 0.90f, 0.90f, 2.5f };
        ThemeEffectConfig acrylicDark = { 30.0f, 20.0f, 0.36f, 0.90f, 0.11f, 2.5f };
        ThemeEffectConfig acrylicLight = { 34.0f, 20.0f, 0.60f, 0.90f, 0.98f, 2.5f };
    };
}
