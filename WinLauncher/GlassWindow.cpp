#define NOMINMAX
#include "GlassWindow.h"
#include "DpiHelper.h"
#include "App/Logger.h"
#include "Config/UIStyle.h"
#include <windowsx.h>
#include <dwmapi.h>
#include <d2d1helper.h>
#include <d2d1effects.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "dwrite.lib")

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

static bool IsWindows11OrLater();

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

void GlassWindow::ApplySystemBackdrop()
{
    MARGINS m{ -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(m_hWnd, &m);

    if (IsWindows11OrLater())
    {
        DWORD corner = 2;
        DwmSetWindowAttribute(m_hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
        m_cornerRadius = 8.0f;
    }
    else
    {
        m_cornerRadius = 0.0f;
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
        
        float s = isLight ? 0.02f : 0.03f;
        D2D1_COLOR_F rgb = UIStyle::HslToRgb(cfg.hue, s, cfg.brightness, cfg.opacity);
        
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
            if (SUCCEEDED(DwmSetWindowAttribute(m_hWnd, attr, &backdropType, sizeof(backdropType))))
                break;
        }
        SetAccent(m_hWnd, 2, 0x00000000);
    }
}

bool GlassWindow::EnsureD2D()
{
    if (m_rt) return true;
    if (!m_d2d)
    {
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_PPV_ARGS(&m_d2d));
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
    HRESULT hr = m_d2d->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED)),
        D2D1::HwndRenderTargetProperties(m_hWnd, D2D1::SizeU(cr.right, cr.bottom)),
        &m_rt);
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
    m_sheenGsc.Reset();
    m_sheenBrush.Reset();
    m_effectWinSize = {};
    m_tf.Reset();
    m_dw.Reset();
    m_rt.Reset();
    m_d2d.Reset();
    m_pixbuf.clear();
}

void GlassWindow::UpdateTheme()
{
    m_brushCache.clear();
    m_sheenGsc.Reset();
    m_sheenBrush.Reset();
    ApplySystemBackdrop();
    m_bgDirty = true;
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

    RECT wr;
    GetWindowRect(m_hWnd, &wr);
    int w = wr.right - wr.left;
    int h = wr.bottom - wr.top;
    if (w <= 0 || h <= 0) return;

    HDC sdc = GetDC(nullptr);
    HDC mdc = CreateCompatibleDC(sdc);
    HBITMAP bmp = CreateCompatibleBitmap(sdc, w, h);
    SelectObject(mdc, bmp);
    BitBlt(mdc, 0, 0, w, h, sdc, wr.left, wr.top, SRCCOPY);

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
}

void GlassWindow::CompositeBackgroundToCache()
{
    if (!m_rt || !m_bgCap) return;

    RECT cr;
    GetClientRect(m_hWnd, &cr);
    float scale = GetWindowScale(m_hWnd);
    float w = (float)cr.right / scale, h = (float)cr.bottom / scale;

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
    if (sizeChanged || m_effectWinSize.width != w || m_effectWinSize.height != h)
    {
        m_blurEffect.Reset(); m_satEffect.Reset();
        m_sheenGsc.Reset(); m_sheenBrush.Reset();
        m_effectWinSize = { w, h };
    }

    ComPtr<ID2D1BitmapRenderTarget> bmpRt = m_compositeRt;
    bmpRt->BeginDraw();
    bmpRt->Clear(UIStyle::ThemeColor::WindowClear().d2d);

    // Blur + Saturation — cache and reuse the effect graph
    ID2D1DeviceContext* dc = nullptr;
    if (SUCCEEDED(bmpRt->QueryInterface(&dc)))
    {
        if (!m_blurEffect)
            dc->CreateEffect(CLSID_D2D1GaussianBlur, &m_blurEffect);
        if (!m_satEffect)
            dc->CreateEffect(CLSID_D2D1Saturation, &m_satEffect);

        auto& cfg = (UIStyle::GetThemeMode() == UIStyle::ThemeMode::Light) ? UIStyle::g_LightConfig : UIStyle::g_DarkConfig;

        if (m_blurEffect && m_satEffect)
        {
            // Only update input and parameters; effects already exist
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

    // Sheen — cache gradient collection and brush
    {
        if (!m_sheenGsc)
        {
            D2D1_GRADIENT_STOP gs[2];
            gs[0].position = 0;
            gs[0].color = UIStyle::ThemeColor::Sheen().d2d;
            gs[1].position = 1;
            gs[1].color = D2D1::ColorF(1, 1, 1, 0);
            bmpRt->CreateGradientStopCollection(gs, 2, D2D1_GAMMA_1_0, D2D1_EXTEND_MODE_CLAMP, &m_sheenGsc);
        }
        if (m_sheenGsc && !m_sheenBrush)
        {
            bmpRt->CreateRadialGradientBrush(
                D2D1::RadialGradientBrushProperties(
                    D2D1::Point2F(w * 0.1f, h * 0.1f),
                    D2D1::Point2F(0, 0),
                    w * 0.85f, h * 0.85f),
                m_sheenGsc.Get(), &m_sheenBrush);
        }
        if (m_sheenBrush)
        {
            bmpRt->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(1, 1, w - 1, h - 1), m_cornerRadius, m_cornerRadius), m_sheenBrush.Get());
        }
    }

    // Tint
    {
        auto ti = GetOrCreateBrush(UIStyle::ThemeColor::WindowTint().d2d);
        if (ti)
            bmpRt->FillRectangle(D2D1::RectF(0, 0, w, h), ti.Get());
    }

    // Border
    {
        auto bo = GetOrCreateBrush(UIStyle::ThemeColor::WindowBorder().d2d);
        if (bo)
        {
            bmpRt->DrawRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(0.5f, 0.5f, w - 0.5f, h - 0.5f), m_cornerRadius, m_cornerRadius),
                bo.Get(), 1.0f);
        }
    }

    HRESULT hr = bmpRt->EndDraw();
    if (SUCCEEDED(hr))
    {
        bmpRt->GetBitmap(&m_bgFinal);
    }
    else
    {
        m_bgFinal.Reset();
    }
}

