#define NOMINMAX
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "GlassBackdrop.h"
#include "GlassWindow.h"
#include "DpiHelper.h"
#include "App/Logger.h"
#include "Config/UIStyle.h"
#include <dwmapi.h>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "dwmapi.lib")

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

using SWCAFn = BOOL(WINAPI*)(HWND, void*);
struct AccentPolicy { int s, f; unsigned int g; int a; };
struct WinCompAttr { int attr; void* data; size_t size; };

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

static const wchar_t* WindowModeName(int windowMode)
{
    return windowMode == 1 ? L"acrylic" : L"glass";
}

float GlassBackdrop::ClampCornerRadius(float radius, float width, float height)
{
    if (radius < 0.0f)
        return 0.0f;
    if (width > 0.0f && height > 0.0f)
    {
        float maxRadius = (std::min)(width, height) * 0.5f;
        if (radius > maxRadius)
            return maxRadius;
    }
    return radius;
}

void GlassBackdrop::SetAccent(HWND hwnd, int accentState, unsigned int gradientColor)
{
    auto u = GetModuleHandleW(L"user32.dll");
    if (!u) return;
    auto fn = (SWCAFn)GetProcAddress(u, "SetWindowCompositionAttribute");
    if (!fn) return;
    AccentPolicy ap{ accentState, 2, gradientColor, 0 };
    WinCompAttr d{ 19, &ap, sizeof(ap) };
    fn(hwnd, &d);
}

GlassBackdrop::GlassBackdrop()
{
    m_cornerRadius = IsWindows11OrLater() ? 8.0f : 0.0f;
    m_lastAppliedAccentState = -1;
}

void GlassBackdrop::UpdateWindowCornerRadius()
{
    m_cornerRadius = IsWindows11OrLater() ? 8.0f : 0.0f;
}

float GlassBackdrop::GetDrawCornerRadius(HWND hwnd, float renderScale, float width, float height) const
{
    float systemScale = GlassWindow::GetSystemWindowScale(hwnd);
    if (renderScale <= 0.0f)
        renderScale = systemScale > 0.0f ? systemScale : 1.0f;

    float radius = m_cornerRadius * (systemScale / renderScale);
    return ClampCornerRadius(radius, width, height);
}

float GlassBackdrop::GetPhysicalCornerRadius(HWND hwnd) const
{
    return ClampCornerRadius(m_cornerRadius * GlassWindow::GetSystemWindowScale(hwnd), 0.0f, 0.0f);
}

void GlassBackdrop::UpdateWindowRoundRegion(HWND hwnd)
{
    if (!hwnd)
        return;

    RECT cr{};
    if (!GetClientRect(hwnd, &cr))
        return;

    int width = cr.right - cr.left;
    int height = cr.bottom - cr.top;
    if (width <= 0 || height <= 0)
        return;

    if (m_cornerRadius <= 0.0f)
    {
        SetWindowRgn(hwnd, nullptr, TRUE);
        return;
    }

    if (IsWindows11OrLater())
    {
        SetWindowRgn(hwnd, nullptr, TRUE);
        return;
    }

    int radiusPx = (int)std::round(GetPhysicalCornerRadius(hwnd));
    if (radiusPx < 1) radiusPx = 1;

    HRGN region = CreateRoundRectRgn(0, 0, width + 1, height + 1, radiusPx * 2, radiusPx * 2);
    if (region && SetWindowRgn(hwnd, region, TRUE) == 0)
    {
        DeleteObject(region);
    }
}

