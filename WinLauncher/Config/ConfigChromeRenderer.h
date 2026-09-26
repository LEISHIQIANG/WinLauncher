#pragma once
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string>

class GlassWindow;

namespace ConfigChromeRenderer
{
    struct UpdatePillGeometry
    {
        static constexpr float kPillLeft = 72.0f;
        static constexpr float kTop = 10.0f;
        static constexpr float kBottom = 30.0f;

        static inline float PillRight(bool isDownloading) { return isDownloading ? 138.0f : 110.0f; }
        static inline float TextRight(bool isDownloading) { return isDownloading ? 126.0f : 98.0f; }
    };

    bool HitTestCloseButton(POINT pt);
    bool HitTestSettingsButton(POINT pt);
    bool HitTestAddButton(POINT pt);
    bool HitTestUpdatePillText(POINT pt, bool isDownloading);
    bool HitTestUpdatePillClose(POINT pt, bool isDownloading);

    void DrawHeaderTitle(ID2D1HwndRenderTarget* rt, IDWriteTextFormat* tfHeader, GlassWindow* glassWnd, bool showSettings);
    void DrawUpdatePill(ID2D1HwndRenderTarget* rt, IDWriteTextFormat* tfLeft, GlassWindow* glassWnd,
                        bool isDownloading, int downloadProgress, bool hoveredText, bool hoveredClose);
    void DrawCloseButton(ID2D1HwndRenderTarget* rt, GlassWindow* glassWnd, bool hoveredClose);
    void DrawSettingsButton(ID2D1HwndRenderTarget* rt, IDWriteTextFormat* tfLeft, GlassWindow* glassWnd,
                            bool showSettings, bool hoveredSettingsBtn);
    void DrawAddButton(ID2D1HwndRenderTarget* rt, IDWriteTextFormat* tfLeft, GlassWindow* glassWnd,
                       bool hoveredAddBtn, bool dropDownMenuVisible);
}
