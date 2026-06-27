#pragma once
#include "../Model/AppearanceSettings.h"
#include <windows.h>
#include <d2d1.h>
#include <string>

namespace UIStyle
{
    // Color representation with matching D2D and GDI formats
    struct Color
    {
        D2D1_COLOR_F d2d;
        COLORREF gdi;

        Color() : d2d(D2D1::ColorF(0, 0, 0, 0)), gdi(RGB(0, 0, 0)) {}
        Color(float r, float g, float b, float a = 1.0f)
        {
            d2d = D2D1::ColorF(r, g, b, a);
            // Add 0.5f before casting to BYTE to round correctly rather than truncate
            gdi = RGB((BYTE)(r * 255.0f + 0.5f), (BYTE)(g * 255.0f + 0.5f), (BYTE)(b * 255.0f + 0.5f));
        }
        Color(D2D1_COLOR_F d2dVal)
        {
            d2d = d2dVal;
            gdi = RGB((BYTE)(d2dVal.r * 255.0f + 0.5f), (BYTE)(d2dVal.g * 255.0f + 0.5f), (BYTE)(d2dVal.b * 255.0f + 0.5f));
        }
    };

    enum class ThemeMode
    {
        Dark = 0,
        Light = 1
    };

    inline ThemeMode g_ThemeMode = ThemeMode::Dark;
    inline void SetThemeMode(ThemeMode mode) { g_ThemeMode = mode; }
    inline ThemeMode GetThemeMode() { return g_ThemeMode; }

    inline int g_ThemeColorIndex = 0;
    inline int ThemeColorPresetCount() { return 12; }
    inline void SetThemeColorIndex(int index)
    {
        if (index < 0) index = 0;
        if (index >= ThemeColorPresetCount()) index = ThemeColorPresetCount() - 1;
        g_ThemeColorIndex = index;
    }
    inline int GetThemeColorIndex() { return g_ThemeColorIndex; }

    inline int g_WindowMode = 0; // 0 = Glass, 1 = Acrylic
    inline void SetWindowMode(int mode) { g_WindowMode = mode; }
    inline int GetWindowMode() { return g_WindowMode; }

    struct ThemeConfig
    {
        float hue;
        float blur;
        float opacity;
        float highlight;
        float brightness;
        float saturation;
    };

    inline ThemeConfig g_DarkConfig = { 32.0f, 20.0f, 0.50f, 0.90f, 0.11f, 1.8f };
    inline ThemeConfig g_LightConfig = { 36.0f, 20.0f, 0.30f, 0.90f, 0.91f, 1.6f };
    inline ThemeConfig g_AcrylicDarkConfig = { 30.0f, 20.0f, 0.36f, 0.90f, 0.11f, 1.7f };
    inline ThemeConfig g_AcrylicLightConfig = { 34.0f, 20.0f, 0.36f, 0.90f, 0.96f, 1.5f };

    inline ThemeConfig ToThemeConfig(const Model::ThemeEffectConfig& config)
    {
        return { config.hue, config.blur, config.opacity, config.highlight, config.brightness, config.saturation };
    }

    inline Model::ThemeEffectConfig ToModelConfig(const ThemeConfig& config)
    {
        return { config.hue, config.blur, config.opacity, config.highlight, config.brightness, config.saturation };
    }

    inline void ApplyAppearanceSettings(const Model::AppearanceSettings& settings)
    {
        g_DarkConfig = ToThemeConfig(settings.dark);
        g_LightConfig = ToThemeConfig(settings.light);
        g_AcrylicDarkConfig = ToThemeConfig(settings.acrylicDark);
        g_AcrylicLightConfig = ToThemeConfig(settings.acrylicLight);
    }

    inline Model::AppearanceSettings CaptureAppearanceSettings()
    {
        Model::AppearanceSettings settings;
        settings.dark = ToModelConfig(g_DarkConfig);
        settings.light = ToModelConfig(g_LightConfig);
        settings.acrylicDark = ToModelConfig(g_AcrylicDarkConfig);
        settings.acrylicLight = ToModelConfig(g_AcrylicLightConfig);
        return settings;
    }

