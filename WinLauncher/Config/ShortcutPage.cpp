#define NOMINMAX
#include "ShortcutPage.h"
#include "IConfigWindow.h"
#include "UIStyle.h"
#include "ContextMenu.h"
#include "PromptWindow.h"
#include "ConfirmWindow.h"
#include "ShortcutDialog.h"
#include "../DpiHelper.h"
#include "../Services/SyncFolderService.h"
#include <windowsx.h>
#include <shlobj.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <commdlg.h>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "comdlg32.lib")

static const int ICON_SIZE = 24;

ShortcutPage::ShortcutPage(IConfigWindow* owner)
    : m_owner(owner)
    , m_pageData(nullptr)
    , m_hoveredShortcut(-1)
    , m_hoveredAddShortcut(false)
    , m_scrollY(0.0f)
    , m_targetScrollY(0.0f)
    , m_scrollVelocity(0.0f)
    , m_animating(false)
    , m_dragIndex(-1)
    , m_dragCurrentInsertIndex(-1)
    , m_grabOffsetX(0.0f)
    , m_grabOffsetY(0.0f)
    , m_selectionAnchorIndex(-1)
    , m_dragStartPt{ 0, 0 }
    , m_lastRt(nullptr)
    , m_trackMouse(false)
{
}

ShortcutPage::~ShortcutPage()
{
    m_bmpBrushCache.clear();
}

void ShortcutPage::SetPageData(RendPopupPage* page, bool preserveScroll)
{
    m_pageData = page;
    if (!preserveScroll)
    {
        m_scrollY = 0.0f;
        m_targetScrollY = 0.0f;
        m_scrollVelocity = 0.0f;
        m_animating = false;
    }
    m_shortcutStates.clear();
    m_selectionAnchorIndex = -1;
    m_dragIndex = -1;
    m_dragCurrentInsertIndex = -1;
    m_hoveredShortcut = -1;
    m_hoveredAddShortcut = false;
    m_lastRt = nullptr;
    m_brushCache.clear();
    m_bmpBrushCache.clear();
}

void ShortcutPage::ShowAddShortcutDialog()
{
    if (!m_pageData) return;
    HWND hWnd = m_owner->GetWindowHWND();
    ShortcutDialogResult result;
    if (ShortcutDialog::Show(hWnd, L"添加快捷方式", result, nullptr, nullptr))
    {
        if (!result.targetPath.empty())
        {
            RendShortcutInfo sc;
            sc.name        = result.name;
            sc.targetPath  = result.targetPath;
            sc.arguments   = result.arguments;
            sc.iconPath    = result.iconPath;
            sc.hIcon       = ShortcutManager::GetShortcutIcon(result.iconPath.empty() ? result.targetPath : result.iconPath);

            m_pageData->shortcuts.push_back(sc);
            ID2D1Bitmap* bmp = m_owner->CreateD2DBitmapFromHicon(sc.hIcon);
            m_pageData->iconBitmaps.push_back(bmp);

            m_owner->NotifyConfigChanged();
            InvalidateRect(hWnd, nullptr, FALSE);
        }
    }
}

void ShortcutPage::UpdateTheme()
{
    m_brushCache.clear();
}

