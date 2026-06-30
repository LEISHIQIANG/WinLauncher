#pragma once
#include "IRenderLayer.h"
#include "../../App/Logger.h"
#include "../../Config/UIStyle.h"
#include <cmath>
#include <d2d1helper.h>
#include <dwmapi.h>
#include <vector>

#pragma comment(lib, "dwmapi.lib")

class BackgroundLayer : public IRenderLayer
{
public:
    BackgroundLayer(HWND hWnd) : m_hWnd(hWnd) {}

    void ApplySystemBackdrop()
    {
        MARGINS m{ -1, -1, -1, -1 };
        HRESULT frameHr = DwmExtendFrameIntoClientArea(m_hWnd, &m);

        DWORD corner = 2;
        HRESULT cornerHr = DwmSetWindowAttribute(m_hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

        DWORD border = 0xFFFFFFFE;
        HRESULT borderHr = DwmSetWindowAttribute(m_hWnd, 34, &border, sizeof(border));

        LOG_G_INFO_NODE(
            L"render.background",
            L"system_backdrop_apply",
            L"frameHr=0x%08X cornerHr=0x%08X borderHr=0x%08X hwnd=%p",
            frameHr,
            cornerHr,
            borderHr,
            m_hWnd);

        int backdropType = 1;
        for (int attr : {38, 1029})
        {
            HRESULT backdropHr = DwmSetWindowAttribute(m_hWnd, attr, &backdropType, sizeof(backdropType));
            if (FAILED(backdropHr))
            {
                LOG_G_WARNING_NODE(
                    L"render.background",
                    L"dwm_backdrop_disable_failed",
                    L"attr=%d hr=0x%08X hwnd=%p",
                    attr,
                    backdropHr,
                    m_hWnd);
            }
        }

        auto u = GetModuleHandleW(L"user32.dll");
        if (u)
        {
            using SWCAFn = BOOL(WINAPI*)(HWND, void*);
            auto fn = (SWCAFn)GetProcAddress(u, "SetWindowCompositionAttribute");
            if (fn)
            {
                struct AccentPolicy { int s, f; unsigned int g; int a; };
                struct WinCompAttr { int attr; void* data; size_t size; };
                AccentPolicy ap{ 2, 2, 0x00000000, 0 };
                WinCompAttr d{ 19, &ap, sizeof(ap) };
                fn(m_hWnd, &d);
            }
        }
    }

    virtual void Render(ID2D1HwndRenderTarget* rt, ID2D1DeviceContext* dc,
                        const D2D1_SIZE_F& size, float scale) override
    {
        double renderStartMs = PerfNowMs();
        SetRenderTarget(rt);

        int windowMode = UIStyle::GetWindowMode();
        if (windowMode == 1) // Acrylic mode is rendered by system DWM backdrop
        {
            rt->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
            LogSlowRender(renderStartMs, size, scale, windowMode, L"acrylic");
            return;
        }

        if (m_dirty)
        {
            CaptureBackground();
            m_dirty = false;
        }

        rt->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

        if (!m_bgCap)
        {
            static ULONGLONG s_lastNoBitmapLogTick = 0;
            if (Logger::ShouldLogEvery(s_lastNoBitmapLogTick, 1000))
            {
                LOG_G_WARNING_NODE(L"render.background", L"render_skipped", L"reason=no_background_bitmap size=%.0fx%.0f hwnd=%p", size.width, size.height, m_hWnd);
            }
            return;
        }

        auto& cfg = UIStyle::ThemeColor::ConfigFor(UIStyle::GetThemeMode(), windowMode);

        if (dc)
        {
            ComPtr<ID2D1Effect> blur;
            ComPtr<ID2D1Effect> sat;
            if (SUCCEEDED(dc->CreateEffect(CLSID_D2D1GaussianBlur, &blur)) &&
                SUCCEEDED(dc->CreateEffect(CLSID_D2D1Saturation, &sat)))
            {
                blur->SetInput(0, m_bgCap.Get());
                blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, cfg.blur);
                blur->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);

                ComPtr<ID2D1Image> blurOut;
                blur->GetOutput(&blurOut);
                if (blurOut)
                {
                    sat->SetInput(0, blurOut.Get());
                    sat->SetValue(D2D1_SATURATION_PROP_SATURATION, cfg.saturation);
                    dc->DrawImage(sat.Get());
                }
                LogSlowRender(renderStartMs, size, scale, windowMode, L"effect");
                return;
            }
            static ULONGLONG s_lastEffectFallbackLogTick = 0;
            if (Logger::ShouldLogEvery(s_lastEffectFallbackLogTick, 5000))
            {
                LOG_G_WARNING_NODE(L"render.background", L"effect_fallback", L"size=%.0fx%.0f hwnd=%p", size.width, size.height, m_hWnd);
            }
            dc->DrawImage(m_bgCap.Get());
        }
        else
        {
            rt->DrawBitmap(m_bgCap.Get(), D2D1::RectF(0, 0, size.width, size.height));
        }
        LogSlowRender(renderStartMs, size, scale, windowMode, dc ? L"fallback" : L"bitmap");
    }

    virtual void OnResize(const D2D1_SIZE_F& size) override
    {
        static ULONGLONG s_lastResizeLogTick = 0;
        if (Logger::ShouldLogEvery(s_lastResizeLogTick, 500))
        {
            LOG_G_INFO_NODE(L"render.background", L"resize", L"size=%.0fx%.0f hwnd=%p", size.width, size.height, m_hWnd);
        }
        MarkDirty();
    }

    void MarkDirty()
    {
        m_dirty = true;
        if (m_bgCap)
        {
            D2D1_SIZE_U cur = m_bgCap->GetPixelSize();
            auto rt = GetD2DRenderTarget();
            D2D1_SIZE_U targetSize = rt ? rt->GetPixelSize() : D2D1::SizeU(0, 0);
            int w = (int)targetSize.width;
            int h = (int)targetSize.height;
            if (cur.width != (UINT32)w || cur.height != (UINT32)h)
            {
                LOG_G_DEBUG_NODE(
                    L"render.background",
                    L"cache_reset",
                    L"reason=size_changed old=%ux%u new=%dx%d hwnd=%p",
                    cur.width,
                    cur.height,
                    w,
                    h,
                    m_hWnd);
                m_bgCap.Reset();
            }
        }
    }