    struct ThemeColorPreset
    {
        const wchar_t* name;
        Color dark;
        Color light;
    };

    inline const ThemeColorPreset& GetThemeColorPreset(int index)
    {
        static const ThemeColorPreset presets[] = {
            { L"琥珀", Color(0.66f, 0.48f, 0.30f, 1.0f), Color(0.54f, 0.38f, 0.23f, 1.0f) },
            { L"珊瑚", Color(0.82f, 0.34f, 0.28f, 1.0f), Color(0.70f, 0.22f, 0.18f, 1.0f) },
            { L"赤红", Color(0.86f, 0.24f, 0.32f, 1.0f), Color(0.72f, 0.12f, 0.20f, 1.0f) },
            { L"玫瑰", Color(0.84f, 0.28f, 0.55f, 1.0f), Color(0.68f, 0.16f, 0.42f, 1.0f) },
            { L"紫晶", Color(0.62f, 0.42f, 0.88f, 1.0f), Color(0.46f, 0.28f, 0.72f, 1.0f) },
            { L"靛蓝", Color(0.36f, 0.52f, 0.92f, 1.0f), Color(0.22f, 0.38f, 0.78f, 1.0f) },
            { L"天蓝", Color(0.24f, 0.62f, 0.92f, 1.0f), Color(0.10f, 0.48f, 0.78f, 1.0f) },
            { L"青绿", Color(0.18f, 0.70f, 0.74f, 1.0f), Color(0.08f, 0.56f, 0.60f, 1.0f) },
            { L"薄荷", Color(0.16f, 0.72f, 0.58f, 1.0f), Color(0.06f, 0.56f, 0.44f, 1.0f) },
            { L"翡翠", Color(0.22f, 0.68f, 0.42f, 1.0f), Color(0.10f, 0.52f, 0.30f, 1.0f) },
            { L"草绿", Color(0.50f, 0.72f, 0.22f, 1.0f), Color(0.34f, 0.56f, 0.10f, 1.0f) },
            { L"柠黄", Color(0.82f, 0.68f, 0.18f, 1.0f), Color(0.64f, 0.50f, 0.08f, 1.0f) },
        };
        if (index < 0) index = 0;
        if (index >= ThemeColorPresetCount()) index = ThemeColorPresetCount() - 1;
        return presets[index];
    }

    inline Color GetThemeColorPresetColor(int index)
    {
        const ThemeColorPreset& preset = GetThemeColorPreset(index);
        return (g_ThemeMode == ThemeMode::Light) ? preset.light : preset.dark;
    }

    inline const wchar_t* GetThemeColorPresetName(int index)
    {
        return GetThemeColorPreset(index).name;
    }

    inline float ClampColor(float value)
    {
        if (value < 0.0f) return 0.0f;
        if (value > 1.0f) return 1.0f;
        return value;
    }

    inline Color ShiftColor(const Color& color, float delta, float alpha)
    {
        return Color(
            ClampColor(color.d2d.r + delta),
            ClampColor(color.d2d.g + delta),
            ClampColor(color.d2d.b + delta),
            alpha);
    }

    inline D2D1_COLOR_F HslToRgb(float h, float s, float l, float a)
    {
        float c = (1.0f - fabsf(2.0f * l - 1.0f)) * s;
        float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
        float m = l - c / 2.0f;
        float r = 0, g = 0, b = 0;
        if (h < 0) h = 0;
        if (h >= 360) h = fmodf(h, 360.0f);
        if (0 <= h && h < 60) { r = c; g = x; b = 0; }
        else if (60 <= h && h < 120) { r = x; g = c; b = 0; }
        else if (120 <= h && h < 180) { r = 0; g = c; b = x; }
        else if (180 <= h && h < 240) { r = 0; g = x; b = c; }
        else if (240 <= h && h < 300) { r = x; g = 0; b = c; }
        else if (300 <= h && h <= 360) { r = c; g = 0; b = x; }
        return D2D1::ColorF(r + m, g + m, b + m, a);
    }

