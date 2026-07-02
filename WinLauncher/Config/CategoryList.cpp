#include "CategoryList.h"
#include "IConfigWindow.h"
#include "ConfirmWindow.h"
#include "PromptWindow.h"
#include "ContextMenu.h"
#include "SceneSettingsWindow.h"
#include "UIStyle.h"
#include "../DpiHelper.h"
#include "../GlassWindow.h"
#include "../ShortcutManager.h"
#include "../Services/SyncFolderService.h"
#include <shobjidl.h>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>

namespace
{
    constexpr float kCategoryLeft = 10.0f;
    constexpr float kCategoryRight = 140.0f;
    constexpr float kCategoryTop = 72.0f;
    constexpr float kCategoryItemHeight = 32.0f;
    constexpr float kCategoryStep = 40.0f;
    constexpr float kCategoryBottomPadding = 10.0f;
    constexpr float kAddCategoryReservedHeight = 52.0f;
    constexpr int kDragStartThresholdPx = 4;

    struct CopiedSceneSettings
    {
        bool hasData = false;
        Model::PageSceneMode mode = Model::PageSceneMode::Whitelist;
        std::vector<std::wstring> selectedApps;
        std::vector<std::wstring> availableApps;
    };

    CopiedSceneSettings g_copiedSceneSettings;
}

CategoryList::CategoryList(IConfigWindow* owner)
    : m_owner(owner)
    , m_hoveredCategory(-1)
    , m_hoveredAddCategory(false)
    , m_trackMouse(false)
    , m_dragIndex(-1)
    , m_pendingDragIndex(-1)
    , m_pendingDragStartPt({ 0, 0 })
    , m_dragCurrentInsertIndex(-1)
    , m_grabOffsetY(0.0f)
    , m_animating(false)
    , m_selectAnimCurrentY(-1.0f)
    , m_selectAnimTargetY(-1.0f)
    , m_selectAnimStartY(-1.0f)
    , m_selectAnimElapsed(0.0f)
    , m_isAnimatingSelect(false)
    , m_scrollY(0.0f)
    , m_targetScrollY(0.0f)
    , m_scrollVelocity(0.0f)
{
}

CategoryList::~CategoryList()
{
}

void CategoryList::Reset()
{
    m_categoryStates.clear();
    m_dragIndex = -1;
    m_pendingDragIndex = -1;
    m_pendingDragStartPt = { 0, 0 };
    m_dragCurrentInsertIndex = -1;
    m_animating = false;
    m_selectAnimCurrentY = -1.0f;
    m_selectAnimTargetY = -1.0f;
    m_selectAnimStartY = -1.0f;
    m_selectAnimElapsed = 0.0f;
    m_isAnimatingSelect = false;
    m_scrollY = 0.0f;
    m_targetScrollY = 0.0f;
    m_scrollVelocity = 0.0f;
}

static float EaseOutCubicLocal(float t)
{
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float p = 1.0f - t;
    return 1.0f - p * p * p;
}

void CategoryList::BeginSelectAnimation(float targetY)
{
    if (m_selectAnimCurrentY < 0.0f || !UIStyle::Animation::IsEnabled())
    {
        m_selectAnimCurrentY = targetY;
        m_selectAnimTargetY = targetY;
        m_isAnimatingSelect = false;
        return;
    }

    if (std::abs(targetY - m_selectAnimTargetY) <= 0.1f)
        return;

    m_selectAnimTargetY = targetY;
    m_isAnimatingSelect = true;
}

void CategoryList::EnsureCategoryStates()
{
    if (m_owner->IsSettingsMode())
    {
        m_categoryStates.clear();
        ClampScroll();
        return;
    }
    size_t count = m_owner->GetCategoryCount();
    if (m_categoryStates.size() != count)
    {
        size_t oldSize = m_categoryStates.size();
        m_categoryStates.resize(count);
        for (size_t i = oldSize; i < count; i++)
        {
            float targetY = kCategoryTop + (float)i * kCategoryStep;
            m_categoryStates[i].currentY = targetY;
            m_categoryStates[i].targetY = targetY;
        }
    }
    ClampScroll();
}

float CategoryList::GetListViewportBottom() const
{
    RECT cr{};
    GetClientRect(m_owner->GetWindowHWND(), &cr);
    float scale = GlassWindow::GetWindowScale(m_owner->GetWindowHWND());
    float h = (scale > 0.0f) ? (float)cr.bottom / scale : (float)cr.bottom;
    float reserved = m_owner->IsSettingsMode() ? kCategoryBottomPadding : kAddCategoryReservedHeight;
    return (std::max)(kCategoryTop, h - reserved);
}

