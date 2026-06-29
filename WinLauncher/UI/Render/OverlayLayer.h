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
        float borderOffset = (0.5f * systemScale) / scale;
        float borderWidth = (1.0f * systemScale) / scale;

        if (m_dirty || m_size.width != w || m_size.height != h)
        {
            ReleaseResources();
            m_size = size;
            D2D1_SIZE_F sheenLayerSize = D2D1::SizeF(w, h);
            D2D1_PIXEL_FORMAT sheenLayerFormat =
                D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED);
            rt->CreateCompatibleRenderTarget(
                &sheenLayerSize,
                nullptr,
                &sheenLayerFormat,
                D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS_NONE,
                &m_sheenLayerRt);

            if (m_sheenLayerRt)
            {
                FLOAT dpiX = 96.0f;
                FLOAT dpiY = 96.0f;
                rt->GetDpi(&dpiX, &dpiY);
                m_sheenLayerRt->SetDpi(dpiX, dpiY);
            }

            D2D1_COLOR_F sheen = UIStyle::ThemeColor::Sheen().d2d;
            D2D1_GRADIENT_STOP gs[5];
            gs[0].position = 0.00f;
            gs[0].color = D2D1::ColorF(sheen.r, sheen.g, sheen.b, (std::min)(1.0f, sheen.a * 1.35f));
            gs[1].position = 0.22f;
            gs[1].color = D2D1::ColorF(sheen.r, sheen.g, sheen.b, (std::min)(1.0f, sheen.a * 1.05f));
            gs[2].position = 0.56f;
            gs[2].color = D2D1::ColorF(sheen.r, sheen.g, sheen.b, sheen.a * 0.60f);
            gs[3].position = 0.86f;
            gs[3].color = D2D1::ColorF(sheen.r, sheen.g, sheen.b, sheen.a * 0.22f);
            gs[4].position = 1.00f;
            gs[4].color = D2D1::ColorF(sheen.r, sheen.g, sheen.b, 0.0f);
            if (m_sheenLayerRt)
                m_sheenLayerRt->CreateGradientStopCollection(gs, 5, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &m_gsc);

            if (m_sheenLayerRt && m_gsc)
            {
                m_sheenLayerRt->CreateRadialGradientBrush(
                    D2D1::RadialGradientBrushProperties(
                        D2D1::Point2F(w * 0.10f, h * 0.10f),
                        D2D1::Point2F(0, 0),
                        w * 1.18f, h * 1.12f),
                    m_gsc.Get(), &m_sheenBrush);
            }

            rt->CreateSolidColorBrush(UIStyle::ThemeColor::WindowTint().d2d, &m_tintBrush);

            rt->CreateSolidColorBrush(UIStyle::ThemeColor::WindowBorder().d2d, &m_borderBrush);

            m_dirty = false;
        }

        // Tint
        if (m_tintBrush)
        {
            rt->FillRectangle(D2D1::RectF(0, 0, w, h), m_tintBrush.Get());
        }

        // Sheen - draw after tint and use additive compositing so blurred
        // transparent pixels cannot darken light-mode glass.
        if (m_sheenLayerRt && m_sheenBrush)
        {
            m_sheenLayerBitmap.Reset();
            m_sheenLayerRt->BeginDraw();
            m_sheenLayerRt->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
            m_sheenLayerRt->FillRoundedRectangle(
                D2D1::RoundedRect(
                    D2D1::RectF(borderOffset, borderOffset, w - borderOffset, h - borderOffset),
                    (std::max)(0.0f, drawCornerRadius - borderOffset),
                    (std::max)(0.0f, drawCornerRadius - borderOffset)),
                m_sheenBrush.Get());
            HRESULT sheenHr = m_sheenLayerRt->EndDraw();
            if (SUCCEEDED(sheenHr))
                m_sheenLayerRt->GetBitmap(&m_sheenLayerBitmap);
        }

        if (m_sheenLayerBitmap)
        {
            if (dc)
            {
                if (!m_sheenBlurEffect)
                    dc->CreateEffect(CLSID_D2D1GaussianBlur, &m_sheenBlurEffect);

                if (m_sheenBlurEffect)
                {
                    m_sheenBlurEffect->SetInput(0, m_sheenLayerBitmap.Get());
                    m_sheenBlurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, 4.0f);
                    m_sheenBlurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION, D2D1_GAUSSIANBLUR_OPTIMIZATION_QUALITY);
                    m_sheenBlurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_SOFT);
                    dc->DrawImage(
                        m_sheenBlurEffect.Get(),
                        nullptr,
                        nullptr,
                        D2D1_INTERPOLATION_MODE_LINEAR,
                        D2D1_COMPOSITE_MODE_PLUS);
                }
                else
                {
                    rt->DrawBitmap(m_sheenLayerBitmap.Get(), D2D1::RectF(0, 0, w, h));
                }
            }
            else
            {
                rt->DrawBitmap(m_sheenLayerBitmap.Get(), D2D1::RectF(0, 0, w, h));
            }
        }

        // Border
        if (m_borderBrush)
        {
            rt->DrawRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(borderOffset, borderOffset, w - borderOffset, h - borderOffset), drawCornerRadius, drawCornerRadius),
                m_borderBrush.Get(), borderWidth);
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
        m_sheenBlurEffect.Reset();
        m_sheenLayerRt.Reset();
        m_sheenLayerBitmap.Reset();
        m_tintBrush.Reset();
        m_borderBrush.Reset();
    }

    D2D1_SIZE_F m_size = {};
    float m_cornerRadius = 8.0f;
    ComPtr<ID2D1GradientStopCollection> m_gsc;
    ComPtr<ID2D1RadialGradientBrush> m_sheenBrush;
    ComPtr<ID2D1Effect> m_sheenBlurEffect;
    ComPtr<ID2D1BitmapRenderTarget> m_sheenLayerRt;
    ComPtr<ID2D1Bitmap> m_sheenLayerBitmap;
    ComPtr<ID2D1SolidColorBrush> m_tintBrush;
    ComPtr<ID2D1SolidColorBrush> m_borderBrush;
};