    // Shared theme tokens
    struct ThemeColor
    {
        static inline Color TextNormal()
        {
            return (g_ThemeMode == ThemeMode::Light) ? Color(0.15f, 0.15f, 0.2f, 1.0f) : Color(0.92f, 0.92f, 0.95f, 1.0f);
        }
        static inline Color TextMuted()
        {
            return (g_ThemeMode == ThemeMode::Light) ? Color(0.15f, 0.15f, 0.2f, 0.6f) : Color(0.92f, 0.92f, 0.95f, 0.6f);
        }
        static inline Color Accent()
        {
            return GetThemeColorPresetColor(g_ThemeColorIndex);
        }
        static inline Color AccentHover()
        {
            float delta = (g_ThemeMode == ThemeMode::Light) ? 0.07f : 0.10f;
            return ShiftColor(Accent(), delta, 0.82f);
        }
        static inline Color AccentSubtle()
        {
            Color color = Accent();
            color.d2d.a = (g_ThemeMode == ThemeMode::Light) ? 0.18f : 0.22f;
            return color;
        }
        static inline Color DangerRed()
        {
            return Color(1.0f, 0.2f, 0.2f, 0.8f);
        }
        static inline Color TextOnAccent()
        {
            return Color(1.0f, 1.0f, 1.0f, 1.0f);
        }
        static inline Color CardBg()
        {
            return (g_ThemeMode == ThemeMode::Light) ? Color(0.0f, 0.0f, 0.0f, 0.03f) : Color(1.0f, 1.0f, 1.0f, 0.02f);
        }
        static inline Color CardBorder()
        {
            return (g_ThemeMode == ThemeMode::Light) ? Color(0.0f, 0.0f, 0.0f, 0.08f) : Color(1.0f, 1.0f, 1.0f, 0.06f);
        }
        static inline Color EditBg()
        {
            return (g_ThemeMode == ThemeMode::Light) ? Color(0.0f, 0.0f, 0.0f, 0.05f) : Color(1.0f, 1.0f, 1.0f, 0.12f);
        }
        static inline Color EditBorderNormal()
        {
            return (g_ThemeMode == ThemeMode::Light) ? Color(0.0f, 0.0f, 0.0f, 0.12f) : Color(1.0f, 1.0f, 1.0f, 0.15f);
        }
        static inline Color EditBorderFocus()
        {
            return AccentHover();
        }

        // Window-level helper colors
        static inline Color WindowTint()
        {
            if (g_ThemeMode == ThemeMode::Light)
            {
                auto& cfg = (g_WindowMode == 1) ? g_AcrylicLightConfig : g_LightConfig;
                float sat = (g_WindowMode == 1) ? 0.02f : 0.05f;
                D2D1_COLOR_F rgb = HslToRgb(cfg.hue, sat, cfg.brightness, cfg.opacity);
                return Color(rgb);
            }
            else
            {
                auto& cfg = (g_WindowMode == 1) ? g_AcrylicDarkConfig : g_DarkConfig;
                float sat = (g_WindowMode == 1) ? 0.03f : 0.10f;
                D2D1_COLOR_F rgb = HslToRgb(cfg.hue, sat, cfg.brightness, cfg.opacity);
                return Color(rgb);
            }
        }
        
        static inline Color WindowClear()
        {
            if (g_ThemeMode == ThemeMode::Light)
            {
                auto& cfg = (g_WindowMode == 1) ? g_AcrylicLightConfig : g_LightConfig;
                D2D1_COLOR_F rgb = HslToRgb(cfg.hue, (g_WindowMode == 1) ? 0.02f : 0.05f, cfg.brightness, cfg.opacity * 0.6f);
                return Color(rgb);
            }
            else
            {
                auto& cfg = (g_WindowMode == 1) ? g_AcrylicDarkConfig : g_DarkConfig;
                D2D1_COLOR_F rgb = HslToRgb(cfg.hue, (g_WindowMode == 1) ? 0.03f : 0.10f, cfg.brightness, cfg.opacity * 0.36f);
                return Color(rgb);
            }
        }

