#pragma once
#include <windows.h>
#include <d2d1.h>

class GlassWindow;

// Collaborator responsible for DWM window corner preferences, accent policy,
// system backdrop application, and physical/render corner radius calculations.
class GlassBackdrop
{
public:
    GlassBackdrop();
    ~GlassBackdrop() = default;

    void ApplySystemBackdrop(GlassWindow* window);
    void UpdateWindowCornerRadius();
    void UpdateWindowRoundRegion(HWND hwnd);

    float GetDrawCornerRadius(HWND hwnd, float renderScale, float width = 0.0f, float height = 0.0f) const;
    float GetPhysicalCornerRadius(HWND hwnd) const;

    float CornerRadius() const noexcept { return m_cornerRadius; }
    int LastAppliedAccentState() const noexcept { return m_lastAppliedAccentState; }

    static float ClampCornerRadius(float radius, float width, float height);
    static void SetAccent(HWND hwnd, int accentState, unsigned int gradientColor);

private:
    float m_cornerRadius = 8.0f;
    int m_lastAppliedAccentState = -1;
};