float CategoryList::GetMaxScrollY() const
{
    size_t count = m_owner->GetCategoryCount();
    if (count == 0) return 0.0f;

    float viewportH = GetListViewportBottom() - kCategoryTop;
    if (viewportH <= 0.0f) return 0.0f;

    float contentH = ((float)count - 1.0f) * kCategoryStep + kCategoryItemHeight;
    return (std::max)(0.0f, contentH - viewportH);
}

void CategoryList::ClampScroll()
{
    float maxScrollY = GetMaxScrollY();
    m_targetScrollY = (std::max)(0.0f, (std::min)(m_targetScrollY, maxScrollY));
    m_scrollY = (std::max)(0.0f, (std::min)(m_scrollY, maxScrollY));
    if (m_scrollY == 0.0f || m_scrollY == maxScrollY)
    {
        m_scrollVelocity = 0.0f;
    }
}

bool CategoryList::HitTestListViewport(POINT pt) const
{
    return pt.x >= 0 && pt.x < 150 &&
        pt.y >= (int)kCategoryTop &&
        pt.y <= (int)GetListViewportBottom();
}

void CategoryList::EnsureCurrentCategoryVisible()
{
    int currentCategory = m_owner->GetCurrentCategoryIndex();
    if (currentCategory < 0 || currentCategory >= (int)m_owner->GetCategoryCount())
        return;

    float viewportBottom = GetListViewportBottom();
    float maxScrollY = GetMaxScrollY();
    float itemTop = kCategoryTop + (float)currentCategory * kCategoryStep;
    float itemBottom = itemTop + kCategoryItemHeight;

    if (itemTop - m_targetScrollY < kCategoryTop)
    {
        m_targetScrollY = itemTop - kCategoryTop;
    }
    else if (itemBottom - m_targetScrollY > viewportBottom)
    {
        m_targetScrollY = itemBottom - viewportBottom;
    }

    m_targetScrollY = (std::max)(0.0f, (std::min)(m_targetScrollY, maxScrollY));
    if (!UIStyle::Animation::IsEnabled())
    {
        m_scrollY = m_targetScrollY;
        m_scrollVelocity = 0.0f;
    }
    else if (std::abs(m_targetScrollY - m_scrollY) > 0.1f)
    {
        m_animating = true;
        m_owner->StartAnimation();
    }
}

void CategoryList::MoveSelectionToCategory(int index)
{
    if (index < 0 || index >= (int)m_owner->GetCategoryCount())
        return;

    EnsureCategoryStates();

    float selectedY = kCategoryTop + (float)index * kCategoryStep - m_scrollY;
    if (!m_owner->IsSettingsMode() && index < (int)m_categoryStates.size())
    {
        selectedY = m_categoryStates[index].targetY - m_scrollY;
    }

    if (UIStyle::Animation::IsEnabled() && m_selectAnimCurrentY >= 0.0f)
    {
        BeginSelectAnimation(selectedY);
        m_animating = true;
        m_owner->StartAnimation();
    }
    else
    {
        m_selectAnimCurrentY = selectedY;
        m_selectAnimTargetY = selectedY;
        m_isAnimatingSelect = false;
    }
}

