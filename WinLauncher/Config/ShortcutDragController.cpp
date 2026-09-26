#include "ShortcutDragController.h"
#include "ShortcutSelectionModel.h"
#include "DeleteCursorFactory.h"
#include "ConfirmWindow.h"
#include "../UI/MouseCaptureController.h"
#include "../ShortcutManager.h"
#include "../DpiHelper.h"
#include <cmath>
#include <algorithm>

ShortcutDragController::ShortcutDragController()
    : m_dragIndex(-1)
    , m_dragCurrentInsertIndex(-1)
    , m_dragActive(false)
    , m_dragDeleteCursorShown(false)
    , m_dragStartPt{ 0, 0 }
    , m_grabOffsetX(0.0f)
    , m_grabOffsetY(0.0f)
    , m_deleteCursor(nullptr)
{
}

ShortcutDragController::~ShortcutDragController()
{
    if (m_deleteCursor)
    {
        DestroyCursor(m_deleteCursor);
        m_deleteCursor = nullptr;
    }
}

void ShortcutDragController::BeginCandidate(int shortcutIndex, POINT pt)
{
    m_dragIndex = shortcutIndex;
    m_dragCurrentInsertIndex = shortcutIndex;
    m_dragActive = false;
    m_dragStartPt = pt;
}

void ShortcutDragController::Reset()
{
    m_dragIndex = -1;
    m_dragCurrentInsertIndex = -1;
    m_dragActive = false;
    if (m_dragDeleteCursorShown)
    {
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        m_dragDeleteCursorShown = false;
    }
}

bool ShortcutDragController::HasExceededThreshold(POINT pt) const
{
    return ShortcutSelectionModel::HasDragExceededThreshold(m_dragStartPt, pt);
}

void ShortcutDragController::UpdateCursor(HWND hWnd, POINT pt)
{
    bool showDeleteCursor = m_dragActive && ShortcutSelectionModel::IsPointOutsideWindow(hWnd, pt);
    if (showDeleteCursor)
    {
        SetCursor(GetDeleteCursor());
        m_dragDeleteCursorShown = true;
    }
    else if (m_dragDeleteCursorShown)
    {
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        m_dragDeleteCursorShown = false;
    }
}

HCURSOR ShortcutDragController::GetDeleteCursor()
{
    if (m_deleteCursor) return m_deleteCursor;
    m_deleteCursor = DeleteCursorFactory::CreateDeleteCursor();
    return m_deleteCursor ? m_deleteCursor : LoadCursorW(nullptr, IDC_ARROW);
}

bool ShortcutDragController::StartDrag(
    IConfigWindow* owner,
    RendPopupPage* pageData,
    std::vector<ShortcutVisualState>& states,
    float scrollY,
    POINT pt)
{
    if (!pageData || m_dragIndex < 0 || m_dragIndex >= (int)states.size()) return false;
    if (!states[m_dragIndex].selected) return false;

    if (owner && owner->GetSortMode() == 1)
    {
        const bool switchToCustom = ConfirmWindow::Show(
            owner->GetWindowHWND(),
            L"智能排序已启用",
            L"当前为智能排序，不能拖动图标。是否切换到自定义排序？",
            owner->GetAppContext());
        if (!switchToCustom)
        {
            MouseCaptureController::Complete(owner ? owner->GetWindowHWND() : nullptr);
            Reset();
            return false;
        }

        owner->SetSortMode(0);
        owner->NotifyConfigChanged();
    }

    m_dragActive = true;
    m_dragCurrentInsertIndex = m_dragIndex;

    float leaderCurrentX = states[m_dragIndex].currentX;
    float leaderCurrentY = states[m_dragIndex].currentY;
    m_grabOffsetX = (float)m_dragStartPt.x - leaderCurrentX;
    m_grabOffsetY = (float)(m_dragStartPt.y + scrollY) - leaderCurrentY;

    for (auto& s : states)
    {
        if (s.selected)
        {
            s.dragOffsetX = s.currentX - leaderCurrentX;
            s.dragOffsetY = s.currentY - leaderCurrentY;
        }
    }

    UpdateDragAndSortState(pt, scrollY, states);
    if (owner)
    {
        owner->StartAnimation();
    }
    return true;
}

