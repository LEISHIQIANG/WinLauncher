#define NOMINMAX
#include "GlassWindow.h"
#include "DpiHelper.h"
#include "App/Logger.h"
#include "Config/UIStyle.h"
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

static bool IsWindows11OrLater();
static float EaseOutCubic(float t);
static float EaseInCubic(float t);
static double PerfNowMs();
static float ClampCornerRadius(float radius, float width, float height);

using SWCAFn = BOOL(WINAPI*)(HWND, void*);
struct AccentPolicy { int s, f; unsigned int g; int a; };
struct WinCompAttr { int attr; void* data; size_t size; };

static void SetAccent(HWND hwnd, int accentState, unsigned int gradientColor)
{
    auto u = GetModuleHandleW(L"user32.dll");
    if (!u) return;
    auto fn = (SWCAFn)GetProcAddress(u, "SetWindowCompositionAttribute");
    if (!fn) return;
    AccentPolicy ap{ accentState, 2, gradientColor, 0 };
    WinCompAttr d{ 19, &ap, sizeof(ap) };
    fn(hwnd, &d);
}

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
    m_cornerRadius = IsWindows11OrLater() ? 8.0f : 0.0f;
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

static bool IsWindows11OrLater()
{
    auto ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return false;
    auto fn = (LONG(WINAPI*)(void*))GetProcAddress(ntdll, "RtlGetVersion");
    if (!fn) return false;
    OSVERSIONINFOW vi = { sizeof(vi) };
    if (fn(&vi) != 0) return false;
    return vi.dwBuildNumber >= 22000;
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
    if (elapsedMs < thresholdMs)
        return false;
    ULONGLONG now = GetTickCount64();
    if (now - lastLogTick < 1000)
        return false;
    lastLogTick = now;
    return true;
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
    m_cornerRadius = IsWindows11OrLater() ? 8.0f : 0.0f;
}

float GlassWindow::GetDrawCornerRadius(float renderScale, float width, float height) const
{
    float systemScale = GetSystemWindowScale(m_hWnd);
    if (renderScale <= 0.0f)
        renderScale = systemScale > 0.0f ? systemScale : 1.0f;

    float radius = m_cornerRadius * (systemScale / renderScale);
    return ClampCornerRadius(radius, width, height);
}

float GlassWindow::GetPhysicalCornerRadius() const
{
    return ClampCornerRadius(m_cornerRadius * GetSystemWindowScale(m_hWnd), 0.0f, 0.0f);
}

void GlassWindow::UpdateWindowRoundRegion()
{
    if (!m_hWnd)
        return;

    RECT cr{};
    if (!GetClientRect(m_hWnd, &cr))
        return;

    int width = cr.right - cr.left;
    int height = cr.bottom - cr.top;
    if (width <= 0 || height <= 0)
        return;

    if (m_cornerRadius <= 0.0f)
    {
        SetWindowRgn(m_hWnd, nullptr, TRUE);
        return;
    }

    if (IsWindows11OrLater())
    {
        SetWindowRgn(m_hWnd, nullptr, TRUE);
        return;
    }

    int radiusPx = (int)std::round(GetPhysicalCornerRadius());
    if (radiusPx < 1) radiusPx = 1;

    HRGN region = CreateRoundRectRgn(0, 0, width + 1, height + 1, radiusPx * 2, radiusPx * 2);
    if (region && SetWindowRgn(m_hWnd, region, TRUE) == 0)
    {
        DeleteObject(region);
    }
}