private:
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

    void LogSlowRender(double renderStartMs, const D2D1_SIZE_F& size, float scale, int windowMode, const wchar_t* path)
    {
        double elapsedMs = PerfNowMs() - renderStartMs;
        static ULONGLONG s_lastRenderLogTick = 0;
        if (Logger::ShouldLogElapsed(s_lastRenderLogTick, elapsedMs, 12.0, 1000))
        {
            LOG_G_WARNING_NODE(
                L"render.background",
                L"render_slow",
                L"path=%s elapsedMs=%.2f thresholdMs=12.00 size=%.0fx%.0f scale=%.2f windowMode=%d hasBitmap=%d hwnd=%p",
                path ? path : L"unknown",
                elapsedMs,
                size.width,
                size.height,
                scale,
                windowMode,
                m_bgCap ? 1 : 0,
                m_hWnd);
        }
    }

    void CaptureBackground()
    {
        auto rt = GetD2DRenderTarget();
        if (!rt)
        {
            LOG_G_WARNING_NODE(L"render.background", L"capture_skipped", L"reason=no_render_target hwnd=%p", m_hWnd);
            return;
        }

        double captureStartMs = PerfNowMs();
        D2D1_SIZE_U pixelSize = rt->GetPixelSize();
        POINT clientOrigin{ 0, 0 };
        if (!ClientToScreen(m_hWnd, &clientOrigin))
        {
            LOG_G_ERROR_NODE(L"render.background", L"capture_client_to_screen_failed", L"error=%lu hwnd=%p", GetLastError(), m_hWnd);
            return;
        }
        int w = (int)pixelSize.width;
        int h = (int)pixelSize.height;
        if (w <= 0 || h <= 0)
        {
            LOG_G_WARNING_NODE(L"render.background", L"capture_skipped", L"reason=invalid_size size=%dx%d hwnd=%p", w, h, m_hWnd);
            return;
        }

        HDC sdc = GetDC(nullptr);
        if (!sdc)
        {
            LOG_G_ERROR_NODE(L"render.background", L"capture_getdc_failed", L"error=%lu size=%dx%d hwnd=%p", GetLastError(), w, h, m_hWnd);
            return;
        }
        HDC mdc = CreateCompatibleDC(sdc);
        if (!mdc)
        {
            LOG_G_ERROR_NODE(L"render.background", L"capture_create_dc_failed", L"error=%lu size=%dx%d hwnd=%p", GetLastError(), w, h, m_hWnd);
            ReleaseDC(nullptr, sdc);
            return;
        }
        HBITMAP bmp = CreateCompatibleBitmap(sdc, w, h);
        if (!bmp)
        {
            LOG_G_ERROR_NODE(L"render.background", L"capture_create_bitmap_failed", L"error=%lu size=%dx%d hwnd=%p", GetLastError(), w, h, m_hWnd);
            DeleteDC(mdc);
            ReleaseDC(nullptr, sdc);
            return;
        }
        HGDIOBJ oldBitmap = SelectObject(mdc, bmp);
        if (!BitBlt(mdc, 0, 0, w, h, sdc, clientOrigin.x, clientOrigin.y, SRCCOPY))
        {
            LOG_G_ERROR_NODE(
                L"render.background",
                L"capture_bitblt_failed",
                L"error=%lu size=%dx%d origin=(%d,%d) hwnd=%p",
                GetLastError(),
                w,
                h,
                clientOrigin.x,
                clientOrigin.y,
                m_hWnd);
            if (oldBitmap) SelectObject(mdc, oldBitmap);
            DeleteObject(bmp);
            DeleteDC(mdc);
            ReleaseDC(nullptr, sdc);
            return;
        }

        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        std::vector<DWORD> pixbuf(w * h);
        int scanLines = GetDIBits(mdc, bmp, 0, h, pixbuf.data(), &bi, DIB_RGB_COLORS);
        if (scanLines != h)
        {
            LOG_G_ERROR_NODE(
                L"render.background",
                L"capture_getdibits_failed",
                L"error=%lu scanLines=%d expected=%d size=%dx%d hwnd=%p",
                GetLastError(),
                scanLines,
                h,
                w,
                h,
                m_hWnd);
            if (oldBitmap) SelectObject(mdc, oldBitmap);
            DeleteObject(bmp);
            DeleteDC(mdc);
            ReleaseDC(nullptr, sdc);
            return;
        }
        for (int i = 0; i < w * h; i++)
            pixbuf[i] |= 0xFF000000;

        if (oldBitmap) SelectObject(mdc, oldBitmap);
        DeleteObject(bmp);
        DeleteDC(mdc);
        ReleaseDC(nullptr, sdc);

        if (m_bgCap)
        {
            D2D1_SIZE_U cur = m_bgCap->GetPixelSize();
            if (cur.width != (UINT32)w || cur.height != (UINT32)h)
            {
                LOG_G_DEBUG_NODE(
                    L"render.background",
                    L"cache_reset",
                    L"reason=capture_size_changed old=%ux%u new=%dx%d hwnd=%p",
                    cur.width,
                    cur.height,
                    w,
                    h,
                    m_hWnd);
                m_bgCap.Reset();
            }
            else
            {
                auto rt = GetD2DRenderTarget();
                if (rt)
                {
                    FLOAT bmpDpiX = 96.0f;
                    FLOAT bmpDpiY = 96.0f;
                    FLOAT rtDpiX = 96.0f;
                    FLOAT rtDpiY = 96.0f;
                    m_bgCap->GetDpi(&bmpDpiX, &bmpDpiY);
                    rt->GetDpi(&rtDpiX, &rtDpiY);
                    if (fabsf(bmpDpiX - rtDpiX) > 0.5f || fabsf(bmpDpiY - rtDpiY) > 0.5f)
                    {
                        LOG_G_DEBUG_NODE(
                            L"render.background",
                            L"cache_reset",
                            L"reason=capture_dpi_changed bitmapDpi=%.1fx%.1f rtDpi=%.1fx%.1f hwnd=%p",
                            bmpDpiX,
                            bmpDpiY,
                            rtDpiX,
                            rtDpiY,
                            m_hWnd);
                        m_bgCap.Reset();
                    }
                }
            }
        }

        if (m_bgCap)
        {
            HRESULT copyHr = m_bgCap->CopyFromMemory(nullptr, pixbuf.data(), w * 4);
            if (FAILED(copyHr))
            {
                LOG_G_ERROR_NODE(L"render.background", L"capture_copy_failed", L"hr=0x%08X size=%dx%d hwnd=%p", copyHr, w, h, m_hWnd);
                m_bgCap.Reset();
            }
        }
        if (!m_bgCap)
        {
            float dx = 96.0f, dy = 96.0f;
            auto rt = GetD2DRenderTarget();
            if (rt) rt->GetDpi(&dx, &dy);

            D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dx, dy);

            auto d2dRt = GetD2DRenderTarget();
            if (d2dRt)
            {
                HRESULT createHr = d2dRt->CreateBitmap(D2D1::SizeU(w, h), pixbuf.data(), w * 4, &props, &m_bgCap);
                if (FAILED(createHr))
                {
                    LOG_G_ERROR_NODE(
                        L"render.background",
                        L"capture_create_d2d_bitmap_failed",
                        L"hr=0x%08X size=%dx%d dpi=%.1fx%.1f hwnd=%p",
                        createHr,
                        w,
                        h,
                        dx,
                        dy,
                        m_hWnd);
                }
            }
        }

        double elapsedMs = PerfNowMs() - captureStartMs;
        static ULONGLONG s_lastCaptureLogTick = 0;
        if (Logger::ShouldLogElapsed(s_lastCaptureLogTick, elapsedMs, 12.0, 1000))
        {
            LOG_G_WARNING_NODE(
                L"render.background",
                L"capture_slow",
                L"elapsedMs=%.2f thresholdMs=12.00 size=%dx%d origin=(%d,%d) hasBitmap=%d hwnd=%p",
                elapsedMs,
                w,
                h,
                clientOrigin.x,
                clientOrigin.y,
                m_bgCap ? 1 : 0,
                m_hWnd);
        }
    }

    ID2D1HwndRenderTarget* GetD2DRenderTarget()
    {
        return m_rt;
    }

    HWND m_hWnd;
    ComPtr<ID2D1Bitmap> m_bgCap;
    ID2D1HwndRenderTarget* m_rt = nullptr;

public:
    void SetRenderTarget(ID2D1HwndRenderTarget* rt) { m_rt = rt; }
};
