#include "GlassBackgroundCapture.h"
#include "App/Logger.h"
#include <cmath>

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

void GlassBackgroundCapture::Reset()
{
    m_bgCap.Reset();
    m_pixbuf.clear();
}

bool GlassBackgroundCapture::Capture(HWND hwnd, ID2D1HwndRenderTarget* rt)
{
    if (!rt)
    {
        LOG_G_WARNING_NODE(L"ui.glass", L"background_capture_skipped", L"reason=no_render_target hwnd=%p", hwnd);
        return false;
    }

    double captureStartMs = PerfNowMs();
    D2D1_SIZE_U pixelSize = rt->GetPixelSize();
    POINT clientOrigin{ 0, 0 };
    if (!ClientToScreen(hwnd, &clientOrigin))
    {
        LOG_G_ERROR_NODE(L"ui.glass", L"background_capture_client_to_screen_failed", L"error=%lu hwnd=%p", GetLastError(), hwnd);
        return false;
    }
    int w = (int)pixelSize.width;
    int h = (int)pixelSize.height;
    if (w <= 0 || h <= 0)
    {
        LOG_G_WARNING_NODE(L"ui.glass", L"background_capture_skipped", L"reason=invalid_size size=%dx%d hwnd=%p", w, h, hwnd);
        return false;
    }

    HDC sdc = GetDC(nullptr);
    if (!sdc)
    {
        LOG_G_ERROR_NODE(L"ui.glass", L"background_capture_getdc_failed", L"error=%lu size=%dx%d hwnd=%p", GetLastError(), w, h, hwnd);
        return false;
    }
    HDC mdc = CreateCompatibleDC(sdc);
    if (!mdc)
    {
        LOG_G_ERROR_NODE(L"ui.glass", L"background_capture_create_dc_failed", L"error=%lu size=%dx%d hwnd=%p", GetLastError(), w, h, hwnd);
        ReleaseDC(nullptr, sdc);
        return false;
    }
    HBITMAP bmp = CreateCompatibleBitmap(sdc, w, h);
    if (!bmp)
    {
        LOG_G_ERROR_NODE(L"ui.glass", L"background_capture_create_bitmap_failed", L"error=%lu size=%dx%d hwnd=%p", GetLastError(), w, h, hwnd);
        DeleteDC(mdc);
        ReleaseDC(nullptr, sdc);
        return false;
    }
    HGDIOBJ oldBitmap = SelectObject(mdc, bmp);
    if (!oldBitmap)
    {
        LOG_G_ERROR_NODE(L"ui.glass", L"background_capture_select_bitmap_failed", L"error=%lu size=%dx%d hwnd=%p", GetLastError(), w, h, hwnd);
        DeleteObject(bmp);
        DeleteDC(mdc);
        ReleaseDC(nullptr, sdc);
        return false;
    }
    if (!BitBlt(mdc, 0, 0, w, h, sdc, clientOrigin.x, clientOrigin.y, SRCCOPY))
    {
        const DWORD error = GetLastError();
        const ULONGLONG now = GetTickCount64();
        if (error != m_lastError ||
            m_lastLogTick == 0 ||
            now - m_lastLogTick >= 30000)
        {
            LOG_G_WARNING_NODE(
                L"ui.glass",
                L"background_capture_bitblt_failed",
                L"error=%lu size=%dx%d origin=(%d,%d) hwnd=%p fallback=last_valid_cache",
                error,
                w,
                h,
                clientOrigin.x,
                clientOrigin.y,
                hwnd);
            m_lastError = error;
            m_lastLogTick = now;
        }
        SelectObject(mdc, oldBitmap);
        DeleteObject(bmp);
        DeleteDC(mdc);
        ReleaseDC(nullptr, sdc);
        return false;
    }

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    m_pixbuf.resize(w * h);
    int scanLines = GetDIBits(mdc, bmp, 0, h, m_pixbuf.data(), &bi, DIB_RGB_COLORS);
    if (scanLines != h)
    {
        LOG_G_ERROR_NODE(
            L"ui.glass",
            L"background_capture_getdibits_failed",
            L"error=%lu scanLines=%d expected=%d size=%dx%d hwnd=%p",
            GetLastError(),
            scanLines,
            h,
            w,
            h,
            hwnd);
        m_pixbuf.clear();
        SelectObject(mdc, oldBitmap);
        DeleteObject(bmp);
        DeleteDC(mdc);
        ReleaseDC(nullptr, sdc);
        return false;
    }
    for (int i = 0; i < w * h; i++)
        m_pixbuf[i] |= 0xFF000000;

    SelectObject(mdc, oldBitmap);
    DeleteObject(bmp);
    DeleteDC(mdc);
    ReleaseDC(nullptr, sdc);

    if (m_bgCap)
    {
        D2D1_SIZE_U size = m_bgCap->GetPixelSize();
        if (size.width != (UINT32)w || size.height != (UINT32)h)
        {
            LOG_G_DEBUG_NODE(
                L"ui.glass",
                L"background_capture_bitmap_recreated",
                L"reason=size_changed old=%ux%u new=%dx%d hwnd=%p",
                size.width,
                size.height,
                w,
                h,
                hwnd);
            m_bgCap.Reset();
        }
        else
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
                    L"ui.glass",
                    L"background_capture_bitmap_recreated",
                    L"reason=dpi_changed bitmapDpi=%.1fx%.1f rtDpi=%.1fx%.1f hwnd=%p",
                    bmpDpiX,
                    bmpDpiY,
                    rtDpiX,
                    rtDpiY,
                    hwnd);
                m_bgCap.Reset();
            }
        }
    }

    if (m_bgCap)
    {
        HRESULT copyHr = m_bgCap->CopyFromMemory(nullptr, m_pixbuf.data(), w * 4);
        if (FAILED(copyHr))
        {
            LOG_G_ERROR_NODE(L"ui.glass", L"background_capture_copy_failed", L"hr=0x%08X size=%dx%d hwnd=%p", copyHr, w, h, hwnd);
            m_bgCap.Reset();
        }
    }
    if (!m_bgCap)
    {
        float dx = 96.0f, dy = 96.0f;
        rt->GetDpi(&dx, &dy);
        D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dx, dy);
        HRESULT createHr = rt->CreateBitmap(D2D1::SizeU(w, h), m_pixbuf.data(), w * 4, &props, &m_bgCap);
        if (FAILED(createHr))
        {
            LOG_G_ERROR_NODE(
                L"ui.glass",
                L"background_capture_create_d2d_bitmap_failed",
                L"hr=0x%08X size=%dx%d dpi=%.1fx%.1f hwnd=%p",
                createHr,
                w,
                h,
                dx,
                dy,
                hwnd);
        }
    }
    m_pixbuf.clear();
    if (m_bgCap)
    {
        m_lastError = ERROR_SUCCESS;
        m_lastLogTick = 0;
    }
    double elapsedMs = PerfNowMs() - captureStartMs;
    static ULONGLONG s_lastCaptureLogTick = 0;
    if (ShouldLogPerf(s_lastCaptureLogTick, elapsedMs, 12.0))
    {
        FLOAT dpiX = 96.0f;
        FLOAT dpiY = 96.0f;
        rt->GetDpi(&dpiX, &dpiY);
        LOG_G_WARNING_NODE(
            L"ui.glass",
            L"background_capture_slow",
            L"elapsedMs=%.2f thresholdMs=12.00 size=%dx%d dpi=%.1fx%.1f origin=(%d,%d) hasBitmap=%d hwnd=%p",
            elapsedMs,
            w,
            h,
            dpiX,
            dpiY,
            clientOrigin.x,
            clientOrigin.y,
            m_bgCap ? 1 : 0,
            hwnd);
    }
    return m_bgCap != nullptr;
}
