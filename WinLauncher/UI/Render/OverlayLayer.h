#pragma once
#include "IRenderLayer.h"
#include "../../Config/UIStyle.h"
#include "../../DpiHelper.h"
#include <algorithm>
#include <d2d1effects.h>
#include <d2d1helper.h>

class OverlayLayer : public IRenderLayer
{
public:
    OverlayLayer() = default;
    void SetCornerRadius(float r) { m_cornerRadius = r; }
    ~OverlayLayer() { ReleaseResources(); }

    virtual void Render(ID2D1HwndRenderTarget* rt, ID2D1DeviceContext* dc,
                        const D2D1_SIZE_F& size, float scale) override
    {
        float w = size.width;
        float h = size.height;

        HWND hwnd = rt->GetHwnd();
        float systemScale = DpiHelper::GetSystemWindowScale(hwnd);
        float drawCornerRadius = m_cornerRadius * (systemScale / scale);

        if (m_dirty || m_size.width != w || m_size.height != h)
        {
            ReleaseResources();
            m_size = size;
            m_dirty = false;
        }

        auto& cfg = (UIStyle::GetThemeMode() == UIStyle::ThemeMode::Light) ? UIStyle::g_LightConfig : UIStyle::g_DarkConfig;

        // 1. Diagonal Specular Radial Glow (Part A of Layer 3) drawn under the base clear layer
        if (cfg.highlight > 0.0f) {
            ID2D1RadialGradientBrush* sheenBrush = nullptr;
            ID2D1GradientStopCollection* stopsSheen = nullptr;
            D2D1_GRADIENT_STOP stopDataSheen[2];
            stopDataSheen[0].position = 0.0f;
            stopDataSheen[0].color = D2D1::ColorF(1.0f, 1.0f, 1.0f, cfg.highlight * 0.25f);
            stopDataSheen[1].position = 1.0f;
            stopDataSheen[1].color = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.0f);

            rt->CreateGradientStopCollection(stopDataSheen, 2, D2D1_GAMMA_1_0, D2D1_EXTEND_MODE_CLAMP, &stopsSheen);
            if (stopsSheen) {
                rt->CreateRadialGradientBrush(
                    D2D1::RadialGradientBrushProperties(
                        D2D1::Point2F(w * 0.10f, h * 0.10f),
                        D2D1::Point2F(0.0f, 0.0f),
                        w * 0.85f,
                        w * 0.85f
                    ),
                    D2D1::BrushProperties(),
                    stopsSheen,
                    &sheenBrush
                );
                if (sheenBrush) {
                    D2D1_ROUNDED_RECT sheenRR = D2D1::RoundedRect(
                        D2D1::RectF(1.0f, 1.0f, w - 1.0f, h - 1.0f),
                        drawCornerRadius, drawCornerRadius
                    );
                    rt->FillRoundedRectangle(sheenRR, sheenBrush);
                    sheenBrush->Release();
                }
                stopsSheen->Release();
            }
        }

        // 2. Base Tint Layer (combining Opacity & Brightness)
        BYTE base_r = static_cast<BYTE>(20.0f + cfg.brightness * 235.0f);
        BYTE base_g = static_cast<BYTE>(20.0f + cfg.brightness * 235.0f);
        BYTE base_b = static_cast<BYTE>(25.0f + cfg.brightness * 230.0f);
        
        ID2D1SolidColorBrush* bgBrush = nullptr;
        rt->CreateSolidColorBrush(
            D2D1::ColorF(base_r / 255.0f, base_g / 255.0f, base_b / 255.0f, cfg.opacity),
            &bgBrush
        );
        if (bgBrush) {
            rt->FillRectangle(D2D1::RectF(0.0f, 0.0f, w, h), bgBrush);
            bgBrush->Release();
        }

        // 3. Crisp Bevel Edge Borders (Part B & C of Layer 3)
        if (cfg.highlight > 0.0f) {
            // B. Outer thin dark border for desktop contrast
            ID2D1SolidColorBrush* outerDarkBrush = nullptr;
            float darkOpacity = 0.14f * (1.0f - cfg.brightness * 0.4f);
            rt->CreateSolidColorBrush(D2D1::ColorF(15 / 255.0f, 23 / 255.0f, 42 / 255.0f, darkOpacity), &outerDarkBrush);
            if (outerDarkBrush) {
                D2D1_ROUNDED_RECT outerRR = D2D1::RoundedRect(
                    D2D1::RectF(0.5f, 0.5f, w - 0.5f, h - 0.5f),
                    drawCornerRadius, drawCornerRadius
                );
                rt->DrawRoundedRectangle(outerRR, outerDarkBrush, 1.0f);
                outerDarkBrush->Release();
            }

            // C. Inner diagonal gradient bright border for realistic light refraction
            ID2D1LinearGradientBrush* innerBrightBrush = nullptr;
            ID2D1GradientStopCollection* stopsInner = nullptr;
            D2D1_GRADIENT_STOP stopDataInner[2];
            stopDataInner[0].position = 0.0f;
            stopDataInner[0].color = D2D1::ColorF(1.0f, 1.0f, 1.0f, cfg.highlight * 0.75f);
            stopDataInner[1].position = 1.0f;
            stopDataInner[1].color = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.02f);

            rt->CreateGradientStopCollection(stopDataInner, 2, D2D1_GAMMA_1_0, D2D1_EXTEND_MODE_CLAMP, &stopsInner);
            if (stopsInner) {
                rt->CreateLinearGradientBrush(
                    D2D1::LinearGradientBrushProperties(D2D1::Point2F(1.5f, 1.5f), D2D1::Point2F(w - 1.5f, h - 1.5f)),
                    stopsInner,
                    &innerBrightBrush
                );
                if (innerBrightBrush) {
                    D2D1_ROUNDED_RECT innerRR = D2D1::RoundedRect(
                        D2D1::RectF(1.5f, 1.5f, w - 1.5f, h - 1.5f),
                        (std::max)(0.0f, drawCornerRadius - (1.0f * systemScale) / scale),
                        (std::max)(0.0f, drawCornerRadius - (1.0f * systemScale) / scale)
                    );
                    rt->DrawRoundedRectangle(innerRR, innerBrightBrush, 1.0f);
                    innerBrightBrush->Release();
                }
                stopsInner->Release();
            }
        }
    }

    virtual void OnResize(const D2D1_SIZE_F& size) override
    {
        MarkDirty();
    }

private:
    void ReleaseResources()
    {
    }

    D2D1_SIZE_F m_size = {};
    float m_cornerRadius = 8.0f;
};