void GlassWindow::ApplySystemBackdrop()
{
    MARGINS m{ -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(m_hWnd, &m);

    UpdateWindowCornerRadius();
    if (m_cornerRadius > 0.0f)
    {
        DWORD corner = 2;
        DwmSetWindowAttribute(m_hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    }
    UpdateWindowRoundRegion();

    // Dynamically update the display affinity for the window and its shadow window
    SetWindowDisplayAffinitySafe(m_hWnd);
    if (m_shadowWindow)
    {
        SetWindowDisplayAffinitySafe(m_shadowWindow->GetHWND());
    }

    DWORD border = 0xFFFFFFFE;
    DwmSetWindowAttribute(m_hWnd, 34, &border, sizeof(border));

    int windowMode = 0;
    if (m_appCtx && m_appCtx->configService)
    {
        windowMode = m_appCtx->configService->GetWindowMode();
    }

    if (windowMode == 1) // Acrylic
    {
        bool isLight = (UIStyle::GetThemeMode() == UIStyle::ThemeMode::Light);
        auto& cfg = isLight ? UIStyle::g_AcrylicLightConfig : UIStyle::g_AcrylicDarkConfig;
        
        D2D1_COLOR_F rgb = UIStyle::HslToRgb(cfg.hue, 0.0f, cfg.brightness, cfg.opacity);
        
        BYTE r = (BYTE)(fminf(fmaxf(rgb.r, 0.0f), 1.0f) * 255.0f);
        BYTE g = (BYTE)(fminf(fmaxf(rgb.g, 0.0f), 1.0f) * 255.0f);
        BYTE b = (BYTE)(fminf(fmaxf(rgb.b, 0.0f), 1.0f) * 255.0f);
        BYTE a = (BYTE)(cfg.opacity * 255.0f);
        
        unsigned int gradientColor = (a << 24) | (b << 16) | (g << 8) | r;
        SetAccent(m_hWnd, 4, gradientColor);
    }
    else // Glass (custom blur)
    {
        int backdropType = 1; // DWMSBT_DISABLE
        for (int attr : {38, 1029})
        {
            DwmSetWindowAttribute(m_hWnd, attr, &backdropType, sizeof(backdropType));
        }
        SetAccent(m_hWnd, 2, 0x00000000);
    }
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
        D2D1CreateFactory(factoryType, IID_PPV_ARGS(&m_d2d));
    }
    if (!m_dw)
    {
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), &m_dw);
        if (m_dw && !m_tf)
        {
            UIStyle::Typography::CreateTextFormat(
                m_dw.Get(),
                &m_tf,
                11.0f,
                DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_TEXT_ALIGNMENT_CENTER,
                DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            if (m_tf)
            {
            }
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
        if (m_appCtx && m_appCtx->logger)
            LOG_ERROR(m_appCtx->logger, L"Create hardware HwndRenderTarget failed: 0x%08X, falling back to default", hr);
        hr = m_d2d->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED)),
            D2D1::HwndRenderTargetProperties(m_hWnd, D2D1::SizeU(cr.right, cr.bottom)),
            &m_rt);
    }
    if (FAILED(hr))
    {
        if (m_appCtx && m_appCtx->logger)
            LOG_ERROR(m_appCtx->logger, L"CreateHwndRenderTarget failed: 0x%08X", hr);
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

    return true;
}

void GlassWindow::ReleaseD2D()
{
    m_brushCache.clear();
    m_bgCap.Reset();
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
    m_tf.Reset();
    m_dw.Reset();
    m_rt.Reset();
    m_d2d.Reset();
    m_pixbuf.clear();
}

void GlassWindow::UpdateTheme()
{
    if (m_d2d && m_d2dHardwareAccelerationEnabled != UIStyle::Performance::IsHardwareAccelerationEnabled())
    {
        ReleaseD2D();
    }
    m_brushCache.clear();
    m_sheenBlurEffect.Reset();
    m_sheenGsc.Reset();
    m_sheenBrush.Reset();
    m_sheenLayerRt.Reset();
    m_sheenLayerBitmap.Reset();
    if (m_themeTransitionActive)
        m_pendingBackdropUpdate = true;
    else
    {
        int windowMode = 0;
        if (m_appCtx && m_appCtx->configService)
        {
            windowMode = m_appCtx->configService->GetWindowMode();
        }
        if (windowMode == 1)
        {
            ApplySystemBackdrop();
        }
    }
    m_bgCompositeDirty = true;
    if (m_compositor)
    {
        m_compositor->MarkAllDirty();
    }
    if (m_hWnd)
    {
        InvalidateRect(m_hWnd, nullptr, TRUE);
    }
}

