#pragma once
#include <Windows.h>
#include <d2d1.h>
#include <vector>

class IConfigWindow;

class CategoryList
{
public:
    CategoryList(IConfigWindow* owner);
    ~CategoryList();

    void OnPaint(ID2D1HwndRenderTarget* rt, const D2D1_RECT_F& rect);
    void OnMouseMove(POINT pt, bool& repaint);
    void OnMouseLeave(bool& repaint);
    void OnLButtonDown(POINT pt, bool& repaint);
    void OnLButtonUp(POINT pt, bool& repaint);
    virtual void OnRButtonDown(POINT pt, bool& repaint);
    bool IsAnimating() const { return m_animating || m_isAnimatingSelect; }
    void UpdateAnimation(float dt, bool& repaint);
    void Reset();

private:
    struct CategoryVisualState
    {
        float currentY = 0.0f;
        float targetY = 0.0f;
    };

    int HitTestCategory(POINT pt);
    bool HitTestAddCategory(POINT pt);

    void EnsureCategoryStates();
    void UpdateDragAndSortState(POINT pt);
    void DrawCategoryItem(ID2D1HwndRenderTarget* rt, int i, float cy, bool isActive, bool isHovered, bool isDragging, const D2D1_COLOR_F& baseClr, IDWriteTextFormat* tfLeft);
    void BeginSelectAnimation(float targetY);

    IConfigWindow* m_owner;

    int m_hoveredCategory;
    bool m_hoveredAddCategory;
    bool m_trackMouse;

    // Drag and sort states
    std::vector<CategoryVisualState> m_categoryStates;
    int m_dragIndex;
    int m_dragCurrentInsertIndex;
    float m_grabOffsetY;
    bool m_animating;

    float m_selectAnimCurrentY = -1.0f;
    float m_selectAnimTargetY = -1.0f;
    float m_selectAnimStartY = -1.0f;
    float m_selectAnimElapsed = 0.0f;
    bool m_isAnimatingSelect = false;
};
