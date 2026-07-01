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
    void ShowAddHotkeyDialog();
    void ShowAddUrlDialog();
    void ShowAddCommandDialog();
    void ShowAddMacroDialog();
    void ShowAddBatchDialog();
    void ShowBuiltinIconDialog();

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
    bool IsDragging() const { return m_dragActive; }

private:
    void EnsureIcons(ID2D1HwndRenderTarget* rt);
    void EnsureShortcutStates();
    bool HasDragExceededThreshold(POINT pt) const;
    void StartShortcutDrag(POINT pt);
    void UpdateDragAndSortState(POINT clientPt);
    int CountVisibleShortcuts() const;
    void UpdateAddShortcutTarget(bool compactPendingDelete = false, bool snap = false);
    void UpdateDragDeleteCursor(POINT pt);
    HCURSOR GetDeleteCursor();
    bool IsShortcutPendingDelete(int index) const;
    std::vector<int> GetSelectedShortcutIndices() const;
    std::vector<int> NormalizeShortcutIndices(const std::vector<int>& indices) const;
    bool IsPointOutsideWindow(POINT pt) const;
    void ResetShortcutTargets(bool compactPendingDelete = false);
    void DeleteShortcuts(const std::vector<int>& sortedIndices);
    bool ConfirmAndDeleteShortcuts(const std::vector<int>& indices, bool& repaint);
    bool ConfirmPendingDeleteShortcuts(const std::vector<int>& indices, bool& repaint);
    void AddShortcutFromPath(const std::wstring& filePath);
    void AddShortcutFromSingleFile(const std::wstring& path);
    ID2D1Bitmap* CreateShortcutBitmap(const RendShortcutInfo& shortcut) const;

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
    bool m_dragActive;
    bool m_dragDeleteCursorShown;
    HCURSOR m_deleteCursor;
    float m_grabOffsetX;
    float m_grabOffsetY;
    std::vector<ShortcutVisualState> m_shortcutStates;
    std::vector<int> m_pendingDeleteIndices;

    int m_selectionAnchorIndex;
    POINT m_dragStartPt;
    float m_addCardCurrentX = 0.0f;
    float m_addCardCurrentY = 0.0f;
    float m_addCardTargetX = 0.0f;
    float m_addCardTargetY = 0.0f;
    bool m_addCardInitialized = false;

    ID2D1HwndRenderTarget* m_lastRt;
    float m_lastDpi = 96.0f;
    int m_lastIconBitmapSize = 0;
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