void GlassWindow::CaptureBackground()
{
    if (!m_rt) return;

    double captureStartMs = PerfNowMs();
    D2D1_SIZE_U pixelSize = m_rt->GetPixelSize();
    POINT clientOrigin{ 0, 0 };
    ClientToScreen(m_hWnd, &clientOrigin);
    int w = (int)pixelSize.width;
    int h = (int)pixelSize.height;
    if (w <= 0 || h <= 0) return;

    HDC sdc = GetDC(nullptr);
    HDC mdc = CreateCompatibleDC(sdc);
    HBITMAP bmp = CreateCompatibleBitmap(sdc, w, h);
    SelectObject(mdc, bmp);
    BitBlt(mdc, 0, 0, w, h, sdc, clientOrigin.x, clientOrigin.y, SRCCOPY);

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    m_pixbuf.resize(w * h);
    GetDIBits(mdc, bmp, 0, h, m_pixbuf.data(), &bi, DIB_RGB_COLORS);
    for (int i = 0; i < w * h; i++)
        m_pixbuf[i] |= 0xFF000000;

    DeleteObject(bmp);
    DeleteDC(mdc);
    ReleaseDC(nullptr, sdc);

    if (m_bgCap)
    {
        D2D1_SIZE_U size = m_bgCap->GetPixelSize();
        if (size.width != (UINT32)w || size.height != (UINT32)h)
        {
            m_bgCap.Reset();
        }
        else
        {
            FLOAT bmpDpiX = 96.0f;
            FLOAT bmpDpiY = 96.0f;
            FLOAT rtDpiX = 96.0f;
            FLOAT rtDpiY = 96.0f;
            m_bgCap->GetDpi(&bmpDpiX, &bmpDpiY);
            m_rt->GetDpi(&rtDpiX, &rtDpiY);
            if (fabsf(bmpDpiX - rtDpiX) > 0.5f || fabsf(bmpDpiY - rtDpiY) > 0.5f)
            {
                m_bgCap.Reset();
            }
        }
    }

    if (m_bgCap)
    {
        m_bgCap->CopyFromMemory(nullptr, m_pixbuf.data(), w * 4);
    }
    else
    {
        float dx = 96.0f, dy = 96.0f;
        m_rt->GetDpi(&dx, &dy);
        D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dx, dy);
        m_rt->CreateBitmap(D2D1::SizeU(w, h), m_pixbuf.data(), w * 4, &props, &m_bgCap);
    }
    m_pixbuf.clear();

    double elapsedMs = PerfNowMs() - captureStartMs;
    static ULONGLONG s_lastCaptureLogTick = 0;
    if (ShouldLogPerf(s_lastCaptureLogTick, elapsedMs, 12.0))
    {
        LOG_G_WORNING(L"GlassWindow perf: CaptureBackground took %.2fms size=%dx%d hwnd=%p", elapsedMs, w, h, m_hWnd);
    }
}

