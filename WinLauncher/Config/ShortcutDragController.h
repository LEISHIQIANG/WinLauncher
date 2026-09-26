#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d2d1.h>
#include <vector>
#include <functional>
#include "IConfigWindow.h"
#include "ShortcutGridViewHelper.h"

struct RendPopupPage;

struct ShortcutVisualState
{
    float currentX = 0.0f;
    float currentY = 0.0f;
    float targetX = 0.0f;
    float targetY = 0.0f;
    bool selected = false;
    float dragOffsetX = 0.0f;
    float dragOffsetY = 0.0f;
    // 0..1 fade-in progress for a late-arriving icon; 1 means fully
    // shown (the steady state for every initially rendered icon).
    float iconReveal = 1.0f;
};

class ShortcutDragController
{
public:
    ShortcutDragController();
    ~ShortcutDragController();

    bool HasCandidate() const { return m_dragIndex >= 0; }
    bool IsActive() const { return m_dragActive; }
    int GetDragIndex() const { return m_dragIndex; }
    int GetCurrentInsertIndex() const { return m_dragCurrentInsertIndex; }
    POINT GetDragStartPt() const { return m_dragStartPt; }

    void BeginCandidate(int shortcutIndex, POINT pt);
    void Reset();
    bool HasExceededThreshold(POINT pt) const;

    void UpdateCursor(HWND hWnd, POINT pt);
    HCURSOR GetDeleteCursor();

    bool StartDrag(
        IConfigWindow* owner,
        RendPopupPage* pageData,
        std::vector<ShortcutVisualState>& states,
        float scrollY,
        POINT pt);

    void UpdateDragAndSortState(
        POINT clientPt,
        float scrollY,
        std::vector<ShortcutVisualState>& states);

    void UpdateAutoScroll(
        HWND hWnd,
        float dt,
        const RendPopupPage* pageData,
        float& targetScrollY);

    void RenderInsertionSlots(
        ID2D1HwndRenderTarget* rt,
        ShortcutBrushCache& brushCache,
        const std::vector<ShortcutVisualState>& states,
        int totalShortcuts,
        float scrollY) const;

    void HandleLButtonUp(
        POINT pt,
        IConfigWindow* owner,
        RendPopupPage* pageData,
        std::vector<ShortcutVisualState>& states,
        float scrollY,
        int& selectionAnchorIndex,
        const std::function<bool(const std::vector<int>&, bool&)>& confirmPendingDelete,
        const std::function<void(bool)>& resetTargets,
        bool& repaint);

private:
    int m_dragIndex = -1;
    int m_dragCurrentInsertIndex = -1;
    bool m_dragActive = false;
    bool m_dragDeleteCursorShown = false;
    POINT m_dragStartPt = { 0, 0 };
    float m_grabOffsetX = 0.0f;
    float m_grabOffsetY = 0.0f;
    HCURSOR m_deleteCursor = nullptr;
};
