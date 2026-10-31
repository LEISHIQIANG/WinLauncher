#include "GlassBackgroundWorker.h"
#include "App/AppMessages.h"
#include "App/Logger.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl.h>
#include <cstring>

using Microsoft::WRL::ComPtr;

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

// Out-of-line nested type: member structs of a class must be defined at
// namespace scope, not inside an anonymous namespace.
struct GlassBackgroundWorker::DuplicationState
{
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGIOutputDuplication> dup;
    ComPtr<ID3D11Texture2D> desktopCopy;  // last composed desktop frame (full output)
    ComPtr<ID3D11Texture2D> staging;      // CPU-read staging for the subrect copy
    DXGI_OUTPUT_DESC outputDesc{};
    HMONITOR monitor = nullptr;
    UINT outputWidth = 0;
    UINT outputHeight = 0;

    void ReleaseSession()
    {
        staging.Reset();
        desktopCopy.Reset();
        dup.Reset();
        monitor = nullptr;
    }
};

namespace
{
    double PerfNowMs()
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

    bool ShouldLogPerf(ULONGLONG& lastLogTick, double elapsedMs, double thresholdMs)
    {
        return Logger::ShouldLogElapsed(lastLogTick, elapsedMs, thresholdMs, 1000);
    }

    enum class DupResult
    {
        Captured,   // fresh pixels written into the frame buffer
        NoChange,   // desktop image and window rect unchanged: skip this cycle
        Failed,     // session problem: caller should fall back to GDI
    };

    RECT WindowRectScreen(POINT origin, int width, int height)
    {
        RECT rect{ origin.x, origin.y, origin.x + width, origin.y + height };
        return rect;
    }