void GlassBackdrop::ApplySystemBackdrop(GlassWindow* window)
{
    if (!window) return;
    HWND hWnd = window->GetHWND();
    if (!hWnd) return;

    MARGINS m{ -1, -1, -1, -1 };
    HRESULT frameHr = DwmExtendFrameIntoClientArea(hWnd, &m);

    UpdateWindowCornerRadius();
    HRESULT cornerHr = S_OK;
    if (m_cornerRadius > 0.0f)
    {
        DWORD corner = 2;
        cornerHr = DwmSetWindowAttribute(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    }
    UpdateWindowRoundRegion(hWnd);

    // Dynamically update the display affinity for the window and its shadow window
    SetWindowDisplayAffinitySafe(hWnd);
    if (window->m_shadowWindow)
    {
        SetWindowDisplayAffinitySafe(window->m_shadowWindow->GetHWND());
    }

    DWORD border = 0xFFFFFFFE;
    HRESULT borderHr = DwmSetWindowAttribute(hWnd, 34, &border, sizeof(border));

    int windowMode = 0;
    if (window->m_appCtx && window->m_appCtx->configService)
    {
        windowMode = window->m_appCtx->configService->GetWindowMode();
    }

    LOG_G_INFO_NODE(
        L"ui.glass",
        L"system_backdrop_apply",
        L"windowMode=%d(%s) frameHr=0x%08X cornerHr=0x%08X borderHr=0x%08X cornerRadius=%.2f hwnd=%p",
        windowMode,
        WindowModeName(windowMode),
        frameHr,
        cornerHr,
        borderHr,
        m_cornerRadius,
        hWnd);

    const int targetAccentState = (windowMode == 1) ? 4 : 2;
    const bool leavingHostedBackdrop =
        (m_lastAppliedAccentState == 4 && targetAccentState != 4);

    if (leavingHostedBackdrop)
    {
        // Acrylic (accent 4) installs a DWM-hosted backdrop visual. Switching
        // it directly to another accent hot-swaps the composition and can
        // leave a 1px strip of the rebuilt frame visible at the far edges
        // (window size / UI scale dependent). Pass through ACCENT_DISABLED
        // first so DWM tears the hosted backdrop down completely.
        SetAccent(hWnd, 0, 0x00000000);
    }

    if (windowMode == 1) // Acrylic
    {
        bool isLight = (UIStyle::GetThemeMode() == UIStyle::ThemeMode::Light);
        auto& cfg = isLight ? UIStyle::g_AcrylicLightConfig : UIStyle::g_AcrylicDarkConfig;

        D2D1_COLOR_F rgb = UIStyle::HslToRgb(cfg.hue, 0.0f, cfg.brightness, cfg.opacity);

        BYTE r = (BYTE)(fminf(fmaxf(rgb.r, 0.0f), 1.0f) * 255.0f);
        BYTE g = (BYTE)(fminf(fmaxf(rgb.g, 0.0f), 1.0f) * 255.0f);
        BYTE b = (BYTE)(fminf(fmaxf(rgb.b, 0.0f), 1.0f) * 255.0f);
        BYTE a = (BYTE)(cfg.opacity * 255.0f);

        unsigned int gradientColor = (a << 24) | (b << 16) | (g << 8) | r;
        SetAccent(hWnd, 4, gradientColor);
    }
    else // Glass (custom blur)
    {
        int backdropType = 1; // DWMSBT_DISABLE
        for (int attr : {38, 1029})
        {
            HRESULT backdropHr = DwmSetWindowAttribute(hWnd, attr, &backdropType, sizeof(backdropType));
            if (FAILED(backdropHr))
            {
                if (backdropHr == E_INVALIDARG)
                    LOG_G_DEBUG_NODE(L"ui.glass", L"dwm_backdrop_disable_unsupported", L"attr=%d hr=0x%08X hwnd=%p", attr, backdropHr, hWnd);
                else
                    LOG_G_WARNING_NODE(L"ui.glass", L"dwm_backdrop_disable_failed", L"attr=%d hr=0x%08X hwnd=%p", attr, backdropHr, hWnd);
            }
        }
        SetAccent(hWnd, 2, 0x00000000);
    }

    // Accent policy changes rebuild DWM's frame visuals and can drop window
    // attributes set before the change (border color override included, whose
    // default reappears as a 1px theme-colored edge line). Re-assert them
    // after the accent is final.
    {
        DWORD borderNone = 0xFFFFFFFE;
        DwmSetWindowAttribute(hWnd, 34, &borderNone, sizeof(borderNone));
    }
    if (leavingHostedBackdrop)
    {
        UpdateWindowCornerRadius();
        if (m_cornerRadius > 0.0f)
        {
            DWORD corner = 2;
            DwmSetWindowAttribute(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
        }
        UpdateWindowRoundRegion(hWnd);
        if (IsWindowVisible(hWnd))
        {
            SetWindowPos(hWnd, nullptr, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
        LOG_G_INFO_NODE(
            L"ui.glass",
            L"accent_transition_recovery",
            L"from=4 to=%d hwnd=%p",
            targetAccentState,
            hWnd);
    }
    m_lastAppliedAccentState = targetAccentState;

    if (UIStyle::Animation::IsEnabled() && !IsWindowVisible(hWnd))
    {
        LONG_PTR exStyle = GetWindowLongPtr(hWnd, GWL_EXSTYLE);
        if (!(exStyle & WS_EX_LAYERED))
        {
            SetWindowLongPtr(hWnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
        }
        SetLayeredWindowAttributes(hWnd, 0, 0, LWA_ALPHA);
    }
}
