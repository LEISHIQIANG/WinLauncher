#pragma once

#include <d2d1.h>
#include <wrl.h>
#include <vector>

using Microsoft::WRL::ComPtr;

// Desktop background capture for glass windows: BitBlt the screen region
// under the window, upload it into a D2D bitmap, and keep the last valid
// capture across size/DPI changes. Owns the pixel scratch buffer and the
// throttled capture-failure logging. A failed capture leaves the previous
// bitmap untouched so compositing can keep using the last valid cache.
class GlassBackgroundCapture
{
public:
    // Captures the screen area under hwnd at the render target's pixel size.
    // Returns true only for a verified fresh capture.
    bool Capture(HWND hwnd, ID2D1HwndRenderTarget* rt);

    // The last captured raw background (null when none yet or after Reset).
    ID2D1Bitmap* Bitmap() const { return m_bgCap.Get(); }

    // Releases the captured bitmap and the scratch buffer.
    void Reset();

private:
    ComPtr<ID2D1Bitmap> m_bgCap;
    std::vector<DWORD> m_pixbuf;
    DWORD m_lastError = ERROR_SUCCESS;
    ULONGLONG m_lastLogTick = 0;
};