void CategoryList::DrawCategoryItem(ID2D1HwndRenderTarget* rt, int i, float cy, bool isActive, bool isHovered, bool isDragging, const D2D1_COLOR_F& baseClr, IDWriteTextFormat* tfLeft)
{
    D2D1_RECT_F itemRect = D2D1::RectF(kCategoryLeft, cy, kCategoryRight, cy + kCategoryItemHeight);
    D2D1_ROUNDED_RECT roundedItem = D2D1::RoundedRect(itemRect, 6.0f, 6.0f);

    bool isShortcutDragTarget = isHovered && m_owner->IsDraggingShortcut();
    bool animEnabled = UIStyle::Animation::IsEnabled();
    bool drawActiveStyle = isActive && !animEnabled;

    // Background Card
    float alphaBg = drawActiveStyle ? 0.18f : (isHovered ? 0.08f : 0.018f);
    float alphaBorder = drawActiveStyle ? 0.40f : (isHovered ? 0.14f : 0.045f);

    if (isDragging)
    {
        alphaBg = 0.20f;
        alphaBorder = 0.42f;
    }
    else if (isShortcutDragTarget)
    {
        alphaBg = 0.16f;
        alphaBorder = 0.44f;
    }

    ID2D1SolidColorBrush* bgBrush = nullptr;
    if (isDragging || isShortcutDragTarget || drawActiveStyle)
    {
        D2D1_COLOR_F accentClr = UIStyle::ThemeColor::Accent().d2d;
        accentClr.a = alphaBg;
        rt->CreateSolidColorBrush(accentClr, &bgBrush);
    }
    else
    {
        rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, alphaBg), &bgBrush);
    }

    if (bgBrush)
    {
        rt->FillRoundedRectangle(roundedItem, bgBrush);
        bgBrush->Release();
    }

    ID2D1SolidColorBrush* borderBrush = nullptr;
    if (isDragging || isShortcutDragTarget || drawActiveStyle)
    {
        D2D1_COLOR_F accentClr = UIStyle::ThemeColor::Accent().d2d;
        accentClr.a = drawActiveStyle ? 0.55f : alphaBorder;
        rt->CreateSolidColorBrush(accentClr, &borderBrush);
    }
    else
    {
        rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, alphaBorder), &borderBrush);
    }

    if (borderBrush)
    {
        float strokeWidth = UIStyle::Metrics::ControlStroke();
        if (isDragging) strokeWidth = UIStyle::Metrics::EmphasisStroke();
        else if (isShortcutDragTarget) strokeWidth = UIStyle::Metrics::EmphasisStroke();
        else if (drawActiveStyle) strokeWidth = UIStyle::Metrics::HairlineStroke();
        rt->DrawRoundedRectangle(roundedItem, borderBrush, strokeWidth);
        borderBrush->Release();
    }

    // Accent indicator bar (Active category)
    if (drawActiveStyle)
    {
        ID2D1SolidColorBrush* accentBrush = nullptr;
        rt->CreateSolidColorBrush(UIStyle::ThemeColor::Accent().d2d, &accentBrush);
        if (accentBrush)
        {
            D2D1_ROUNDED_RECT accentRect = D2D1::RoundedRect(
                D2D1::RectF(12, cy + 8, 15, cy + 24), 1.5f, 1.5f);
            rt->FillRoundedRectangle(accentRect, accentBrush);
            accentBrush->Release();
        }
    }

    // Category Text
    if (tfLeft)
    {
        float textLeft = 18.0f;
        D2D1_COLOR_F textClr = UIStyle::ThemeColor::TextNormal().d2d;

        if (animEnabled && m_selectAnimCurrentY >= 0.0f)
        {
            float dist = std::abs(cy - m_selectAnimCurrentY);
            float factor = 0.0f;
            if (dist < kCategoryStep) factor = 1.0f - dist / kCategoryStep;

            textLeft = 18.0f + 4.0f * factor;
            textClr = UIStyle::LerpColor(
                UIStyle::ThemeColor::TextNormal().d2d,
                UIStyle::ThemeColor::Accent().d2d,
                factor
            );
        }
        else if (isActive)
        {
            textLeft = 22.0f;
            textClr = UIStyle::ThemeColor::Accent().d2d;
        }

        ID2D1SolidColorBrush* tb = nullptr;
        rt->CreateSolidColorBrush(textClr, &tb);
        if (tb)
        {
            std::wstring name = m_owner->GetCategoryName(i);
            rt->DrawTextW(name.c_str(), (UINT32)name.size(), tfLeft,
                D2D1::RectF(textLeft, cy, 115, cy + kCategoryItemHeight), tb);
            tb->Release();
        }
    }

    RendPopupPage* page = m_owner->GetPageByIndex(i);
    if (page && !page->sceneApps.empty())
    {
        ID2D1SolidColorBrush* sceneBrush = nullptr;
        D2D1_COLOR_F dotClr = UIStyle::ThemeColor::Accent().d2d;
        dotClr.a = 0.92f;
        rt->CreateSolidColorBrush(dotClr, &sceneBrush);
        if (sceneBrush)
        {
            D2D1_ELLIPSE dot = D2D1::Ellipse(
                D2D1::Point2F(kCategoryRight - 12.0f, cy + kCategoryItemHeight * 0.5f),
                2.4f,
                2.4f);
            rt->FillEllipse(dot, sceneBrush);
            sceneBrush->Release();
        }
    }
}