    // Inflate the window rect by the drag margin and clamp to the virtual
    // screen, so BitBlt/duplication never sample undefined desktop area.
    RECT EffectiveCaptureRect(const RECT& windowRect, int margin)
    {
        RECT r = windowRect;
        if (margin > 0)
        {
            r.left -= margin;
            r.top -= margin;
            r.right += margin;
            r.bottom += margin;
        }
        RECT vs{
            GetSystemMetrics(SM_XVIRTUALSCREEN),
            GetSystemMetrics(SM_YVIRTUALSCREEN),
            GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
            GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN) };
        RECT clipped{
            (r.left > vs.left ? r.left : vs.left),
            (r.top > vs.top ? r.top : vs.top),
            (r.right < vs.right ? r.right : vs.right),
            (r.bottom < vs.bottom ? r.bottom : vs.bottom) };
        if (clipped.right <= clipped.left || clipped.bottom <= clipped.top)
        {
            return windowRect;  // degenerate clip: capture the window rect as-is
        }
        return clipped;
    }

    bool FullyInside(const RECT& inner, const RECT& outer)
    {
        return inner.left >= outer.left && inner.top >= outer.top &&
               inner.right <= outer.right && inner.bottom <= outer.bottom;
    }

    // Create (or re-create) the duplication session for the output that fully
    // contains the window rect. Returns false when duplication is unavailable
    // (secure desktop, RDP session, HDR format, spanned monitors, ...).
    bool EnsureDuplicationSession(
        GlassBackgroundWorker::DuplicationState& s,
        const RECT& rectScreen,
        HWND hwnd)
    {
        const POINT center{ (rectScreen.left + rectScreen.right) / 2,
                            (rectScreen.top + rectScreen.bottom) / 2 };
        HMONITOR monitor = MonitorFromPoint(center, MONITOR_DEFAULTTONEAREST);
        if (!monitor)
        {
            return false;
        }
        if (s.dup && s.monitor == monitor &&
            FullyInside(rectScreen, s.outputDesc.DesktopCoordinates))
        {
            return true;
        }
        s.ReleaseSession();

        if (!s.device)
        {
            HRESULT hr = D3D11CreateDevice(
                nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
                &s.device, nullptr, &s.context);
            if (FAILED(hr))
            {
                static ULONGLONG s_lastDeviceLogTick = 0;
                if (Logger::ShouldLogEvery(s_lastDeviceLogTick, 10000))
                {
                    LOG_G_WARNING_NODE(L"ui.glass", L"background_dup_device_failed",
                        L"hr=0x%08X hwnd=%p", hr, hwnd);
                }
                return false;
            }
        }

        ComPtr<IDXGIDevice> dxgiDevice;
        if (FAILED(s.device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) || !dxgiDevice)
        {
            return false;
        }
        ComPtr<IDXGIAdapter> adapter;
        if (FAILED(dxgiDevice->GetAdapter(&adapter)) || !adapter)
        {
            return false;
        }

        for (UINT o = 0; ; ++o)
        {
            ComPtr<IDXGIOutput> output;
            if (FAILED(adapter->EnumOutputs(o, &output)) || !output)
            {
                break;
            }
            DXGI_OUTPUT_DESC desc{};
            if (FAILED(output->GetDesc(&desc)))
            {
                continue;
            }
            if (!FullyInside(rectScreen, desc.DesktopCoordinates))
            {
                continue;  // window spans monitors: GDI handles that case
            }
            ComPtr<IDXGIOutput1> output1;
            if (FAILED(output->QueryInterface(IID_PPV_ARGS(&output1))) || !output1)
            {
                continue;
            }
            HRESULT hr = output1->DuplicateOutput(s.device.Get(), &s.dup);
            if (FAILED(hr))
            {
                static ULONGLONG s_lastDupLogTick = 0;
                if (Logger::ShouldLogEvery(s_lastDupLogTick, 10000))
                {
                    LOG_G_WARNING_NODE(L"ui.glass", L"background_dup_init_failed",
                        L"hr=0x%08X hwnd=%p", hr, hwnd);
                }
                continue;
            }
            s.monitor = monitor;
            s.outputDesc = desc;
            s.outputWidth = (UINT)(desc.DesktopCoordinates.right - desc.DesktopCoordinates.left);
            s.outputHeight = (UINT)(desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top);
            s.desktopCopy.Reset();
            s.staging.Reset();
            return true;
        }
        return false;
    }

    // Extract the window subrect out of the persistent desktop copy. Called
    // both after a fresh frame and when only the window rect moved. The
    // requested rect is clamped to the output bounds; the effective rect is
    // reported through the frame so the UI can align the bitmap exactly.
    DupResult ExtractDuplicationSubrect(
        GlassBackgroundWorker::DuplicationState& s,
        const RECT& captureRect,
        GlassBackgroundWorker::Frame& frame)
    {
        if (!s.desktopCopy || !s.staging || !s.context)
        {
            return DupResult::Failed;
        }
        const RECT outputRect = s.outputDesc.DesktopCoordinates;
        RECT local{
            captureRect.left - outputRect.left,
            captureRect.top - outputRect.top,
            captureRect.right - outputRect.left,
            captureRect.bottom - outputRect.top };
        local.left = (std::max)(local.left, 0L);
        local.top = (std::max)(local.top, 0L);
        local.right = (std::min)(local.right, (LONG)s.outputWidth);
        local.bottom = (std::min)(local.bottom, (LONG)s.outputHeight);
        const int w = local.right - local.left;
        const int h = local.bottom - local.top;
        if (w <= 0 || h <= 0)
        {
            return DupResult::Failed;
        }

        D3D11_BOX srcBox{
            (UINT)local.left, (UINT)local.top, 0,
            (UINT)local.right, (UINT)local.bottom, 1 };
        s.context->CopySubresourceRegion(
            s.staging.Get(), 0, (UINT)local.left, (UINT)local.top, 0,
            s.desktopCopy.Get(), 0, &srcBox);

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(s.context->Map(s.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        {
            return DupResult::Failed;
        }
        frame.pixels->assign((size_t)w * h, 0);
        for (int y = 0; y < h; ++y)
        {
            const BYTE* src = (const BYTE*)mapped.pData +
                (size_t)(local.top + y) * mapped.RowPitch + (size_t)local.left * 4;
            DWORD* dst = frame.pixels->data() + (size_t)y * w;
            memcpy(dst, src, (size_t)w * 4);
            for (int x = 0; x < w; ++x)
            {
                dst[x] |= 0xFF000000;
            }
        }
        s.context->Unmap(s.staging.Get(), 0);

        frame.captureRect = RECT{
            local.left + outputRect.left,
            local.top + outputRect.top,
            local.right + outputRect.left,
            local.bottom + outputRect.top };
        frame.width = w;
        frame.height = h;
        return DupResult::Captured;
    }

    // Acquire one desktop frame, keep a persistent full-output copy, and
    // extract the capture rect when either the image or the rect changed.
    DupResult TryCaptureDuplication(
        GlassBackgroundWorker::DuplicationState& s,
        const RECT& captureRect,
        bool rectChanged,
        UINT timeoutMs,
        GlassBackgroundWorker::Frame& frame)
    {
        DXGI_OUTDUPL_FRAME_INFO info{};
        ComPtr<IDXGIResource> resource;
        HRESULT hr = s.dup->AcquireNextFrame(timeoutMs, &info, &resource);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT)
        {
            // Nothing on this output changed. If the window itself did not
            // move, the previous subrect copy is still exact: skip all work.
            // When only the rect moved, re-extract from the persistent copy.
            if (!rectChanged)
            {
                return DupResult::NoChange;
            }
            return ExtractDuplicationSubrect(s, captureRect, frame);
        }
        if (FAILED(hr))
        {
            // DXGI_ERROR_ACCESS_LOST and friends: rebuild next cycle.
            s.ReleaseSession();
            return DupResult::Failed;
        }

        bool frameValid = info.LastPresentTime.QuadPart != 0;
        if (frameValid)
        {
            ComPtr<ID3D11Texture2D> tex;
            if (FAILED(resource->QueryInterface(IID_PPV_ARGS(&tex))) || !tex)
            {
                s.dup->ReleaseFrame();
                return DupResult::Failed;
            }
            D3D11_TEXTURE2D_DESC desc{};
            tex->GetDesc(&desc);
            if (desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM)
            {
                // Advanced-color (HDR) desktop: keep the GDI SDR pipeline for
                // byte-identical output instead of tone-mapping here.
                s.dup->ReleaseFrame();
                return DupResult::Failed;
            }
            if (!s.desktopCopy ||
                s.outputWidth != desc.Width || s.outputHeight != desc.Height)
            {
                s.desktopCopy.Reset();
                s.staging.Reset();
                desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                desc.Usage = D3D11_USAGE_DEFAULT;
                desc.CPUAccessFlags = 0;
                desc.MipLevels = 1;
                desc.ArraySize = 1;
                desc.SampleDesc.Count = 1;
                if (FAILED(s.device->CreateTexture2D(&desc, nullptr, &s.desktopCopy)))
                {
                    s.dup->ReleaseFrame();
                    return DupResult::Failed;
                }
                D3D11_TEXTURE2D_DESC stagingDesc = desc;
                stagingDesc.Usage = D3D11_USAGE_STAGING;
                stagingDesc.BindFlags = 0;
                stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                if (FAILED(s.device->CreateTexture2D(&stagingDesc, nullptr, &s.staging)))
                {
                    s.desktopCopy.Reset();
                    s.dup->ReleaseFrame();
                    return DupResult::Failed;
                }
                s.outputWidth = desc.Width;
                s.outputHeight = desc.Height;
            }
            s.context->CopyResource(s.desktopCopy.Get(), tex.Get());
        }
        s.dup->ReleaseFrame();

        if (!frameValid && !rectChanged)
        {
            return DupResult::NoChange;  // metadata-only update
        }
        return ExtractDuplicationSubrect(s, captureRect, frame);
    }
}