void ShortcutPage::OnPaint(ID2D1HwndRenderTarget* rt, const D2D1_RECT_F& rect)
{
    if (!m_pageData) return;

    EnsureIcons(rt);
    EnsureShortcutStates();

    IDWriteTextFormat* tfTitle = m_owner->GetTitleFont();
    IDWriteTextFormat* tfDefault = m_owner->GetDefaultFont();
    if (tfDefault)
    {
        tfDefault->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    // Active Category Title
    if (tfTitle)
    {
        auto textBrush = GetOrCreateBrush(rt, UIStyle::ThemeColor::TextNormal().d2d);
        if (textBrush)
        {
            rt->DrawTextW(m_pageData->name.c_str(), (UINT32)m_pageData->name.size(), tfTitle,
                D2D1::RectF(160, 42, 510, 62), textBrush.Get());
        }
    }

    // Draw Shortcut Grid within a clip viewport
    rt->PushAxisAlignedClip(D2D1::RectF(150, 72, 520, 470), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    int n = (int)m_pageData->shortcuts.size();

    // 1. Draw placeholder outlines at the target insert slots if dragging
    if (m_dragIndex >= 0 && m_dragCurrentInsertIndex >= 0 && m_dragCurrentInsertIndex < n)
    {
        std::vector<int> selectedIndices;
        int leaderSelIdx = -1;
        for (int i = 0; i < n; i++)
        {
            if (m_shortcutStates[i].selected)
            {
                if (i == m_dragIndex) leaderSelIdx = (int)selectedIndices.size();
                selectedIndices.push_back(i);
            }
        }

        int k = (int)selectedIndices.size();
        int startSlot = m_dragCurrentInsertIndex - leaderSelIdx;
        if (startSlot < 0) startSlot = 0;
        if (startSlot > n - k) startSlot = n - k;

        D2D1_COLOR_F phClr = UIStyle::ThemeColor::Accent().d2d;
        phClr.a = 0.18f;
        auto placeholderBrush = GetOrCreateBrush(rt, phClr);
        if (placeholderBrush)
        {
            for (int j = 0; j < k; j++)
            {
                int targetSlot = startSlot + j;
                int col = targetSlot % 5;
                int row = targetSlot / 5;
                float X_insert = (float)(160 + col * 72);
                float Y_insert = std::roundf(72.0f + row * 72.0f - m_scrollY);

                D2D1_RECT_F insertRect = D2D1::RectF(X_insert, Y_insert, X_insert + 62, Y_insert + 62);
                D2D1_ROUNDED_RECT roundedInsert = D2D1::RoundedRect(insertRect, 8.0f, 8.0f);
                rt->DrawRoundedRectangle(roundedInsert, placeholderBrush.Get(), UIStyle::Metrics::EmphasisStroke());
            }
        }
    }

    // 2. Draw all non-dragged shortcut cards
    for (int i = 0; i < n; i++)
    {
        if (m_dragIndex >= 0 && m_shortcutStates[i].selected) continue;
        if (i >= (int)m_shortcutStates.size()) continue;

        float X = std::roundf(m_shortcutStates[i].currentX);
        float Y = std::roundf(m_shortcutStates[i].currentY - m_scrollY);

        bool isHovered = (i == m_hoveredShortcut) && (m_dragIndex == -1);
        D2D1_RECT_F cardRect = D2D1::RectF(X, Y, X + 62, Y + 62);
        D2D1_ROUNDED_RECT roundedCard = D2D1::RoundedRect(cardRect, 8.0f, 8.0f);

        bool isSelected = m_shortcutStates[i].selected;
        D2D1_COLOR_F baseClr = (UIStyle::GetThemeMode() == UIStyle::ThemeMode::Light) ? D2D1::ColorF(0.0f, 0.0f, 0.0f) : D2D1::ColorF(1.0f, 1.0f, 1.0f);
        float alphaBg = isHovered ? 0.105f : 0.035f;
        float alphaBorder = isHovered ? 0.18f : 0.065f;

        D2D1_COLOR_F selBg = UIStyle::ThemeColor::Accent().d2d;
        selBg.a = isHovered ? 0.20f : 0.13f;
        D2D1_COLOR_F normBg = baseClr;
        normBg.a = alphaBg;

        auto bgBrush = GetOrCreateBrush(rt, isSelected ? selBg : normBg);
        if (bgBrush)
        {
            rt->FillRoundedRectangle(roundedCard, bgBrush.Get());
        }

        D2D1_COLOR_F selBorder = UIStyle::ThemeColor::Accent().d2d;
        selBorder.a = isHovered ? 0.42f : 0.30f;
        D2D1_COLOR_F normBorder = baseClr;
        normBorder.a = alphaBorder;

        auto borderBrush = GetOrCreateBrush(rt, isSelected ? selBorder : normBorder);
        if (borderBrush)
        {
            rt->DrawRoundedRectangle(roundedCard, borderBrush.Get(), UIStyle::Metrics::ControlStroke());
        }

        // Draw Icon (Preview)
        if (i < (int)m_pageData->iconBitmaps.size() && m_pageData->iconBitmaps[i])
        {
            float iconX = X + 19;
            float iconY = Y + 8;
            D2D1_RECT_F iconRect = D2D1::RectF(iconX, iconY, iconX + ICON_SIZE, iconY + ICON_SIZE);
            D2D1_ROUNDED_RECT roundedIconRect = D2D1::RoundedRect(iconRect, 6.0f, 6.0f);

            auto bmpBrush = GetOrCreateBitmapBrush(rt, m_pageData->iconBitmaps[i]);
            if (bmpBrush)
            {
                bmpBrush->SetExtendModeX(D2D1_EXTEND_MODE_CLAMP);
                bmpBrush->SetExtendModeY(D2D1_EXTEND_MODE_CLAMP);

                float scale = 0.5f;
                D2D1_MATRIX_3X2_F transform = D2D1::Matrix3x2F::Scale(scale, scale) *
                                              D2D1::Matrix3x2F::Translation(iconX, iconY);
                bmpBrush->SetTransform(transform);

                rt->FillRoundedRectangle(roundedIconRect, bmpBrush.Get());
            }
        }

        // Label
        if (tfDefault)
        {
            D2D1_COLOR_F tbClr = UIStyle::ThemeColor::TextNormal().d2d;
            tbClr.a = 0.9f;
            auto tb = GetOrCreateBrush(rt, tbClr);
            if (tb)
            {
                std::wstring dispName = m_pageData->shortcuts[i].name;
                if (dispName.length() > 6) dispName = dispName.substr(0, 6) + L"…";
                rt->DrawTextW(dispName.c_str(), (UINT32)dispName.size(), tfDefault,
                    D2D1::RectF(X + 1, Y + 36, X + 61, Y + 58), tb.Get());
            }
        }
    }

    // 3. Draw "+ 添加" Card
    if (!m_pageData->isSyncFolder)
    {
        int col = n % 5, row = n / 5;
        float X = (float)(160 + col * 72);
        float Y = std::roundf(72.0f + row * 72.0f - m_scrollY);

        D2D1_RECT_F addCardRect = D2D1::RectF(X, Y, X + 62, Y + 62);
        D2D1_ROUNDED_RECT roundedAdd = D2D1::RoundedRect(addCardRect, 8.0f, 8.0f);

        D2D1_COLOR_F baseClr = (UIStyle::GetThemeMode() == UIStyle::ThemeMode::Light) ? D2D1::ColorF(0.0f, 0.0f, 0.0f) : D2D1::ColorF(1.0f, 1.0f, 1.0f);
        float alphaBg = m_hoveredAddShortcut ? 0.09f : 0.02f;
        float alphaBorder = m_hoveredAddShortcut ? 0.16f : 0.065f;

        auto bgBrush = GetOrCreateBrush(rt, D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, alphaBg));
        if (bgBrush)
        {
            rt->FillRoundedRectangle(roundedAdd, bgBrush.Get());
        }

        auto borderBrush = GetOrCreateBrush(rt, D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, alphaBorder));
        if (borderBrush)
        {
            rt->DrawRoundedRectangle(roundedAdd, borderBrush.Get(), UIStyle::Metrics::ControlStroke());
        }

        // Draw Plus sign
        auto plBrush = GetOrCreateBrush(rt, UIStyle::ThemeColor::TextMuted().d2d);
        if (plBrush)
        {
            rt->DrawLine(D2D1::Point2F(X + 31, Y + 14), D2D1::Point2F(X + 31, Y + 26), plBrush.Get(), UIStyle::Metrics::IconStroke());
            rt->DrawLine(D2D1::Point2F(X + 25, Y + 20), D2D1::Point2F(X + 37, Y + 20), plBrush.Get(), UIStyle::Metrics::IconStroke());
        }

        // Label
        if (tfDefault)
        {
            auto tb = GetOrCreateBrush(rt, UIStyle::ThemeColor::TextMuted().d2d);
            if (tb)
            {
                rt->DrawTextW(L"添加", 2, tfDefault, D2D1::RectF(X + 1, Y + 36, X + 61, Y + 58), tb.Get());
            }
        }
    }

    rt->PopAxisAlignedClip();

    // 4. Draw the dragged items on top of everything (drawn after PopAxisAlignedClip to avoid clipping when dragging to the left panel)
    if (m_dragIndex >= 0)
    {
        for (int i = 0; i < n; i++)
        {
            if (!m_shortcutStates[i].selected) continue;
            if (i >= (int)m_shortcutStates.size()) continue;

            float X = std::roundf(m_shortcutStates[i].currentX);
            float Y = std::roundf(m_shortcutStates[i].currentY - m_scrollY);

            D2D1_RECT_F cardRect = D2D1::RectF(X, Y, X + 62, Y + 62);
            D2D1_ROUNDED_RECT roundedCard = D2D1::RoundedRect(cardRect, 8.0f, 8.0f);

            D2D1_COLOR_F dragBg = UIStyle::ThemeColor::Accent().d2d;
            dragBg.a = 0.20f;
            auto bgBrush = GetOrCreateBrush(rt, dragBg);
            if (bgBrush)
            {
                rt->FillRoundedRectangle(roundedCard, bgBrush.Get());
            }

            D2D1_COLOR_F dragBorder = UIStyle::ThemeColor::Accent().d2d;
            dragBorder.a = 0.42f;
            auto borderBrush = GetOrCreateBrush(rt, dragBorder);
            if (borderBrush)
            {
                rt->DrawRoundedRectangle(roundedCard, borderBrush.Get(), UIStyle::Metrics::EmphasisStroke());
            }

            // Draw Icon (Preview)
            if (i < (int)m_pageData->iconBitmaps.size() && m_pageData->iconBitmaps[i])
            {
                float iconX = X + 19;
                float iconY = Y + 8;
                D2D1_RECT_F iconRect = D2D1::RectF(iconX, iconY, iconX + ICON_SIZE, iconY + ICON_SIZE);
                D2D1_ROUNDED_RECT roundedIconRect = D2D1::RoundedRect(iconRect, 6.0f, 6.0f);

                auto bmpBrush = GetOrCreateBitmapBrush(rt, m_pageData->iconBitmaps[i]);
                if (bmpBrush)
                {
                    bmpBrush->SetExtendModeX(D2D1_EXTEND_MODE_CLAMP);
                    bmpBrush->SetExtendModeY(D2D1_EXTEND_MODE_CLAMP);

                    float scale = 0.5f;
                    D2D1_MATRIX_3X2_F transform = D2D1::Matrix3x2F::Scale(scale, scale) *
                                                  D2D1::Matrix3x2F::Translation(iconX, iconY);
                    bmpBrush->SetTransform(transform);

                    rt->FillRoundedRectangle(roundedIconRect, bmpBrush.Get());
                }
            }

            // Label
            if (tfDefault)
            {
                auto tb = GetOrCreateBrush(rt, UIStyle::ThemeColor::TextNormal().d2d);
                if (tb)
                {
                    std::wstring dispName = m_pageData->shortcuts[i].name;
                    if (dispName.length() > 6) dispName = dispName.substr(0, 6) + L"…";
                    rt->DrawTextW(dispName.c_str(), (UINT32)dispName.size(), tfDefault,
                        D2D1::RectF(X + 1, Y + 36, X + 61, Y + 58), tb.Get());
                }
            }
        }
    }
}

