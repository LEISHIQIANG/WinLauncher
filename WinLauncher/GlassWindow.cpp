#include "App/AppMessages.h"
#define NOMINMAX
#include "GlassWindow.h"
#include "DpiHelper.h"
#include "App/Logger.h"
#include "Config/UIStyle.h"
#include "Services/ConfigPath.h"
#include "UI/MouseCaptureController.h"
#include <windowsx.h>
#include <dwmapi.h>
#include <d2d1helper.h>
#include <d2d1effects.h>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "dwrite.lib")

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

static float EaseOutCubic(float t);
static float EaseInCubic(float t);
static double PerfNowMs();
static const wchar_t* WindowModeName(int windowMode);
static float ClampCornerRadius(float radius, float width, float height);

float GlassWindow::GetDpiScaleForMonitor(HMONITOR hMonitor)
{
    return DpiHelper::GetDpiScaleForMonitor(hMonitor);
}

float GlassWindow::GetWindowScale(HWND hwnd)
{
    return DpiHelper::GetWindowScale(hwnd);
}

float GlassWindow::GetSystemWindowScale(HWND hwnd)
{
    return DpiHelper::GetSystemWindowScale(hwnd);
}

GlassWindow::GlassWindow()
{
    m_consecutiveDeviceLossCount = 0;
}

GlassWindow::~GlassWindow()
{
    ReleaseD2D();
}

ComPtr<ID2D1SolidColorBrush> GlassWindow::GetOrCreateBrush(const D2D1_COLOR_F& color)
{
    for (auto& entry : m_brushCache)
    {
        if (entry.color.r == color.r && entry.color.g == color.g &&
            entry.color.b == color.b && entry.color.a == color.a)
        {
            return entry.brush;
        }
    }

    ComPtr<ID2D1SolidColorBrush> brush;
    if (m_rt)
    {
        m_rt->CreateSolidColorBrush(color, &brush);
        if (brush)
        {
            m_brushCache.push_back({ color, brush });
        }
    }
    return brush;
}

ComPtr<ID2D1SolidColorBrush> GlassWindow::GetCachedBrush(const D2D1_COLOR_F& color)
{
    for (auto& entry : m_brushCache)
    {
        if (entry.color.r == color.r && entry.color.g == color.g &&
            entry.color.b == color.b && entry.color.a == color.a)
        {
            return entry.brush;
        }
    }
    return nullptr;
}

static float Clamp01(float t)
{
    if (t < 0.0f) return 0.0f;
    if (t > 1.0f) return 1.0f;
    return t;
}

static float EaseOutCubic(float t)
{
    t = 1.0f - Clamp01(t);
    return 1.0f - t * t * t;
}

static float EaseInCubic(float t)
{
    t = Clamp01(t);
    return t * t * t;
}

static double PerfNowMs()
{
    static double freq = 0.0;
    if (freq == 0.0)
    {
        LARGE_INTEGER li;
        QueryPerformanceFrequency(&li);
        freq = (double)li.QuadPart;
    }
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    return ((double)li.QuadPart * 1000.0) / freq;
}

static bool ShouldLogPerf(ULONGLONG& lastLogTick, double elapsedMs, double thresholdMs)
{
    return Logger::ShouldLogElapsed(lastLogTick, elapsedMs, thresholdMs, 1000);
}

static const wchar_t* WindowModeName(int windowMode)
{
    return windowMode == 1 ? L"acrylic" : L"glass";
}

static float ClampCornerRadius(float radius, float width, float height)
{
    if (radius < 0.0f)
        return 0.0f;
    if (width > 0.0f && height > 0.0f)
    {
        float maxRadius = (std::min)(width, height) * 0.5f;
        if (radius > maxRadius)
            return maxRadius;
    }
    return radius;
}

struct MaterialFrameGeometry
{
    D2D1_RECT_F clipBounds = {};
    D2D1_RECT_F borderBounds = {};
    float clipRadius = 0.0f;
    float borderRadius = 0.0f;
    float borderWidth = 0.0f;
};

// Keep the material clip and its visible edge derived from the same physical
// pixel metrics.  The previous 0.5/1.5 DIP offsets produced mismatched edges
// at scaled DPI and made the dark bevel especially noticeable on the right.
static MaterialFrameGeometry BuildMaterialFrameGeometry(
    float width,
    float height,
    float cornerRadius,
    float borderOffset,
    float borderWidth)
{
    MaterialFrameGeometry geometry;
    geometry.clipBounds = D2D1::RectF(0.0f, 0.0f, width, height);
    geometry.clipRadius = ClampCornerRadius(cornerRadius, width, height);

    const float maxInset = (std::min)(width, height) * 0.5f;
    const float inset = (std::min)((std::max)(0.0f, borderOffset), maxInset);
    geometry.borderBounds = D2D1::RectF(inset, inset, width - inset, height - inset);
    geometry.borderRadius = ClampCornerRadius(
        geometry.clipRadius - inset,
        geometry.borderBounds.right - geometry.borderBounds.left,
        geometry.borderBounds.bottom - geometry.borderBounds.top);
    geometry.borderWidth = (std::max)(0.0f, borderWidth);
    return geometry;
}

ShadowSettings GlassWindow::GetShadowSettings() const
{
    ShadowSettings s;
    s.margin = 55;
    s.blurRadius = 32;
    s.offsetX = 0;
    s.offsetY = 8;
    s.opacity = 0.35f;
    s.color = RGB(0, 0, 0);
    return s;
}

void GlassWindow::UpdateWindowCornerRadius()
{
    m_backdrop.UpdateWindowCornerRadius();
}

float GlassWindow::GetDrawCornerRadius(float renderScale, float width, float height) const
{
    return m_backdrop.GetDrawCornerRadius(m_hWnd, renderScale, width, height);
}

float GlassWindow::GetPhysicalCornerRadius() const
{
    return m_backdrop.GetPhysicalCornerRadius(m_hWnd);
}

void GlassWindow::UpdateWindowRoundRegion()
{
    m_backdrop.UpdateWindowRoundRegion(m_hWnd);
}

void GlassWindow::ApplySystemBackdrop()
{
    m_backdrop.ApplySystemBackdrop(this);
}

bool GlassWindow::EnsureD2D()
{
    if (m_rt) return true;
    m_d2dHardwareAccelerationEnabled = UIStyle::Performance::IsHardwareAccelerationEnabled();
    if (!m_d2d)
    {
        D2D1_FACTORY_TYPE factoryType = m_d2dHardwareAccelerationEnabled
            ? D2D1_FACTORY_TYPE_MULTI_THREADED
            : D2D1_FACTORY_TYPE_SINGLE_THREADED;
        HRESULT factoryHr = D2D1CreateFactory(factoryType, IID_PPV_ARGS(&m_d2d));
        if (FAILED(factoryHr))
        {
            LOG_G_ERROR_NODE(
                L"ui.glass",
                L"d2d_factory_failed",
                L"hr=0x%08X hardwareAcceleration=%d hwnd=%p",
                factoryHr,
                (int)m_d2dHardwareAccelerationEnabled,
                m_hWnd);
            return false;
        }
    }
    if (!m_dw)
    {
        HRESULT dwriteHr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), &m_dw);
        if (FAILED(dwriteHr))
        {
            LOG_G_ERROR_NODE(L"ui.glass", L"dwrite_factory_failed", L"hr=0x%08X hwnd=%p", dwriteHr, m_hWnd);
        }
        if (m_dw && !m_tf)
        {
            UIStyle::Typography::CreateTextFormat(
                m_dw.Get(),
                &m_tf,
                11.0f,
                DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_TEXT_ALIGNMENT_CENTER,
                DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }
    if (!m_d2d) return false;

    RECT cr; GetClientRect(m_hWnd, &cr);
    D2D1_RENDER_TARGET_TYPE targetType = m_d2dHardwareAccelerationEnabled
        ? D2D1_RENDER_TARGET_TYPE_HARDWARE
        : D2D1_RENDER_TARGET_TYPE_SOFTWARE;
    HRESULT hr = m_d2d->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(targetType,
            D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED)),
        D2D1::HwndRenderTargetProperties(m_hWnd, D2D1::SizeU(cr.right, cr.bottom)),
        &m_rt);
    if (FAILED(hr) && m_d2dHardwareAccelerationEnabled)
    {
        LOG_G_WARNING_NODE(
            L"ui.glass",
            L"render_target_hardware_failed",
            L"hr=0x%08X fallback=default size=%dx%d hwnd=%p",
            hr,
            cr.right,
            cr.bottom,
            m_hWnd);
        hr = m_d2d->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED)),
            D2D1::HwndRenderTargetProperties(m_hWnd, D2D1::SizeU(cr.right, cr.bottom)),
            &m_rt);
    }
    if (FAILED(hr))
    {
        LOG_G_ERROR_NODE(
            L"ui.glass",
            L"render_target_failed",
            L"hr=0x%08X hardwareAcceleration=%d size=%dx%d hwnd=%p",
            hr,
            (int)m_d2dHardwareAccelerationEnabled,
            cr.right,
            cr.bottom,
            m_hWnd);
        return false;
    }

    float scale = GetWindowScale(m_hWnd);
    float dpi = scale * 96.0f;
    m_rt->SetDpi(dpi, dpi);
    UIStyle::Typography::ApplyRenderTargetTextDefaults(m_rt.Get());
    m_brushCache.clear();

    // Link background layer to render target
    if (m_compositor)
    {
        auto* bg = m_compositor->GetBackgroundLayer();
        if (bg) bg->SetRenderTarget(m_rt.Get());
        m_compositor->MarkAllDirty();
    }

    LOG_G_INFO_NODE(
        L"ui.glass",
        L"render_target_ready",
        L"size=%dx%d dpi=%.1f hardwareAcceleration=%d targetType=%d hwnd=%p",
        cr.right,
        cr.bottom,
        dpi,
        (int)m_d2dHardwareAccelerationEnabled,
        (int)targetType,
        m_hWnd);

    return true;
}

