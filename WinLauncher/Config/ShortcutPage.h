#pragma once
#include "ConfigPage.h"
#include "../ShortcutManager.h"
#include <vector>
#include <unordered_map>
#include <d2d1.h>
#include <wrl.h>

using Microsoft::WRL::ComPtr;

class IConfigWindow;

class ShortcutPage : public ConfigPage
{
public:
    ShortcutPage(IConfigWindow* owner);
    virtual ~ShortcutPage() override;

    // Caller must ensure vector stability — re-obtain after any m_pages mutation
    void SetPageData(RendPopupPage* page, bool preserveScroll = false);
    void ShowAddShortcutDialog();

    virtual void OnPaint(ID2D1HwndRenderTarget* rt, const D2D1_RECT_F& rect) override;
    virtual void OnMouseMove(POINT pt, bool& repaint) override;
    virtual void OnMouseLeave(bool& repaint) override;
    virtual void OnLButtonDown(POINT pt, bool& repaint) override;
    virtual void OnLButtonDblClk(POINT pt, bool& repaint) override;
    virtual void OnLButtonUp(POINT pt, bool& repaint) override;
    virtual void OnRButtonDown(POINT pt, bool& repaint) override;

    void EditShortcut(int index, bool& repaint);
    virtual void OnMouseWheel(short zDelta, POINT pt, bool& repaint) override;
    virtual void OnDropFiles(HDROP hDrop, bool& repaint) override;
    virtual bool IsAnimating() const override { return m_animating; }
    virtual void UpdateAnimation(float dt, bool& repaint) override;
    void UpdateTheme();
    bool IsDragging() const { return m_dragIndex >= 0; }

private:
    void EnsureIcons(ID2D1HwndRenderTarget* rt);
    void EnsureShortcutStates();
    void UpdateDragAndSortState(POINT clientPt);
    void AddShortcutFromPath(const std::wstring& filePath);
    void AddShortcutFromSingleFile(const std::wstring& path);

    int HitTestShortcut(POINT pt);
    bool HitTestAddShortcut(POINT pt);

    IConfigWindow* m_owner;
    RendPopupPage* m_pageData = nullptr;

    // Hover states
    int m_hoveredShortcut;
    bool m_hoveredAddShortcut;

    // Scroll states
    float m_scrollY;
    float m_targetScrollY;
    float m_scrollVelocity;
    bool m_animating;

    // Drag-and-drop sorting states
    struct ShortcutVisualState
    {
        float currentX = 0.0f;
        float currentY = 0.0f;
        float targetX = 0.0f;
        float targetY = 0.0f;
        bool selected = false;
        float dragOffsetX = 0.0f;
        float dragOffsetY = 0.0f;
    };

    int m_dragIndex;
    int m_dragCurrentInsertIndex;
    float m_grabOffsetX;
    float m_grabOffsetY;
    std::vector<ShortcutVisualState> m_shortcutStates;

    int m_selectionAnchorIndex;
    POINT m_dragStartPt;

    ID2D1HwndRenderTarget* m_lastRt;
    bool m_trackMouse;

    // Cached D2D brushes for OnPaint
    struct BrushCacheEntry
    {
        D2D1_COLOR_F color;
        ComPtr<ID2D1SolidColorBrush> brush;
    };
    std::vector<BrushCacheEntry> m_brushCache;
    std::unordered_map<ID2D1Bitmap*, ComPtr<ID2D1BitmapBrush>> m_bmpBrushCache;
    ComPtr<ID2D1SolidColorBrush> GetOrCreateBrush(ID2D1HwndRenderTarget* rt, const D2D1_COLOR_F& color);
    ComPtr<ID2D1BitmapBrush> GetOrCreateBitmapBrush(ID2D1HwndRenderTarget* rt, ID2D1Bitmap* bmp);
};