void ShortcutPage::OnMouseMove(POINT pt, bool& repaint)
{
    if (!m_pageData) return;

    if (!m_trackMouse)
    {
        m_trackMouse = true;
    }

    if (m_dragIndex >= 0)
    {
        UpdateDragAndSortState(pt);
        repaint = true;
        return;
    }

    int hs = HitTestShortcut(pt);
    bool has = HitTestAddShortcut(pt);

    if (hs != m_hoveredShortcut || has != m_hoveredAddShortcut)
    {
        m_hoveredShortcut = hs;
        m_hoveredAddShortcut = has;
        repaint = true;
    }
}

void ShortcutPage::OnMouseLeave(bool& repaint)
{
    m_hoveredShortcut = -1;
    m_hoveredAddShortcut = false;
    m_trackMouse = false;
    repaint = true;
}

void ShortcutPage::OnLButtonDown(POINT pt, bool& repaint)
{
    if (!m_pageData) return;

    HWND hWnd = m_owner->GetWindowHWND();

    int hs = HitTestShortcut(pt);
    if (hs >= 0)
    {


        m_dragIndex = hs;
        m_dragCurrentInsertIndex = hs;
        m_dragStartPt = pt;

        EnsureShortcutStates();

        bool ctrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

        if (shiftPressed)
        {
            if (m_selectionAnchorIndex == -1)
            {
                m_selectionAnchorIndex = hs;
            }
            // Clear all selections first
            for (auto& s : m_shortcutStates)
            {
                s.selected = false;
            }
            // Select range
            int start = std::min(m_selectionAnchorIndex, hs);
            int end = std::max(m_selectionAnchorIndex, hs);
            for (int i = start; i <= end; i++)
            {
                m_shortcutStates[i].selected = true;
            }
        }
        else if (ctrlPressed)
        {
            m_shortcutStates[hs].selected = !m_shortcutStates[hs].selected;
            if (m_shortcutStates[hs].selected)
            {
                m_selectionAnchorIndex = hs;
            }
        }
        else
        {
            // Click without modifiers
            if (!m_shortcutStates[hs].selected)
            {
                // Clear others and select this
                for (auto& s : m_shortcutStates)
                {
                    s.selected = false;
                }
                m_shortcutStates[hs].selected = true;
            }
            m_selectionAnchorIndex = hs;
        }

        // Calculate grab offset for the leader
        int col = hs % 5, row = hs / 5;
        float origX = (float)(160 + col * 72);
        float origY = (float)(72 + row * 72);
        m_grabOffsetX = pt.x - origX;
        m_grabOffsetY = (pt.y + m_scrollY) - origY;

        // Calculate relative drag offsets for all selected items from the leader's current position
        float leaderCurrentX = m_shortcutStates[hs].currentX;
        float leaderCurrentY = m_shortcutStates[hs].currentY;
        for (auto& s : m_shortcutStates)
        {
            if (s.selected)
            {
                s.dragOffsetX = s.currentX - leaderCurrentX;
                s.dragOffsetY = s.currentY - leaderCurrentY;
            }
        }

        SetCapture(hWnd);

        m_animating = true;
        m_owner->StartAnimation();

        repaint = true;
        return;
    }

    // If clicked in the shortcuts container but not on a shortcut, clear selection
    if (pt.x >= 160 && pt.x <= 510 && pt.y >= 72 && pt.y <= 440)
    {
        int clickedShortcut = HitTestShortcut(pt);
        bool clickedAdd = HitTestAddShortcut(pt);
        if (clickedShortcut == -1 && !clickedAdd)
        {
            EnsureShortcutStates();
            for (auto& s : m_shortcutStates)
            {
                s.selected = false;
            }
            m_selectionAnchorIndex = -1;
            repaint = true;
        }
    }

    if (HitTestAddShortcut(pt))
    {
        ShowAddShortcutDialog();
        repaint = true;
        return;
    }
}