void GlassWindow::ReleaseD2D()
{
    ResetBackgroundResources(L"release_d2d", true);
    m_tf.Reset();
    m_dw.Reset();
    m_d2d.Reset();
}

void GlassWindow::ResetBackgroundResources(const wchar_t* reason, bool includeRenderTarget)
{
    bool hadResources =
        m_backgroundCapture.Bitmap() || m_bgFinal.Get() || m_compositeRt.Get() ||
        m_blurEffect.Get() || m_satEffect.Get() || m_sheenBlurEffect.Get() ||
        m_sheenGsc.Get() || m_sheenBrush.Get() || m_sheenLayerRt.Get() ||
        m_sheenLayerBitmap.Get() || m_roundedClipLayer.Get() ||
        m_roundedClipGeometry.Get() || (includeRenderTarget && m_rt.Get());

    if (hadResources)
    {
        LOG_G_INFO_NODE(
            L"ui.glass",
            L"background_resources_reset",
            L"reason=%s includeRenderTarget=%d hwnd=%p",
            reason ? reason : L"unknown",
            includeRenderTarget ? 1 : 0,
            m_hWnd);
    }

    m_brushCache.clear();
    m_backgroundCapture.Reset();
    m_bgFinal.Reset();
    m_compositeRt.Reset();
    m_blurEffect.Reset();
    m_satEffect.Reset();
    m_sheenBlurEffect.Reset();
    m_sheenGsc.Reset();
    m_sheenBrush.Reset();
    m_sheenLayerRt.Reset();
    m_sheenLayerBitmap.Reset();
    m_roundedClipLayer.Reset();
    m_roundedClipGeometry.Reset();
    m_effectWinSize = {};
    m_effectCornerRadius = -1.0f;
    m_themeTransition.Reset();
    if (includeRenderTarget)
    {
        m_rt.Reset();
    }
    m_bgCaptureDirty = true;
    m_bgCompositeDirty = true;
}

void GlassWindow::MarkBackgroundDirty(const wchar_t* reason, bool logEvent)
{
    m_bgCaptureDirty = true;
    m_bgCompositeDirty = true;
    if (m_compositor)
    {
        m_compositor->MarkAllDirty();
    }
    if (logEvent)
    {
        LOG_G_INFO_NODE(
            L"ui.glass",
            L"background_dirty",
            L"reason=%s hwnd=%p",
            reason ? reason : L"unknown",
            m_hWnd);
    }
}

void GlassWindow::UpdateTheme()
{
    if (m_d2d && m_d2dHardwareAccelerationEnabled != UIStyle::Performance::IsHardwareAccelerationEnabled())
    {
        LOG_G_INFO_NODE(
            L"ui.glass",
            L"hardware_acceleration_changed",
            L"old=%d new=%d reason=theme_update hwnd=%p",
            (int)m_d2dHardwareAccelerationEnabled,
            (int)UIStyle::Performance::IsHardwareAccelerationEnabled(),
            m_hWnd);
        ReleaseD2D();
    }
    m_brushCache.clear();
    m_sheenBlurEffect.Reset();
    m_sheenGsc.Reset();
    m_sheenBrush.Reset();
    m_sheenLayerRt.Reset();
    m_sheenLayerBitmap.Reset();
    if (m_themeTransition.IsActive())
        m_themeTransition.SetPendingBackdropUpdate(true);
    else
    {
        // Reapply for every mode, not just acrylic: leaving acrylic must
        // replace the DWM accent visual, otherwise the stale acrylic backdrop
        // bleeds through the painted frame as a dark edge seam.
        ApplySystemBackdrop();
    }
    m_bgCompositeDirty = true;
    if (m_compositor)
    {
        m_compositor->MarkAllDirty();
    }
    LOG_G_INFO_NODE(
        L"ui.glass",
        L"theme_updated",
        L"themeMode=%d themeTransitionActive=%d pendingBackdrop=%d hwnd=%p",
        (int)UIStyle::GetThemeMode(),
        (int)m_themeTransition.IsActive(),
        (int)m_themeTransition.PendingBackdropUpdate(),
        m_hWnd);
    if (m_hWnd)
    {
        InvalidateRect(m_hWnd, nullptr, TRUE);
    }
}

void GlassWindow::UpdateBackgroundStyle()
{
    if (m_d2d && m_d2dHardwareAccelerationEnabled != UIStyle::Performance::IsHardwareAccelerationEnabled())
    {
        LOG_G_INFO_NODE(
            L"ui.glass",
            L"hardware_acceleration_changed",
            L"old=%d new=%d reason=background_style hwnd=%p",
            (int)m_d2dHardwareAccelerationEnabled,
            (int)UIStyle::Performance::IsHardwareAccelerationEnabled(),
            m_hWnd);
        ReleaseD2D();
    }
    m_brushCache.clear();
    m_sheenBlurEffect.Reset();
    m_sheenGsc.Reset();
    m_sheenBrush.Reset();
    m_sheenLayerRt.Reset();
    m_sheenLayerBitmap.Reset();

    int windowMode = 0;
    if (m_appCtx && m_appCtx->configService)
    {
        windowMode = m_appCtx->configService->GetWindowMode();
    }
    ApplySystemBackdrop();

    m_bgCompositeDirty = true;
    LOG_G_INFO_NODE(
        L"ui.glass",
        L"background_style_updated",
        L"windowMode=%d(%s) hwnd=%p",
        windowMode,
        WindowModeName(windowMode),
        m_hWnd);
    if (m_hWnd)
    {
        InvalidateRect(m_hWnd, nullptr, TRUE);
    }
}

bool GlassWindow::CaptureBackground()
{
    return m_backgroundCapture.Capture(m_hWnd, m_rt.Get());
}

