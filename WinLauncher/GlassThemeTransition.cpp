#include "GlassThemeTransition.h"
#include "DpiHelper.h"
#include "Config/UIStyle.h"
#include <algorithm>

void GlassThemeTransition::Reset()
{
    m_active = false;
    m_progress = 0.0f;
    m_startTime = 0;
    m_oldBitmap.Reset();
    m_pendingBackdropUpdate = false;
}

void GlassThemeTransition::CaptureSnapshot(ID2D1HwndRenderTarget* rt)
{
    m_oldBitmap.Reset();
    if (!rt)
        return;

    rt->Flush();

    D2D1_SIZE_U size = rt->GetPixelSize();
    if (size.width == 0 || size.height == 0)
        return;

    FLOAT dpiX = 96.0f;
    FLOAT dpiY = 96.0f;
    rt->GetDpi(&dpiX, &dpiY);

    D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
        rt->GetPixelFormat(),
        dpiX,
        dpiY);

    ComPtr<ID2D1Bitmap> snapshot;
    HRESULT hr = rt->CreateBitmap(size, nullptr, 0, props, &snapshot);
    if (SUCCEEDED(hr) && snapshot)
    {
        hr = snapshot->CopyFromRenderTarget(nullptr, rt, nullptr);
        if (SUCCEEDED(hr))
            m_oldBitmap = snapshot;
    }
}

bool GlassThemeTransition::Start(HWND hwnd, ID2D1HwndRenderTarget* rt, POINT clickPt)
{
    if (!UIStyle::Animation::IsEnabled())
    {
        m_active = false;
        m_oldBitmap.Reset();
        UIStyle::ThemeTransition::End();
        return false;
    }

    m_active = true;
    m_progress = 0.0f;
    m_startTime = GetTickCount64();
    UIStyle::ThemeTransition::SetProgress(0.0f);
    CaptureSnapshot(rt);

    RECT cr{};
    GetClientRect(hwnd, &cr);
    float scale = DpiHelper::GetWindowScale(hwnd);
    float w = (float)cr.right / scale;
    float h = (float)cr.bottom / scale;

    if (clickPt.x == -1 && clickPt.y == -1)
    {
        m_center = D2D1::Point2F(w / 2.0f, h / 2.0f);
    }
    else
    {
        m_center = D2D1::Point2F((float)clickPt.x, (float)clickPt.y);
    }

    return true;
}

bool GlassThemeTransition::Step(float durationMs, const std::function<void()>& onApplyBackdrop)
{
    if (!m_active)
        return false;

    if (durationMs <= 0.0f) durationMs = 1.0f;
    float elapsed = (float)(GetTickCount64() - m_startTime);
    m_progress = elapsed / durationMs;

    if (m_pendingBackdropUpdate && m_progress >= 0.45f)
    {
        if (onApplyBackdrop) onApplyBackdrop();
        m_pendingBackdropUpdate = false;
    }

    if (m_progress >= 1.0f)
    {
        m_progress = 1.0f;
        m_active = false;
        m_oldBitmap.Reset();
        if (m_pendingBackdropUpdate)
        {
            if (onApplyBackdrop) onApplyBackdrop();
            m_pendingBackdropUpdate = false;
        }
        UIStyle::ThemeTransition::End();
        return false;
    }

    UIStyle::ThemeTransition::SetProgress(m_progress);
    return true;
}

void GlassThemeTransition::DrawOverlay(ID2D1HwndRenderTarget* rt, float w, float h)
{
    if (!m_active || !m_oldBitmap || !rt)
        return;

    float opacity = 1.0f - UIStyle::ThemeTransition::BlendProgress();
    if (opacity <= 0.001f)
        return;
    if (opacity > 1.0f)
        opacity = 1.0f;

    rt->DrawBitmap(
        m_oldBitmap.Get(),
        D2D1::RectF(0.0f, 0.0f, w, h),
        opacity,
        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}