void GlassWindow::CompositeBackgroundToCache()
{
    if (!m_rt || !m_bgCap) return;

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
        if (FAILED(hr)) { m_bgFinal.Reset(); return; }
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
    if (drawCornerRadius > 0.0f)
    {
        if (!m_roundedClipGeometry)
        {
            ComPtr<ID2D1Factory> roundedFactory;
            bmpRt->GetFactory(&roundedFactory);
            if (roundedFactory)
            {
                roundedFactory->CreateRoundedRectangleGeometry(
                    D2D1::RoundedRect(D2D1::RectF(0.0f, 0.0f, w, h), drawCornerRadius, drawCornerRadius),
                    &m_roundedClipGeometry);
            }
        }
        if (!m_roundedClipLayer)
            bmpRt->CreateLayer(D2D1::SizeF(w, h), &m_roundedClipLayer);

        if (m_roundedClipGeometry && m_roundedClipLayer)
        {
            bmpRt->PushLayer(
                D2D1::LayerParameters(
                    D2D1::RectF(0.0f, 0.0f, w, h),
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
            dc->CreateEffect(CLSID_D2D1GaussianBlur, &m_blurEffect);
        if (!m_satEffect)
            dc->CreateEffect(CLSID_D2D1Saturation, &m_satEffect);

        if (m_blurEffect && m_satEffect)
        {
            m_blurEffect->SetInput(0, m_bgCap.Get());
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
            dc->DrawImage(m_bgCap.Get());
        }
        dc->Release();
    }
    else
    {
        bmpRt->DrawBitmap(m_bgCap.Get(), D2D1::RectF(0, 0, w, h));
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
                    D2D1::RectF(1.0f, 1.0f, w - 1.0f, h - 1.0f),
                    drawCornerRadius, drawCornerRadius
                );
                bmpRt->FillRoundedRectangle(sheenRR, sheenBrush);
                sheenBrush->Release();
            }
            stopsSheen->Release();
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
            D2D1::RoundedRect(D2D1::RectF(0.0f, 0.0f, w, h), drawCornerRadius, drawCornerRadius),
            bgBrush);
        bgBrush->Release();
    }

    // D. Crisp Bevel Edge Borders (Part B & C of Layer 3)
    if (cfg.highlight > 0.0f)
    {
        // B. Outer thin dark border for desktop contrast
        ID2D1SolidColorBrush* outerDarkBrush = nullptr;
        float darkOpacity = 0.14f * (1.0f - cfg.brightness * 0.4f);
        bmpRt->CreateSolidColorBrush(D2D1::ColorF(15 / 255.0f, 23 / 255.0f, 42 / 255.0f, darkOpacity), &outerDarkBrush);
        if (outerDarkBrush)
        {
            D2D1_ROUNDED_RECT outerRR = D2D1::RoundedRect(
                D2D1::RectF(0.5f, 0.5f, w - 0.5f, h - 0.5f),
                drawCornerRadius, drawCornerRadius
            );
            bmpRt->DrawRoundedRectangle(outerRR, outerDarkBrush, 1.0f);
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

        bmpRt->CreateGradientStopCollection(stopDataInner, 2, D2D1_GAMMA_1_0, D2D1_EXTEND_MODE_CLAMP, &stopsInner);
        if (stopsInner)
        {
            bmpRt->CreateLinearGradientBrush(
                D2D1::LinearGradientBrushProperties(D2D1::Point2F(1.5f, 1.5f), D2D1::Point2F(w - 1.5f, h - 1.5f)),
                stopsInner,
                &innerBrightBrush
            );
            if (innerBrightBrush)
            {
                D2D1_ROUNDED_RECT innerRR = D2D1::RoundedRect(
                    D2D1::RectF(1.5f, 1.5f, w - 1.5f, h - 1.5f),
                    (std::max)(0.0f, drawCornerRadius - (1.0f * systemScale) / scale),
                    (std::max)(0.0f, drawCornerRadius - (1.0f * systemScale) / scale)
                );
                bmpRt->DrawRoundedRectangle(innerRR, innerBrightBrush, 1.0f);
                innerBrightBrush->Release();
            }
            stopsInner->Release();
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
    }

    double elapsedMs = PerfNowMs() - compositeStartMs;
    static ULONGLONG s_lastCompositeLogTick = 0;
    if (ShouldLogPerf(s_lastCompositeLogTick, elapsedMs, 12.0))
    {
        LOG_G_WORNING(L"GlassWindow perf: CompositeBackgroundToCache took %.2fms size=%.0fx%.0f hwnd=%p", elapsedMs, w, h, m_hWnd);
    }
}

void GlassWindow::DoPaint()
{
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
        m_compositor->Render(m_rt.Get(), scale);
        OnPaintContent(m_rt.Get());
        DrawThemeTransitionOverlay(m_rt.Get(), w, h);

        if (transformModified)
        {
            m_rt->SetTransform(originalTransform);
        }
        HRESULT hr = m_rt->EndDraw();
        double elapsedMs = PerfNowMs() - paintStartMs;
        static ULONGLONG s_lastCompositorPaintLogTick = 0;
        if (ShouldLogPerf(s_lastCompositorPaintLogTick, elapsedMs, 16.0))
        {
            LOG_G_WORNING(L"GlassWindow perf: DoPaint compositor path took %.2fms size=%.0fx%.0f hwnd=%p", elapsedMs, w, h, m_hWnd);
        }
        if (hr == D2DERR_RECREATE_TARGET)
        {
            m_bgCap.Reset(); m_bgFinal.Reset(); m_compositeRt.Reset();
            m_blurEffect.Reset(); m_satEffect.Reset(); m_sheenBlurEffect.Reset();
            m_sheenGsc.Reset(); m_sheenBrush.Reset();
            m_sheenLayerRt.Reset(); m_sheenLayerBitmap.Reset();
            m_roundedClipLayer.Reset(); m_roundedClipGeometry.Reset();
            m_effectWinSize = {};
            m_effectCornerRadius = -1.0f;
            m_rt.Reset();
            m_brushCache.clear();
            m_themeTransitionStopCollection.Reset();
            if (m_compositor) m_compositor->MarkAllDirty();
            m_bgCaptureDirty = true;
            m_bgCompositeDirty = true;
            EnsureD2D();
            InvalidateRect(m_hWnd, nullptr, FALSE);
        }
        return;
    }

    // Legacy rendering path

    int windowMode = 0;
    if (m_appCtx && m_appCtx->configService)
    {
        windowMode = m_appCtx->configService->GetWindowMode();
    }

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
            CaptureBackground();
            m_bgCaptureDirty = false;
            m_bgCompositeDirty = true;
        }

        if (m_bgCompositeDirty)
        {
            CompositeBackgroundToCache();
            m_bgCompositeDirty = false;
        }

        if (m_bgFinal)
        {
            m_rt->DrawBitmap(m_bgFinal.Get(), D2D1::RectF(0, 0, w, h));
        }
        else if (m_bgCap)
        {
            m_rt->DrawBitmap(m_bgCap.Get(), D2D1::RectF(0, 0, w, h));
        }
        else
        {
            m_rt->Clear(UIStyle::ThemeColor::WindowClear().d2d);
        }
    }

    OnPaintContent(m_rt.Get());
    DrawThemeTransitionOverlay(m_rt.Get(), w, h);

    if (transformModified)
    {
        m_rt->SetTransform(originalTransform);
    }

    HRESULT hr = m_rt->EndDraw();
    double elapsedMs = PerfNowMs() - paintStartMs;
    static ULONGLONG s_lastPaintLogTick = 0;
    if (ShouldLogPerf(s_lastPaintLogTick, elapsedMs, 16.0))
    {
        LOG_G_WORNING(L"GlassWindow perf: DoPaint legacy path took %.2fms size=%.0fx%.0f bgDirty=%d hwnd=%p", elapsedMs, w, h, (int)m_bgCaptureDirty, m_hWnd);
    }
    if (hr == D2DERR_RECREATE_TARGET)
    {
        m_bgCap.Reset(); m_bgFinal.Reset(); m_compositeRt.Reset();
        m_blurEffect.Reset(); m_satEffect.Reset(); m_sheenBlurEffect.Reset();
        m_sheenGsc.Reset(); m_sheenBrush.Reset();
        m_sheenLayerRt.Reset(); m_sheenLayerBitmap.Reset();
        m_roundedClipLayer.Reset(); m_roundedClipGeometry.Reset();
        m_effectWinSize = {};
        m_effectCornerRadius = -1.0f;
        m_rt.Reset();
        m_brushCache.clear();
        m_themeTransitionStopCollection.Reset();
        m_bgCaptureDirty = true;
        m_bgCompositeDirty = true;
        EnsureD2D();
        InvalidateRect(m_hWnd, nullptr, FALSE);
    }
}

