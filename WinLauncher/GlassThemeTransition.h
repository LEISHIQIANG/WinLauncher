#pragma once

#include <d2d1.h>
#include <wrl.h>
#include <functional>

using Microsoft::WRL::ComPtr;

// Desktop theme transition manager for glass windows.
// Captures a snapshot of the current render target before a theme switch,
// renders a smooth fading overlay over the new theme, and coordinates the
// deferred DWM backdrop update at mid-transition (~45%).
class GlassThemeTransition
{
public:
    // Captures current RT as snapshot and starts transition animation.
    // Returns true if animation was started and a timer tick is needed.
    bool Start(HWND hwnd, ID2D1HwndRenderTarget* rt, POINT clickPt);

    // Advances the transition animation by one tick.
    // Invokes onApplyBackdrop when progress reaches >= 0.45 or on completion.
    // Returns true if the transition is still active.
    bool Step(float durationMs, const std::function<void()>& onApplyBackdrop);

    // Draws the fading snapshot overlay on rt.
    void DrawOverlay(ID2D1HwndRenderTarget* rt, float w, float h);

    // Resets transition state and releases the snapshot bitmap.
    void Reset();

    bool IsActive() const { return m_active; }
    float Progress() const { return m_progress; }
    bool PendingBackdropUpdate() const { return m_pendingBackdropUpdate; }
    void SetPendingBackdropUpdate(bool pending) { m_pendingBackdropUpdate = pending; }

private:
    void CaptureSnapshot(ID2D1HwndRenderTarget* rt);

    bool m_active = false;
    float m_progress = 0.0f;
    ULONGLONG m_startTime = 0;
    D2D1_POINT_2F m_center = { 0.0f, 0.0f };
    ComPtr<ID2D1Bitmap> m_oldBitmap;
    bool m_pendingBackdropUpdate = false;
};