void GlassWindow::DoPaint()
{
    if (!EnsureD2D()) return;

    RECT cr;
    GetClientRect(m_hWnd, &cr);
    float scale = GetWindowScale(m_hWnd);
    float w = (float)cr.right / scale, h = (float)cr.bottom / scale;

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

    bool themeTransitionActive = m_themeTransitionActive && (m_themeTransitionOldBitmap != nullptr);
    if (themeTransitionActive)
    {
        m_rt->DrawBitmap(m_themeTransitionOldBitmap.Get(), D2D1::RectF(0, 0, w, h));

        float dx1 = m_themeTransitionCenter.x;
        float dx2 = w - m_themeTransitionCenter.x;
        float dy1 = m_themeTransitionCenter.y;
        float dy2 = h - m_themeTransitionCenter.y;
        float maxR = (std::max)({
            sqrtf(dx1*dx1 + dy1*dy1),
            sqrtf(dx2*dx2 + dy1*dy1),
            sqrtf(dx1*dx1 + dy2*dy2),
            sqrtf(dx2*dx2 + dy2*dy2)
        });
        
        // Soft radial expansion: expand slightly beyond maxR so the feathered edge fully clears the window at 1.0 progress
        float radius = (maxR * 1.15f) * m_themeTransitionProgress;
        if (radius <= 0.0f) radius = 0.1f;

        if (!m_themeTransitionStopCollection)
        {
            D2D1_GRADIENT_STOP stops[3];
            stops[0].position = 0.0f;
            stops[0].color = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);
            stops[1].position = 0.85f;
            stops[1].color = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);
            stops[2].position = 1.0f;
            stops[2].color = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.0f);
            m_rt->CreateGradientStopCollection(stops, 3, &m_themeTransitionStopCollection);
        }

        ComPtr<ID2D1RadialGradientBrush> radialBrush;
        if (m_themeTransitionStopCollection)
        {
            m_rt->CreateRadialGradientBrush(
                D2D1::RadialGradientBrushProperties(
                    m_themeTransitionCenter,
                    D2D1::Point2F(0.0f, 0.0f),
                    radius, radius
                ),
                m_themeTransitionStopCollection.Get(),
                &radialBrush
            );
        }

        if (!m_themeTransitionLayer)
        {
            m_rt->CreateLayer(nullptr, &m_themeTransitionLayer);
        }

        m_rt->PushLayer(
            D2D1::LayerParameters(
                D2D1::InfiniteRect(),
                nullptr,
                D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                D2D1::IdentityMatrix(),
                1.0f,
                radialBrush.Get()
            ),
            m_themeTransitionLayer.Get()
        );
    }

    if (m_compositor)
    {
        m_compositor->Render(m_rt.Get(), scale);
        OnPaintContent(m_rt.Get());

        if (themeTransitionActive)
        {
            m_rt->PopLayer();
        }

        if (transformModified)
        {
            m_rt->SetTransform(originalTransform);
        }
        HRESULT hr = m_rt->EndDraw();
        if (hr == D2DERR_RECREATE_TARGET)
        {
            m_bgCap.Reset(); m_bgFinal.Reset(); m_compositeRt.Reset();
            m_blurEffect.Reset(); m_satEffect.Reset();
            m_sheenGsc.Reset(); m_sheenBrush.Reset(); m_effectWinSize = {};
            m_rt.Reset();
            m_brushCache.clear();
            m_themeTransitionStopCollection.Reset();
            if (m_compositor) m_compositor->MarkAllDirty();
            m_bgDirty = true;
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
                m_rt->DrawRoundedRectangle(
                    D2D1::RoundedRect(D2D1::RectF(0.5f, 0.5f, w - 0.5f, h - 0.5f), m_cornerRadius, m_cornerRadius),
                    bo.Get(), 1.0f);
            }
        }
    }
    else // Glass (custom blur)
    {
        if (m_bgDirty)
        {
            CaptureBackground();
            CompositeBackgroundToCache();
            m_bgDirty = false;
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

    if (themeTransitionActive)
    {
        m_rt->PopLayer();
    }

    if (transformModified)
    {
        m_rt->SetTransform(originalTransform);
    }

    HRESULT hr = m_rt->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET)
    {
        m_bgCap.Reset(); m_bgFinal.Reset(); m_compositeRt.Reset();
        m_blurEffect.Reset(); m_satEffect.Reset();
        m_sheenGsc.Reset(); m_sheenBrush.Reset(); m_effectWinSize = {};
        m_rt.Reset();
        m_brushCache.clear();
        m_themeTransitionStopCollection.Reset();
        m_bgDirty = true;
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
            bool zorderChanged = !(wp->flags & SWP_NOZORDER);
            bool isVisible = (wp->flags & SWP_HIDEWINDOW) ? false : ((wp->flags & SWP_SHOWWINDOW) ? true : (IsWindowVisible(hWnd) && !IsIconic(hWnd)));

            if (sizeChanged && m_rt)
            {
                m_rt->Resize(D2D1::SizeU(cr.right, cr.bottom));
                if (m_compositor)
                    m_compositor->OnResize(D2D1::SizeF((float)cr.right / GetWindowScale(hWnd),
                                                       (float)cr.bottom / GetWindowScale(hWnd)));
            }
            if ((sizeChanged || posChanged) && m_rt)
            {
                m_bgDirty = true;
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

                if (sizeChanged)
                {
                    m_shadowWindow->UpdateShadow(mainW, mainH, m_cornerRadius, scale);
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

            InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_EXITSIZEMOVE:
        m_bgDirty = true;
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;

    case WM_DWMCOMPOSITIONCHANGED:
        if (m_appCtx && m_appCtx->logger)
            LOG_INFO(m_appCtx->logger, L"GlassWindow: DWM composition changed");
        ApplySystemBackdrop();
        m_bgCap.Reset();
        m_bgDirty = true;
        InvalidateRect(hWnd, nullptr, TRUE);
        return 0;

    case WM_SHOWWINDOW:
        if (wParam)
        {
            m_bgDirty = true;
            if (m_bgRefreshMs > 0)
                SetTimer(hWnd, 0x888, m_bgRefreshMs, nullptr);

            if (!m_shadowWindow)
            {
                m_shadowWindow = std::make_unique<ShadowWindow>(hWnd);
                m_shadowWindow->SetSettings(GetShadowSettings());
            }
            RECT wr;
            GetWindowRect(hWnd, &wr);
            m_shadowWindow->UpdateShadow(wr.right - wr.left, wr.bottom - wr.top, m_cornerRadius, GetWindowScale(hWnd));

            if (UIStyle::Animation::IsEnabled() && m_animState != AnimState::Opening)
            {
                StartOpenTransition();
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
            m_bgDirty = true;
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
                        alpha = (BYTE)(m_animProgress * 255.0f);
                    else if (m_animState == AnimState::Closing)
                        alpha = (BYTE)((1.0f - m_animProgress) * 255.0f);
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
                if (m_themeTransitionProgress >= 1.0f)
                {
                    m_themeTransitionProgress = 1.0f;
                    m_themeTransitionActive = false;
                    m_themeTransitionOldBitmap.Reset();
                }
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
        float newDpiX = (float)LOWORD(wParam);
        float newDpiY = (float)HIWORD(wParam);
        if (m_appCtx && m_appCtx->logger)
            LOG_INFO(m_appCtx->logger, L"GlassWindow: DPI changed: newDpiX = %.2f, newDpiY = %.2f", newDpiX, newDpiY);
        if (m_rt)
        {
            m_rt->SetDpi(newDpiX, newDpiY);
            UIStyle::Typography::ApplyRenderTargetTextDefaults(m_rt.Get());
            m_bgDirty = true;
        }
        RECT* const prcNewWindow = (RECT*)lParam;
        if (prcNewWindow)
        {
            SetWindowPos(hWnd, nullptr,
                prcNewWindow->left, prcNewWindow->top,
                prcNewWindow->right - prcNewWindow->left,
                prcNewWindow->bottom - prcNewWindow->top,
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

void GlassWindow::StartOpenTransition()
{
    if (!UIStyle::Animation::IsEnabled()) return;

    m_animState = AnimState::Opening;
    m_animProgress = 0.0f;
    m_animStartTime = GetTickCount64();

    // Determine Apple-style animation center from the current mouse cursor position
    RECT cr;
    GetClientRect(m_hWnd, &cr);
    float scale = GetWindowScale(m_hWnd);
    float w = (float)cr.right / scale;
    float h = (float)cr.bottom / scale;

    POINT cursorPt;
    GetCursorPos(&cursorPt);
    ScreenToClient(m_hWnd, &cursorPt);
    float cx = (float)cursorPt.x / scale;
    float cy = (float)cursorPt.y / scale;
    cx = (std::max)(0.0f, (std::min)(w, cx));
    cy = (std::max)(0.0f, (std::min)(h, cy));
    m_animCenter = D2D1::Point2F(cx, cy);

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

void GlassWindow::StartCloseTransition(std::function<void()> onComplete)
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

    // Determine Apple-style animation center from the current mouse cursor position
    RECT cr;
    GetClientRect(m_hWnd, &cr);
    float scale = GetWindowScale(m_hWnd);
    float w = (float)cr.right / scale;
    float h = (float)cr.bottom / scale;

    POINT cursorPt;
    GetCursorPos(&cursorPt);
    ScreenToClient(m_hWnd, &cursorPt);
    float cx = (float)cursorPt.x / scale;
    float cy = (float)cursorPt.y / scale;
    cx = (std::max)(0.0f, (std::min)(w, cx));
    cy = (std::max)(0.0f, (std::min)(h, cy));
    m_animCenter = D2D1::Point2F(cx, cy);

    LONG_PTR exStyle = GetWindowLongPtr(m_hWnd, GWL_EXSTYLE);
    if (!(exStyle & WS_EX_LAYERED))
    {
        SetWindowLongPtr(m_hWnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
    }

    SetTimer(m_hWnd, 0x889, 10, nullptr);
}

void GlassWindow::GetAnimationTransform(float w, float h, float progress, AnimState state, D2D1_MATRIX_3X2_F& transform)
{
    float scale = 1.0f;
    if (state == AnimState::Opening)
    {
        scale = 0.90f + 0.10f * progress; // Apple-style 90% to 100% pop
    }
    else if (state == AnimState::Closing)
    {
        scale = 1.0f - 0.10f * progress; // Apple-style 100% to 90% shrink
    }

    transform = D2D1::Matrix3x2F::Scale(
        scale, scale,
        m_animCenter
    );
}

void GlassWindow::CaptureTransitionSnapshot()
{
    if (!EnsureD2D()) return;

    RECT cr;
    GetClientRect(m_hWnd, &cr);
    int w = cr.right - cr.left;
    int h = cr.bottom - cr.top;
    if (w <= 0 || h <= 0) return;

    m_themeTransitionOldBitmap.Reset();

    // Use GDI-based screen capture of the window's client area for maximum type-safety
    // and resource domain separation.
    HDC wdc = GetDC(m_hWnd);
    if (!wdc) return;
    HDC mdc = CreateCompatibleDC(wdc);
    HBITMAP bmp = CreateCompatibleBitmap(wdc, w, h);
    HGDIOBJ oldBmp = SelectObject(mdc, bmp);
    
    BitBlt(mdc, 0, 0, w, h, wdc, 0, 0, SRCCOPY);

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    std::vector<DWORD> pixels(w * h);
    GetDIBits(mdc, bmp, 0, h, pixels.data(), &bi, DIB_RGB_COLORS);

    SelectObject(mdc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mdc);
    ReleaseDC(m_hWnd, wdc);

    // Create Direct2D bitmap directly on the main target m_rt
    float dx = 96.0f, dy = 96.0f;
    m_rt->GetDpi(&dx, &dy);
    D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dx, dy);

    m_rt->CreateBitmap(D2D1::SizeU(w, h), pixels.data(), w * 4, &props, &m_themeTransitionOldBitmap);
}

void GlassWindow::StartThemeTransition(POINT clickPt)
{
    if (!UIStyle::Animation::IsEnabled() || !m_themeTransitionOldBitmap)
    {
        m_themeTransitionActive = false;
        m_themeTransitionOldBitmap.Reset();
        return;
    }

    m_themeTransitionActive = true;
    m_themeTransitionProgress = 0.0f;
    m_themeTransitionStartTime = GetTickCount64();

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

    m_bgDirty = true;
    if (m_compositor) m_compositor->MarkAllDirty();

    SetTimer(m_hWnd, 0x889, 10, nullptr);
}
