#pragma once
#include <windows.h>

struct ShadowSettings {
    int margin = 50;       // shadow margin in pixels (at 100% scale)
    int blurRadius = 30;   // gaussian blur radius in pixels (at 100% scale)
    int offsetX = 0;       // horizontal offset in pixels (at 100% scale)
    int offsetY = 6;       // vertical offset in pixels (at 100% scale)
    float opacity = 0.35f;  // shadow opacity (0.0 to 1.0)
    COLORREF color = RGB(0, 0, 0); // shadow color
};

class ShadowWindow
{
public:
    ShadowWindow(HWND hMainWnd);
    ~ShadowWindow();

    void SetSettings(const ShadowSettings& settings);
    void SyncPosition(bool mainVisible);
    void UpdateShadow(int mainWidth, int mainHeight, float physicalCornerRadius, float scale);
    void SetOpacity(float factor);
    void SetOpacityAndScale(float factor, float animScale, POINT animCenter);
    void Destroy();
    HWND GetHWND() const { return m_hShadowWnd; }

private:
    void RegisterClass();
    void CreateShadowWindow();
    void GenerateShadowBitmap(int w, int h, int radius, int margin, int offsetX, int offsetY, float cornerRadius, float opacity, COLORREF color);

    HWND m_hMainWnd = nullptr;
    HWND m_hShadowWnd = nullptr;
    HBITMAP m_hBitmap = nullptr;
    int m_cachedWidth = 0;
    int m_cachedHeight = 0;
    float m_cachedCornerRadius = 0.0f;
    float m_cachedScale = 1.0f;

    ShadowSettings m_settings;
    float m_animScale = 1.0f;
    POINT m_animCenter = { 0, 0 };
    float m_animOpacity = 1.0f;
};