void GlassWindow::CompositeBackgroundToCache()
{
    if (!m_rt || !m_backgroundCapture.Bitmap()) return;

    double compositeStartMs = PerfNowMs();
    D2D1_SIZE_F rtSize = m_rt->GetSize();
    float w = rtSize.width;
    float h = rtSize.height;
    if (w <= 0.0f || h <= 0.0f) return;

    float scale = GetWindowScale(m_hWnd);
    float systemScale = GetSystemWindowScale(m_hWnd);
    float drawCornerRadius = GetDrawCornerRadius(scale, w, h);
    float borderOffset = (0.5f * systemScale) / scale;
    float borderWidth = (1.0f * systemScale) / scale;
    const MaterialFrameGeometry materialFrame =
        BuildMaterialFrameGeometry(w, h, drawCornerRadius, borderOffset, borderWidth);

    // Reuse bitmap render target if size matches
    bool sizeChanged = false;
    if (m_compositeRt)
    {
        D2D1_SIZE_F size = m_compositeRt->GetSize();
        if (size.width != w || size.height != h)
        {
            m_compositeRt.Reset();
            sizeChanged = true;
        }
    }
    if (!m_compositeRt)
    {
        HRESULT hr = m_rt->CreateCompatibleRenderTarget(
            D2D1::SizeF(w, h), &m_compositeRt);
        if (FAILED(hr))
        {
            m_bgFinal.Reset();
            LOG_G_ERROR_NODE(
                L"ui.glass",
                L"background_composite_target_failed",
                L"hr=0x%08X size=%.0fx%.0f scale=%.2f systemScale=%.2f hwnd=%p",
                hr,
                w,
                h,
                scale,
                systemScale,
                m_hWnd);
            return;
        }
        FLOAT dpiX, dpiY;
        m_rt->GetDpi(&dpiX, &dpiY);
        m_compositeRt->SetDpi(dpiX, dpiY);
    }

    // If window size changed, rebuild cached effects
    if (sizeChanged ||
        m_effectWinSize.width != w ||
        m_effectWinSize.height != h ||
        fabsf(m_effectCornerRadius - drawCornerRadius) > 0.01f)
    {
        m_blurEffect.Reset(); m_satEffect.Reset(); m_sheenBlurEffect.Reset();
        m_sheenGsc.Reset(); m_sheenBrush.Reset();
        m_sheenLayerRt.Reset(); m_sheenLayerBitmap.Reset();
        m_roundedClipLayer.Reset(); m_roundedClipGeometry.Reset();
        m_effectWinSize = { w, h };
        m_effectCornerRadius = drawCornerRadius;
    }

    ComPtr<ID2D1BitmapRenderTarget> bmpRt = m_compositeRt;
    bmpRt->BeginDraw();
    bmpRt->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
    D2D1_ANTIALIAS_MODE originalAntialiasMode = bmpRt->GetAntialiasMode();
    bmpRt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    bool roundedLayerPushed = false;
    if (materialFrame.clipRadius > 0.0f)
    {
        if (!m_roundedClipGeometry)
        {
            ComPtr<ID2D1Factory> roundedFactory;
            bmpRt->GetFactory(&roundedFactory);
            if (roundedFactory)
            {
                roundedFactory->CreateRoundedRectangleGeometry(
                    D2D1::RoundedRect(materialFrame.clipBounds, materialFrame.clipRadius, materialFrame.clipRadius),
                    &m_roundedClipGeometry);
            }
        }
        if (!m_roundedClipLayer)
            bmpRt->CreateLayer(D2D1::SizeF(w, h), &m_roundedClipLayer);

        if (m_roundedClipGeometry && m_roundedClipLayer)
        {
            bmpRt->PushLayer(
                D2D1::LayerParameters(
                    materialFrame.clipBounds,
                    m_roundedClipGeometry.Get(),
                    D2D1_ANTIALIAS_MODE_PER_PRIMITIVE),
                m_roundedClipLayer.Get());
            roundedLayerPushed = true;
        }
    }

    // A. Draw Blurred Desktop Screenshot (Layer 0)
    ID2D1DeviceContext* dc = nullptr;
    int windowMode = 0;
    if (m_appCtx && m_appCtx->configService)
    {
        windowMode = m_appCtx->configService->GetWindowMode();
    }
    auto& cfg = UIStyle::ThemeColor::ConfigFor(UIStyle::GetThemeMode(), windowMode);

    if (SUCCEEDED(bmpRt->QueryInterface(&dc)))
    {
        if (!m_blurEffect)
        {
            HRESULT blurHr = dc->CreateEffect(CLSID_D2D1GaussianBlur, &m_blurEffect);
            if (FAILED(blurHr))
            {
                static ULONGLONG s_lastBlurEffectLogTick = 0;
                if (Logger::ShouldLogEvery(s_lastBlurEffectLogTick, 5000))
                {
                    LOG_G_WARNING_NODE(L"ui.glass", L"background_blur_effect_failed", L"hr=0x%08X hwnd=%p", blurHr, m_hWnd);
                }
            }
        }
        if (!m_satEffect)
        {
            HRESULT satHr = dc->CreateEffect(CLSID_D2D1Saturation, &m_satEffect);
            if (FAILED(satHr))
            {
                static ULONGLONG s_lastSaturationEffectLogTick = 0;
                if (Logger::ShouldLogEvery(s_lastSaturationEffectLogTick, 5000))
                {
                    LOG_G_WARNING_NODE(L"ui.glass", L"background_saturation_effect_failed", L"hr=0x%08X hwnd=%p", satHr, m_hWnd);
                }
            }
        }

        if (m_blurEffect && m_satEffect)
        {
            m_blurEffect->SetInput(0, m_backgroundCapture.Bitmap());
            m_blurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, cfg.blur);
            m_blurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);

            ComPtr<ID2D1Image> blurOut;
            m_blurEffect->GetOutput(&blurOut);
            if (blurOut)
            {
                m_satEffect->SetInput(0, blurOut.Get());
                m_satEffect->SetValue(D2D1_SATURATION_PROP_SATURATION, cfg.saturation);
                dc->DrawImage(m_satEffect.Get());
            }
        }
        else
        {
            dc->DrawImage(m_backgroundCapture.Bitmap());
        }
        dc->Release();
    }
    else
    {
        bmpRt->DrawBitmap(m_backgroundCapture.Bitmap(), D2D1::RectF(0, 0, w, h));
    }

    // B. Diagonal Specular Radial Glow (Part A of Layer 3)
    if (cfg.highlight > 0.0f)
    {
        ID2D1RadialGradientBrush* sheenBrush = nullptr;
        ID2D1GradientStopCollection* stopsSheen = nullptr;
        D2D1_GRADIENT_STOP stopDataSheen[2];
        stopDataSheen[0].position = 0.0f;
        stopDataSheen[0].color = D2D1::ColorF(1.0f, 1.0f, 1.0f, cfg.highlight * 0.25f);
        stopDataSheen[1].position = 1.0f;
        stopDataSheen[1].color = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.0f);

        bmpRt->CreateGradientStopCollection(stopDataSheen, 2, D2D1_GAMMA_1_0, D2D1_EXTEND_MODE_CLAMP, &stopsSheen);
        if (stopsSheen)
        {
            bmpRt->CreateRadialGradientBrush(
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
            if (sheenBrush)
            {
                D2D1_ROUNDED_RECT sheenRR = D2D1::RoundedRect(
                    materialFrame.clipBounds,
                    materialFrame.clipRadius, materialFrame.clipRadius
                );
                bmpRt->FillRoundedRectangle(sheenRR, sheenBrush);
                sheenBrush->Release();
            }
            stopsSheen->Release();
        }
    }

    if (windowMode == 0 && cfg.highlight > 0.0f)
    {
        D2D1_COLOR_F accent = UIStyle::ThemeColor::Accent().d2d;
        ID2D1RadialGradientBrush* glowBrush = nullptr;
        ID2D1GradientStopCollection* stopsGlow = nullptr;
        D2D1_GRADIENT_STOP stopDataGlow[2];
        stopDataGlow[0].position = 0.0f;
        stopDataGlow[0].color = D2D1::ColorF(accent.r, accent.g, accent.b, cfg.highlight * 0.20f);
        stopDataGlow[1].position = 1.0f;
        stopDataGlow[1].color = D2D1::ColorF(accent.r, accent.g, accent.b, 0.0f);

        bmpRt->CreateGradientStopCollection(stopDataGlow, 2, D2D1_GAMMA_1_0, D2D1_EXTEND_MODE_CLAMP, &stopsGlow);
        if (stopsGlow)
        {
            bmpRt->CreateRadialGradientBrush(
                D2D1::RadialGradientBrushProperties(
                    D2D1::Point2F(w * 0.80f, h * 0.25f),
                    D2D1::Point2F(0.0f, 0.0f),
                    w * 0.70f,
                    h * 0.85f
                ),
                D2D1::BrushProperties(),
                stopsGlow,
                &glowBrush
            );
            if (glowBrush)
            {
                bmpRt->FillRoundedRectangle(
                    D2D1::RoundedRect(materialFrame.clipBounds, materialFrame.clipRadius, materialFrame.clipRadius),
                    glowBrush);
                glowBrush->Release();
            }
            stopsGlow->Release();
        }
    }

    // C. Base Tint Layer (combining Opacity & Brightness)
    BYTE base_r = static_cast<BYTE>(20.0f + cfg.brightness * 235.0f);
    BYTE base_g = static_cast<BYTE>(20.0f + cfg.brightness * 235.0f);
    BYTE base_b = static_cast<BYTE>(25.0f + cfg.brightness * 230.0f);

    ID2D1SolidColorBrush* bgBrush = nullptr;
    bmpRt->CreateSolidColorBrush(
        D2D1::ColorF(base_r / 255.0f, base_g / 255.0f, base_b / 255.0f, cfg.opacity),
        &bgBrush
    );
    if (bgBrush)
    {
        bmpRt->FillRoundedRectangle(
            D2D1::RoundedRect(materialFrame.clipBounds, materialFrame.clipRadius, materialFrame.clipRadius),
            bgBrush);
        bgBrush->Release();
    }

    // D. One DPI-aligned refraction edge.  Dark materials deliberately use a
    // soft light edge instead of the old hard dark desktop-contrast outline.
    if (cfg.highlight > 0.0f && materialFrame.borderWidth > 0.0f)
    {
        const bool isDarkMaterial = UIStyle::GetThemeMode() == UIStyle::ThemeMode::Dark;
        ID2D1LinearGradientBrush* frameBrush = nullptr;
        ID2D1GradientStopCollection* frameStops = nullptr;
        D2D1_GRADIENT_STOP stopDataFrame[2];
        stopDataFrame[0].position = 0.0f;
        stopDataFrame[0].color = D2D1::ColorF(
            1.0f, 1.0f, 1.0f,
            cfg.highlight * (isDarkMaterial ? 0.16f : 0.75f));
        stopDataFrame[1].position = 1.0f;
        stopDataFrame[1].color = isDarkMaterial
            ? D2D1::ColorF(1.0f, 1.0f, 1.0f, cfg.highlight * 0.035f)
            : UIStyle::ThemeColor::WindowBorder().d2d;

        bmpRt->CreateGradientStopCollection(stopDataFrame, 2, D2D1_GAMMA_1_0, D2D1_EXTEND_MODE_CLAMP, &frameStops);
        if (frameStops)
        {
            bmpRt->CreateLinearGradientBrush(
                D2D1::LinearGradientBrushProperties(
                    D2D1::Point2F(materialFrame.borderBounds.left, materialFrame.borderBounds.top),
                    D2D1::Point2F(materialFrame.borderBounds.right, materialFrame.borderBounds.bottom)),
                frameStops,
                &frameBrush
            );
            if (frameBrush)
            {
                bmpRt->DrawRoundedRectangle(
                    D2D1::RoundedRect(
                        materialFrame.borderBounds,
                        materialFrame.borderRadius,
                        materialFrame.borderRadius),
                    frameBrush,
                    materialFrame.borderWidth);
                frameBrush->Release();
            }
            frameStops->Release();
        }
    }

    if (roundedLayerPushed)
    {
        bmpRt->PopLayer();
    }
    bmpRt->SetAntialiasMode(originalAntialiasMode);

    HRESULT hr = bmpRt->EndDraw();
    if (SUCCEEDED(hr))
    {
        bmpRt->GetBitmap(&m_bgFinal);
    }
    else
    {
        m_bgFinal.Reset();
        LOG_G_ERROR_NODE(
            L"ui.glass",
            L"background_composite_enddraw_failed",
            L"hr=0x%08X size=%.0fx%.0f windowMode=%d(%s) hwnd=%p",
            hr,
            w,
            h,
            windowMode,
            WindowModeName(windowMode),
            m_hWnd);
    }

    double elapsedMs = PerfNowMs() - compositeStartMs;
    static ULONGLONG s_lastCompositeLogTick = 0;
    if (ShouldLogPerf(s_lastCompositeLogTick, elapsedMs, 12.0))
    {
        LOG_G_WARNING_NODE(
            L"ui.glass",
            L"background_composite_slow",
            L"elapsedMs=%.2f thresholdMs=12.00 size=%.0fx%.0f scale=%.2f systemScale=%.2f corner=%.2f windowMode=%d(%s) finalBitmap=%d hwnd=%p",
            elapsedMs,
            w,
            h,
            scale,
            systemScale,
            drawCornerRadius,
            windowMode,
            WindowModeName(windowMode),
            m_bgFinal ? 1 : 0,
            m_hWnd);
    }
}

