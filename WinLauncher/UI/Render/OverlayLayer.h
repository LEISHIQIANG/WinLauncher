#pragma once
#include "IRenderLayer.h"
#include "../../Config/UIStyle.h"
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

        if (m_dirty || m_size.width != w || m_size.height != h)
        {
            ReleaseResources();
            m_size = size;

            D2D1_GRADIENT_STOP gs[2];
            gs[0].position = 0;
            gs[0].color = UIStyle::ThemeColor::Sheen().d2d;
            gs[1].position = 1;
            gs[1].color = D2D1::ColorF(1, 1, 1, 0);
            rt->CreateGradientStopCollection(gs, 2, D2D1_GAMMA_1_0, D2D1_EXTEND_MODE_CLAMP, &m_gsc);

            if (m_gsc)
            {
                rt->CreateRadialGradientBrush(
                    D2D1::RadialGradientBrushProperties(
                        D2D1::Point2F(w * 0.1f, h * 0.1f),
                        D2D1::Point2F(0, 0),
                        w * 0.85f, h * 0.85f),
                    m_gsc.Get(), &m_sheenBrush);
            }

            rt->CreateSolidColorBrush(UIStyle::ThemeColor::WindowTint().d2d, &m_tintBrush);

            rt->CreateSolidColorBrush(UIStyle::ThemeColor::WindowBorder().d2d, &m_borderBrush);

            m_dirty = false;
        }

        // Sheen
        if (m_sheenBrush)
        {
            rt->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(1, 1, w - 1, h - 1), m_cornerRadius, m_cornerRadius), m_sheenBrush.Get());
        }

        // Tint
        if (m_tintBrush)
        {
            rt->FillRectangle(D2D1::RectF(0, 0, w, h), m_tintBrush.Get());
        }

        // Border
        if (m_borderBrush)
        {
            rt->DrawRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(0.5f, 0.5f, w - 0.5f, h - 0.5f), m_cornerRadius, m_cornerRadius),
                m_borderBrush.Get(), 1.0f);
        }
    }

    virtual void OnResize(const D2D1_SIZE_F& size) override
    {
        MarkDirty();
    }

    void MarkDirty() override
    {
        m_dirty = true;
    }

private:
    void ReleaseResources()
    {
        m_gsc.Reset();
        m_sheenBrush.Reset();
        m_tintBrush.Reset();
        m_borderBrush.Reset();
    }

    D2D1_SIZE_F m_size = {};
    float m_cornerRadius = 8.0f;
    ComPtr<ID2D1GradientStopCollection> m_gsc;
    ComPtr<ID2D1RadialGradientBrush> m_sheenBrush;
    ComPtr<ID2D1SolidColorBrush> m_tintBrush;
    ComPtr<ID2D1SolidColorBrush> m_borderBrush;
};