void CategoryList::OnPaint(ID2D1HwndRenderTarget* rt, const D2D1_RECT_F& rect)
{
    ClampScroll();

    // Draw Category List Title
    IDWriteTextFormat* tfTitle = m_owner->GetTitleFont();
    if (tfTitle)
    {
        ID2D1SolidColorBrush* textBrush = nullptr;
        rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextMuted().d2d, &textBrush);
        if (textBrush)
        {
            rt->DrawTextW(L"分类列表", 4, tfTitle, D2D1::RectF(kCategoryLeft, 42, kCategoryRight, 62), textBrush);
            textBrush->Release();
        }
    }

    size_t count = m_owner->GetCategoryCount();
    int currentCategory = m_owner->GetCurrentCategoryIndex();
    IDWriteTextFormat* tfLeft = m_owner->GetLeftFont();

    D2D1_COLOR_F baseClr = UIStyle::ThemeColor::ThemeBase().d2d;

    EnsureCategoryStates();

    float viewportBottom = GetListViewportBottom();
    rt->PushAxisAlignedClip(D2D1::RectF(0.0f, kCategoryTop, 150.0f, viewportBottom), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // Draw sliding active background and indicator if animations are enabled
    if (UIStyle::Animation::IsEnabled() && m_selectAnimCurrentY >= 0.0f)
    {
        float selectionY = std::roundf(m_selectAnimCurrentY);
        if (m_dragIndex >= 0 && m_dragIndex == currentCategory && m_dragIndex < (int)m_categoryStates.size())
        {
            selectionY = std::roundf(m_categoryStates[m_dragIndex].currentY - m_scrollY);
        }
        D2D1_RECT_F itemRect = D2D1::RectF(kCategoryLeft, selectionY, kCategoryRight, selectionY + kCategoryItemHeight);
        D2D1_ROUNDED_RECT roundedItem = D2D1::RoundedRect(itemRect, 6.0f, 6.0f);

        ID2D1SolidColorBrush* bgBrush = nullptr;
        D2D1_COLOR_F accentClr = UIStyle::ThemeColor::Accent().d2d;
        accentClr.a = 0.18f;
        rt->CreateSolidColorBrush(accentClr, &bgBrush);
        if (bgBrush)
        {
            rt->FillRoundedRectangle(roundedItem, bgBrush);
            bgBrush->Release();
        }

        ID2D1SolidColorBrush* borderBrush = nullptr;
        accentClr.a = 0.55f;
        rt->CreateSolidColorBrush(accentClr, &borderBrush);
        if (borderBrush)
        {
            rt->DrawRoundedRectangle(roundedItem, borderBrush, UIStyle::Metrics::HairlineStroke());
            borderBrush->Release();
        }

        ID2D1SolidColorBrush* accentBrush = nullptr;
        rt->CreateSolidColorBrush(UIStyle::ThemeColor::Accent().d2d, &accentBrush);
        if (accentBrush)
        {
            D2D1_ROUNDED_RECT accentRect = D2D1::RoundedRect(
                D2D1::RectF(12, selectionY + 8, 15, selectionY + 24), 1.5f, 1.5f);
            rt->FillRoundedRectangle(accentRect, accentBrush);
            accentBrush->Release();
        }
    }

    // Draw Categories
    for (int i = 0; i < (int)count; i++)
    {
        if (m_owner->IsSettingsMode())
        {
            float cy = kCategoryTop + (float)i * kCategoryStep - m_scrollY;
            bool isActive = (i == currentCategory);
            bool isHovered = (i == m_hoveredCategory);
            DrawCategoryItem(rt, i, cy, isActive, isHovered, false, baseClr, tfLeft);
        }
        else
        {
            if (i == m_dragIndex) continue;
            if (i >= (int)m_categoryStates.size()) continue;

            float cy = std::roundf(m_categoryStates[i].currentY - m_scrollY);
            bool isActive = (i == currentCategory);
            bool isHovered = (i == m_hoveredCategory) && (m_dragIndex == -1);
            DrawCategoryItem(rt, i, cy, isActive, isHovered, false, baseClr, tfLeft);
        }
    }

    // Draw Dragged Category on top
    if (m_dragIndex >= 0 && m_dragIndex < (int)count && m_dragIndex < (int)m_categoryStates.size())
    {
        float cy = std::roundf(m_categoryStates[m_dragIndex].currentY - m_scrollY);
        bool isActive = (m_dragIndex == currentCategory);
        DrawCategoryItem(rt, m_dragIndex, cy, isActive, false, true, baseClr, tfLeft);
    }

    rt->PopAxisAlignedClip();

    // "+ 新建分类" Button
    if (!m_owner->IsSettingsMode())
    {
        D2D1_RECT_F addCatRect = D2D1::RectF(kCategoryLeft, rect.bottom - 42.0f, kCategoryRight, rect.bottom - kCategoryBottomPadding);
        D2D1_ROUNDED_RECT roundedAdd = D2D1::RoundedRect(addCatRect, 6.0f, 6.0f);

        float alphaBg = m_hoveredAddCategory ? 0.08f : 0.018f;
        float alphaBorder = m_hoveredAddCategory ? 0.14f : 0.055f;

        ID2D1SolidColorBrush* bgBrush = nullptr;
        rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, alphaBg), &bgBrush);
        if (bgBrush)
        {
            rt->FillRoundedRectangle(roundedAdd, bgBrush);
            bgBrush->Release();
        }

        ID2D1SolidColorBrush* borderBrush = nullptr;
        rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, alphaBorder), &borderBrush);
        if (borderBrush)
        {
            rt->DrawRoundedRectangle(roundedAdd, borderBrush, UIStyle::Metrics::ControlStroke());
            borderBrush->Release();
        }

        IDWriteTextFormat* tfDefault = m_owner->GetDefaultFont();
        if (tfDefault)
        {
            tfDefault->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            ID2D1SolidColorBrush* tb = nullptr;
            rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextMuted().d2d, &tb);
            if (tb)
            {
                rt->DrawTextW(L"+ 新建分类", 6, tfDefault, addCatRect, tb);
                tb->Release();
            }
        }
    }
}