void ShortcutPage::OnLButtonUp(POINT pt, bool& repaint)
{
    if (m_dragIndex >= 0)
    {
        ReleaseCapture();

        POINT mousePt = pt;
        int dx = mousePt.x - m_dragStartPt.x;
        int dy = mousePt.y - m_dragStartPt.y;
        bool ctrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

        if (std::abs(dx) <= 3 && std::abs(dy) <= 3 && !ctrlPressed && !shiftPressed)
        {
            // Simple click without modifiers.
            // Clear all selections except the one that was clicked.
            for (int i = 0; i < (int)m_shortcutStates.size(); i++)
            {
                m_shortcutStates[i].selected = (i == m_dragIndex);
            }
        }

        // Check if dropped on the category list area on the left
        int targetCatIdx = -1;
        bool droppedOnLeft = (pt.x < 150);
        if (droppedOnLeft && !m_owner->IsSettingsMode())
        {
            size_t count = m_owner->GetCategoryCount();
            for (int i = 0; i < (int)count; i++)
            {
                int y = 72 + i * 40;
                if (pt.y >= y && pt.y < y + 40)
                {
                    targetCatIdx = i;
                    break;
                }
            }
        }

        if (targetCatIdx >= 0 && targetCatIdx < (int)m_owner->GetCategoryCount() && targetCatIdx != m_owner->GetCurrentCategoryIndex())
        {
            RendPopupPage* destPage = m_owner->GetPageByIndex(targetCatIdx);
            if (destPage && m_pageData)
            {
                if (!destPage->isSyncFolder && !m_pageData->isSyncFolder)
                {
                    std::vector<int> selectedIndices;
                    for (int i = 0; i < (int)m_shortcutStates.size(); i++)
                    {
                        if (m_shortcutStates[i].selected)
                        {
                            selectedIndices.push_back(i);
                        }
                    }

                    if (!selectedIndices.empty())
                    {
                        // Move to destination category
                        for (int idx : selectedIndices)
                        {
                            destPage->shortcuts.push_back(m_pageData->shortcuts[idx]);
                            destPage->iconBitmaps.push_back(m_pageData->iconBitmaps[idx]);
                        }

                        // Remove from source category
                        for (auto it = selectedIndices.rbegin(); it != selectedIndices.rend(); ++it)
                        {
                            m_pageData->shortcuts.erase(m_pageData->shortcuts.begin() + *it);
                            m_pageData->iconBitmaps.erase(m_pageData->iconBitmaps.begin() + *it);
                            m_shortcutStates.erase(m_shortcutStates.begin() + *it);
                        }

                        // Re-calculate target positions for remaining icons to animate them back
                        int n = (int)m_shortcutStates.size();
                        for (int i = 0; i < n; i++)
                        {
                            m_shortcutStates[i].targetX = (float)(160 + (i % 5) * 72);
                            m_shortcutStates[i].targetY = (float)(72 + (i / 5) * 72);
                        }

                        m_selectionAnchorIndex = -1;
                        m_owner->NotifyConfigChanged();

                        m_animating = true;
                        m_owner->StartAnimation();
                    }
                }
            }

            m_dragIndex = -1;
            m_dragCurrentInsertIndex = -1;
            repaint = true;
            return;
        }

        if (m_dragCurrentInsertIndex >= 0 && m_pageData)
        {
            // Gather selected indices in sorted order
            std::vector<int> selectedIndices;
            int leaderSelIdx = -1;
            for (int i = 0; i < (int)m_shortcutStates.size(); i++)
            {
                if (m_shortcutStates[i].selected)
                {
                    if (i == m_dragIndex) leaderSelIdx = (int)selectedIndices.size();
                    selectedIndices.push_back(i);
                }
            }

            if (!selectedIndices.empty())
            {
                int k = (int)selectedIndices.size();
                int n = (int)m_shortcutStates.size();
                int startSlot = m_dragCurrentInsertIndex - leaderSelIdx;
                if (startSlot < 0) startSlot = 0;
                if (startSlot > n - k) startSlot = n - k;

                bool orderChanged = false;
                if (startSlot != selectedIndices[0])
                {
                    orderChanged = true;
                }
                else
                {
                    // Check if selection is non-contiguous
                    for (int j = 0; j < k; j++)
                    {
                        if (selectedIndices[j] != startSlot + j)
                        {
                            orderChanged = true;
                            break;
                        }
                    }
                }

                if (orderChanged && !droppedOnLeft)
                {
                    // Extract selected elements
                    std::vector<RendShortcutInfo> selShortcuts;
                    std::vector<ID2D1Bitmap*> selBitmaps;
                    std::vector<ShortcutVisualState> selStates;
                    for (int idx : selectedIndices)
                    {
                        selShortcuts.push_back(m_pageData->shortcuts[idx]);
                        selBitmaps.push_back(m_pageData->iconBitmaps[idx]);
                        selStates.push_back(m_shortcutStates[idx]);
                    }

                    // Remove selected elements from high to low indices
                    for (auto it = selectedIndices.rbegin(); it != selectedIndices.rend(); ++it)
                    {
                        m_pageData->shortcuts.erase(m_pageData->shortcuts.begin() + *it);
                        m_pageData->iconBitmaps.erase(m_pageData->iconBitmaps.begin() + *it);
                        m_shortcutStates.erase(m_shortcutStates.begin() + *it);
                    }

                    // Insert them back at startSlot
                    m_pageData->shortcuts.insert(m_pageData->shortcuts.begin() + startSlot, selShortcuts.begin(), selShortcuts.end());
                    m_pageData->iconBitmaps.insert(m_pageData->iconBitmaps.begin() + startSlot, selBitmaps.begin(), selBitmaps.end());
                    m_shortcutStates.insert(m_shortcutStates.begin() + startSlot, selStates.begin(), selStates.end());

                    m_owner->NotifyConfigChanged();
                }

                // Set target positions of all items to settle in their standard slots
                for (int i = 0; i < n; i++)
                {
                    m_shortcutStates[i].targetX = (float)(160 + (i % 5) * 72);
                    m_shortcutStates[i].targetY = (float)(72 + (i / 5) * 72);
                }
            }
        }

        m_dragIndex = -1;
        m_dragCurrentInsertIndex = -1;
        repaint = true;
    }
}