GlassBackgroundWorker::~GlassBackgroundWorker()
{
    Stop();
}

GlassBackgroundWorker::Backend GlassBackgroundWorker::ResolveBackend()
{
    wchar_t value[16]{};
    DWORD len = GetEnvironmentVariableW(L"WINLAUNCHER_GLASS_BACKEND", value, 16);
    if (len > 0 && len < 16)
    {
        if (_wcsicmp(value, L"duplication") == 0) return Backend::Duplication;
        if (_wcsicmp(value, L"gdi") == 0) return Backend::Gdi;
    }
    return Backend::Auto;
}

void GlassBackgroundWorker::Start(HWND hwnd, UINT refreshMs)
{
    if (m_thread)
    {
        return;
    }
    m_hwnd = hwnd;
    m_refreshMs = refreshMs ? refreshMs : 11;
    m_stop.store(false);
    m_wakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!m_wakeEvent)
    {
        LOG_G_ERROR_NODE(L"ui.glass", L"background_worker_start_failed",
            L"reason=wake_event hwnd=%p", hwnd);
        return;
    }
    m_thread = CreateThread(nullptr, 0,
        [](LPVOID param) -> DWORD
        {
            static_cast<GlassBackgroundWorker*>(param)->ThreadLoop();
            return 0;
        },
        this, 0, nullptr);
    if (!m_thread)
    {
        LOG_G_ERROR_NODE(L"ui.glass", L"background_worker_start_failed",
            L"reason=thread hwnd=%p", hwnd);
        CloseHandle(m_wakeEvent);
        m_wakeEvent = nullptr;
        return;
    }
    LOG_G_INFO_NODE(L"ui.glass", L"background_worker_started",
        L"backend=%d refreshMs=%u hwnd=%p",
        (int)ResolveBackend(), m_refreshMs, hwnd);
}

