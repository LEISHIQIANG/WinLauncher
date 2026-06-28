#include "CategoryList.h"
#include "IConfigWindow.h"
#include "ConfirmWindow.h"
#include "PromptWindow.h"
#include "ContextMenu.h"
#include "UIStyle.h"
#include "../GlassWindow.h"
#include "../ShortcutManager.h"
#include "../Services/SyncFolderService.h"
#include <shobjidl.h>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>

CategoryList::CategoryList(IConfigWindow* owner)
    : m_owner(owner)
    , m_hoveredCategory(-1)
    , m_hoveredAddCategory(false)
    , m_trackMouse(false)
    , m_dragIndex(-1)
    , m_dragCurrentInsertIndex(-1)
    , m_grabOffsetY(0.0f)
    , m_animating(false)
{
}

CategoryList::~CategoryList()
{
}

void CategoryList::Reset()
{
    m_categoryStates.clear();
    m_dragIndex = -1;
    m_dragCurrentInsertIndex = -1;
    m_animating = false;
}

void CategoryList::EnsureCategoryStates()
{
    if (m_owner->IsSettingsMode())
    {
        m_categoryStates.clear();
        return;
    }
    size_t count = m_owner->GetCategoryCount();
    if (m_categoryStates.size() != count)
    {
        size_t oldSize = m_categoryStates.size();
        m_categoryStates.resize(count);
        for (size_t i = oldSize; i < count; i++)
        {
            float targetY = (float)(72 + i * 40);
            m_categoryStates[i].currentY = targetY;
            m_categoryStates[i].targetY = targetY;
        }
    }
}

void CategoryList::DrawCategoryItem(ID2D1HwndRenderTarget* rt, int i, float cy, bool isActive, bool isHovered, bool isDragging, const D2D1_COLOR_F& baseClr, IDWriteTextFormat* tfLeft)
{
    D2D1_RECT_F itemRect = D2D1::RectF(10, cy, 140, cy + 32);
    D2D1_ROUNDED_RECT roundedItem = D2D1::RoundedRect(itemRect, 6.0f, 6.0f);

    bool isShortcutDragTarget = isHovered && m_owner->IsDraggingShortcut();

    // Background Card
    float alphaBg = isActive ? 0.18f : (isHovered ? 0.08f : 0.018f);
    float alphaBorder = isActive ? 0.40f : (isHovered ? 0.14f : 0.045f);

    if (isDragging)
    {
        alphaBg = 0.18f;
        alphaBorder = 0.36f;
    }
    else if (isShortcutDragTarget)
    {
        alphaBg = 0.16f;
        alphaBorder = 0.44f;
    }

    ID2D1SolidColorBrush* bgBrush = nullptr;
    if (isDragging || isShortcutDragTarget || isActive)
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
    if (isDragging || isShortcutDragTarget || isActive)
    {
        D2D1_COLOR_F accentClr = UIStyle::ThemeColor::Accent().d2d;
        accentClr.a = isActive ? 0.55f : alphaBorder;
        rt->CreateSolidColorBrush(accentClr, &borderBrush);
    }
    else
    {
        rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, alphaBorder), &borderBrush);
    }

    if (borderBrush)
    {
        float strokeWidth = UIStyle::Metrics::ControlStroke();
        if (isShortcutDragTarget) strokeWidth = UIStyle::Metrics::EmphasisStroke();
        else if (isActive) strokeWidth = UIStyle::Metrics::HairlineStroke();
        rt->DrawRoundedRectangle(roundedItem, borderBrush, strokeWidth);
        borderBrush->Release();
    }

    // Accent indicator bar (Active category)
    if (isActive)
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
        ID2D1SolidColorBrush* tb = nullptr;
        rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextNormal().d2d, &tb);
        if (tb)
        {
            std::wstring name = m_owner->GetCategoryName(i);
            float textLeft = isActive ? 22.0f : 18.0f;
            rt->DrawTextW(name.c_str(), (UINT32)name.size(), tfLeft,
                D2D1::RectF(textLeft, cy, 115, cy + 32), tb);
            tb->Release();
        }
    }
}