void CategoryList::OnMouseMove(POINT pt, bool& repaint)
{
    if (!m_trackMouse)
    {
        m_trackMouse = true;
    }

    if (m_dragIndex >= 0)
    {
        UpdateDragAndSortState(pt);
        m_animating = true;
        m_owner->StartAnimation();
        repaint = true;
        return;
    }

    if (m_pendingDragIndex >= 0)
    {
        int dx = pt.x - m_pendingDragStartPt.x;
        int dy = pt.y - m_pendingDragStartPt.y;
        if (dx * dx + dy * dy >= kDragStartThresholdPx * kDragStartThresholdPx)
        {
            m_dragIndex = m_pendingDragIndex;
            m_dragCurrentInsertIndex = m_pendingDragIndex;
            EnsureCategoryStates();

            float origY = kCategoryTop + (float)m_dragIndex * kCategoryStep;
            m_grabOffsetY = (float)m_pendingDragStartPt.y + m_scrollY - origY;

            UpdateDragAndSortState(pt);
            m_animating = true;
            m_owner->StartAnimation();
            repaint = true;
            return;
        }
    }

    int hc = HitTestCategory(pt);
    bool hac = m_owner->IsSettingsMode() ? false : HitTestAddCategory(pt);

    if (hc != m_hoveredCategory || hac != m_hoveredAddCategory)
    {
        m_hoveredCategory = hc;
        m_hoveredAddCategory = hac;
        repaint = true;
    }
}

void CategoryList::OnMouseLeave(bool& repaint)
{
    m_hoveredCategory = -1;
    m_hoveredAddCategory = false;
    m_trackMouse = false;
    repaint = true;
}

void CategoryList::OnLButtonDown(POINT pt, bool& repaint)
{
    HWND hwnd = m_owner->GetWindowHWND();

    if (!m_owner->IsSettingsMode() && HitTestAddCategory(pt))
    {
        m_owner->AddCategory(L"");
        EnsureCategoryStates();
        EnsureCurrentCategoryVisible();
        repaint = true;
        return;
    }

    int hc = HitTestCategory(pt);
    if (hc >= 0 && hc < (int)m_owner->GetCategoryCount())
    {
        m_owner->SetCurrentCategoryIndex(hc);
        MoveSelectionToCategory(hc);

        // Start dragging if not in Settings mode and not the DOCK category
        if (!m_owner->IsSettingsMode() && m_owner->GetCategoryName(hc) != L"DOCK")
        {
            m_pendingDragIndex = hc;
            m_pendingDragStartPt = pt;
            SetCapture(hwnd);
        }

        repaint = true;
    }
}

void CategoryList::OnLButtonUp(POINT pt, bool& repaint)
{
    if (m_pendingDragIndex >= 0 && m_dragIndex < 0)
    {
        ReleaseCapture();
        m_pendingDragIndex = -1;
        m_pendingDragStartPt = { 0, 0 };
        return;
    }

    if (m_dragIndex >= 0)
    {
        ReleaseCapture();

        int insertIndex = m_dragCurrentInsertIndex;
        if (insertIndex >= 1 && insertIndex < (int)m_owner->GetCategoryCount() && insertIndex != m_dragIndex)
        {
            m_owner->ReorderCategories(m_dragIndex, insertIndex);

            // Adjust visual states vector to match new order
            CategoryVisualState draggedState = m_categoryStates[m_dragIndex];
            m_categoryStates.erase(m_categoryStates.begin() + m_dragIndex);
            m_categoryStates.insert(m_categoryStates.begin() + insertIndex, draggedState);
        }

        // Settle all items back to standard positions
        size_t count = m_owner->GetCategoryCount();
        EnsureCategoryStates();
        for (size_t i = 0; i < count; i++)
        {
            if (i < m_categoryStates.size())
            {
                m_categoryStates[i].targetY = kCategoryTop + (float)i * kCategoryStep;
            }
        }

        m_dragIndex = -1;
        m_pendingDragIndex = -1;
        m_pendingDragStartPt = { 0, 0 };
        m_dragCurrentInsertIndex = -1;

        m_animating = true;
        m_owner->StartAnimation();

        repaint = true;
    }
}

void CategoryList::OnMouseWheel(short zDelta, POINT pt, bool& repaint)
{
    if (!HitTestListViewport(pt))
        return;

    ClampScroll();
    float maxScrollY = GetMaxScrollY();
    if (maxScrollY <= 0.0f)
        return;

    float oldTarget = m_targetScrollY;
    m_targetScrollY -= (zDelta / 120.0f) * kCategoryStep;
    m_targetScrollY = (std::max)(0.0f, (std::min)(m_targetScrollY, maxScrollY));

    if (std::abs(m_targetScrollY - oldTarget) <= 0.1f)
        return;

    if (!UIStyle::Animation::IsEnabled())
    {
        m_scrollY = m_targetScrollY;
        m_scrollVelocity = 0.0f;
    }
    else
    {
        m_animating = true;
        m_owner->StartAnimation();
    }

    repaint = true;
}

