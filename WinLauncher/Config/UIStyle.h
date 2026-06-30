#pragma once
#include "../Model/AppearanceSettings.h"
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <cmath>
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
            gdi = RGB((BYTE)(fminf(fmaxf(r, 0.0f), 1.0f) * 255.0f + 0.5f), 
                      (BYTE)(fminf(fmaxf(g, 0.0f), 1.0f) * 255.0f + 0.5f), 
                      (BYTE)(fminf(fmaxf(b, 0.0f), 1.0f) * 255.0f + 0.5f));
        }
        Color(D2D1_COLOR_F d2dVal)
        {
            d2d = d2dVal;
            gdi = RGB((BYTE)(fminf(fmaxf(d2dVal.r, 0.0f), 1.0f) * 255.0f + 0.5f), 
                      (BYTE)(fminf(fmaxf(d2dVal.g, 0.0f), 1.0f) * 255.0f + 0.5f), 
                      (BYTE)(fminf(fmaxf(d2dVal.b, 0.0f), 1.0f) * 255.0f + 0.5f));
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

    inline int g_WindowMode = 0; // 0 = Glow, 1 = Acrylic, 2 = Glass
    inline void SetWindowMode(int mode) { g_WindowMode = mode; }
    inline int GetWindowMode() { return g_WindowMode; }
    inline constexpr float GlowMaterialMinOpacity = 0.50f;

    namespace Scaling
    {
        inline constexpr int MinPercent = 80;
        inline constexpr int MaxPercent = 250;
        inline constexpr int StepPercent = 10;
        inline int g_GlobalScalePercent = 100;
        inline bool g_HasCustomGlobalScalePercent = false;

        inline int ClampPercent(int percent)
        {
            if (percent < MinPercent) percent = MinPercent;
            if (percent > MaxPercent) percent = MaxPercent;
            return percent;
        }

        inline int NormalizePercent(int percent)
        {
            percent = ClampPercent(percent);
            int remainder = percent % StepPercent;
            if (remainder != 0)
                percent += (remainder >= StepPercent / 2) ? (StepPercent - remainder) : -remainder;
            return ClampPercent(percent);
        }

        inline void SetGlobalScalePercent(int percent)
        {
            g_GlobalScalePercent = NormalizePercent(percent);
            g_HasCustomGlobalScalePercent = true;
        }

        inline void SetDefaultGlobalScalePercent(int percent)
        {
            g_GlobalScalePercent = ClampPercent(percent);
            g_HasCustomGlobalScalePercent = false;
        }

        inline int GetGlobalScalePercent()
        {
            return g_GlobalScalePercent;
        }

        inline bool HasCustomGlobalScalePercent()
        {
            return g_HasCustomGlobalScalePercent;
        }

        inline float GetGlobalScaleFactor()
        {
            return g_GlobalScalePercent / 100.0f;
        }

        inline float EffectiveScaleFactor(float systemScale)
        {
            return g_HasCustomGlobalScalePercent ? GetGlobalScaleFactor() : systemScale;
        }
    }

    namespace Performance
    {
        inline bool g_HardwareAccelerationEnabled = true;

        inline void SetHardwareAccelerationEnabled(bool enabled)
        {
            g_HardwareAccelerationEnabled = enabled;
        }

        inline bool IsHardwareAccelerationEnabled()
        {
            return g_HardwareAccelerationEnabled;
        }

        inline void ApplyProcessPolicy()
        {
            HANDLE process = GetCurrentProcess();
            HANDLE thread = GetCurrentThread();
            if (g_HardwareAccelerationEnabled)
            {
                SetPriorityClass(process, HIGH_PRIORITY_CLASS);
                SetThreadPriority(thread, THREAD_PRIORITY_ABOVE_NORMAL);
            }
            else
            {
                SetPriorityClass(process, NORMAL_PRIORITY_CLASS);
                SetThreadPriority(thread, THREAD_PRIORITY_NORMAL);
            }
        }
    }

    struct ThemeConfig
    {
        float hue;
        float blur;
        float opacity;
        float highlight;
        float brightness;
        float saturation;
    };

    inline ThemeConfig g_DarkConfig = { 32.0f, 20.0f, 0.50f, 0.90f, 0.11f, 2.5f };
    inline ThemeConfig g_LightConfig = { 36.0f, 20.0f, 0.50f, 0.90f, 0.90f, 2.5f };
    inline ThemeConfig g_AcrylicDarkConfig = { 30.0f, 20.0f, 0.36f, 0.90f, 0.11f, 2.5f };
    inline ThemeConfig g_AcrylicLightConfig = { 34.0f, 20.0f, 0.60f, 0.90f, 0.98f, 2.5f };
    inline ThemeConfig g_GlassDarkConfig = { 32.0f, 20.0f, 0.30f, 0.90f, 0.90f, 2.5f };
    inline ThemeConfig g_GlassLightConfig = { 36.0f, 20.0f, 0.30f, 0.90f, 0.90f, 2.5f };

    inline ThemeConfig ToThemeConfig(const Model::ThemeEffectConfig& config)
    {
        return { config.hue, config.blur, config.opacity, config.highlight, config.brightness, config.saturation };
    }

    inline void ClampGlowMaterialConfig(ThemeConfig& config)
    {
        if (config.opacity < GlowMaterialMinOpacity) config.opacity = GlowMaterialMinOpacity;
        if (config.opacity > 1.0f) config.opacity = 1.0f;
    }

    inline float ClampMaterialOpacity(int windowMode, float opacity)
    {
        if (windowMode == 0 && opacity < GlowMaterialMinOpacity) opacity = GlowMaterialMinOpacity;
        if (opacity < 0.0f) opacity = 0.0f;
        if (opacity > 1.0f) opacity = 1.0f;
        return opacity;
    }

    inline Model::ThemeEffectConfig ToModelConfig(const ThemeConfig& config)
    {
        return { config.hue, config.blur, config.opacity, config.highlight, config.brightness, config.saturation };
    }

    inline void ApplyAppearanceSettings(const Model::AppearanceSettings& settings)
    {
        g_DarkConfig = ToThemeConfig(settings.dark);
        g_LightConfig = ToThemeConfig(settings.light);
        ClampGlowMaterialConfig(g_DarkConfig);
        ClampGlowMaterialConfig(g_LightConfig);
        g_AcrylicDarkConfig = ToThemeConfig(settings.acrylicDark);
        g_AcrylicLightConfig = ToThemeConfig(settings.acrylicLight);
        g_GlassDarkConfig = ToThemeConfig(settings.glassDark);
        g_GlassLightConfig = ToThemeConfig(settings.glassLight);
    }

    inline Model::AppearanceSettings CaptureAppearanceSettings()
    {
        Model::AppearanceSettings settings;
        ClampGlowMaterialConfig(g_DarkConfig);
        ClampGlowMaterialConfig(g_LightConfig);
        settings.dark = ToModelConfig(g_DarkConfig);
        settings.light = ToModelConfig(g_LightConfig);
        settings.acrylicDark = ToModelConfig(g_AcrylicDarkConfig);
        settings.acrylicLight = ToModelConfig(g_AcrylicLightConfig);
        settings.glassDark = ToModelConfig(g_GlassDarkConfig);
        settings.glassLight = ToModelConfig(g_GlassLightConfig);
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

    inline Color GetThemeColorPresetColorFor(int index, ThemeMode mode)
    {
        const ThemeColorPreset& preset = GetThemeColorPreset(index);
        return (mode == ThemeMode::Light) ? preset.light : preset.dark;
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

    inline D2D1_COLOR_F LerpD2DColor(const D2D1_COLOR_F& c1, const D2D1_COLOR_F& c2, float t)
    {
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        return D2D1::ColorF(
            c1.r + (c2.r - c1.r) * t,
            c1.g + (c2.g - c1.g) * t,
            c1.b + (c2.b - c1.b) * t,
            c1.a + (c2.a - c1.a) * t
        );
    }

    inline Color BlendColor(const Color& from, const Color& to, float t)
    {
        return Color(LerpD2DColor(from.d2d, to.d2d, t));
    }

    namespace ThemeTransition
    {
        inline bool g_Active = false;
        inline float g_Progress = 1.0f;
        inline ThemeMode g_FromThemeMode = ThemeMode::Dark;
        inline int g_FromThemeColorIndex = 0;
        inline int g_FromWindowMode = 0;

        inline float Ease(float t)
        {
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            if (t < 0.5f) return 4.0f * t * t * t;
            float p = -2.0f * t + 2.0f;
            return 1.0f - (p * p * p) / 2.0f;
        }

        inline void Begin(ThemeMode fromThemeMode, int fromThemeColorIndex, int fromWindowMode)
        {
            g_FromThemeMode = fromThemeMode;
            g_FromThemeColorIndex = fromThemeColorIndex;
            g_FromWindowMode = fromWindowMode;
            g_Progress = 0.0f;
            g_Active = true;
        }

        inline void SetProgress(float progress)
        {
            if (progress < 0.0f) progress = 0.0f;
            if (progress > 1.0f) progress = 1.0f;
            g_Progress = progress;
        }

        inline void End()
        {
            g_Progress = 1.0f;
            g_Active = false;
        }

        inline bool IsActive()
        {
            return g_Active;
        }

        inline float BlendProgress()
        {
            return Ease(g_Progress);
        }
    }

    inline float RelativeLuminance(const D2D1_COLOR_F& color)
    {
        return color.r * 0.2126f + color.g * 0.7152f + color.b * 0.0722f;
    }

    namespace Typography
    {
        inline const wchar_t* FontFamily()
        {
            return L"Microsoft YaHei UI";
        }

        inline const wchar_t* LocaleName()
        {
            return L"zh-cn";
        }

        inline DWRITE_FONT_WEIGHT NormalizeWeight(DWRITE_FONT_WEIGHT weight)
        {
            if (weight <= DWRITE_FONT_WEIGHT_LIGHT)
                return DWRITE_FONT_WEIGHT_NORMAL;
            if (weight >= DWRITE_FONT_WEIGHT_BOLD)
                return DWRITE_FONT_WEIGHT_SEMI_BOLD;
            return weight;
        }

        inline HRESULT CreateTextFormat(
            IDWriteFactory* factory,
            IDWriteTextFormat** format,
            float size,
            DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_TEXT_ALIGNMENT textAlignment = DWRITE_TEXT_ALIGNMENT_LEADING,
            DWRITE_PARAGRAPH_ALIGNMENT paragraphAlignment = DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
            DWRITE_WORD_WRAPPING wrapping = DWRITE_WORD_WRAPPING_NO_WRAP,
            bool trimCharacters = true)
        {
            if (!factory || !format)
                return E_INVALIDARG;

            *format = nullptr;
            HRESULT hr = factory->CreateTextFormat(
                FontFamily(),
                nullptr,
                NormalizeWeight(weight),
                DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,
                size,
                LocaleName(),
                format);

            if (SUCCEEDED(hr) && *format)
            {
                (*format)->SetTextAlignment(textAlignment);
                (*format)->SetParagraphAlignment(paragraphAlignment);
                (*format)->SetWordWrapping(wrapping);

                if (trimCharacters)
                {
                    DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
                    (*format)->SetTrimming(&trimming, nullptr);
                }
            }

            return hr;
        }

        inline void ApplyRenderTargetTextDefaults(ID2D1RenderTarget* rt)
        {
            if (!rt)
                return;
            rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        }
    }

    inline D2D1_COLOR_F HslToRgb(float h, float s, float l, float a)
    {
        if (h < 0.0f) h = 0.0f;
        if (h >= 360.0f) h = fmodf(h, 360.0f);

        float actualL = l * 1.5f;
        float overflow = 0.0f;
        if (actualL > 1.0f)
        {
            overflow = actualL - 1.0f;
            actualL = 1.0f;
        }
        float c = (1.0f - fabsf(2.0f * actualL - 1.0f)) * s;
        float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
        float m = actualL - c / 2.0f;
        float r = 0.0f, g = 0.0f, b = 0.0f;
        if (0.0f <= h && h < 60.0f) { r = c; g = x; b = 0.0f; }
        else if (60.0f <= h && h < 120.0f) { r = x; g = c; b = 0.0f; }
        else if (120.0f <= h && h < 180.0f) { r = 0.0f; g = c; b = x; }
        else if (180.0f <= h && h < 240.0f) { r = 0.0f; g = x; b = c; }
        else if (240.0f <= h && h < 300.0f) { r = x; g = 0.0f; b = c; }
        else if (300.0f <= h && h <= 360.0f) { r = c; g = 0.0f; b = x; }
        return D2D1::ColorF(r + m + overflow, g + m + overflow, b + m + overflow, a);
    }

    // Shared theme tokens
    struct ThemeColor
    {
        static inline Color BlendToken(const Color& from, const Color& to)
        {
            return ThemeTransition::IsActive()
                ? BlendColor(from, to, ThemeTransition::BlendProgress())
                : to;
        }

        static inline const ThemeConfig& ConfigFor(ThemeMode mode, int windowMode)
        {
            if (windowMode == 2)
                return (mode == ThemeMode::Light) ? g_GlassLightConfig : g_GlassDarkConfig;
            if (windowMode == 1)
                return (mode == ThemeMode::Light) ? g_AcrylicLightConfig : g_AcrylicDarkConfig;
            return (mode == ThemeMode::Light) ? g_LightConfig : g_DarkConfig;
        }

        static inline Color TextNormalFor(ThemeMode mode)
        {
            return (mode == ThemeMode::Light) ? Color(0.055f, 0.065f, 0.085f, 1.0f) : Color(0.94f, 0.95f, 0.97f, 1.0f);
        }

        static inline Color TextNormal()
        {
            return BlendToken(TextNormalFor(ThemeTransition::g_FromThemeMode), TextNormalFor(g_ThemeMode));
        }

        static inline Color TextMutedFor(ThemeMode mode)
        {
            return (mode == ThemeMode::Light) ? Color(0.055f, 0.065f, 0.085f, 0.72f) : Color(0.94f, 0.95f, 0.97f, 0.68f);
        }

        static inline Color TextMuted()
        {
            return BlendToken(TextMutedFor(ThemeTransition::g_FromThemeMode), TextMutedFor(g_ThemeMode));
        }

        static inline Color Accent()
        {
            return BlendToken(
                GetThemeColorPresetColorFor(ThemeTransition::g_FromThemeColorIndex, ThemeTransition::g_FromThemeMode),
                GetThemeColorPresetColorFor(g_ThemeColorIndex, g_ThemeMode));
        }

        static inline Color AccentHover()
        {
            float fromDelta = (ThemeTransition::g_FromThemeMode == ThemeMode::Light) ? 0.07f : 0.10f;
            float toDelta = (g_ThemeMode == ThemeMode::Light) ? 0.07f : 0.10f;
            return BlendToken(
                ShiftColor(GetThemeColorPresetColorFor(ThemeTransition::g_FromThemeColorIndex, ThemeTransition::g_FromThemeMode), fromDelta, 0.82f),
                ShiftColor(GetThemeColorPresetColorFor(g_ThemeColorIndex, g_ThemeMode), toDelta, 0.82f));
        }

        static inline Color AccentSubtle()
        {
            Color from = GetThemeColorPresetColorFor(ThemeTransition::g_FromThemeColorIndex, ThemeTransition::g_FromThemeMode);
            from.d2d.a = (ThemeTransition::g_FromThemeMode == ThemeMode::Light) ? 0.18f : 0.22f;
            Color to = GetThemeColorPresetColorFor(g_ThemeColorIndex, g_ThemeMode);
            to.d2d.a = (g_ThemeMode == ThemeMode::Light) ? 0.18f : 0.22f;
            return BlendToken(from, to);
        }

        static inline Color DangerRed()
        {
            return Color(1.0f, 0.2f, 0.2f, 0.8f);
        }

        static inline Color TextOnAccentFor(int colorIndex, ThemeMode mode)
        {
            return RelativeLuminance(GetThemeColorPresetColorFor(colorIndex, mode).d2d) > 0.46f
                ? Color(0.035f, 0.045f, 0.06f, 1.0f)
                : Color(1.0f, 1.0f, 1.0f, 1.0f);
        }

        static inline Color TextOnAccent()
        {
            return BlendToken(
                TextOnAccentFor(ThemeTransition::g_FromThemeColorIndex, ThemeTransition::g_FromThemeMode),
                TextOnAccentFor(g_ThemeColorIndex, g_ThemeMode));
        }

        static inline Color ThemeBaseFor(ThemeMode mode)
        {
            return (mode == ThemeMode::Light) ? Color(0.0f, 0.0f, 0.0f, 1.0f) : Color(1.0f, 1.0f, 1.0f, 1.0f);
        }

        static inline Color ThemeBase()
        {
            return BlendToken(ThemeBaseFor(ThemeTransition::g_FromThemeMode), ThemeBaseFor(g_ThemeMode));
        }

        static inline Color CardBgFor(ThemeMode mode)
        {
            return (mode == ThemeMode::Light) ? Color(0.0f, 0.0f, 0.0f, 0.03f) : Color(1.0f, 1.0f, 1.0f, 0.02f);
        }

        static inline Color CardBg()
        {
            return BlendToken(CardBgFor(ThemeTransition::g_FromThemeMode), CardBgFor(g_ThemeMode));
        }

        static inline Color CardBorderFor(ThemeMode mode)
        {
            return (mode == ThemeMode::Light) ? Color(0.0f, 0.0f, 0.0f, 0.08f) : Color(1.0f, 1.0f, 1.0f, 0.06f);
        }

        static inline Color CardBorder()
        {
            return BlendToken(CardBorderFor(ThemeTransition::g_FromThemeMode), CardBorderFor(g_ThemeMode));
        }

        static inline Color EditBgFor(ThemeMode mode)
        {
            return (mode == ThemeMode::Light) ? Color(0.0f, 0.0f, 0.0f, 0.05f) : Color(1.0f, 1.0f, 1.0f, 0.12f);
        }

        static inline Color EditBg()
        {
            return BlendToken(EditBgFor(ThemeTransition::g_FromThemeMode), EditBgFor(g_ThemeMode));
        }

        static inline Color EditBorderNormalFor(ThemeMode mode)
        {
            return (mode == ThemeMode::Light) ? Color(0.0f, 0.0f, 0.0f, 0.12f) : Color(1.0f, 1.0f, 1.0f, 0.15f);
        }

        static inline Color EditBorderNormal()
        {
            return BlendToken(EditBorderNormalFor(ThemeTransition::g_FromThemeMode), EditBorderNormalFor(g_ThemeMode));
        }

        static inline Color EditBorderFocus()
        {
            return AccentHover();
        }

        // Window-level helper colors
        static inline Color WindowTintFor(ThemeMode mode, int windowMode)
        {
            if (mode == ThemeMode::Light)
            {
                const auto& cfg = ConfigFor(mode, windowMode);
                float sat = (windowMode == 1) ? 0.02f : 0.05f;
                D2D1_COLOR_F rgb = HslToRgb(cfg.hue, sat, cfg.brightness, 1.0f - cfg.highlight);
                return Color(rgb);
            }
            else
            {
                const auto& cfg = ConfigFor(mode, windowMode);
                float sat = (windowMode == 1) ? 0.03f : 0.10f;
                D2D1_COLOR_F rgb = HslToRgb(cfg.hue, sat, cfg.brightness, 1.0f - cfg.highlight);
                return Color(rgb);
            }
        }

        static inline Color WindowTint()
        {
            return BlendToken(
                WindowTintFor(ThemeTransition::g_FromThemeMode, ThemeTransition::g_FromWindowMode),
                WindowTintFor(g_ThemeMode, g_WindowMode));
        }
        
        static inline Color WindowClearFor(ThemeMode mode, int windowMode)
        {
            if (mode == ThemeMode::Light)
            {
                const auto& cfg = ConfigFor(mode, windowMode);
                D2D1_COLOR_F rgb = HslToRgb(cfg.hue, 0.0f, cfg.brightness, cfg.opacity * 0.6f);
                return Color(rgb);
            }
            else
            {
                const auto& cfg = ConfigFor(mode, windowMode);
                D2D1_COLOR_F rgb = HslToRgb(cfg.hue, 0.0f, cfg.brightness, cfg.opacity * 0.36f);
                return Color(rgb);
            }
        }

        static inline Color WindowClear()
        {
            return BlendToken(
                WindowClearFor(ThemeTransition::g_FromThemeMode, ThemeTransition::g_FromWindowMode),
                WindowClearFor(g_ThemeMode, g_WindowMode));
        }

        static inline Color WindowBorderFor(ThemeMode mode, int windowMode)
        {
            if (mode == ThemeMode::Light)
            {
                const auto& cfg = ConfigFor(mode, windowMode);
                return Color(15 / 255.0f, 23 / 255.0f, 42 / 255.0f, cfg.highlight * 0.15f);
            }
            else
            {
                const auto& cfg = ConfigFor(mode, windowMode);
                return Color(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, cfg.highlight * 0.09f);
            }
        }

        static inline Color WindowBorder()
        {
            return BlendToken(
                WindowBorderFor(ThemeTransition::g_FromThemeMode, ThemeTransition::g_FromWindowMode),
                WindowBorderFor(g_ThemeMode, g_WindowMode));
        }

        static inline Color SheenFor(ThemeMode mode, int windowMode)
        {
            if (mode == ThemeMode::Light)
            {
                const auto& cfg = ConfigFor(mode, windowMode);
                return Color(1.0f, 1.0f, 1.0f, cfg.highlight * 0.27f);
            }
            else
            {
                const auto& cfg = ConfigFor(mode, windowMode);
                return Color(1.0f, 1.0f, 1.0f, cfg.highlight * 0.055f);
            }
        }

        static inline Color Sheen()
        {
            return BlendToken(
                SheenFor(ThemeTransition::g_FromThemeMode, ThemeTransition::g_FromWindowMode),
                SheenFor(g_ThemeMode, g_WindowMode));
        }

        // Generic Button Colors
        static inline Color ButtonBgHoverFor(ThemeMode mode)
        {
            return (mode == ThemeMode::Light) ? Color(0.0f, 0.0f, 0.0f, 0.045f) : Color(1.0f, 1.0f, 1.0f, 0.105f);
        }
        static inline Color ButtonBgHover()
        {
            return BlendToken(ButtonBgHoverFor(ThemeTransition::g_FromThemeMode), ButtonBgHoverFor(g_ThemeMode));
        }
        static inline Color ButtonBgNormalFor(ThemeMode mode)
        {
            return (mode == ThemeMode::Light) ? Color(0.0f, 0.0f, 0.0f, 0.018f) : Color(1.0f, 1.0f, 1.0f, 0.035f);
        }
        static inline Color ButtonBgNormal()
        {
            return BlendToken(ButtonBgNormalFor(ThemeTransition::g_FromThemeMode), ButtonBgNormalFor(g_ThemeMode));
        }
        static inline Color ButtonBorderHoverFor(ThemeMode mode)
        {
            return (mode == ThemeMode::Light) ? Color(0.0f, 0.0f, 0.0f, 0.105f) : Color(1.0f, 1.0f, 1.0f, 0.18f);
        }
        static inline Color ButtonBorderHover()
        {
            return BlendToken(ButtonBorderHoverFor(ThemeTransition::g_FromThemeMode), ButtonBorderHoverFor(g_ThemeMode));
        }
        static inline Color ButtonBorderNormalFor(ThemeMode mode)
        {
            return (mode == ThemeMode::Light) ? Color(0.0f, 0.0f, 0.0f, 0.035f) : Color(1.0f, 1.0f, 1.0f, 0.065f);
        }
        static inline Color ButtonBorderNormal()
        {
            return BlendToken(ButtonBorderNormalFor(ThemeTransition::g_FromThemeMode), ButtonBorderNormalFor(g_ThemeMode));
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
        float fontSize; // DIPs; DirectWrite scales these through the render target DPI
        DWRITE_FONT_WEIGHT fontWeight;

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
            , fontFamily(Typography::FontFamily())
            , fontSize(12.0f)
            , fontWeight(DWRITE_FONT_WEIGHT_NORMAL)
        {
        }
    };

    namespace Animation
    {
        inline bool g_Enabled = true;
        inline float g_DurationMs = 200.0f;

        inline bool IsEnabled() { return g_Enabled; }
        inline void SetEnabled(bool enable) { g_Enabled = enable; }

        inline float GetDurationMs() { return g_DurationMs; }
        inline void SetDurationMs(float duration) { g_DurationMs = duration; }
    }

    inline D2D1_COLOR_F LerpColor(const D2D1_COLOR_F& c1, const D2D1_COLOR_F& c2, float t)
    {
        return D2D1::ColorF(
            c1.r + (c2.r - c1.r) * t,
            c1.g + (c2.g - c1.g) * t,
            c1.b + (c2.b - c1.b) * t,
            c1.a + (c2.a - c1.a) * t
        );
    }
}