void ShortcutPage::OnRButtonDown(POINT pt, bool& repaint)
{
    if (!m_pageData) return;
    if (m_pageData->isSyncFolder) return;

    int hs = HitTestShortcut(pt);
    if (hs >= 0 && hs < (int)m_pageData->shortcuts.size())
    {
        HWND hWnd = m_owner->GetWindowHWND();
        POINT screenPt = pt;
        ClientToScreen(hWnd, &screenPt);

        // Select the right-clicked item (clearing others for clarity)
        EnsureShortcutStates();
        for (auto& s : m_shortcutStates)
        {
            s.selected = false;
        }
        m_shortcutStates[hs].selected = true;
        m_selectionAnchorIndex = hs;
        repaint = true;

        std::vector<ContextMenu::Item> menuItems;

        // ── 编辑 ──────────────────────────────────────────────────────────
        menuItems.push_back({ L"编辑", [this, hs]() {
            bool dummy = false;
            EditShortcut(hs, dummy);
            if (dummy) InvalidateRect(m_owner->GetWindowHWND(), nullptr, FALSE);
        } });

        // ── 删除 ──────────────────────────────────────────────────────────
        menuItems.push_back({ L"删除", [this, hs]() {
            HWND hWnd = m_owner->GetWindowHWND();
            if (hs < (int)m_pageData->shortcuts.size())
            {
                if (ConfirmWindow::Show(hWnd, L"确认删除", L"确定要删除该快捷方式吗？"))
                {
                    if (hs < (int)m_pageData->iconBitmaps.size() && m_pageData->iconBitmaps[hs])
                        m_pageData->iconBitmaps[hs]->Release();

                    m_pageData->shortcuts.erase(m_pageData->shortcuts.begin() + hs);
                    if (hs < (int)m_pageData->iconBitmaps.size())
                        m_pageData->iconBitmaps.erase(m_pageData->iconBitmaps.begin() + hs);

                    if (hs < (int)m_shortcutStates.size())
                        m_shortcutStates.erase(m_shortcutStates.begin() + hs);

                    if (m_selectionAnchorIndex == hs)
                        m_selectionAnchorIndex = -1;
                    else if (m_selectionAnchorIndex > hs)
                        m_selectionAnchorIndex--;

                    // Update targets of remaining items to fill the gap
                    for (int i = 0; i < (int)m_shortcutStates.size(); i++)
                    {
                        m_shortcutStates[i].targetX = (float)(160 + (i % 5) * 72);
                        m_shortcutStates[i].targetY = (float)(72 + (i / 5) * 72);
                    }

                    // Start timer to animate gap filling
                    m_animating = true;
                    m_owner->StartAnimation();

                    m_owner->NotifyConfigChanged();
                    InvalidateRect(hWnd, nullptr, FALSE);
                }
            }
        } });

        menuItems.push_back({ L"重命名", [this, hs]() {
            HWND hWnd = m_owner->GetWindowHWND();
            std::wstring name = m_pageData->shortcuts[hs].name;
            if (PromptWindow::Show(hWnd, L"重命名快捷方式", L"输入新的名称:", name, name.c_str(), nullptr))
            {
                while (!name.empty() && (name.back() == L' ' || name.back() == L'\t'))
                    name.pop_back();
                size_t start = 0;
                while (start < name.size() && (name[start] == L' ' || name[start] == L'\t'))
                    start++;
                if (start > 0) name = name.substr(start);

                if (!name.empty() && hs < (int)m_pageData->shortcuts.size())
                {
                    m_pageData->shortcuts[hs].name = name;
                    m_owner->NotifyConfigChanged();
                    InvalidateRect(hWnd, nullptr, FALSE);
                }
            }
        } });

        ContextMenu::Show(hWnd, screenPt, menuItems);
    }
}