        static inline Color WindowBorder()
        {
            if (g_ThemeMode == ThemeMode::Light)
            {
                auto& cfg = (g_WindowMode == 1) ? g_AcrylicLightConfig : g_LightConfig;
                return Color(15 / 255.0f, 23 / 255.0f, 42 / 255.0f, cfg.highlight * 0.15f);
            }
            else
            {
                auto& cfg = (g_WindowMode == 1) ? g_AcrylicDarkConfig : g_DarkConfig;
                return Color(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, cfg.highlight * 0.09f);
            }
        }

        static inline Color Sheen()
        {
            if (g_ThemeMode == ThemeMode::Light)
            {
                auto& cfg = (g_WindowMode == 1) ? g_AcrylicLightConfig : g_LightConfig;
                return Color(1.0f, 1.0f, 1.0f, cfg.highlight * 0.27f);
            }
            else
            {
                auto& cfg = (g_WindowMode == 1) ? g_AcrylicDarkConfig : g_DarkConfig;
                return Color(1.0f, 1.0f, 1.0f, cfg.highlight * 0.055f);
            }
        }

        // Generic Button Colors
        static inline Color ButtonBgHover()
        {
            return (g_ThemeMode == ThemeMode::Light) ? Color(0.0f, 0.0f, 0.0f, 0.045f) : Color(1.0f, 1.0f, 1.0f, 0.105f);
        }
        static inline Color ButtonBgNormal()
        {
            return (g_ThemeMode == ThemeMode::Light) ? Color(0.0f, 0.0f, 0.0f, 0.018f) : Color(1.0f, 1.0f, 1.0f, 0.035f);
        }
        static inline Color ButtonBorderHover()
        {
            return (g_ThemeMode == ThemeMode::Light) ? Color(0.0f, 0.0f, 0.0f, 0.105f) : Color(1.0f, 1.0f, 1.0f, 0.18f);
        }
        static inline Color ButtonBorderNormal()
        {
            return (g_ThemeMode == ThemeMode::Light) ? Color(0.0f, 0.0f, 0.0f, 0.035f) : Color(1.0f, 1.0f, 1.0f, 0.065f);
        }
    };

    struct Metrics
    {
        static inline float HairlineStroke() { return 0.75f; }
        static inline float ControlStroke() { return 0.85f; }
        static inline float EmphasisStroke() { return 1.0f; }
        static inline float IconStroke() { return 1.15f; }
    };

    // Configurable styles for TextBox control
    struct TextBoxStyle
    {
        Color bgNormal;
        Color borderNormal;
        Color borderFocused;
        Color textNormal;
        Color textMuted;

        float cornerRadius;
        float borderThickness;

        // Inner padding layout
        float paddingLeft;
        float paddingTop;
        float paddingRight;
        float paddingBottom;

        std::wstring fontFamily;
        int fontSize; // Logical font size (unscaled pt)

        // Explicit default constructor to guarantee initialization across all compilers/modes
        TextBoxStyle()
            : bgNormal(ThemeColor::EditBg())
            , borderNormal(ThemeColor::EditBorderNormal())
            , borderFocused(ThemeColor::EditBorderFocus())
            , textNormal(ThemeColor::TextNormal())
            , textMuted(ThemeColor::TextMuted())
            , cornerRadius(6.0f)
            , borderThickness(Metrics::ControlStroke())
            , paddingLeft(8.0f)
            , paddingTop(6.0f)
            , paddingRight(8.0f)
            , paddingBottom(6.0f)
            , fontFamily(L"Microsoft YaHei UI")
            , fontSize(14)
        {
        }
    };
}