void ShortcutDragController::UpdateDragAndSortState(
    POINT clientPt,
    float scrollY,
    std::vector<ShortcutVisualState>& states)
{
    if (!m_dragActive || m_dragIndex < 0) return;

    // 1. Get list of selected indices in sorted order
    std::vector<int> selectedIndices;
    int leaderSelIdx = -1;
    for (int i = 0; i < (int)states.size(); i++)
    {
        if (states[i].selected)
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
        states[m_dragIndex].selected = true;
        leaderSelIdx = 0;
        selectedIndices.clear();
        selectedIndices.push_back(m_dragIndex);
    }

    int k = (int)selectedIndices.size();
    int n = (int)states.size();

    // 2. Update all selected items' visual positions to follow the mouse
    float unscrolledMouseX = (float)clientPt.x;
    float unscrolledMouseY = (float)(clientPt.y + scrollY);

    float leaderCurrentX = unscrolledMouseX - m_grabOffsetX;
    float leaderCurrentY = unscrolledMouseY - m_grabOffsetY;

    for (int idx : selectedIndices)
    {
        states[idx].currentX = leaderCurrentX + states[idx].dragOffsetX;
        states[idx].currentY = leaderCurrentY + states[idx].dragOffsetY;
        states[idx].targetX = states[idx].currentX;
        states[idx].targetY = states[idx].currentY;
    }

    // 3. Find closest slot based on leader's center with hysteresis to eliminate boundary jitter
    float centerX = states[m_dragIndex].currentX + 31.0f;
    float centerY = states[m_dragIndex].currentY + 31.0f;

    int currentSlot = m_dragCurrentInsertIndex;
    int closestSlot = currentSlot;

    float currentDistSq = -1.0f;
    if (currentSlot >= 0 && currentSlot < n)
    {
        float curX = (float)(160 + (currentSlot % 5) * 72 + 31);
        float curY = (float)(72 + (currentSlot / 5) * 72 + 31);
        float dx = centerX - curX;
        float dy = centerY - curY;
        currentDistSq = dx * dx + dy * dy;
    }

    float minDistSq = currentDistSq;

    for (int j = 0; j < n; j++)
    {
        float slotX = (float)(160 + (j % 5) * 72 + 31);
        float slotY = (float)(72 + (j / 5) * 72 + 31);
        float dx = centerX - slotX;
        float dy = centerY - slotY;
        float distSq = dx * dx + dy * dy;

        // Candidate slot must be noticeably closer than current slot to switch
        if (currentDistSq < 0.0f || distSq < currentDistSq * 0.72f)
        {
            if (minDistSq < 0.0f || distSq < minDistSq)
            {
                minDistSq = distSq;
                closestSlot = j;
            }
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
        if (states[i].selected) continue;

        int targetSlot = (m < startSlot) ? m : (m + k);
        states[i].targetX = (float)(160 + (targetSlot % 5) * 72);
        states[i].targetY = (float)(72 + (targetSlot / 5) * 72);
        m++;
    }
}

void ShortcutDragController::UpdateAutoScroll(
    HWND hWnd,
    float dt,
    const RendPopupPage* pageData,
    float& targetScrollY)
{
    if (!m_dragActive || m_dragIndex < 0 || !pageData) return;

    POINT mousePt;
    GetCursorPos(&mousePt);
    ScreenToClient(hWnd, &mousePt);
    float scale = DpiHelper::GetWindowScale(hWnd);
    mousePt.x = (int)(mousePt.x / scale);
    mousePt.y = (int)(mousePt.y / scale);

    int n = (int)pageData->shortcuts.size();
    int rows = pageData->isSyncFolder ? ((n + 4) / 5) : ((n + 1 + 4) / 5);
    float maxScrollY = (std::max)(0.0f, (rows * 72) - 368.0f);

    if (mousePt.x >= 150 && mousePt.x <= 520)
    {
        if (mousePt.y >= 72 && mousePt.y < 117)
        {
            float speed = (117.0f - mousePt.y) * 2.0f;
            targetScrollY = (std::max)(0.0f, targetScrollY - speed * dt);
        }
        else if (mousePt.y > 395 && mousePt.y <= 440)
        {
            float speed = (mousePt.y - 395.0f) * 2.0f;
            targetScrollY = (std::min)(maxScrollY, targetScrollY + speed * dt);
        }
    }
}

void ShortcutDragController::RenderInsertionSlots(
    ID2D1HwndRenderTarget* rt,
    ShortcutBrushCache& brushCache,
    const std::vector<ShortcutVisualState>& states,
    int totalShortcuts,
    float scrollY) const
{
    if (!m_dragActive || m_dragIndex < 0 || m_dragCurrentInsertIndex < 0 || m_dragCurrentInsertIndex >= totalShortcuts)
        return;

    std::vector<int> selectedIndices;
    int leaderSelIdx = -1;
    for (int i = 0; i < totalShortcuts; i++)
    {
        if (states[i].selected)
        {
            if (i == m_dragIndex) leaderSelIdx = (int)selectedIndices.size();
            selectedIndices.push_back(i);
        }
    }

    int k = (int)selectedIndices.size();
    int startSlot = m_dragCurrentInsertIndex - leaderSelIdx;
    if (startSlot < 0) startSlot = 0;
    if (startSlot > totalShortcuts - k) startSlot = totalShortcuts - k;

    for (int j = 0; j < k; j++)
    {
        int targetSlot = startSlot + j;
        int col = targetSlot % 5;
        int row = targetSlot / 5;
        float X_insert = (float)(160 + col * 72);
        float Y_insert = std::roundf(72.0f + row * 72.0f - scrollY);
        ShortcutGridViewHelper::RenderInsertionSlot(rt, brushCache, X_insert, Y_insert);
    }
}

void ShortcutDragController::HandleLButtonUp(
    POINT pt,
    IConfigWindow* owner,
    RendPopupPage* pageData,
    std::vector<ShortcutVisualState>& states,
    float scrollY,
    int& selectionAnchorIndex,
    const std::function<bool(const std::vector<int>&, bool&)>& confirmPendingDelete,
    const std::function<void(bool)>& resetTargets,
    bool& repaint)
{
    if (m_dragIndex < 0) return;

    POINT mousePt = pt;
    int dx = mousePt.x - m_dragStartPt.x;
    int dy = mousePt.y - m_dragStartPt.y;
    bool ctrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

    if (!m_dragActive && HasExceededThreshold(pt))
    {
        StartDrag(owner, pageData, states, scrollY, pt);
    }

    if (!m_dragActive)
    {
        if (std::abs(dx) <= 3 && std::abs(dy) <= 3 && !ctrlPressed && !shiftPressed)
        {
            // Simple click without modifiers.
            // Clear all selections except the one that was clicked.
            ShortcutSelectionModel::SelectSingle(states, selectionAnchorIndex, m_dragIndex);
        }

        Reset();
        repaint = true;
        return;
    }

    HWND hWnd = owner ? owner->GetWindowHWND() : nullptr;
    if (ShortcutSelectionModel::IsPointOutsideWindow(hWnd, pt))
    {
        std::vector<int> selectedIndices;
        for (int i = 0; i < (int)states.size(); i++)
        {
            if (states[i].selected)
            {
                selectedIndices.push_back(i);
            }
        }
        if (selectedIndices.empty())
        {
            selectedIndices.push_back(m_dragIndex);
        }

        if (confirmPendingDelete)
        {
            confirmPendingDelete(selectedIndices, repaint);
        }
        return;
    }

    // Check if dropped on the category list area on the left
    int targetCatIdx = -1;
    bool droppedOnLeft = (pt.x < 150);
    if (droppedOnLeft && owner && !owner->IsSettingsMode())
    {
        size_t count = owner->GetCategoryCount();
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

    if (targetCatIdx >= 0 && owner && targetCatIdx < (int)owner->GetCategoryCount() && targetCatIdx != owner->GetCurrentCategoryIndex())
    {
        RendPopupPage* destPage = owner->GetPageByIndex(targetCatIdx);
        if (destPage && pageData)
        {
            if (!destPage->isSyncFolder && !pageData->isSyncFolder)
            {
                std::vector<int> selectedIndices;
                for (int i = 0; i < (int)states.size(); i++)
                {
                    if (states[i].selected)
                    {
                        selectedIndices.push_back(i);
                    }
                }

                if (!selectedIndices.empty())
                {
                    owner->RecordShortcutHistoryCheckpoint();

                    // Move to destination category
                    for (int idx : selectedIndices)
                    {
                        destPage->shortcuts.push_back(pageData->shortcuts[idx]);
                        destPage->iconBitmaps.push_back(pageData->iconBitmaps[idx]);
                    }

                    // Remove from source category
                    for (auto it = selectedIndices.rbegin(); it != selectedIndices.rend(); ++it)
                    {
                        pageData->shortcuts.erase(pageData->shortcuts.begin() + *it);
                        pageData->iconBitmaps.erase(pageData->iconBitmaps.begin() + *it);
                        states.erase(states.begin() + *it);
                    }

                    if (resetTargets)
                    {
                        resetTargets(false);
                    }

                    selectionAnchorIndex = -1;
                    owner->NotifyConfigChanged();
                    owner->StartAnimation();
                }
            }
        }

        Reset();
        repaint = true;
        return;
    }

    if (m_dragCurrentInsertIndex >= 0 && pageData)
    {
        // Gather selected indices in sorted order
        std::vector<int> selectedIndices;
        int leaderSelIdx = -1;
        for (int i = 0; i < (int)states.size(); i++)
        {
            if (states[i].selected)
            {
                if (i == m_dragIndex) leaderSelIdx = (int)selectedIndices.size();
                selectedIndices.push_back(i);
            }
        }

        if (!selectedIndices.empty())
        {
            int k = (int)selectedIndices.size();
            int n = (int)states.size();
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

            if (orderChanged && !droppedOnLeft && owner)
            {
                owner->RecordShortcutHistoryCheckpoint();

                // Extract selected elements
                std::vector<RendShortcutInfo> selShortcuts;
                std::vector<ID2D1Bitmap*> selBitmaps;
                std::vector<ShortcutVisualState> selStates;
                for (int idx : selectedIndices)
                {
                    selShortcuts.push_back(pageData->shortcuts[idx]);
                    selBitmaps.push_back(pageData->iconBitmaps[idx]);
                    selStates.push_back(states[idx]);
                }

                // Remove selected elements from high to low indices
                for (auto it = selectedIndices.rbegin(); it != selectedIndices.rend(); ++it)
                {
                    pageData->shortcuts.erase(pageData->shortcuts.begin() + *it);
                    pageData->iconBitmaps.erase(pageData->iconBitmaps.begin() + *it);
                    states.erase(states.begin() + *it);
                }

                // Insert them back at startSlot
                pageData->shortcuts.insert(pageData->shortcuts.begin() + startSlot, selShortcuts.begin(), selShortcuts.end());
                pageData->iconBitmaps.insert(pageData->iconBitmaps.begin() + startSlot, selBitmaps.begin(), selBitmaps.end());
                states.insert(states.begin() + startSlot, selStates.begin(), selStates.end());

                owner->NotifyConfigChanged();
            }

            if (resetTargets)
            {
                resetTargets(false);
            }
            if (owner)
            {
                owner->StartAnimation();
            }
        }
    }

    Reset();
    repaint = true;
}