void ShortcutPage::OnMouseWheel(short zDelta, POINT pt, bool& repaint)
{
    if (!m_pageData) return;

    HWND hWnd = m_owner->GetWindowHWND();
    if (pt.x >= 150)
    {
        int n = (int)m_pageData->shortcuts.size();
        int rows = m_pageData->isSyncFolder ? ((n + 4) / 5) : ((n + 1 + 4) / 5);
        float maxScrollY = std::max(0.0f, (rows * 72) - 368.0f);

        m_targetScrollY -= (zDelta / 120.0f) * 72.0f;
        m_targetScrollY = std::max(0.0f, std::min(m_targetScrollY, maxScrollY));

        if (m_targetScrollY != m_scrollY && !m_animating)
        {
            m_animating = true;
            m_owner->StartAnimation();
        }
        repaint = true;
    }
}

void ShortcutPage::OnDropFiles(HDROP hDrop, bool& repaint)
{
    if (!m_pageData) return;
    if (m_pageData->isSyncFolder) return;

    UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
    if (fileCount > 0)
    {
        for (UINT i = 0; i < fileCount; ++i)
        {
            wchar_t filePath[MAX_PATH]{};
            if (DragQueryFileW(hDrop, i, filePath, MAX_PATH))
            {
                AddShortcutFromPath(filePath);
            }
        }
        m_owner->NotifyConfigChanged();
        repaint = true;
    }
}

void ShortcutPage::UpdateAnimation(float dt, bool& repaint)
{
    if (!m_animating) return;

    HWND hWnd = m_owner->GetWindowHWND();

    // 1. Drag auto-scroll logic
    if (m_dragIndex >= 0 && m_pageData)
    {
        POINT mousePt;
        GetCursorPos(&mousePt);
        ScreenToClient(hWnd, &mousePt);
        float scale = DpiHelper::GetWindowScale(hWnd);
        mousePt.x = (int)(mousePt.x / scale);
        mousePt.y = (int)(mousePt.y / scale);

        int n = (int)m_pageData->shortcuts.size();
        int rows = m_pageData->isSyncFolder ? ((n + 4) / 5) : ((n + 1 + 4) / 5);
        float maxScrollY = std::max(0.0f, (rows * 72) - 368.0f);

        if (mousePt.x >= 150 && mousePt.x <= 520)
        {
            if (mousePt.y >= 72 && mousePt.y < 117)
            {
                float speed = (117.0f - mousePt.y) * 2.0f;
                m_targetScrollY = std::max(0.0f, m_targetScrollY - speed * dt);
            }
            else if (mousePt.y > 395 && mousePt.y <= 440)
            {
                float speed = (mousePt.y - 395.0f) * 2.0f;
                m_targetScrollY = std::min(maxScrollY, m_targetScrollY + speed * dt);
            }
        }
    }

    // 2. Scroll animation physics update
    float scrollError = m_targetScrollY - m_scrollY;
    float stiffness = 200.0f;
    float damping = 22.0f;
    float force = scrollError * stiffness - m_scrollVelocity * damping;
    m_scrollVelocity += force * dt;
    m_scrollY += m_scrollVelocity * dt;

    // 3. Update dragged item position after scroll has changed
    if (m_dragIndex >= 0)
    {
        POINT mousePt;
        GetCursorPos(&mousePt);
        ScreenToClient(hWnd, &mousePt);
        float scale = DpiHelper::GetWindowScale(hWnd);
        mousePt.x = (int)(mousePt.x / scale);
        mousePt.y = (int)(mousePt.y / scale);
        UpdateDragAndSortState(mousePt);
    }

    // 4. Update visual positions of other shortcuts using smooth decay
    bool anyIconMoving = false;
    for (auto& state : m_shortcutStates)
    {
        float dx = state.targetX - state.currentX;
        float dy = state.targetY - state.currentY;
        if (std::abs(dx) > 0.1f || std::abs(dy) > 0.1f)
        {
            state.currentX += dx * (1.0f - std::exp(-15.0f * dt));
            state.currentY += dy * (1.0f - std::exp(-15.0f * dt));
            anyIconMoving = true;
        }
        else
        {
            state.currentX = state.targetX;
            state.currentY = state.targetY;
        }
    }

    // 5. Determine whether we still need the animation loop
    bool scrollAnimating = (std::abs(m_targetScrollY - m_scrollY) > 0.2f || std::abs(m_scrollVelocity) > 1.0f);
    bool dragging = (m_dragIndex >= 0);

    if (scrollAnimating || dragging || anyIconMoving)
    {
        // Keep animating, trigger repaint
    }
    else
    {
        // Clean up and stop animating
        m_scrollY = m_targetScrollY;
        m_scrollVelocity = 0.0f;
        m_animating = false;
    }

    repaint = true;
}

ComPtr<ID2D1SolidColorBrush> ShortcutPage::GetOrCreateBrush(ID2D1HwndRenderTarget* rt, const D2D1_COLOR_F& color)
{
    for (auto& entry : m_brushCache)
    {
        if (entry.color.r == color.r && entry.color.g == color.g &&
            entry.color.b == color.b && entry.color.a == color.a)
        {
            return entry.brush;
        }
    }

    ComPtr<ID2D1SolidColorBrush> brush;
    if (rt)
    {
        rt->CreateSolidColorBrush(color, &brush);
        if (brush)
        {
            m_brushCache.push_back({ color, brush });
        }
    }
    return brush;
}

