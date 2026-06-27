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

void GlassWindow::ApplySystemBackdrop()
{
    MARGINS m{ -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(m_hWnd, &m);

    DWORD corner = 2;
    DwmSetWindowAttribute(m_hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

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
        
        BYTE r = (BYTE)(rgb.r * 255.0f);
        BYTE g = (BYTE)(rgb.g * 255.0f);
        BYTE b = (BYTE)(rgb.b * 255.0f);
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
            m_dw->CreateTextFormat(L"Microsoft YaHei UI", nullptr,
                DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL, 11, L"", &m_tf);
            if (m_tf)
            {
                m_tf->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                m_tf->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

                DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
                m_tf->SetTrimming(&trimming, nullptr);
                m_tf->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
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
    m_rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
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
                D2D1::RoundedRect(D2D1::RectF(1, 1, w - 1, h - 1), 8, 8), m_sheenBrush.Get());
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
                D2D1::RoundedRect(D2D1::RectF(0.5f, 0.5f, w - 0.5f, h - 0.5f), 8, 8),
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

    if (m_compositor)
    {
        m_compositor->Render(m_rt.Get(), scale);
        OnPaintContent(m_rt.Get());
        HRESULT hr = m_rt->EndDraw();
        if (hr == D2DERR_RECREATE_TARGET)
        {
            m_bgCap.Reset(); m_bgFinal.Reset(); m_compositeRt.Reset();
            m_blurEffect.Reset(); m_satEffect.Reset();
            m_sheenGsc.Reset(); m_sheenBrush.Reset(); m_effectWinSize = {};
            m_rt.Reset();
            m_brushCache.clear();
            if (m_compositor) m_compositor->MarkAllDirty();
            EnsureD2D();
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
                    D2D1::RoundedRect(D2D1::RectF(0.5f, 0.5f, w - 0.5f, h - 0.5f), 8, 8),
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

    HRESULT hr = m_rt->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET)
    {
        m_bgCap.Reset(); m_bgFinal.Reset(); m_compositeRt.Reset();
        m_blurEffect.Reset(); m_satEffect.Reset();
        m_sheenGsc.Reset(); m_sheenBrush.Reset(); m_effectWinSize = {};
        m_rt.Reset();
        m_brushCache.clear();
        EnsureD2D();
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
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_EXITSIZEMOVE:
        m_bgDirty = true;
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;

    case WM_DWMCOMPOSITIONCHANGED:
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
        }
        else
        {
            KillTimer(hWnd, 0x888);
        }
        break;

    case WM_TIMER:
        if (wParam == 0x888)
        {
            m_bgDirty = true;
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        }
        break;

    case WM_DPICHANGED:
    {
        float newDpiX = (float)LOWORD(wParam);
        float newDpiY = (float)HIWORD(wParam);
        if (m_rt)
        {
            m_rt->SetDpi(newDpiX, newDpiY);
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

    case WM_DESTROY:
        KillTimer(hWnd, 0x888);
        ReleaseD2D();
        return 0;
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}