static std::wstring ChooseFolder(HWND owner)
{
    std::wstring folderPath;
    IFileOpenDialog* pFileOpen = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFileOpen));
    if (SUCCEEDED(hr))
    {
        DWORD dwOptions;
        if (SUCCEEDED(pFileOpen->GetOptions(&dwOptions)))
        {
            pFileOpen->SetOptions(dwOptions | FOS_PICKFOLDERS);
        }
        
        hr = pFileOpen->Show(owner);
        if (SUCCEEDED(hr))
        {
            IShellItem* pItem = nullptr;
            hr = pFileOpen->GetResult(&pItem);
            if (SUCCEEDED(hr))
            {
                PWSTR pszFilePath = nullptr;
                hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
                if (SUCCEEDED(hr))
                {
                    folderPath = pszFilePath;
                    CoTaskMemFree(pszFilePath);
                }
                pItem->Release();
            }
        }
        pFileOpen->Release();
    }
    return folderPath;
}

void CategoryList::OnRButtonDown(POINT pt, bool& repaint)
{
    if (m_owner->IsSettingsMode()) return;

    int hc = HitTestCategory(pt);
    if (hc >= 1 && hc < (int)m_owner->GetCategoryCount())
    {
        // Select category on right click
        m_owner->SetCurrentCategoryIndex(hc);
        MoveSelectionToCategory(hc);
        repaint = true;

        HWND hwnd = m_owner->GetWindowHWND();
        POINT screenPt = DpiHelper::LogicalClientToScreen(hwnd, pt);

        RendPopupPage* page = m_owner->GetPageByIndex(hc);

        std::vector<ContextMenu::Item> menuItems;
        menuItems.push_back({ L"删除", [this, hc]() {
            HWND hwnd = m_owner->GetWindowHWND();
            if (ConfirmWindow::Show(hwnd, L"确认删除", L"确定要删除该分类及其所有快捷方式吗？", m_owner->GetAppContext()))
            {
                if (hc < (int)m_categoryStates.size())
                {
                    m_categoryStates.erase(m_categoryStates.begin() + hc);
                }
                m_owner->DeleteCategory(hc);

                size_t count = m_owner->GetCategoryCount();
                for (size_t i = 0; i < count; i++)
                {
                    if (i < m_categoryStates.size())
                    {
                        m_categoryStates[i].targetY = kCategoryTop + (float)i * kCategoryStep;
                    }
                }
                ClampScroll();

                m_animating = true;
                m_owner->StartAnimation();

                InvalidateRect(hwnd, nullptr, FALSE);
            }
        } });

        menuItems.push_back({ L"重命名", [this, hc]() {
            HWND hwnd = m_owner->GetWindowHWND();
            std::wstring name = m_owner->GetCategoryName(hc);
            if (PromptWindow::Show(hwnd, L"重命名分类", L"输入新分类的名称:", name, name.c_str(), m_owner->GetAppContext()))
            {
                m_owner->RenameCategory(hc, name);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        } });

        menuItems.push_back({ L"复制场景", [this, hc]() {
            RendPopupPage* p = m_owner->GetPageByIndex(hc);
            if (!p)
                return;

            g_copiedSceneSettings.hasData = true;
            g_copiedSceneSettings.mode = p->sceneMode;
            g_copiedSceneSettings.selectedApps = p->sceneApps;
            g_copiedSceneSettings.availableApps = p->sceneAvailableApps;
        } });

        menuItems.push_back({ L"粘贴场景", [this, hc]() {
            if (!g_copiedSceneSettings.hasData)
                return;

            HWND hwnd = m_owner->GetWindowHWND();
            RendPopupPage* p = m_owner->GetPageByIndex(hc);
            if (!p)
                return;

            p->sceneMode = g_copiedSceneSettings.mode;
            p->sceneApps = g_copiedSceneSettings.selectedApps;
            p->sceneAvailableApps = g_copiedSceneSettings.availableApps;
            m_owner->NotifyConfigChanged();
            InvalidateRect(hwnd, nullptr, FALSE);
        }, !g_copiedSceneSettings.hasData });

        menuItems.push_back({ L"场景设置", [this, hc]() {
            HWND hwnd = m_owner->GetWindowHWND();
            RendPopupPage* p = m_owner->GetPageByIndex(hc);
            if (p && SceneSettingsWindow::Show(hwnd, p, m_owner->GetAppContext(), [this, hwnd]() {
                m_owner->NotifyConfigChanged();
                InvalidateRect(hwnd, nullptr, FALSE);
            }))
            {
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        } });

        bool isSyncNow = page ? page->isSyncFolder : false;
        bool hasFolderPath = page && !page->folderPath.empty();
        bool isDock = (m_owner->GetCategoryName(hc) == L"DOCK");

        if (!isDock)
        {
            if (isSyncNow)
            {
                // Currently syncing → offer to pause
                menuItems.push_back({ L"停止同步", [this, hc]() {
                    HWND hwnd = m_owner->GetWindowHWND();
                    RendPopupPage* p = m_owner->GetPageByIndex(hc);
                    if (p)
                    {
                        // Pause: keep folderPath so the user can resume later
                        p->isSyncFolder = false;
                    }
                    m_owner->NotifyConfigChanged();
                    InvalidateRect(hwnd, nullptr, FALSE);
                } });
            }
            else if (hasFolderPath)
            {
                // Paused sync (has folderPath but isSyncFolder=false) → offer to resume
                menuItems.push_back({ L"开始同步", [this, hc]() {
                    HWND hwnd = m_owner->GetWindowHWND();
                    RendPopupPage* p = m_owner->GetPageByIndex(hc);
                    if (p && !p->folderPath.empty())
                    {
                        p->isSyncFolder = true;

                        // Reload from the already-associated folder
                        ShortcutManager::FreeShortcuts(p->shortcuts);
                        for (auto* bmp : p->iconBitmaps)
                        {
                            if (bmp) bmp->Release();
                        }
                        p->iconBitmaps.clear();
                        p->shortcuts = SyncFolderService::LoadRendShortcuts(p->folderPath);

                        m_owner->NotifyConfigChanged();
                        m_owner->SetCurrentCategoryIndex(hc);
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                } });
            }
            // Regular categories (no folderPath) get neither option
        }

        ContextMenu::Show(hwnd, screenPt, menuItems, m_owner->GetAppContext());
    }
}

void CategoryList::UpdateAnimation(float dt, bool& repaint)
{
    int currentCategory = m_owner->GetCurrentCategoryIndex();
    size_t count = m_owner->GetCategoryCount();
    bool selectMoving = false;

    if (currentCategory >= 0 && currentCategory < (int)count)
    {
        float targetActiveY = 0.0f;
        if (m_owner->IsSettingsMode())
        {
            targetActiveY = kCategoryTop + (float)currentCategory * kCategoryStep - m_scrollY;
        }
        else if (currentCategory < (int)m_categoryStates.size())
        {
            targetActiveY = (m_dragIndex == currentCategory)
                ? m_categoryStates[currentCategory].currentY - m_scrollY
                : m_categoryStates[currentCategory].targetY - m_scrollY;
        }

        if (m_selectAnimCurrentY < 0.0f)
        {
            m_selectAnimCurrentY = targetActiveY;
            m_selectAnimTargetY = targetActiveY;
            m_isAnimatingSelect = false;
        }
        else
        {
            bool scrolling = std::abs(m_targetScrollY - m_scrollY) > 0.2f || std::abs(m_scrollVelocity) > 1.0f;
            bool draggingSelection = (m_dragIndex >= 0 && m_dragIndex == currentCategory);
            if (scrolling || draggingSelection)
            {
                m_selectAnimCurrentY = targetActiveY;
                m_selectAnimTargetY = targetActiveY;
                m_isAnimatingSelect = false;
            }
            else if (std::abs(targetActiveY - m_selectAnimTargetY) > 0.1f)
            {
                BeginSelectAnimation(targetActiveY);
            }
        }
    }

    if (!UIStyle::Animation::IsEnabled())
    {
        m_scrollY = m_targetScrollY;
        m_scrollVelocity = 0.0f;
        m_selectAnimCurrentY = m_selectAnimTargetY;
        m_isAnimatingSelect = false;
        for (auto& state : m_categoryStates)
        {
            state.currentY = state.targetY;
        }
        m_animating = (m_dragIndex >= 0);
        repaint = true;
        return;
    }

    if (m_isAnimatingSelect)
    {
        float dy = m_selectAnimTargetY - m_selectAnimCurrentY;
        if (std::abs(dy) > 0.05f)
        {
            m_selectAnimCurrentY += dy * (1.0f - std::exp(-20.0f * dt));
            selectMoving = true;
            repaint = true;
        }
        else
        {
            m_selectAnimCurrentY = m_selectAnimTargetY;
            m_isAnimatingSelect = false;
        }
    }

    bool scrollMoving = false;
    float maxScrollY = GetMaxScrollY();
    m_targetScrollY = (std::max)(0.0f, (std::min)(m_targetScrollY, maxScrollY));
    float scrollError = m_targetScrollY - m_scrollY;
    if (std::abs(scrollError) > 0.05f || std::abs(m_scrollVelocity) > 1.0f)
    {
        const float stiffness = 70.0f;
        const float damping = 16.0f;
        float force = scrollError * stiffness - m_scrollVelocity * damping;
        m_scrollVelocity += force * dt;
        m_scrollY += m_scrollVelocity * dt;
        m_scrollY = (std::max)(0.0f, (std::min)(m_scrollY, maxScrollY));
        scrollMoving = true;
        repaint = true;
    }
    else
    {
        m_scrollY = m_targetScrollY;
        m_scrollVelocity = 0.0f;
    }

    if (!m_animating && !selectMoving && !scrollMoving) return;

    // Smooth interpolation
    bool anyCategoryMoving = false;
    for (auto& state : m_categoryStates)
    {
        float dy = state.targetY - state.currentY;
        if (std::abs(dy) > 0.1f)
        {
            state.currentY += dy * (1.0f - std::exp(-15.0f * dt));
            anyCategoryMoving = true;
        }
        else
        {
            state.currentY = state.targetY;
        }
    }

    if (anyCategoryMoving || selectMoving || scrollMoving)
    {
        // Keep animating, trigger repaint
    }
    else
    {
        m_animating = false;
    }

    repaint = true;
}

void CategoryList::UpdateDragAndSortState(POINT pt)
{
    if (m_dragIndex < 0) return;

    size_t count = m_owner->GetCategoryCount();

    // 1. Follow mouse Y with grab offset
    float mouseY = (float)pt.y + m_scrollY;
    float leaderCurrentY = mouseY - m_grabOffsetY;

    // Capping boundary
    float minDragY = kCategoryTop + 20.0f;
    float maxDragY = kCategoryTop + ((float)count - 1.0f) * kCategoryStep + 20.0f;
    if (leaderCurrentY < minDragY) leaderCurrentY = minDragY;
    if (leaderCurrentY > maxDragY) leaderCurrentY = maxDragY;

    if (m_dragIndex < (int)m_categoryStates.size())
    {
        m_categoryStates[m_dragIndex].currentY = leaderCurrentY;
        m_categoryStates[m_dragIndex].targetY = leaderCurrentY;
        if (m_dragIndex == m_owner->GetCurrentCategoryIndex())
        {
            float selectionY = leaderCurrentY - m_scrollY;
            m_selectAnimCurrentY = selectionY;
            m_selectAnimTargetY = selectionY;
            m_isAnimatingSelect = false;
        }
    }

    // 2. Find closest slot (DOCK is slot 0, and is fixed, so slots start at 1)
    float centerY = leaderCurrentY + kCategoryItemHeight * 0.5f;
    int closestSlot = m_dragCurrentInsertIndex;
    float minDistSq = -1.0f;

    for (int j = 1; j < (int)count; j++)
    {
        float slotY = kCategoryTop + (float)j * kCategoryStep + kCategoryItemHeight * 0.5f;
        float dy = centerY - slotY;
        float distSq = dy * dy;

        if (minDistSq < 0.0f || distSq < minDistSq)
        {
            minDistSq = distSq;
            closestSlot = j;
        }
    }

    if (closestSlot != m_dragCurrentInsertIndex)
    {
        m_dragCurrentInsertIndex = closestSlot;
    }

    // 3. Update target positions for other categories
    int insertIndex = m_dragCurrentInsertIndex;
    for (int i = 1; i < (int)count; i++)
    {
        if (i == m_dragIndex) continue;
        if (i >= (int)m_categoryStates.size()) continue;

        int targetSlot = i;
        if (i < m_dragIndex)
        {
            if (i >= insertIndex)
            {
                targetSlot = i + 1;
            }
        }
        else if (i > m_dragIndex)
        {
            if (i <= insertIndex)
            {
                targetSlot = i - 1;
            }
        }

        m_categoryStates[i].targetY = kCategoryTop + (float)targetSlot * kCategoryStep;
    }
}

int CategoryList::HitTestCategory(POINT pt)
{
    if (pt.x < (int)kCategoryLeft || pt.x > (int)kCategoryRight || !HitTestListViewport(pt)) return -1;
    float contentY = (float)pt.y + m_scrollY;
    size_t count = m_owner->GetCategoryCount();
    for (int i = 0; i < (int)count; i++)
    {
        float y = kCategoryTop + (float)i * kCategoryStep;
        if (contentY >= y && contentY <= y + kCategoryItemHeight)
            return i;
    }
    return -1;
}

bool CategoryList::HitTestAddCategory(POINT pt)
{
    RECT cr; GetClientRect(m_owner->GetWindowHWND(), &cr);
    float scale = GlassWindow::GetWindowScale(m_owner->GetWindowHWND());
    float h = (float)cr.bottom / scale;
    return (pt.x >= (int)kCategoryLeft && pt.x <= (int)kCategoryRight && pt.y >= h - 42.0f && pt.y <= h - kCategoryBottomPadding);
}