ComPtr<ID2D1BitmapBrush> ShortcutPage::GetOrCreateBitmapBrush(ID2D1HwndRenderTarget* rt, ID2D1Bitmap* bmp)
{
    auto it = m_bmpBrushCache.find(bmp);
    if (it != m_bmpBrushCache.end())
    {
        return it->second;
    }

    ComPtr<ID2D1BitmapBrush> brush;
    if (rt && bmp)
    {
        rt->CreateBitmapBrush(bmp, &brush);
        if (brush)
        {
            m_bmpBrushCache[bmp] = brush;
        }
    }
    return brush;
}

void ShortcutPage::EnsureIcons(ID2D1HwndRenderTarget* rt)
{
    if (!m_pageData) return;

    bool rtChanged = (rt != m_lastRt);
    if (rtChanged)
    {
        m_lastRt = rt;
        m_brushCache.clear();
        m_bmpBrushCache.clear();
    }

    int n = (int)m_pageData->shortcuts.size();
    if (rtChanged || m_pageData->iconBitmaps.size() != (size_t)n)
    {
        for (auto* bmp : m_pageData->iconBitmaps)
        {
            if (bmp) bmp->Release();
        }
        m_pageData->iconBitmaps.clear();
        m_pageData->iconBitmaps.resize(n, nullptr);

        for (int i = 0; i < n; i++)
        {
            m_pageData->iconBitmaps[i] = m_owner->CreateD2DBitmapFromHicon(m_pageData->shortcuts[i].hIcon);
        }
    }
}

void ShortcutPage::EnsureShortcutStates()
{
    if (!m_pageData)
    {
        m_shortcutStates.clear();
        return;
    }
    const auto& shortcuts = m_pageData->shortcuts;
    size_t n = shortcuts.size();
    if (m_shortcutStates.size() != n)
    {
        size_t oldSize = m_shortcutStates.size();
        m_shortcutStates.resize(n);
        for (size_t i = oldSize; i < n; i++)
        {
            int col = (int)i % 5;
            int row = (int)i / 5;
            float targetX = (float)(160 + col * 72);
            float targetY = (float)(72 + row * 72);
            m_shortcutStates[i].currentX = targetX;
            m_shortcutStates[i].currentY = targetY;
            m_shortcutStates[i].targetX = targetX;
            m_shortcutStates[i].targetY = targetY;
        }
    }
}

void ShortcutPage::UpdateDragAndSortState(POINT clientPt)
{
    if (m_dragIndex < 0) return;

    // 1. Get list of selected indices in sorted order
    std::vector<int> selectedIndices;
    int leaderSelIdx = -1;
    for (int i = 0; i < (int)m_shortcutStates.size(); i++)
    {
        if (m_shortcutStates[i].selected)
        {
            if (i == m_dragIndex)
            {
                leaderSelIdx = (int)selectedIndices.size();
            }
            selectedIndices.push_back(i);
        }
    }

    // Safety fallback
    if (leaderSelIdx == -1)
    {
        m_shortcutStates[m_dragIndex].selected = true;
        leaderSelIdx = 0;
        selectedIndices.clear();
        selectedIndices.push_back(m_dragIndex);
    }

    int k = (int)selectedIndices.size();
    int n = (int)m_shortcutStates.size();

    // 2. Update all selected items' visual positions to follow the mouse
    float unscrolledMouseX = (float)clientPt.x;
    float unscrolledMouseY = (float)(clientPt.y + m_scrollY);

    float leaderCurrentX = unscrolledMouseX - m_grabOffsetX;
    float leaderCurrentY = unscrolledMouseY - m_grabOffsetY;

    for (int idx : selectedIndices)
    {
        m_shortcutStates[idx].currentX = leaderCurrentX + m_shortcutStates[idx].dragOffsetX;
        m_shortcutStates[idx].currentY = leaderCurrentY + m_shortcutStates[idx].dragOffsetY;
        m_shortcutStates[idx].targetX = m_shortcutStates[idx].currentX;
        m_shortcutStates[idx].targetY = m_shortcutStates[idx].currentY;
    }

    // 3. Find closest slot based on leader's center
    float centerX = m_shortcutStates[m_dragIndex].currentX + 31.0f;
    float centerY = m_shortcutStates[m_dragIndex].currentY + 31.0f;

    int closestSlot = m_dragCurrentInsertIndex;
    float minDistSq = -1.0f;

    for (int j = 0; j < n; j++)
    {
        float slotX = (float)(160 + (j % 5) * 72 + 31);
        float slotY = (float)(72 + (j / 5) * 72 + 31);
        float dx = centerX - slotX;
        float dy = centerY - slotY;
        float distSq = dx * dx + dy * dy;

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

    // 4. Calculate startSlot for the contiguous selected block
    int startSlot = closestSlot - leaderSelIdx;
    if (startSlot < 0) startSlot = 0;
    if (startSlot > n - k) startSlot = n - k;

    // 5. Update target positions for all items (non-selected items will animate to make room)

    int m = 0;
    for (int i = 0; i < n; i++)
    {
        if (m_shortcutStates[i].selected) continue;

        int targetSlot = (m < startSlot) ? m : (m + k);
        m_shortcutStates[i].targetX = (float)(160 + (targetSlot % 5) * 72);
        m_shortcutStates[i].targetY = (float)(72 + (targetSlot / 5) * 72);
        m++;
    }
}

void ShortcutPage::AddShortcutFromPath(const std::wstring& filePath)
{
    if (!m_pageData) return;

    std::wstring path = filePath;
    if (!path.empty() && (path.back() == L'\\' || path.back() == L'/'))
    {
        path.pop_back();
    }

    // If this is a directory, expand its contents
    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY))
    {
        std::wstring searchPath = path + L"\\*";
        WIN32_FIND_DATAW ffd;
        HANDLE hFind = FindFirstFileW(searchPath.c_str(), &ffd);
        if (hFind != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (SyncFolderService::ShouldIgnoreFile(ffd)) continue;
                if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue; // skip sub-dirs

                std::wstring childPath = path + L"\\" + ffd.cFileName;
                AddShortcutFromSingleFile(childPath);
            } while (FindNextFileW(hFind, &ffd));
            FindClose(hFind);
        }
        return;
    }

    // Regular file
    AddShortcutFromSingleFile(path);
}