LRESULT GlassWindow::HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_NCCALCSIZE:
        if (wParam == TRUE) return 0;
        break;

    case WM_ACTIVATE:
        if (LOWORD(wParam) != WA_INACTIVE && m_shadowWindow)
        {
            m_shadowWindow->SyncPosition(true);
        }
        break;

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
                m_rt->Resize(D2D1::SizeU(cr.right, cr.bottom));
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
                        m_bgCap.Reset();
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
                        if (m_compositor)
                            m_compositor->MarkAllDirty();
                    }
                    m_bgCaptureDirty = true;
                    m_bgCompositeDirty = true;
                }
            }

            // Sync shadow window
            if (isVisible)
            {
                if (!m_shadowWindow)
                {
                    m_shadowWindow = std::make_unique<ShadowWindow>(hWnd);
                    m_shadowWindow->SetSettings(GetShadowSettings());
                }
                
                RECT wr;
                GetWindowRect(hWnd, &wr);
                int mainW = wr.right - wr.left;
                int mainH = wr.bottom - wr.top;
                float scale = GetWindowScale(hWnd);
                float physicalRadius = GetPhysicalCornerRadius();

                if (sizeChanged)
                {
                    UpdateWindowCornerRadius();
                    m_shadowWindow->UpdateShadow(mainW, mainH, physicalRadius, scale);
                }
                else
                {
                    m_shadowWindow->SyncPosition(true);
                }
            }
            else if (m_shadowWindow)
            {
                m_shadowWindow->SyncPosition(false);
            }

            if (sizeChanged || posChanged || (wp->flags & (SWP_SHOWWINDOW | SWP_HIDEWINDOW)))
            {
                InvalidateRect(hWnd, nullptr, FALSE);
            }
        }
        return 0;
    }

    case WM_EXITSIZEMOVE:
        m_bgCaptureDirty = true;
        m_bgCompositeDirty = true;
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;

    case WM_DWMCOMPOSITIONCHANGED:
        if (m_appCtx && m_appCtx->logger)
            LOG_INFO(m_appCtx->logger, L"GlassWindow: DWM composition changed");
        ApplySystemBackdrop();
        m_bgCap.Reset();
        m_bgCaptureDirty = true;
        m_bgCompositeDirty = true;
        InvalidateRect(hWnd, nullptr, TRUE);
        return 0;

    case WM_SHOWWINDOW:
        if (wParam)
        {
            m_bgCaptureDirty = true;
            m_bgCompositeDirty = true;
            if (m_bgRefreshMs > 0)
                SetTimer(hWnd, 0x888, m_bgRefreshMs, nullptr);

            if (!m_shadowWindow)
            {
                m_shadowWindow = std::make_unique<ShadowWindow>(hWnd);
                m_shadowWindow->SetSettings(GetShadowSettings());
            }
            RECT wr;
            GetWindowRect(hWnd, &wr);
            UpdateWindowCornerRadius();
            UpdateWindowRoundRegion();
            float scale = GetWindowScale(hWnd);
            float physicalRadius = GetPhysicalCornerRadius();
            m_shadowWindow->UpdateShadow(wr.right - wr.left, wr.bottom - wr.top, physicalRadius, scale);

            if (UIStyle::Animation::IsEnabled() && m_animState != AnimState::Opening)
            {
                StartOpenTransition();
            }
            else if (!UIStyle::Animation::IsEnabled())
            {
                m_animState = AnimState::None;
                LONG_PTR exStyle = GetWindowLongPtr(hWnd, GWL_EXSTYLE);
                if (exStyle & WS_EX_LAYERED)
                {
                    SetLayeredWindowAttributes(hWnd, 0, 255, LWA_ALPHA);
                }
                if (m_shadowWindow)
                {
                    m_shadowWindow->SetOpacity(1.0f);
                }
            }
        }
        else
        {
            KillTimer(hWnd, 0x888);
            if (m_shadowWindow)
            {
                m_shadowWindow->SyncPosition(false);
            }
        }
        break;

    case WM_TIMER:
        if (wParam == 0x888)
        {
            m_bgCaptureDirty = true;
            m_bgCompositeDirty = true;
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        }
        else if (wParam == 0x889)
        {
            bool animating = false;

            if (m_animState != AnimState::None)
            {
                animating = true;
                float duration = UIStyle::Animation::GetDurationMs();
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
                        SetLayeredWindowAttributes(hWnd, 0, 255, LWA_ALPHA);
                        if (m_shadowWindow) m_shadowWindow->SetOpacity(1.0f);
                    }
                    else if (oldState == AnimState::Closing)
                    {
                        SetLayeredWindowAttributes(hWnd, 0, 0, LWA_ALPHA);
                        if (m_shadowWindow) m_shadowWindow->SetOpacity(0.0f);
                        if (m_animOnComplete)
                        {
                            auto cb = m_animOnComplete;
                            m_animOnComplete = nullptr;
                            cb();
                        }
                    }
                }
                else
                {
                    BYTE alpha = 255;
                    if (m_animState == AnimState::Opening)
                        alpha = (BYTE)(EaseOutCubic(m_animProgress) * 255.0f);
                    else if (m_animState == AnimState::Closing)
                        alpha = (BYTE)((1.0f - EaseInCubic(m_animProgress)) * 255.0f);
                    SetLayeredWindowAttributes(hWnd, 0, alpha, LWA_ALPHA);
                    if (m_shadowWindow) m_shadowWindow->SetOpacity((float)alpha / 255.0f);
                }
            }

            if (m_themeTransitionActive)
            {
                animating = true;
                float duration = UIStyle::Animation::GetDurationMs();
                if (duration <= 0.0f) duration = 1.0f;
                float elapsed = (float)(GetTickCount64() - m_themeTransitionStartTime);
                m_themeTransitionProgress = elapsed / duration;
                if (m_pendingBackdropUpdate && m_themeTransitionProgress >= 0.45f)
                {
                    ApplySystemBackdrop();
                    m_pendingBackdropUpdate = false;
                }
                if (m_themeTransitionProgress >= 1.0f)
                {
                    m_themeTransitionProgress = 1.0f;
                    m_themeTransitionActive = false;
                    m_themeTransitionOldBitmap.Reset();
                    if (m_pendingBackdropUpdate)
                    {
                        ApplySystemBackdrop();
                        m_pendingBackdropUpdate = false;
                    }
                    UIStyle::ThemeTransition::End();
                }
                else
                {
                    UIStyle::ThemeTransition::SetProgress(m_themeTransitionProgress);
                }
                m_brushCache.clear();
                if (m_compositor) m_compositor->MarkAllDirty();
                m_bgCompositeDirty = true;
            }

            if (!animating)
            {
                KillTimer(hWnd, 0x889);
            }

            InvalidateRect(hWnd, nullptr, FALSE);
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
            LOG_INFO(m_appCtx->logger, L"GlassWindow: DPI changed: oldScale = %.2f, newScale = %.2f, newSystemScale = %.2f", oldScale, newScale, newSystemScale);

        if (m_rt)
        {
            m_rt->SetDpi(newDpiX, newDpiY);
            UIStyle::Typography::ApplyRenderTargetTextDefaults(m_rt.Get());
            m_bgCap.Reset();
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
            if (m_compositor)
                m_compositor->MarkAllDirty();
            m_bgCaptureDirty = true;
            m_bgCompositeDirty = true;
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
        if (m_appCtx && m_appCtx->logger)
            LOG_INFO(m_appCtx->logger, L"GlassWindow: destroying window and releasing resources");
        KillTimer(hWnd, 0x888);
        KillTimer(hWnd, 0x889);
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

void GlassWindow::StartOpenTransition(bool fromWindowCenter)
{
    if (!UIStyle::Animation::IsEnabled()) return;

    m_animState = AnimState::Opening;
    m_animProgress = 0.0f;
    m_animStartTime = GetTickCount64();

    SetAnimationCenter(fromWindowCenter);

    LONG_PTR exStyle = GetWindowLongPtr(m_hWnd, GWL_EXSTYLE);
    if (!(exStyle & WS_EX_LAYERED))
    {
        SetWindowLongPtr(m_hWnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
    }
    SetLayeredWindowAttributes(m_hWnd, 0, 0, LWA_ALPHA);
    if (m_shadowWindow)
    {
        m_shadowWindow->SetOpacity(0.0f);
    }

    SetTimer(m_hWnd, 0x889, 10, nullptr);
}

void GlassWindow::StartCloseTransition(std::function<void()> onComplete, bool fromWindowCenter)
{
    if (!UIStyle::Animation::IsEnabled())
    {
        if (onComplete) onComplete();
        return;
    }

    m_animState = AnimState::Closing;
    m_animProgress = 0.0f;
    m_animStartTime = GetTickCount64();
    m_animOnComplete = onComplete;

    SetAnimationCenter(fromWindowCenter);

    LONG_PTR exStyle = GetWindowLongPtr(m_hWnd, GWL_EXSTYLE);
    if (!(exStyle & WS_EX_LAYERED))
    {
        SetWindowLongPtr(m_hWnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
    }

    SetTimer(m_hWnd, 0x889, 10, nullptr);
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

void GlassWindow::GetAnimationTransform(float w, float h, float progress, AnimState state, D2D1_MATRIX_3X2_F& transform)
{
    float scale = 1.0f;
    if (state == AnimState::Opening)
    {
        scale = 0.90f + 0.10f * EaseOutCubic(progress); // Apple-style 90% to 100% pop
    }
    else if (state == AnimState::Closing)
    {
        scale = 1.0f - 0.10f * EaseInCubic(progress); // Apple-style 100% to 90% shrink
    }

    transform = D2D1::Matrix3x2F::Scale(
        scale, scale,
        m_animCenter
    );
}

void GlassWindow::CaptureTransitionSnapshot()
{
    m_themeTransitionOldBitmap.Reset();
    if (!EnsureD2D() || !m_rt)
        return;

    m_rt->Flush();

    D2D1_SIZE_U size = m_rt->GetPixelSize();
    if (size.width == 0 || size.height == 0)
        return;

    FLOAT dpiX = 96.0f;
    FLOAT dpiY = 96.0f;
    m_rt->GetDpi(&dpiX, &dpiY);

    D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
        m_rt->GetPixelFormat(),
        dpiX,
        dpiY);

    ComPtr<ID2D1Bitmap> snapshot;
    HRESULT hr = m_rt->CreateBitmap(size, nullptr, 0, props, &snapshot);
    if (SUCCEEDED(hr) && snapshot)
    {
        hr = snapshot->CopyFromRenderTarget(nullptr, m_rt.Get(), nullptr);
        if (SUCCEEDED(hr))
            m_themeTransitionOldBitmap = snapshot;
    }
}

void GlassWindow::DrawThemeTransitionOverlay(ID2D1HwndRenderTarget* rt, float w, float h)
{
    if (!m_themeTransitionActive || !m_themeTransitionOldBitmap || !rt)
        return;

    float opacity = 1.0f - UIStyle::ThemeTransition::BlendProgress();
    if (opacity <= 0.001f)
        return;
    if (opacity > 1.0f)
        opacity = 1.0f;

    rt->DrawBitmap(
        m_themeTransitionOldBitmap.Get(),
        D2D1::RectF(0.0f, 0.0f, w, h),
        opacity,
        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}

void GlassWindow::StartThemeTransition(POINT clickPt)
{
    if (!UIStyle::Animation::IsEnabled())
    {
        m_themeTransitionActive = false;
        m_themeTransitionOldBitmap.Reset();
        UIStyle::ThemeTransition::End();
        return;
    }

    m_themeTransitionActive = true;
    m_themeTransitionProgress = 0.0f;
    m_themeTransitionStartTime = GetTickCount64();
    UIStyle::ThemeTransition::SetProgress(0.0f);
    CaptureTransitionSnapshot();

    RECT cr;
    GetClientRect(m_hWnd, &cr);
    float scale = GetWindowScale(m_hWnd);
    float w = (float)cr.right / scale;
    float h = (float)cr.bottom / scale;

    if (clickPt.x == -1 && clickPt.y == -1)
    {
        m_themeTransitionCenter = D2D1::Point2F(w / 2.0f, h / 2.0f);
    }
    else
    {
        m_themeTransitionCenter = D2D1::Point2F((float)clickPt.x, (float)clickPt.y);
    }

    m_bgCompositeDirty = true;
    if (m_compositor) m_compositor->MarkAllDirty();
    m_brushCache.clear();

    SetTimer(m_hWnd, 0x889, 10, nullptr);
}
