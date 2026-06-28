#pragma once
#include "BaseWindow.h"
#include "App/AppContext.h"
#include "UI/Render/Compositor.h"
#include "ShadowWindow.h"
#include <d2d1.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl.h>
#include <vector>
#include <memory>
#include <functional>

using Microsoft::WRL::ComPtr;

class GlassWindow : public BaseWindow
{
public:
    GlassWindow();
    virtual ~GlassWindow();

    enum class AnimState
    {
        None,
        Opening,
        Closing
    };

    void StartOpenTransition();
    void StartCloseTransition(std::function<void()> onComplete = nullptr);

    static float GetWindowScale(HWND hwnd);
    static float GetDpiScaleForMonitor(HMONITOR hMonitor);

    ID2D1Factory* GetD2DFactory() const { return m_d2d.Get(); }
    IDWriteFactory* GetDWFactory() const { return m_dw.Get(); }
    void UpdateTheme();

    // Returns D2D render target for subclasses that need custom rendering
    ID2D1HwndRenderTarget* GetRenderTarget() const { return m_rt.Get(); }

    // Cached brush helpers
    ComPtr<ID2D1SolidColorBrush> GetOrCreateBrush(const D2D1_COLOR_F& color);
    ComPtr<ID2D1SolidColorBrush> GetCachedBrush(const D2D1_COLOR_F& color);

protected:
    virtual void OnPaintContent(ID2D1HwndRenderTarget* rt) = 0;
    virtual LRESULT HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override;

    void ApplySystemBackdrop();
    bool EnsureD2D();
    void ReleaseD2D();
    void CaptureBackground();
    void CompositeBackgroundToCache();
    void DoPaint();

    // Compositor-based rendering (long-term)
    void SetCompositor(std::unique_ptr<Compositor> compositor)
    {
        m_compositor = std::move(compositor);
        if (m_compositor && m_rt)
        {
            auto* bg = m_compositor->GetBackgroundLayer();
            if (bg) bg->SetRenderTarget(m_rt.Get());
            m_compositor->MarkAllDirty();
        }
    }
    Compositor* GetCompositor() { return m_compositor.get(); }

    // D2D & DWrite factories/resources
    ComPtr<ID2D1Factory>          m_d2d;
    ComPtr<ID2D1HwndRenderTarget> m_rt;
    ComPtr<IDWriteFactory>        m_dw;
    ComPtr<IDWriteTextFormat>     m_tf;

    // Background capture
    ComPtr<ID2D1Bitmap>       m_bgCap;    // raw screen capture
    ComPtr<ID2D1Bitmap>       m_bgFinal;  // pre-composited (blur + sheen + tint + border)
    ComPtr<ID2D1BitmapRenderTarget> m_compositeRt;  // cached composite RT
    std::vector<DWORD>        m_pixbuf;
    bool                      m_bgDirty = true;

    // Cached effects and resources for CompositeBackgroundToCache
    ComPtr<ID2D1Effect>       m_blurEffect;
    ComPtr<ID2D1Effect>       m_satEffect;
    ComPtr<ID2D1GradientStopCollection> m_sheenGsc;
    ComPtr<ID2D1RadialGradientBrush>    m_sheenBrush;
    D2D1_SIZE_F               m_effectWinSize = {};

    // Corner radius for window decoration; 0 = no DWM rounded corners (Win10)
    float m_cornerRadius = 8.0f;

    // Background refresh rate (ms). 11 = ~90 fps for live background behind the window.
    UINT     m_bgRefreshMs = 11;

protected:
    virtual ShadowSettings GetShadowSettings() const;

    virtual void GetAnimationTransform(float w, float h, float progress, AnimState state, D2D1_MATRIX_3X2_F& transform);

    void CaptureTransitionSnapshot();
    void StartThemeTransition(POINT clickPt);

    AnimState m_animState = AnimState::None;
    float m_animProgress = 0.0f;
    ULONGLONG m_animStartTime = 0;
    std::function<void()> m_animOnComplete = nullptr;
    D2D1_POINT_2F m_animCenter = { 0.0f, 0.0f };

    bool m_themeTransitionActive = false;
    float m_themeTransitionProgress = 0.0f;
    ULONGLONG m_themeTransitionStartTime = 0;
    D2D1_POINT_2F m_themeTransitionCenter = { 0.0f, 0.0f };
    ComPtr<ID2D1Bitmap> m_themeTransitionOldBitmap = nullptr;
    ComPtr<ID2D1Layer> m_themeTransitionLayer = nullptr;
    ComPtr<ID2D1GradientStopCollection> m_themeTransitionStopCollection = nullptr;

public:
    // App context for dependency injection (public for free function access)
    AppContext* m_appCtx = nullptr;

private:
    struct BrushCacheEntry
    {
        D2D1_COLOR_F color;
        ComPtr<ID2D1SolidColorBrush> brush;
    };
    std::vector<BrushCacheEntry> m_brushCache;

    // Compositor-based rendering (optional, long-term)
    std::unique_ptr<Compositor> m_compositor;

    std::unique_ptr<ShadowWindow> m_shadowWindow;
};