void ShortcutPage::AddShortcutFromSingleFile(const std::wstring& path)
{
    if (!m_pageData) return;

    std::wstring targetPath = path;
    std::wstring arguments = L"";
    std::wstring name;

    wchar_t nameBuf[MAX_PATH]{};
    wcscpy_s(nameBuf, PathFindFileNameW(path.c_str()));
    PathRemoveExtensionW(nameBuf);
    name = nameBuf;

    if (path.size() >= 4 && _wcsicmp(path.c_str() + path.size() - 4, L".lnk") == 0)
    {
        IShellLinkW* psl = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&psl);
        if (SUCCEEDED(hr) && psl)
        {
            IPersistFile* ppf = nullptr;
            hr = psl->QueryInterface(IID_IPersistFile, (void**)&ppf);
            if (SUCCEEDED(hr) && ppf)
            {
                hr = ppf->Load(path.c_str(), STGM_READ);
                if (SUCCEEDED(hr))
                {
                    wchar_t buf[MAX_PATH]{};
                    if (SUCCEEDED(psl->GetPath(buf, MAX_PATH, nullptr, SLGP_RAWPATH)))
                    {
                        if (wcslen(buf) > 0)
                        {
                            targetPath = buf;
                        }
                    }
                    wchar_t args[4096]{};
                    if (SUCCEEDED(psl->GetArguments(args, 4096)))
                        arguments = args;
                }
                ppf->Release();
            }
            psl->Release();
        }
    }

    RendShortcutInfo sc;
    sc.name = name;
    sc.targetPath = targetPath;
    sc.arguments = arguments;
    sc.hIcon = ShortcutManager::GetShortcutIcon(targetPath);

    m_pageData->shortcuts.push_back(sc);

    ID2D1Bitmap* bmp = m_owner->CreateD2DBitmapFromHicon(sc.hIcon);
    m_pageData->iconBitmaps.push_back(bmp);
}

int ShortcutPage::HitTestShortcut(POINT pt)
{
    if (!m_pageData) return -1;
    if (pt.x < 160 || pt.x > 510 || pt.y < 72 || pt.y > 440) return -1;

    float scrolledY = pt.y - 72 + m_scrollY;
    int n = (int)m_pageData->shortcuts.size();
    for (int i = 0; i < n; i++)
    {
        int col = i % 5, row = i / 5;
        float X = (float)(160 + col * 72);
        float Y = (float)(row * 72);
        if (pt.x >= X && pt.x <= X + 62 && scrolledY >= Y && scrolledY <= Y + 62)
            return i;
    }
    return -1;
}



bool ShortcutPage::HitTestAddShortcut(POINT pt)
{
    if (!m_pageData) return false;
    if (m_pageData->isSyncFolder) return false;
    if (pt.x < 160 || pt.x > 510 || pt.y < 72 || pt.y > 440) return false;

    float scrolledY = pt.y - 72 + m_scrollY;
    int n = (int)m_pageData->shortcuts.size();
    int col = n % 5, row = n / 5;
    float X = (float)(160 + col * 72);
    float Y = (float)(row * 72);
    return (pt.x >= X && pt.x <= X + 62 && scrolledY >= Y && scrolledY <= Y + 62);
}

void ShortcutPage::EditShortcut(int index, bool& repaint)
{
    HWND hWnd = m_owner->GetWindowHWND();
    if (!m_pageData || index < 0 || index >= (int)m_pageData->shortcuts.size()) return;

    RendShortcutInfo& sc = m_pageData->shortcuts[index];

    ShortcutDialog::InitParams init;
    init.name       = sc.name;
    init.targetPath = sc.targetPath;
    init.arguments  = sc.arguments;
    init.iconPath   = sc.iconPath;

    ShortcutDialogResult result;
    if (ShortcutDialog::Show(hWnd, L"编辑快捷方式", result, &init, nullptr))
    {
        bool changed = false;

        if (sc.name != result.name)
            { sc.name = result.name; changed = true; }

        if (sc.arguments != result.arguments)
            { sc.arguments = result.arguments; changed = true; }

        if (sc.iconPath != result.iconPath)
            { sc.iconPath = result.iconPath; changed = true; }

        if (sc.targetPath != result.targetPath || changed)
        {
            if (sc.targetPath != result.targetPath)
            {
                sc.targetPath = result.targetPath;
            }
            
            // Refresh icon using custom icon path if present, otherwise target path
            if (sc.hIcon) { DestroyIcon(sc.hIcon); sc.hIcon = nullptr; }
            sc.hIcon = ShortcutManager::GetShortcutIcon(sc.iconPath.empty() ? sc.targetPath : sc.iconPath);

            // Refresh D2D bitmap
            if (index < (int)m_pageData->iconBitmaps.size() && m_pageData->iconBitmaps[index])
            {
                m_pageData->iconBitmaps[index]->Release();
                m_pageData->iconBitmaps[index] = nullptr;
            }
            if (index < (int)m_pageData->iconBitmaps.size())
                m_pageData->iconBitmaps[index] = m_owner->CreateD2DBitmapFromHicon(sc.hIcon);

            changed = true;
        }

        if (changed)
        {
            m_owner->NotifyConfigChanged();
            repaint = true;
        }
    }
}

void ShortcutPage::OnLButtonDblClk(POINT pt, bool& repaint)
{
    if (m_pageData && !m_pageData->isSyncFolder)
    {
        int hs = HitTestShortcut(pt);
        if (hs >= 0 && hs < (int)m_pageData->shortcuts.size())
        {
            EditShortcut(hs, repaint);
        }
    }
}