void GlassBackgroundWorker::Stop()
{
    if (!m_thread)
    {
        return;
    }
    m_stop.store(true);
    if (m_wakeEvent)
    {
        SetEvent(m_wakeEvent);
    }
    if (WaitForSingleObject(m_thread, 2000) != WAIT_OBJECT_0)
    {
        // The thread only ever waits on the wake event or captures the screen,
        // so a 2 s grace is generous; log rather than leak a dangling join.
        LOG_G_WARNING_NODE(L"ui.glass", L"background_worker_stop_timeout", L"hwnd=%p", m_hwnd);
    }
    CloseHandle(m_thread);
    m_thread = nullptr;
    if (m_wakeEvent)
    {
        CloseHandle(m_wakeEvent);
        m_wakeEvent = nullptr;
    }
    std::lock_guard<std::mutex> lock(m_frameMutex);
    m_pendingFrame.reset();
}

void GlassBackgroundWorker::UpdateGeometry(POINT clientScreenOrigin, int widthPx, int heightPx, FLOAT dpiX, FLOAT dpiY)
{
    {
        std::lock_guard<std::mutex> lock(m_geometryMutex);
        m_geometry.clientScreenOrigin = clientScreenOrigin;
        m_geometry.width = widthPx;
        m_geometry.height = heightPx;
        m_geometry.dpiX = dpiX;
        m_geometry.dpiY = dpiY;
        m_geometry.valid = (widthPx > 0 && heightPx > 0);
    }
    if (m_wakeEvent)
    {
        SetEvent(m_wakeEvent);
    }
}

void GlassBackgroundWorker::SetLive(bool live)
{
    m_live.store(live);
    if (live && m_wakeEvent)
    {
        SetEvent(m_wakeEvent);
    }
}

void GlassBackgroundWorker::RequestCapture()
{
    if (m_wakeEvent)
    {
        SetEvent(m_wakeEvent);
    }
}

void GlassBackgroundWorker::SetDragMode(bool dragging)
{
    m_dragging.store(dragging);
    if (m_wakeEvent)
    {
        SetEvent(m_wakeEvent);
    }
}

bool GlassBackgroundWorker::TryConsumeFrame(Frame& out)
{
    std::lock_guard<std::mutex> lock(m_frameMutex);
    if (!m_pendingFrame)
    {
        return false;
    }
    out = std::move(*m_pendingFrame);
    m_pendingFrame.reset();
    return true;
}

GlassBackgroundWorker::Geometry GlassBackgroundWorker::SnapshotGeometry() const
{
    std::lock_guard<std::mutex> lock(m_geometryMutex);
    return m_geometry;
}

bool GlassBackgroundWorker::ProduceGdiFrame(const Geometry& g, const RECT& captureRect, Frame& frame)
{
    double captureStartMs = PerfNowMs();
    const int w = captureRect.right - captureRect.left;
    const int h = captureRect.bottom - captureRect.top;

    HDC sdc = GetDC(nullptr);
    if (!sdc)
    {
        return false;
    }
    HDC mdc = CreateCompatibleDC(sdc);
    HBITMAP bmp = mdc ? CreateCompatibleBitmap(sdc, w, h) : nullptr;
    bool captured = false;
    if (bmp)
    {
        HGDIOBJ oldBitmap = SelectObject(mdc, bmp);
        if (oldBitmap)
        {
            if (BitBlt(mdc, 0, 0, w, h, sdc, captureRect.left, captureRect.top, SRCCOPY))
            {
                BITMAPINFO bi{};
                bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bi.bmiHeader.biWidth = w;
                bi.bmiHeader.biHeight = -h;
                bi.bmiHeader.biPlanes = 1;
                bi.bmiHeader.biBitCount = 32;
                bi.bmiHeader.biCompression = BI_RGB;
                if (GetDIBits(mdc, bmp, 0, h, frame.pixels->data(), &bi, DIB_RGB_COLORS) == h)
                {
                    const size_t count = (size_t)w * h;
                    for (size_t i = 0; i < count; i++)
                    {
                        (*frame.pixels)[i] |= 0xFF000000;
                    }
                    captured = true;
                }
            }
            SelectObject(mdc, oldBitmap);
        }
        DeleteObject(bmp);
    }
    if (mdc)
    {
        DeleteDC(mdc);
    }
    ReleaseDC(nullptr, sdc);

    if (!captured)
    {
        static ULONGLONG s_lastFailLogTick = 0;
        if (Logger::ShouldLogEvery(s_lastFailLogTick, 30000))
        {
            LOG_G_WARNING_NODE(L"ui.glass", L"background_worker_gdi_failed",
                L"size=%dx%d hwnd=%p", w, h, m_hwnd);
        }
        return false;
    }

    double elapsedMs = PerfNowMs() - captureStartMs;
    static ULONGLONG s_lastCaptureLogTick = 0;
    if (ShouldLogPerf(s_lastCaptureLogTick, elapsedMs, 12.0))
    {
        LOG_G_WARNING_NODE(L"ui.glass", L"background_capture_slow",
            L"elapsedMs=%.2f thresholdMs=12.00 size=%dx%d path=gdi_worker hwnd=%p",
            elapsedMs, w, h, m_hwnd);
    }
    frame.width = w;
    frame.height = h;
    frame.captureRect = captureRect;
    frame.fromDuplication = false;
    return true;
}

