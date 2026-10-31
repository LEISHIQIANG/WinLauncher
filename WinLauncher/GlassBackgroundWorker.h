#pragma once

#include <Windows.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

// Background screen-capture producer for glass windows.
//
// Runs a dedicated worker thread that captures the desktop region under the
// window (GDI BitBlt, or DXGI Desktop Duplication when available) and hands
// BGRA pixel buffers to the UI thread through a one-slot pending frame plus a
// posted message. The worker never touches Direct2D or the target window's
// resources: the UI thread owns every upload, composite, and draw step, so
// dragging never serializes capture work against the message pump.
//
// Frame pacing:
//   - Duplication backend: paced by the DWM composition rate (vsync), with
//     zero work when neither the desktop image nor the window rect changes.
//   - GDI backend: paced by m_refreshMs (the historical 11 ms live cadence).
//
// Lifetime: Start once per window, Stop before the HWND dies (the destructor
// joins). SetLive toggles capture while the window is shown or hidden without
// tearing the thread down.
class GlassBackgroundWorker
{
public:
    enum class Backend
    {
        Auto,
        Duplication,
        Gdi,
    };

    // One captured desktop region, ready for upload on the UI thread.
    struct Frame
    {
        std::shared_ptr<std::vector<DWORD>> pixels;  // top-down BGRA, alpha forced opaque
        int width = 0;
        int height = 0;
        FLOAT dpiX = 96.0f;
        FLOAT dpiY = 96.0f;
        POINT clientScreenOrigin = { 0, 0 };  // screen coords of the client origin at capture time
        unsigned sequence = 0;
        bool fromDuplication = false;
    };

    GlassBackgroundWorker() = default;
    ~GlassBackgroundWorker();

    GlassBackgroundWorker(const GlassBackgroundWorker&) = delete;
    GlassBackgroundWorker& operator=(const GlassBackgroundWorker&) = delete;

    void Start(HWND hwnd, UINT refreshMs);
    void Stop();

    // UI thread only: publish the geometry the next frame should capture and
    // wake the worker. Called on show, move, resize, and DPI changes.
    void UpdateGeometry(POINT clientScreenOrigin, int widthPx, int heightPx, FLOAT dpiX, FLOAT dpiY);

    // UI thread only: enable/disable capture while the window shows/hides.
    void SetLive(bool live);

    // UI thread only: nudge the worker (theme/style changes, drag end).
    void RequestCapture();

    // Opaque per-thread duplication state (D3D11/DXGI handles stay out of
    // this header and out of every other thread). Public because the capture
    // helpers in the implementation file operate on it.
    struct DuplicationState;

    // UI thread only: take the newest pending frame (if any).
    bool TryConsumeFrame(Frame& out);

    // Resolve the capture backend once per process from the override
    // environment variable WINLAUNCHER_GLASS_BACKEND (auto|duplication|gdi).
    static Backend ResolveBackend();

private:
    struct Geometry
    {
        POINT clientScreenOrigin = { 0, 0 };
        int width = 0;
        int height = 0;
        FLOAT dpiX = 96.0f;
        FLOAT dpiY = 96.0f;
        bool valid = false;
    };

    void ThreadLoop();
    Geometry SnapshotGeometry() const;

    bool ProduceGdiFrame(const Geometry& g, Frame& frame);

    void Publish(Frame&& frame);

    HWND m_hwnd = nullptr;
    UINT m_refreshMs = 11;
    std::atomic<bool> m_stop{ false };
    std::atomic<bool> m_live{ false };
    HANDLE m_wakeEvent = nullptr;
    HANDLE m_thread = nullptr;

    mutable std::mutex m_geometryMutex;
    Geometry m_geometry;

    std::mutex m_frameMutex;
    std::shared_ptr<Frame> m_pendingFrame;  // worker -> UI, one-slot backpressure
    unsigned m_sequence = 0;
};