void CategoryList::OnPaint(ID2D1HwndRenderTarget* rt, const D2D1_RECT_F& rect)
{
    // Draw Category List Title
    IDWriteTextFormat* tfTitle = m_owner->GetTitleFont();
    if (tfTitle)
    {
        ID2D1SolidColorBrush* textBrush = nullptr;
        rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextMuted().d2d, &textBrush);
        if (textBrush)
        {
            rt->DrawTextW(L"分类列表", 4, tfTitle, D2D1::RectF(10, 42, 140, 62), textBrush);
            textBrush->Release();
        }
    }

    size_t count = m_owner->GetCategoryCount();
    int currentCategory = m_owner->GetCurrentCategoryIndex();
    IDWriteTextFormat* tfLeft = m_owner->GetLeftFont();

    D2D1_COLOR_F baseClr = UIStyle::ThemeColor::ThemeBase().d2d;

    EnsureCategoryStates();

    // Draw Categories
    for (int i = 0; i < (int)count; i++)
    {
        if (m_owner->IsSettingsMode())
        {
            float cy = (float)(72 + i * 40);
            bool isActive = (i == currentCategory);
            bool isHovered = (i == m_hoveredCategory);
            DrawCategoryItem(rt, i, cy, isActive, isHovered, false, baseClr, tfLeft);
        }
        else
        {
            if (i == m_dragIndex) continue;
            if (i >= (int)m_categoryStates.size()) continue;

            float cy = std::roundf(m_categoryStates[i].currentY);
            bool isActive = (i == currentCategory);
            bool isHovered = (i == m_hoveredCategory) && (m_dragIndex == -1);
            DrawCategoryItem(rt, i, cy, isActive, isHovered, false, baseClr, tfLeft);
        }
    }

    // Draw Dragged Category on top
    if (m_dragIndex >= 0 && m_dragIndex < (int)count && m_dragIndex < (int)m_categoryStates.size())
    {
        float cy = std::roundf(m_categoryStates[m_dragIndex].currentY);
        bool isActive = (m_dragIndex == currentCategory);
        DrawCategoryItem(rt, m_dragIndex, cy, isActive, false, true, baseClr, tfLeft);
    }

    // "+ 新建分类" Button
    if (!m_owner->IsSettingsMode())
    {
        D2D1_RECT_F addCatRect = D2D1::RectF(10, rect.bottom - 42.0f, 140, rect.bottom - 10.0f);
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
        repaint = true;
        return;
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
        repaint = true;
        return;
    }

    int hc = HitTestCategory(pt);
    if (hc >= 0 && hc < (int)m_owner->GetCategoryCount())
    {
        m_owner->SetCurrentCategoryIndex(hc);

        // Start dragging if not in Settings mode and not the DOCK category
        if (!m_owner->IsSettingsMode() && m_owner->GetCategoryName(hc) != L"DOCK")
        {
            m_dragIndex = hc;
            m_dragCurrentInsertIndex = hc;

            EnsureCategoryStates();

            float origY = (float)(72 + hc * 40);
            m_grabOffsetY = pt.y - origY;

            SetCapture(hwnd);

            m_animating = true;
            m_owner->StartAnimation();
        }

        repaint = true;
    }
}

void CategoryList::OnLButtonUp(POINT pt, bool& repaint)
{
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
                m_categoryStates[i].targetY = (float)(72 + i * 40);
            }
        }

        m_dragIndex = -1;
        m_dragCurrentInsertIndex = -1;

        m_animating = true;
        m_owner->StartAnimation();

        repaint = true;
    }
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
        repaint = true;

        HWND hwnd = m_owner->GetWindowHWND();
        POINT screenPt = pt;
        ClientToScreen(hwnd, &screenPt);

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
                        m_categoryStates[i].targetY = (float)(72 + i * 40);
                    }
                }

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
    if (!m_animating) return;

    HWND hWnd = m_owner->GetWindowHWND();

    if (m_dragIndex >= 0)
    {
        POINT mousePt;
        GetCursorPos(&mousePt);
        ScreenToClient(hWnd, &mousePt);
        float scale = GlassWindow::GetWindowScale(hWnd);
        mousePt.x = (int)(mousePt.x / scale);
        mousePt.y = (int)(mousePt.y / scale);
        UpdateDragAndSortState(mousePt);
    }

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

    bool dragging = (m_dragIndex >= 0);

    if (dragging || anyCategoryMoving)
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
    float mouseY = (float)pt.y;
    float leaderCurrentY = mouseY - m_grabOffsetY;

    // Capping boundary
    float minDragY = 92.0f;
    float maxDragY = 72.0f + (count - 1) * 40.0f + 20.0f;
    if (leaderCurrentY < minDragY) leaderCurrentY = minDragY;
    if (leaderCurrentY > maxDragY) leaderCurrentY = maxDragY;

    if (m_dragIndex < (int)m_categoryStates.size())
    {
        m_categoryStates[m_dragIndex].currentY = leaderCurrentY;
        m_categoryStates[m_dragIndex].targetY = leaderCurrentY;
    }

    // 2. Find closest slot (DOCK is slot 0, and is fixed, so slots start at 1)
    float centerY = leaderCurrentY + 16.0f;
    int closestSlot = m_dragCurrentInsertIndex;
    float minDistSq = -1.0f;

    for (int j = 1; j < (int)count; j++)
    {
        float slotY = (float)(72 + j * 40 + 16);
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

        m_categoryStates[i].targetY = (float)(72 + targetSlot * 40);
    }
}

int CategoryList::HitTestCategory(POINT pt)
{
    if (pt.x < 10 || pt.x > 140 || pt.y < 72) return -1;
    size_t count = m_owner->GetCategoryCount();
    for (int i = 0; i < (int)count; i++)
    {
        int y = 72 + i * 40;
        if (pt.y >= y && pt.y <= y + 32)
            return i;
    }
    return -1;
}

bool CategoryList::HitTestAddCategory(POINT pt)
{
    RECT cr; GetClientRect(m_owner->GetWindowHWND(), &cr);
    float scale = GlassWindow::GetWindowScale(m_owner->GetWindowHWND());
    float h = (float)cr.bottom / scale;
    return (pt.x >= 10 && pt.x <= 140 && pt.y >= h - 42.0f && pt.y <= h - 10.0f);
}