void GlassBackgroundWorker::Publish(Frame&& frame)
{
    bool posted = false;
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        if (m_pendingFrame)
        {
            return;  // UI has not consumed the previous frame yet: one-slot backpressure
        }
        m_pendingFrame = std::make_shared<Frame>(std::move(frame));
        posted = true;
    }
    if (posted && m_hwnd)
    {
        PostMessageW(m_hwnd, AppMessages::GlassBackgroundFrame, 0, 0);
    }
}

void GlassBackgroundWorker::ThreadLoop()
{
    const Backend backend = ResolveBackend();
    std::unique_ptr<DuplicationState> duplication;
    if (backend != Backend::Gdi)
    {
        duplication = std::make_unique<DuplicationState>();
    }
    bool duplicationBroken = false;
    ULONGLONG duplicationRetryTick = 0;
    RECT lastCaptureRect{ 0, 0, 0, 0 };
    bool haveLastCapture = false;

    while (!m_stop.load())
    {
        DWORD timeout = m_live.load()
            ? (m_dragging.load() ? kDragRefreshMs : m_refreshMs)
            : 500;
        WaitForSingleObject(m_wakeEvent, timeout);
        if (m_stop.load())
        {
            break;
        }
        if (!m_live.load())
        {
            continue;
        }
        const Geometry g = SnapshotGeometry();
        if (!g.valid)
        {
            continue;
        }

        const RECT windowRect = WindowRectScreen(g.clientScreenOrigin, g.width, g.height);
        const RECT captureRect = EffectiveCaptureRect(
            windowRect, m_dragging.load() ? kDragCaptureMarginPx : 0);

        Frame frame;
        frame.width = captureRect.right - captureRect.left;
        frame.height = captureRect.bottom - captureRect.top;
        frame.dpiX = g.dpiX;
        frame.dpiY = g.dpiY;
        frame.clientScreenOrigin = g.clientScreenOrigin;
        frame.captureRect = captureRect;
        frame.pixels = std::make_shared<std::vector<DWORD>>(
            (size_t)frame.width * (size_t)frame.height, 0);

        const bool rectChanged = !haveLastCapture ||
            lastCaptureRect.left != captureRect.left ||
            lastCaptureRect.top != captureRect.top ||
            lastCaptureRect.right != captureRect.right ||
            lastCaptureRect.bottom != captureRect.bottom;

        bool produced = false;
        bool dupAttempted = false;
        const ULONGLONG now = GetTickCount64();
        if (duplication && (!duplicationBroken || now >= duplicationRetryTick))
        {
            dupAttempted = true;
            if (EnsureDuplicationSession(*duplication, captureRect, m_hwnd))
            {
                switch (TryCaptureDuplication(*duplication, captureRect, rectChanged, timeout, frame))
                {
                case DupResult::Captured:
                    frame.fromDuplication = true;
                    produced = true;
                    duplicationBroken = false;
                    break;
                case DupResult::NoChange:
                    duplicationBroken = false;
                    break;  // desktop and rect unchanged: skip this cycle entirely
                case DupResult::Failed:
                default:
                    duplicationBroken = true;
                    duplicationRetryTick = now + 1000;  // short backoff; GDI covers meanwhile
                    break;
                }
            }
            else
            {
                duplicationBroken = true;
                duplicationRetryTick = now + 2000;  // unavailable here (RDP, secure desktop, ...): back off
            }
        }
        // GDI produces when: no duplication backend, backoff active, or the
        // duplication attempt just failed for this frame.
        if (!produced && (!dupAttempted || duplicationBroken))
        {
            produced = ProduceGdiFrame(g, captureRect, frame);
        }

        if (produced)
        {
            frame.sequence = ++m_sequence;
            lastCaptureRect = captureRect;
            haveLastCapture = true;
            Publish(std::move(frame));
        }
    }

    if (duplication)
    {
        duplication->ReleaseSession();
        duplication->context.Reset();
        duplication->device.Reset();
    }
}