bool GlassWindow::RefreshBackgroundCache()
{
    const bool captured = CaptureBackground();
    m_bgCaptureDirty = false;
    // A failed capture may still have an older raw bitmap, but it must never
    // be promoted into a new composite.  DoPaint will retain the last verified
    // final bitmap, or clear safely when no verified cache exists yet.
    m_bgCompositeDirty = captured;
    if (captured)
    {
        CompositeBackgroundToCache();
        m_bgCompositeDirty = false;
    }
    return captured;
}


void GlassWindow::DrawBackgroundFullTarget(ID2D1Bitmap* bitmap)
{
    // A DIP-sized destination rect can land a float hair inside the render
    // target's last pixel column at fractional UI scales (90/110/120%), and
    // Direct2D's rasterization then leaves that column only partially
    // covered, letting residue from the previous material's frame (acrylic's
    // border stroke) show as a 1px theme-colored edge line.  Overdraw one DIP
    // past the far edges: the excess is clipped and the sub-pixel stretch of
    // a blurred background is invisible.
    if (!bitmap || !m_rt) return;

    D2D1_SIZE_F size = m_rt->GetSize();
    m_rt->DrawBitmap(
        bitmap,
        D2D1::RectF(0.0f, 0.0f, size.width + 1.0f, size.height + 1.0f));
}

void GlassWindow::DoPaint()
{
    m_lastPaintSucceeded = false;
    if (!EnsureD2D()) return;

    double paintStartMs = PerfNowMs();

    D2D1_SIZE_F rtSize = m_rt->GetSize();
    float w = rtSize.width;
    float h = rtSize.height;
    if (w <= 0.0f || h <= 0.0f) return;
    FLOAT dpiX = 96.0f;
    FLOAT dpiY = 96.0f;
    m_rt->GetDpi(&dpiX, &dpiY);
    float scale = dpiX / 96.0f;

    m_rt->BeginDraw();

    D2D1_MATRIX_3X2_F originalTransform;
    m_rt->GetTransform(&originalTransform);
    bool transformModified = false;

    if (UIStyle::Animation::IsEnabled() && m_animState != AnimState::None)
    {
        D2D1_MATRIX_3X2_F scaleTransform;
        GetAnimationTransform(w, h, m_animProgress, m_animState, scaleTransform);
        m_rt->SetTransform(scaleTransform * originalTransform);
        transformModified = true;
    }

    if (m_compositor)
    {
        const double compositorStartMs = PerfNowMs();
        m_compositor->Render(m_rt.Get(), scale);
        const double compositorMs = PerfNowMs() - compositorStartMs;

        const double contentStartMs = PerfNowMs();
        OnPaintContent(m_rt.Get());
        const double contentMs = PerfNowMs() - contentStartMs;

        const double transitionStartMs = PerfNowMs();
        DrawThemeTransitionOverlay(m_rt.Get(), w, h);
        const double transitionMs = PerfNowMs() - transitionStartMs;

        if (transformModified)
        {
            m_rt->SetTransform(originalTransform);
        }
        const double endDrawStartMs = PerfNowMs();
        HRESULT hr = m_rt->EndDraw();
        m_lastPaintSucceeded = SUCCEEDED(hr);
        const double endDrawMs = PerfNowMs() - endDrawStartMs;
        double elapsedMs = PerfNowMs() - paintStartMs;
        const double renderMs = elapsedMs - endDrawMs;
        static ULONGLONG s_lastCompositorPaintLogTick = 0;
        if (ShouldLogPerf(s_lastCompositorPaintLogTick, renderMs, 16.0))
        {
            LOG_G_WARNING_NODE(
                L"ui.glass",
                L"paint_slow",
                L"path=compositor elapsedMs=%.2f thresholdMs=16.00 compositorMs=%.2f contentMs=%.2f transitionMs=%.2f endDrawMs=%.2f size=%.0fx%.0f scale=%.2f themeTransition=%d animState=%d hwnd=%p",
                elapsedMs,
                compositorMs,
                contentMs,
                transitionMs,
                endDrawMs,
                w,
                h,
                scale,
                (int)m_themeTransition.IsActive(),
                (int)m_animState,
                m_hWnd);
        }
        else
        {
            static ULONGLONG s_lastCompositorPacedLogTick = 0;
            if (ShouldLogPerf(s_lastCompositorPacedLogTick, endDrawMs, 16.0))
            {
                LOG_G_DEBUG_NODE(
                    L"ui.glass",
                    L"paint_paced",
                    L"path=compositor elapsedMs=%.2f renderMs=%.2f endDrawMs=%.2f size=%.0fx%.0f scale=%.2f hwnd=%p",
                    elapsedMs,
                    renderMs,
                    endDrawMs,
                    w,
                    h,
                    scale,
                    m_hWnd);
            }
        }
        if (hr == D2DERR_RECREATE_TARGET)
        {
            LOG_G_WARNING_NODE(L"ui.glass", L"paint_recreate_target", L"path=compositor hr=0x%08X hwnd=%p", hr, m_hWnd);
            m_consecutiveDeviceLossCount++;
            if (m_consecutiveDeviceLossCount > 3)
            {
                LOG_G_ERROR_NODE(L"ui.glass", L"paint_device_loss_loop", L"Device loss loop detected on compositor path, writing marker and falling back");
                {
                    const std::wstring markerPath = ConfigPath::GetGpuCrashMarkerPath();
                    ConfigPath::EnsureDirectoryExists(ConfigPath::GetUserConfigDirectory());
                    HANDLE hMarker = CreateFileW(markerPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                    if (hMarker != INVALID_HANDLE_VALUE) CloseHandle(hMarker);
                }
                UIStyle::Performance::SetHardwareAccelerationEnabled(false);
                if (m_appCtx && m_appCtx->configService) m_appCtx->configService->SetHardwareAccelerationEnabled(false);
            }
            ResetBackgroundResources(L"paint_recreate_target_compositor", true);
            if (m_compositor) m_compositor->MarkAllDirty();
            EnsureD2D();
            InvalidateRect(m_hWnd, nullptr, FALSE);
        }
        else if (FAILED(hr))
        {
            static ULONGLONG s_lastCompositorEndDrawLogTick = 0;
            if (Logger::ShouldLogEvery(s_lastCompositorEndDrawLogTick, 1000))
            {
                LOG_G_ERROR_NODE(L"ui.glass", L"paint_enddraw_failed", L"path=compositor hr=0x%08X hwnd=%p", hr, m_hWnd);
            }
        }
        else
        {
            m_consecutiveDeviceLossCount = 0;
        }
        return;
    }

    // Legacy rendering path

    int windowMode = 0;
    if (m_appCtx && m_appCtx->configService)
    {
        windowMode = m_appCtx->configService->GetWindowMode();
    }

    const double backgroundStartMs = PerfNowMs();
    if (windowMode == 1) // Acrylic
    {
        m_rt->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

        // Border
        {
            ComPtr<ID2D1SolidColorBrush> bo = GetOrCreateBrush(UIStyle::ThemeColor::WindowBorder().d2d);
            if (bo)
            {
                float systemScale = GetSystemWindowScale(m_hWnd);
                float drawCornerRadius = GetDrawCornerRadius(scale, w, h);
                float borderOffset = (0.5f * systemScale) / scale;
                float borderWidth = (1.0f * systemScale) / scale;
                m_rt->DrawRoundedRectangle(
                    D2D1::RoundedRect(D2D1::RectF(borderOffset, borderOffset, w - borderOffset, h - borderOffset), drawCornerRadius, drawCornerRadius),
                    bo.Get(), borderWidth);
            }
        }
    }
    else // Glass (custom blur)
    {
        if (m_bgCaptureDirty)
        {
            const bool captured = CaptureBackground();
            m_bgCaptureDirty = false;
            // Do not rebuild from an empty or stale capture. Preserve the last
            // verified composite; without one, the existing clear fallback is used.
            m_bgCompositeDirty = captured;
        }

        if (m_bgCompositeDirty)
        {
            CompositeBackgroundToCache();
            m_bgCompositeDirty = false;
        }

        if (m_bgFinal)
        {
            DrawBackgroundFullTarget(m_bgFinal.Get());
        }
        else if (m_backgroundCapture.Bitmap())
        {
            DrawBackgroundFullTarget(m_backgroundCapture.Bitmap());
        }
        else
        {
            m_rt->Clear(UIStyle::ThemeColor::WindowClear().d2d);
        }
    }
    const double backgroundMs = PerfNowMs() - backgroundStartMs;

    const double contentStartMs = PerfNowMs();
    OnPaintContent(m_rt.Get());
    const double contentMs = PerfNowMs() - contentStartMs;

    const double transitionStartMs = PerfNowMs();
    DrawThemeTransitionOverlay(m_rt.Get(), w, h);
    const double transitionMs = PerfNowMs() - transitionStartMs;

    if (transformModified)
    {
        m_rt->SetTransform(originalTransform);
    }

    const double endDrawStartMs = PerfNowMs();
    HRESULT hr = m_rt->EndDraw();
    m_lastPaintSucceeded = SUCCEEDED(hr);
    const double endDrawMs = PerfNowMs() - endDrawStartMs;
    double elapsedMs = PerfNowMs() - paintStartMs;
    const double renderMs = elapsedMs - endDrawMs;
    static ULONGLONG s_lastPaintLogTick = 0;
    if (ShouldLogPerf(s_lastPaintLogTick, renderMs, 16.0))
    {
        LOG_G_WARNING_NODE(
            L"ui.glass",
            L"paint_slow",
            L"path=legacy elapsedMs=%.2f thresholdMs=16.00 backgroundMs=%.2f contentMs=%.2f transitionMs=%.2f endDrawMs=%.2f size=%.0fx%.0f scale=%.2f windowMode=%d(%s) captureDirty=%d compositeDirty=%d finalBitmap=%d capBitmap=%d themeTransition=%d animState=%d hwnd=%p",
            elapsedMs,
            backgroundMs,
            contentMs,
            transitionMs,
            endDrawMs,
            w,
            h,
            scale,
            windowMode,
            WindowModeName(windowMode),
            (int)m_bgCaptureDirty,
            (int)m_bgCompositeDirty,
            m_bgFinal ? 1 : 0,
            m_backgroundCapture.Bitmap() ? 1 : 0,
            (int)m_themeTransition.IsActive(),
            (int)m_animState,
            m_hWnd);
    }
    else
    {
        static ULONGLONG s_lastLegacyPacedLogTick = 0;
        if (ShouldLogPerf(s_lastLegacyPacedLogTick, endDrawMs, 16.0))
        {
            LOG_G_DEBUG_NODE(
                L"ui.glass",
                L"paint_paced",
                L"path=legacy elapsedMs=%.2f renderMs=%.2f backgroundMs=%.2f contentMs=%.2f transitionMs=%.2f endDrawMs=%.2f size=%.0fx%.0f scale=%.2f windowMode=%d(%s) hwnd=%p",
                elapsedMs,
                renderMs,
                backgroundMs,
                contentMs,
                transitionMs,
                endDrawMs,
                w,
                h,
                scale,
                windowMode,
                WindowModeName(windowMode),
                m_hWnd);
        }
    }
    if (hr == D2DERR_RECREATE_TARGET)
    {
        LOG_G_WARNING_NODE(L"ui.glass", L"paint_recreate_target", L"path=legacy hr=0x%08X hwnd=%p", hr, m_hWnd);
        m_consecutiveDeviceLossCount++;
        if (m_consecutiveDeviceLossCount > 3)
        {
            LOG_G_ERROR_NODE(L"ui.glass", L"paint_device_loss_loop", L"Device loss loop detected on legacy path, writing marker and falling back");
            {
                const std::wstring markerPath = ConfigPath::GetGpuCrashMarkerPath();
                ConfigPath::EnsureDirectoryExists(ConfigPath::GetUserConfigDirectory());
                HANDLE hMarker = CreateFileW(markerPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (hMarker != INVALID_HANDLE_VALUE) CloseHandle(hMarker);
            }
            UIStyle::Performance::SetHardwareAccelerationEnabled(false);
            if (m_appCtx && m_appCtx->configService) m_appCtx->configService->SetHardwareAccelerationEnabled(false);
        }
        ResetBackgroundResources(L"paint_recreate_target_legacy", true);
        EnsureD2D();
        InvalidateRect(m_hWnd, nullptr, FALSE);
    }
    else if (FAILED(hr))
    {
        static ULONGLONG s_lastLegacyEndDrawLogTick = 0;
        if (Logger::ShouldLogEvery(s_lastLegacyEndDrawLogTick, 1000))
        {
            LOG_G_ERROR_NODE(L"ui.glass", L"paint_enddraw_failed", L"path=legacy hr=0x%08X hwnd=%p", hr, m_hWnd);
        }
    }
    else
    {
        m_consecutiveDeviceLossCount = 0;
    }
}

LRESULT GlassWindow::HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_CAPTURECHANGED)
    {
        MouseCaptureController::OnCaptureChanged(hWnd, reinterpret_cast<HWND>(lParam));
    }
    else if (uMsg == WM_ACTIVATE && LOWORD(wParam) == WA_INACTIVE &&
             MouseCaptureController::CurrentOwner() == hWnd &&
             MouseCaptureController::CurrentMode() == MouseCaptureController::Mode::Gesture)
    {
        MouseCaptureController::Release(hWnd, L"gesture_deactivated");
    }
    else if (uMsg == WM_CANCELMODE)
    {
        if (MouseCaptureController::CurrentOwner() == hWnd)
            MouseCaptureController::Release(hWnd, L"cancel_mode");
    }
    else if ((uMsg == WM_SHOWWINDOW && !wParam) || uMsg == WM_NCDESTROY)
    {
        if (MouseCaptureController::CurrentOwner() == hWnd)
            MouseCaptureController::Release(hWnd, uMsg == WM_NCDESTROY ? L"window_destroyed" : L"window_hidden");
    }

    switch (uMsg)
    {
    case WM_NCCALCSIZE:
        if (wParam == TRUE) return 0;
        break;

    case WM_ACTIVATE:
        // Activation changes can reorder a top-level window even though it remains
        // visible. Let the default procedure finish that reorder first, then put
        // the shadow directly behind the window for both active and inactive states.
        // This keeps unfocused secondary windows from losing their shadow again.
        {
            LRESULT activationResult = DefWindowProcW(hWnd, uMsg, wParam, lParam);
            if (m_shadowWindow)
            {
                m_shadowWindow->SyncPosition(!m_revealFirstFrameBarrier && IsWindowVisible(hWnd) && !IsIconic(hWnd));
            }
            return activationResult;
        }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps{};
        BeginPaint(hWnd, &ps);
        DoPaint();
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT)
        {
            SetCursor(LoadCursor(nullptr, IDC_ARROW));
            return TRUE;
        }
        break;

    case WM_ENTERSIZEMOVE:
        return 0;

    case WM_WINDOWPOSCHANGED:
    {
        WINDOWPOS* wp = (WINDOWPOS*)lParam;
        if (wp)
        {
            RECT cr;
            GetClientRect(hWnd, &cr);
            bool sizeChanged = !(wp->flags & SWP_NOSIZE);
            bool posChanged  = !(wp->flags & SWP_NOMOVE);
            bool isVisible = (wp->flags & SWP_HIDEWINDOW) ? false : ((wp->flags & SWP_SHOWWINDOW) ? true : (IsWindowVisible(hWnd) && !IsIconic(hWnd)));

            if (sizeChanged && m_rt)
            {
                HRESULT resizeHr = m_rt->Resize(D2D1::SizeU(cr.right, cr.bottom));
                if (FAILED(resizeHr))
                {
                    LOG_G_ERROR_NODE(
                        L"ui.glass",
                        L"render_target_resize_failed",
                        L"hr=0x%08X size=%dx%d flags=0x%X hwnd=%p",
                        resizeHr,
                        cr.right,
                        cr.bottom,
                        wp->flags,
                        hWnd);
                }
                else
                {
                    LOG_G_INFO_NODE(
                        L"ui.glass",
                        L"window_resized",
                        L"clientSize=%dx%d flags=0x%X hwnd=%p",
                        cr.right,
                        cr.bottom,
                        wp->flags,
                        hWnd);
                }
                if (m_compositor)
                    m_compositor->OnResize(m_rt->GetSize());
            }
            if (sizeChanged || posChanged)
            {
                UpdateWindowCornerRadius();
                UpdateWindowRoundRegion();

                if (m_rt)
                {
                    float effectiveDpi = GetWindowScale(hWnd) * 96.0f;
                    FLOAT currentDpiX = 96.0f;
                    FLOAT currentDpiY = 96.0f;
                    m_rt->GetDpi(&currentDpiX, &currentDpiY);
                    if (fabsf(currentDpiX - effectiveDpi) > 0.5f || fabsf(currentDpiY - effectiveDpi) > 0.5f)
                    {
                        m_rt->SetDpi(effectiveDpi, effectiveDpi);
                        UIStyle::Typography::ApplyRenderTargetTextDefaults(m_rt.Get());
                        LOG_G_INFO_NODE(
                            L"ui.glass",
                            L"render_target_dpi_adjusted",
                            L"reason=windowpos oldDpi=%.1fx%.1f newDpi=%.1f flags=0x%X hwnd=%p",
                            currentDpiX,
                            currentDpiY,
                            effectiveDpi,
                            wp->flags,
                            hWnd);
                        ResetBackgroundResources(L"windowpos_dpi_changed", false);
                        if (m_compositor)
                            m_compositor->MarkAllDirty();
                    }
                    MarkBackgroundDirty(L"windowpos_changed", false);
                }
            }

            // Sync shadow window
            if (isVisible && !m_revealFirstFrameBarrier)
            {
                if (sizeChanged || !m_shadowWindow)
                    EnsureShadowForCurrentBounds();
                else if (m_shadowWindow)
                    m_shadowWindow->SyncPosition(true);
            }
            else if (m_shadowWindow && m_animState != AnimState::Closing)
            {
                HideShadowNow();
            }

            if (sizeChanged || posChanged || (wp->flags & (SWP_SHOWWINDOW | SWP_HIDEWINDOW)))
            {
                InvalidateRect(hWnd, nullptr, FALSE);
            }
        }
        return 0;
    }

    case WM_EXITSIZEMOVE:
        MarkBackgroundDirty(L"exit_size_move", true);
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;

    case WM_DWMCOMPOSITIONCHANGED:
        LOG_G_INFO_NODE(L"ui.glass", L"dwm_composition_changed", L"hwnd=%p", hWnd);
        ApplySystemBackdrop();
        ResetBackgroundResources(L"dwm_composition_changed", false);
        InvalidateRect(hWnd, nullptr, TRUE);
        return 0;

    case WM_SHOWWINDOW:
        if (wParam)
        {
            bool hadPreparedOpenFrame = m_openTransitionPrepared;
            if (!hadPreparedOpenFrame)
            {
                MarkBackgroundDirty(L"window_show", true);
            }
            if (m_bgRefreshMs > 0)
                SetTimer(hWnd, 0x888, m_bgRefreshMs, nullptr);

            if (m_revealFirstFrameBarrier)
            {
                HideShadowNow();
            }
            else if (UIStyle::Animation::IsEnabled() && m_animState != AnimState::Opening)
            {
                StartOpenTransition();
            }
            else if (!UIStyle::Animation::IsEnabled())
            {
                EnsureShadowForCurrentBounds(m_revealFirstFrameBarrier ? 0.0f : -1.0f);
                m_animState = AnimState::None;
                if (!m_revealFirstFrameBarrier)
                {
                    ApplyVisibilityFrame(1.0f, 1.0f);
                }
            }
            else
            {
                EnsureShadowForCurrentBounds(0.0f);
            }
            m_openTransitionPrepared = false;
        }
        else
        {
            m_revealRetryPending = false;
            KillTimer(hWnd, AppMessages::GlassRevealRetryTimerId);
            LOG_G_INFO_NODE(L"ui.glass", L"window_hidden", L"hwnd=%p", hWnd);
            KillTimer(hWnd, 0x888);
            if (m_shadowWindow && m_animState != AnimState::Closing)
            {
                HideShadowNow();
            }
        }
        break;

    case WM_TIMER:
        if (wParam == AppMessages::GlassRevealRetryTimerId)
        {
            KillTimer(hWnd, AppMessages::GlassRevealRetryTimerId);
            if (m_revealRetryPending) RevealAfterFirstPaint(m_revealShowCommand, false);
            return 0;
        }

        if (wParam == 0x888)
        {
            MarkBackgroundDirty(L"background_refresh_timer", false);
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        }
        else if (wParam == 0x889)
        {
            bool animating = false;

            if (m_animState != AnimState::None)
            {
                animating = true;
                float duration = GetVisibilityAnimationDurationMs(m_animState);
                if (duration <= 0.0f) duration = 1.0f;
                float elapsed = (float)(GetTickCount64() - m_animStartTime);
                m_animProgress = elapsed / duration;
                if (m_animProgress >= 1.0f)
                {
                    m_animProgress = 1.0f;
                    AnimState oldState = m_animState;
                    m_animState = AnimState::None;

                    if (oldState == AnimState::Opening)
                    {
                        ApplyVisibilityFrame(1.0f, 1.0f);
                        OnVisibilityTransitionCompleted(oldState);
                    }
                    else if (oldState == AnimState::Closing)
                    {
                        ApplyVisibilityFrame(0.0f, 1.0f);
                        HideShadowNow();
                        if (m_animOnComplete)
                        {
                            auto cb = m_animOnComplete;
                            m_animOnComplete = nullptr;
                            cb();
                            if (!IsWindow(hWnd))
                            {
                                return 0;
                            }
                        }
                    }
                }
                else
                {
                    BYTE alpha = 255;
                    float animScale = 1.0f;
                    if (m_animState == AnimState::Opening)
                    {
                        m_animProgress = (std::max)(0.0f, (std::min)(1.0f, m_animProgress));
                        alpha = (BYTE)(EaseOutCubic(m_animProgress) * 255.0f);
                        animScale = GetAnimationScale(m_animProgress, m_animState);
                    }
                    else if (m_animState == AnimState::Closing)
                    {
                        m_animProgress = (std::max)(0.0f, (std::min)(1.0f, m_animProgress));
                        alpha = (BYTE)(m_closeStartOpacity * (1.0f - EaseInCubic(m_animProgress)) * 255.0f);
                        animScale = GetAnimationScale(m_animProgress, m_animState);
                    }
                    ApplyVisibilityFrame((float)alpha / 255.0f, animScale);
                }
            }

            if (m_themeTransition.IsActive())
            {
                animating = true;
                float duration = UIStyle::Animation::GetDurationMs();
                m_themeTransition.Step(duration, [this]() {
                    ApplySystemBackdrop();
                });
                m_brushCache.clear();
                if (m_compositor) m_compositor->MarkAllDirty();
                m_bgCompositeDirty = true;
            }

            if (!animating)
            {
                KillTimer(hWnd, 0x889);
            }

            InvalidateRect(hWnd, nullptr, FALSE);
            UpdateWindow(hWnd);
            return 0;
        }
        break;

    case WM_DPICHANGED:
    {
        float oldScale = 1.0f;
        if (m_rt)
        {
            float dpiX = 96.0f, dpiY = 96.0f;
            m_rt->GetDpi(&dpiX, &dpiY);
            oldScale = dpiX / 96.0f;
        }
        else
        {
            oldScale = GetWindowScale(hWnd);
        }

        float newSystemScale = LOWORD(wParam) / 96.0f;
        float newScale = UIStyle::Scaling::EffectiveScaleFactor(newSystemScale);
        float newDpiX = newScale * 96.0f;
        float newDpiY = newDpiX;

        if (m_appCtx && m_appCtx->logger)
            LOG_INFO_NODE(
                m_appCtx->logger,
                L"ui.glass",
                L"wm_dpi_changed",
                L"oldScale=%.2f newScale=%.2f newSystemScale=%.2f suggestedRect=%p hwnd=%p",
                oldScale,
                newScale,
                newSystemScale,
                (void*)lParam,
                hWnd);

        if (m_rt)
        {
            m_rt->SetDpi(newDpiX, newDpiY);
            UIStyle::Typography::ApplyRenderTargetTextDefaults(m_rt.Get());
            ResetBackgroundResources(L"wm_dpi_changed", false);
            if (m_compositor)
                m_compositor->MarkAllDirty();
        }

        RECT* const prcNewWindow = (RECT*)lParam;
        if (prcNewWindow)
        {
            int newW = prcNewWindow->right - prcNewWindow->left;
            int newH = prcNewWindow->bottom - prcNewWindow->top;

            if (ShouldAutoResizeOnDpiChange())
            {
                RECT wr{};
                GetWindowRect(hWnd, &wr);
                int currentW = wr.right - wr.left;
                int currentH = wr.bottom - wr.top;

                newW = (int)std::round(currentW * (newScale / oldScale));
                newH = (int)std::round(currentH * (newScale / oldScale));

                prcNewWindow->right = prcNewWindow->left + newW;
                prcNewWindow->bottom = prcNewWindow->top + newH;
            }

            SetWindowPos(hWnd, nullptr,
                prcNewWindow->left, prcNewWindow->top,
                newW, newH,
                SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    }

    case WM_CLOSE:
        if (UIStyle::Animation::IsEnabled() && m_animState != AnimState::Closing)
        {
            StartCloseTransition([hWnd]() {
                DestroyWindow(hWnd);
            });
            return 0;
        }
        break;

    case WM_DESTROY:
        LOG_G_INFO_NODE(L"ui.glass", L"window_destroy", L"hwnd=%p", hWnd);
        KillTimer(hWnd, 0x888);
        KillTimer(hWnd, 0x889);
        KillTimer(hWnd, AppMessages::GlassRevealRetryTimerId);
        if (m_shadowWindow)
        {
            m_shadowWindow->Destroy();
            m_shadowWindow.reset();
        }
        ReleaseD2D();
        return 0;
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

bool GlassWindow::EnsureShadowForCurrentBounds(float initialOpacity)
{
    if (!m_hWnd || !IsWindow(m_hWnd))
        return false;

    if (!m_shadowWindow)
    {
        m_shadowWindow = std::make_unique<ShadowWindow>(m_hWnd);
        m_shadowWindow->SetSettings(GetShadowSettings());
    }
    if (m_revealFirstFrameBarrier) initialOpacity = 0.0f;
    if (initialOpacity >= 0.0f)
    {
        m_shadowWindow->SetOpacity((std::max)(0.0f, (std::min)(1.0f, initialOpacity)));
    }

    RECT wr{};
    GetWindowRect(m_hWnd, &wr);
    int mainW = wr.right - wr.left;
    int mainH = wr.bottom - wr.top;
    if (mainW <= 0 || mainH <= 0)
        return false;

    UpdateWindowCornerRadius();
    UpdateWindowRoundRegion();

    float scale = GetWindowScale(m_hWnd);
    float physicalRadius = GetPhysicalCornerRadius();
    m_shadowWindow->UpdateShadow(mainW, mainH, physicalRadius, scale);
    return true;
}

void GlassWindow::ApplyVisibilityFrame(float opacity, float animScale)
{
    if (!m_hWnd || !IsWindow(m_hWnd))
        return;

    opacity = (std::max)(0.0f, (std::min)(1.0f, opacity));
    m_visibilityOpacity = opacity;
    BYTE alpha = (BYTE)(opacity * 255.0f + 0.5f);

    LONG_PTR exStyle = GetWindowLongPtr(m_hWnd, GWL_EXSTYLE);
    if (!(exStyle & WS_EX_LAYERED))
    {
        SetWindowLongPtr(m_hWnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
    }
    SetLayeredWindowAttributes(m_hWnd, 0, alpha, LWA_ALPHA);

    if (m_shadowWindow)
    {
        float scale = GetWindowScale(m_hWnd);
        POINT ptCenter = { (LONG)(m_animCenter.x * scale + 0.5f), (LONG)(m_animCenter.y * scale + 0.5f) };
        m_shadowWindow->SetOpacityAndScale(m_revealFirstFrameBarrier ? 0.0f : opacity, animScale, ptCenter);
    }
}

void GlassWindow::HideShadowNow()
{
    if (!m_shadowWindow)
        return;

    m_shadowWindow->SetOpacityAndScale(0.0f, 1.0f, POINT{ 0, 0 });
    m_shadowWindow->SyncPosition(false);
}

void GlassWindow::PrepareOpenTransitionFrame(bool fromWindowCenter)
{
    if (!m_hWnd || !IsWindow(m_hWnd) || !UIStyle::Animation::IsEnabled())
        return;

    KillTimer(m_hWnd, 0x889);
    m_animState = AnimState::Opening;
    m_animProgress = 0.0f;
    SetAnimationCenter(fromWindowCenter);
    EnsureShadowForCurrentBounds(0.0f);
    ApplyVisibilityFrame(0.0f, GetAnimationScale(0.0f, AnimState::Opening));
    m_openTransitionPrepared = true;
}

float GlassWindow::GetVisibilityAnimationDurationMs(AnimState) const
{
    return UIStyle::Animation::GetDurationMs();
}

void GlassWindow::CancelVisibilityTransitionForShow()
{
    if (!m_hWnd || !IsWindow(m_hWnd))
        return;

    m_revealRetryPending = false;
    m_revealFirstFrameBarrier = false;
    KillTimer(m_hWnd, AppMessages::GlassRevealRetryTimerId);
    KillTimer(m_hWnd, 0x889);
    m_animState = AnimState::None;
    m_animProgress = 0.0f;
    m_animOnComplete = nullptr;
    m_openTransitionPrepared = false;

    if (IsWindowVisible(m_hWnd))
    {
        EnsureShadowForCurrentBounds(1.0f);
        ApplyVisibilityFrame(1.0f, 1.0f);
    }
    else
    {
        HideShadowNow();
    }
}

void GlassWindow::RevealAfterFirstPaint(int showCommand, bool refreshBackground)
{
    if (!m_hWnd || !IsWindow(m_hWnd))
        return;

    if (!m_revealRetryPending) m_revealRequestStart = GetTickCount64();

    // Every GlassWindow surface, including a retained hidden instance, must
    // rebuild the material before it can opt out of WM_SHOWWINDOW's normal
    // invalidation via m_openTransitionPrepared.  Keeping this in the base
    // class prevents individual dialogs from exposing a stale/empty frame.
    EnsureD2D();
    if (refreshBackground)
    {
        MarkBackgroundDirty(L"prepared_reveal", false);
        RefreshBackgroundCache();
    }

    const bool animationEnabled = UIStyle::Animation::IsEnabled();
    m_revealFirstFrameBarrier = true;
    m_revealShowCommand = showCommand;
    m_revealRetryPending = false;
    HideShadowNow();
    if (animationEnabled) PrepareOpenTransitionFrame();
    else m_openTransitionPrepared = true;
    DoPaint();
    if (!m_lastPaintSucceeded)
    {
        m_revealRetryPending = true;
        SetTimer(m_hWnd, AppMessages::GlassRevealRetryTimerId, 32, nullptr);
        return;
    }
    ShowWindow(m_hWnd, showCommand);
    if (animationEnabled)
        RedrawWindow(m_hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    if (!m_lastPaintSucceeded)
    {
        ShowWindow(m_hWnd, SW_HIDE);
        HideShadowNow();
        m_revealRetryPending = true;
        SetTimer(m_hWnd, AppMessages::GlassRevealRetryTimerId, 32, nullptr);
        return;
    }
    m_revealFirstFrameBarrier = false;
    LOG_G_DEBUG_NODE(L"ui.glass", L"reveal_ready", L"elapsed_ms=%llu hwnd=%p",
                     GetTickCount64() - m_revealRequestStart, m_hWnd);
    if (animationEnabled) StartOpenTransition();
    else
    {
        EnsureShadowForCurrentBounds(0.0f);
        ApplyVisibilityFrame(1.0f, 1.0f);
    }
}

void GlassWindow::StartOpenTransition(bool fromWindowCenter)
{
    if (!UIStyle::Animation::IsEnabled()) return;

    m_animState = AnimState::Opening;
    m_animProgress = 0.0f;
    m_animStartTime = GetTickCount64();
    m_animOnComplete = nullptr;
    m_closeStartOpacity = 1.0f;

    if (!m_openTransitionPrepared)
    {
        SetAnimationCenter(fromWindowCenter);
        EnsureShadowForCurrentBounds(0.0f);
    }

    ApplyVisibilityFrame(0.0f, GetAnimationScale(0.0f, AnimState::Opening));

    SetTimer(m_hWnd, 0x889, 10, nullptr);
}

void GlassWindow::StartCloseTransition(std::function<void()> onComplete, bool fromWindowCenter)
{
    m_revealRetryPending = false;
    m_revealFirstFrameBarrier = false;
    KillTimer(m_hWnd, AppMessages::GlassRevealRetryTimerId);
    if (!IsWindowVisible(m_hWnd))
    {
        HideShadowNow();
        if (onComplete) onComplete();
        return;
    }

    if (!UIStyle::Animation::IsEnabled())
    {
        if (onComplete) onComplete();
        return;
    }

    m_animState = AnimState::Closing;
    m_animProgress = 0.0f;
    m_animStartTime = GetTickCount64();
    m_animOnComplete = onComplete;
    m_closeStartOpacity = (std::max)(0.0f, (std::min)(1.0f, m_visibilityOpacity));

    SetAnimationCenter(fromWindowCenter);
    EnsureShadowForCurrentBounds(m_closeStartOpacity);

    ApplyVisibilityFrame(m_closeStartOpacity, GetAnimationScale(0.0f, AnimState::Closing));

    SetTimer(m_hWnd, 0x889, 10, nullptr);
}

void GlassWindow::HideImmediately()
{
    if (!m_hWnd)
        return;

    // A normal ShowWindow(SW_HIDE) while an opening transition is still
    // active leaves its timer and opacity state alive. Stop that transition
    // first so the next popup show always starts from a clean first frame.
    m_revealRetryPending = false;
    m_revealFirstFrameBarrier = false;
    KillTimer(m_hWnd, AppMessages::GlassRevealRetryTimerId);
    KillTimer(m_hWnd, 0x889);
    m_animState = AnimState::None;
    m_animProgress = 0.0f;
    m_animOnComplete = nullptr;
    ApplyVisibilityFrame(0.0f, 1.0f);
    HideShadowNow();
    ShowWindow(m_hWnd, SW_HIDE);
}

void GlassWindow::SetAnimationCenter(bool fromWindowCenter)
{
    RECT cr;
    GetClientRect(m_hWnd, &cr);
    float scale = GetWindowScale(m_hWnd);
    float w = (float)cr.right / scale;
    float h = (float)cr.bottom / scale;

    if (fromWindowCenter)
    {
        m_animCenter = D2D1::Point2F(w * 0.5f, h * 0.5f);
        return;
    }

    POINT cursorPt;
    GetCursorPos(&cursorPt);
    ScreenToClient(m_hWnd, &cursorPt);
    float cx = (float)cursorPt.x / scale;
    float cy = (float)cursorPt.y / scale;
    cx = (std::max)(0.0f, (std::min)(w, cx));
    cy = (std::max)(0.0f, (std::min)(h, cy));
    m_animCenter = D2D1::Point2F(cx, cy);
}

float GlassWindow::GetAnimationScale(float progress, AnimState state)
{
    (void)progress;
    (void)state;
    return 1.0f;
}

void GlassWindow::GetAnimationTransform(float w, float h, float progress, AnimState state, D2D1_MATRIX_3X2_F& transform)
{
    (void)w;
    (void)h;
    (void)progress;
    (void)state;
    transform = D2D1::Matrix3x2F::Identity();
}

void GlassWindow::DrawThemeTransitionOverlay(ID2D1HwndRenderTarget* rt, float w, float h)
{
    m_themeTransition.DrawOverlay(rt, w, h);
}

void GlassWindow::StartThemeTransition(POINT clickPt)
{
    EnsureD2D();
    if (m_themeTransition.Start(m_hWnd, m_rt.Get(), clickPt))
    {
        m_bgCompositeDirty = true;
        if (m_compositor) m_compositor->MarkAllDirty();
        m_brushCache.clear();
        SetTimer(m_hWnd, 0x889, 10, nullptr);
    }
}
