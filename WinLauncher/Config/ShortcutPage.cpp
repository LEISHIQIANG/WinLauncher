#define NOMINMAX
#include "ShortcutPage.h"
#include "../UI/MouseCaptureController.h"
#include "ShortcutGridViewHelper.h"
#include "DeleteCursorFactory.h"
#include "IConfigWindow.h"
#include "UIStyle.h"
#include "ContextMenu.h"
#include "PromptWindow.h"
#include "ConfirmWindow.h"
#include "ShortcutDialogController.h"
#include "../DpiHelper.h"
#include "../resource.h"
#include "../ToastWindow.h"
#include "../App/AppContext.h"
#include "../Services/FaviconFetcher.h"
#include "../UI/Controls/IconRenderer.h"
#include <windowsx.h>
#include <shlobj.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <commdlg.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>

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
    , m_selectionAnchorIndex(-1)
    , m_lastRt(nullptr)
    , m_trackMouse(false)
{
    m_faviconFetcher = std::make_unique<FaviconBatchFetcher>();
}

ShortcutPage::~ShortcutPage()
{
    MouseCaptureController::ReleaseForContext(this, L"shortcut_page_destroyed");
    if (m_faviconFetcher)
    {
        m_faviconFetcher->Detach();
    }
    m_brushCache.Clear();
}

void ShortcutPage::CancelPointerInteractionThunk(void* context)
{
    if (context)
        static_cast<ShortcutPage*>(context)->CancelPointerInteraction();
}

void ShortcutPage::CancelPointerInteraction()
{
    const bool changed = m_dragController.HasCandidate() || m_dragController.IsActive();
    m_dragController.Reset();
    ResetShortcutTargets(false);
    if (changed && m_owner)
    {
        m_owner->StartAnimation();
        HWND hwnd = m_owner->GetWindowHWND();
        if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
    }
}

IIconService* ShortcutPage::SharedIconService() const
{
    AppContext* ctx = m_owner ? m_owner->GetAppContext() : nullptr;
    return ctx ? ctx->iconService.get() : nullptr;
}

void ShortcutPage::SetPageData(RendPopupPage* page, bool preserveScroll)
{
    CancelBatchFaviconFetches();
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
    m_dragController.Reset();
    m_pendingDeleteIndices.clear();
    m_addCardInitialized = false;
    m_hoveredShortcut = -1;
    m_hoveredAddShortcut = false;
    m_lastRt = nullptr;
    m_brushCache.Clear();
}

ShortcutDialogController::DialogHostContext ShortcutPage::BuildDialogHostContext() const
{
    return {
        m_owner ? m_owner->GetWindowHWND() : nullptr,
        m_owner ? m_owner->GetAppContext() : nullptr,
        m_pageData,
        m_owner,
        SharedIconService(),
        [this](const RendShortcutInfo& sc) { return CreateShortcutBitmap(sc); },
        [this](bool snap) { const_cast<ShortcutPage*>(this)->NotifyShortcutListChanged(snap); }
    };
}

void ShortcutPage::ShowAddShortcutDialog()
{
    ShortcutDialogController::ShowAddShortcut(BuildDialogHostContext());
}

void ShortcutPage::ShowAddHotkeyDialog()
{
    ShortcutDialogController::ShowAddHotkey(BuildDialogHostContext());
}

void ShortcutPage::ShowAddUrlDialog()
{
    ShortcutDialogController::ShowAddUrl(BuildDialogHostContext());
}

void ShortcutPage::ShowAddCommandDialog()
{
    ShortcutDialogController::ShowAddCommand(BuildDialogHostContext());
}

void ShortcutPage::ShowAddMacroDialog()
{
    ShortcutDialogController::ShowAddMacro(BuildDialogHostContext());
}

void ShortcutPage::ShowAddBatchDialog()
{
    ShortcutDialogController::ShowAddBatch(BuildDialogHostContext());
}

void ShortcutPage::ShowBuiltinIconDialog()
{
    ShortcutDialogController::ShowBuiltinIcon(BuildDialogHostContext());
}

void ShortcutPage::UpdateTheme()
{
    m_brushCache.Clear();
    if (m_pageData)
    {
        for (auto* bmp : m_pageData->iconBitmaps)
        {
            if (bmp) bmp->Release();
        }
        m_pageData->iconBitmaps.clear();
    }
}

ID2D1Bitmap* ShortcutPage::CreateShortcutBitmap(const RendShortcutInfo& shortcut) const
{
    // The popup and the config page can both resolve a real shell icon for a
    // dropped .lnk.  The config card must not discard that HICON merely
    // because the shortcut has automatic icon source/file-like metadata.
    // CreateD2DBitmapFromHicon still creates the text fallback when extraction
    // genuinely failed and hIcon is null.
    HICON hIcon = shortcut.hIcon;
    bool invert = (UIStyle::GetThemeMode() == UIStyle::ThemeMode::Light)
        ? shortcut.iconInvertLight
        : shortcut.iconInvertDark;
    return m_owner->CreateD2DBitmapFromHicon(hIcon, shortcut.name, invert);
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
    if (m_dragController.IsActive())
    {
        m_dragController.RenderInsertionSlots(rt, m_brushCache, m_shortcutStates, n, m_scrollY);
    }

    // 2. Draw all non-dragged shortcut cards
    for (int i = 0; i < n; i++)
    {
        if (m_dragController.IsActive() && m_shortcutStates[i].selected) continue;
        if (IsShortcutPendingDelete(i)) continue;
        if (i >= (int)m_shortcutStates.size()) continue;

        float X = std::roundf(m_shortcutStates[i].currentX);
        float Y = std::roundf(m_shortcutStates[i].currentY - m_scrollY);

        bool isHovered = (i == m_hoveredShortcut) && !m_dragController.IsActive();
        bool isSelected = m_shortcutStates[i].selected;
        ID2D1Bitmap* iconBmp = (i < (int)m_pageData->iconBitmaps.size()) ? m_pageData->iconBitmaps[i] : nullptr;
        float revealAlpha = (i < (int)m_shortcutStates.size()) ? m_shortcutStates[i].iconReveal : 1.0f;

        ShortcutGridViewHelper::RenderCardItem(rt, m_brushCache, X, Y, isSelected, isHovered, false,
            iconBmp, revealAlpha, m_pageData->shortcuts[i].name, tfDefault);
    }

    // 3. Draw "+ 添加" Card
    if (!m_pageData->isSyncFolder)
    {
        UpdateAddShortcutTarget(!m_pendingDeleteIndices.empty(), !m_addCardInitialized);
        float X = std::roundf(m_addCardCurrentX);
        float Y = std::roundf(m_addCardCurrentY - m_scrollY);
        ShortcutGridViewHelper::RenderAddCard(rt, m_brushCache, X, Y, m_hoveredAddShortcut, tfDefault);
    }

    rt->PopAxisAlignedClip();

    // 4. Draw the dragged items on top of everything (drawn after PopAxisAlignedClip to avoid clipping when dragging to the left panel)
    if (m_dragController.IsActive() && m_dragController.GetDragIndex() >= 0)
    {
        for (int i = 0; i < n; i++)
        {
            if (!m_shortcutStates[i].selected) continue;
            if (IsShortcutPendingDelete(i)) continue;
            if (i >= (int)m_shortcutStates.size()) continue;

            float X = std::roundf(m_shortcutStates[i].currentX);
            float Y = std::roundf(m_shortcutStates[i].currentY - m_scrollY);
            ID2D1Bitmap* iconBmp = (i < (int)m_pageData->iconBitmaps.size()) ? m_pageData->iconBitmaps[i] : nullptr;
            float revealAlpha = (i < (int)m_shortcutStates.size()) ? m_shortcutStates[i].iconReveal : 1.0f;

            ShortcutGridViewHelper::RenderCardItem(rt, m_brushCache, X, Y, true, false, true,
                iconBmp, revealAlpha, m_pageData->shortcuts[i].name, tfDefault);
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

    if (m_dragController.HasCandidate())
    {
        if (!m_dragController.IsActive())
        {
            if (m_dragController.HasExceededThreshold(pt))
            {
                if (m_dragController.StartDrag(m_owner, m_pageData, m_shortcutStates, m_scrollY, pt))
                    m_dragController.UpdateCursor(m_owner ? m_owner->GetWindowHWND() : nullptr, pt);
            }
        }
        else
        {
            m_dragController.UpdateDragAndSortState(pt, m_scrollY, m_shortcutStates);
            m_dragController.UpdateCursor(m_owner ? m_owner->GetWindowHWND() : nullptr, pt);
            if (m_owner) m_owner->StartAnimation();
        }
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
        EnsureShortcutStates();

        bool ctrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

        if (shiftPressed)
        {
            ShortcutSelectionModel::SelectRange(m_shortcutStates, m_selectionAnchorIndex, hs);
        }
        else if (ctrlPressed)
        {
            ShortcutSelectionModel::ToggleSelection(m_shortcutStates, m_selectionAnchorIndex, hs);
        }
        else
        {
            ShortcutSelectionModel::SelectSingle(m_shortcutStates, m_selectionAnchorIndex, hs);
        }

        if (m_shortcutStates[hs].selected)
        {
            m_dragController.BeginCandidate(hs, pt);
            if (!MouseCaptureController::CaptureGesture(
                    hWnd, VK_LBUTTON, this, &ShortcutPage::CancelPointerInteractionThunk))
            {
                m_dragController.Reset();
            }
        }

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
            ShortcutSelectionModel::ClearSelection(m_shortcutStates, m_selectionAnchorIndex);
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
    if (m_dragController.HasCandidate())
    {
        MouseCaptureController::Complete(m_owner ? m_owner->GetWindowHWND() : nullptr);

        m_dragController.HandleLButtonUp(
            pt,
            m_owner,
            m_pageData,
            m_shortcutStates,
            m_scrollY,
            m_selectionAnchorIndex,
            [this](const std::vector<int>& indices, bool& rep) {
                return ConfirmPendingDeleteShortcuts(indices, rep);
            },
            [this](bool snap) {
                ResetShortcutTargets(false);
            },
            repaint
        );
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
        POINT screenPt = DpiHelper::LogicalClientToScreen(hWnd, pt);

        // Preserve an existing multi-selection when right-clicking one of its
        // members.  Right-clicking a different item starts a new selection.
        EnsureShortcutStates();
        std::vector<int> selectedIndices = GetSelectedShortcutIndices();
        bool preserveMultiSelection = selectedIndices.size() > 1 && m_shortcutStates[hs].selected;
        if (!preserveMultiSelection)
        {
            for (auto& s : m_shortcutStates)
            {
                s.selected = false;
            }
            m_shortcutStates[hs].selected = true;
            m_selectionAnchorIndex = hs;
            selectedIndices = { hs };
        }
        repaint = true;

        std::vector<ContextMenu::Item> menuItems;

        if (selectedIndices.size() > 1)
        {
            bool hasUrl = false;
            for (int index : selectedIndices)
            {
                if (index >= 0 && index < (int)m_pageData->shortcuts.size() &&
                    m_pageData->shortcuts[index].type == Model::ShortcutType::Url &&
                    !m_pageData->shortcuts[index].targetPath.empty())
                {
                    hasUrl = true;
                    break;
                }
            }

            // Multi-select intentionally has no edit or rename operation.
            menuItems.push_back({ L"删除", [this, selectedIndices]() {
                bool menuRepaint = false;
                if (ConfirmAndDeleteShortcuts(selectedIndices, menuRepaint))
                    InvalidateRect(m_owner->GetWindowHWND(), nullptr, FALSE);
            } });
            menuItems.push_back({ L"获取图标", [this, selectedIndices]() {
                FetchSelectedUrlFavicons(selectedIndices);
            }, !hasUrl });

            ContextMenu::Show(hWnd, screenPt, menuItems, m_owner->GetAppContext());
            return;
        }

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
                bool repaint = false;
                if (ConfirmAndDeleteShortcuts(std::vector<int>{ hs }, repaint))
                {
                    InvalidateRect(hWnd, nullptr, FALSE);
                }
            }
        } });

        menuItems.push_back({ L"重命名", [this, hs]() {
            HWND hWnd = m_owner->GetWindowHWND();
            std::wstring name = m_pageData->shortcuts[hs].name;
            if (PromptWindow::Show(hWnd, L"重命名快捷方式", L"输入新的名称:", name, name.c_str(), m_owner->GetAppContext()))
            {
                while (!name.empty() && (name.back() == L' ' || name.back() == L'\t'))
                    name.pop_back();
                size_t start = 0;
                while (start < name.size() && (name[start] == L' ' || name[start] == L'\t'))
                    start++;
                if (start > 0) name = name.substr(start);

                if (!name.empty() && hs < (int)m_pageData->shortcuts.size())
                {
                    m_owner->RecordShortcutHistoryCheckpoint();
                    m_pageData->shortcuts[hs].name = name;
                    m_owner->NotifyConfigChanged();
                    InvalidateRect(hWnd, nullptr, FALSE);
                }
            }
        } });

        ContextMenu::Show(hWnd, screenPt, menuItems, m_owner->GetAppContext());
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
        m_owner->RecordShortcutHistoryCheckpoint();
        for (UINT i = 0; i < fileCount; ++i)
        {
            wchar_t filePath[MAX_PATH]{};
            if (DragQueryFileW(hDrop, i, filePath, MAX_PATH))
            {
                AddShortcutFromPath(filePath);
            }
        }
        NotifyShortcutListChanged(false);
        m_owner->NotifyConfigChanged();
        repaint = true;
    }
}

void ShortcutPage::UpdateAnimation(float dt, bool& repaint)
{
    if (!m_animating) return;

    HWND hWnd = m_owner->GetWindowHWND();

    if (!UIStyle::Animation::IsEnabled())
    {
        m_scrollY = m_targetScrollY;
        m_scrollVelocity = 0.0f;

        // If dragging, we still need to update drag position and sort state
        if (m_dragController.IsActive())
        {
            POINT mousePt;
            GetCursorPos(&mousePt);
            ScreenToClient(hWnd, &mousePt);
            float scale = DpiHelper::GetWindowScale(hWnd);
            mousePt.x = (int)(mousePt.x / scale);
            mousePt.y = (int)(mousePt.y / scale);
            m_dragController.UpdateDragAndSortState(mousePt, m_scrollY, m_shortcutStates);
        }

        for (auto& state : m_shortcutStates)
        {
            state.currentX = state.targetX;
            state.currentY = state.targetY;
            state.iconReveal = 1.0f;
        }
        m_addCardCurrentX = m_addCardTargetX;
        m_addCardCurrentY = m_addCardTargetY;

        bool scrollAnimating = (std::abs(m_targetScrollY - m_scrollY) > 0.2f || std::abs(m_scrollVelocity) > 1.0f);
        bool dragging = m_dragController.IsActive();
        if (!scrollAnimating && !dragging)
        {
            m_animating = false;
        }
        repaint = true;
        return;
    }

    // 1. Drag auto-scroll logic
    if (m_dragController.IsActive() && m_pageData)
    {
        m_dragController.UpdateAutoScroll(hWnd, dt, m_pageData, m_targetScrollY);
    }

    // 2. Scroll animation physics update
    float scrollError = m_targetScrollY - m_scrollY;
    float stiffness = 200.0f;
    float damping = 22.0f;
    float force = scrollError * stiffness - m_scrollVelocity * damping;
    m_scrollVelocity += force * dt;
    m_scrollY += m_scrollVelocity * dt;

    // 3. Update dragged item position after scroll has changed
    if (m_dragController.IsActive())
    {
        POINT mousePt;
        GetCursorPos(&mousePt);
        ScreenToClient(hWnd, &mousePt);
        float scale = DpiHelper::GetWindowScale(hWnd);
        mousePt.x = (int)(mousePt.x / scale);
        mousePt.y = (int)(mousePt.y / scale);
        m_dragController.UpdateDragAndSortState(mousePt, m_scrollY, m_shortcutStates);
    }

    // 4. Update visual positions of other shortcuts using smooth decay
    bool anyIconMoving = false;
    const float decayRate = 28.0f;
    const float alpha = 1.0f - std::exp(-decayRate * dt);
    for (auto& state : m_shortcutStates)
    {
        float dx = state.targetX - state.currentX;
        float dy = state.targetY - state.currentY;
        if (std::abs(dx) > 0.1f || std::abs(dy) > 0.1f)
        {
            state.currentX += dx * alpha;
            state.currentY += dy * alpha;
            anyIconMoving = true;
        }
        else
        {
            state.currentX = state.targetX;
            state.currentY = state.targetY;
        }
    }
    if (m_addCardInitialized)
    {
        float dx = m_addCardTargetX - m_addCardCurrentX;
        float dy = m_addCardTargetY - m_addCardCurrentY;
        if (std::abs(dx) > 0.1f || std::abs(dy) > 0.1f)
        {
            m_addCardCurrentX += dx * alpha;
            m_addCardCurrentY += dy * alpha;
            anyIconMoving = true;
        }
        else
        {
            m_addCardCurrentX = m_addCardTargetX;
            m_addCardCurrentY = m_addCardTargetY;
        }
    }

    // 5. Determine whether we still need the animation loop
    bool scrollAnimating = (std::abs(m_targetScrollY - m_scrollY) > 0.2f || std::abs(m_scrollVelocity) > 1.0f);
    bool dragging = m_dragController.IsActive();

    // Late-arriving icons fade in over ~0.22s instead of popping in.
    bool anyIconRevealing = false;
    for (auto& state : m_shortcutStates)
    {
        if (state.iconReveal < 1.0f)
        {
            state.iconReveal = std::min(1.0f, state.iconReveal + dt / 0.22f);
            anyIconRevealing = true;
        }
    }

    if (scrollAnimating || dragging || anyIconMoving || anyIconRevealing)
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

void ShortcutPage::EnsureIcons(ID2D1HwndRenderTarget* rt)
{
    if (!m_pageData) return;

    float currentDpi = 96.0f;
    if (rt)
    {
        float dpiY = 96.0f;
        rt->GetDpi(&currentDpi, &dpiY);
    }
    const int iconBitmapSize = IconRenderer::GetRecommendedBitmapSize(rt, static_cast<float>(ICON_SIZE));
    bool rtChanged = (rt != m_lastRt) || (currentDpi != m_lastDpi) || (iconBitmapSize != m_lastIconBitmapSize);
    if (rtChanged)
    {
        m_lastRt = rt;
        m_lastDpi = currentDpi;
        m_lastIconBitmapSize = iconBitmapSize;
        m_brushCache.Clear();
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
            m_pageData->iconBitmaps[i] = CreateShortcutBitmap(m_pageData->shortcuts[i]);
        }
    }
    else
    {
        // Seamless single-icon refresh: a nullptr slot means the real HICON
        // arrived after this page was painted (background icon backfill).
        // Rebuild only that slot and fade it in; cache-warm neighbours and
        // text placeholders stay untouched.
        bool slotRevealed = false;
        EnsureShortcutStates();
        for (int i = 0; i < n; i++)
        {
            if (m_pageData->iconBitmaps[i] || !m_pageData->shortcuts[i].hIcon)
                continue;
            m_pageData->iconBitmaps[i] = CreateShortcutBitmap(m_pageData->shortcuts[i]);
            if (i < (int)m_shortcutStates.size())
            {
                m_shortcutStates[i].iconReveal =
                    UIStyle::Animation::IsEnabled() ? 0.0f : 1.0f;
            }
            slotRevealed = true;
        }
        if (slotRevealed)
        {
            m_animating = true;
            // The fade runs on the owner's animation timer; without starting
            // the pump the reveal progress would stay at 0 (invisible icons).
            m_owner->StartAnimation();
            HWND hWnd = m_owner->GetWindowHWND();
            if (hWnd) InvalidateRect(hWnd, nullptr, FALSE);
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

std::vector<int> ShortcutPage::GetSelectedShortcutIndices() const
{
    return ShortcutSelectionModel::GetSelectedIndices(m_shortcutStates);
}

std::vector<int> ShortcutPage::NormalizeShortcutIndices(const std::vector<int>& indices) const
{
    int shortcutCount = m_pageData ? (int)m_pageData->shortcuts.size() : 0;
    return ShortcutSelectionModel::NormalizeIndices(indices, shortcutCount);
}

bool ShortcutPage::IsShortcutPendingDelete(int index) const
{
    return ShortcutSelectionModel::IsPendingDelete(m_pendingDeleteIndices, index);
}

int ShortcutPage::CountVisibleShortcuts() const
{
    if (!m_pageData) return 0;
    return ShortcutSelectionModel::CountVisible((int)m_pageData->shortcuts.size(), m_pendingDeleteIndices);
}

void ShortcutPage::UpdateAddShortcutTarget(bool compactPendingDelete, bool snap)
{
    int slot = compactPendingDelete ? CountVisibleShortcuts() : (m_pageData ? (int)m_pageData->shortcuts.size() : 0);
    D2D1_POINT_2F pt = ShortcutSelectionModel::ComputeSlotPosition(slot);
    m_addCardTargetX = pt.x;
    m_addCardTargetY = pt.y;

    if (snap || !m_addCardInitialized)
    {
        m_addCardCurrentX = m_addCardTargetX;
        m_addCardCurrentY = m_addCardTargetY;
        m_addCardInitialized = true;
    }
}

bool ShortcutPage::IsPointOutsideWindow(POINT pt) const
{
    return ShortcutSelectionModel::IsPointOutsideWindow(m_owner ? m_owner->GetWindowHWND() : nullptr, pt);
}

void ShortcutPage::ResetShortcutTargets(bool compactPendingDelete)
{
    int visibleSlot = 0;
    for (int i = 0; i < (int)m_shortcutStates.size(); i++)
    {
        int slot = i;
        if (compactPendingDelete && IsShortcutPendingDelete(i))
        {
            // Hidden pending-delete icons keep their current position; visible icons animate into the compacted gaps.
            continue;
        }
        if (compactPendingDelete)
        {
            slot = visibleSlot++;
        }
        D2D1_POINT_2F pt = ShortcutSelectionModel::ComputeSlotPosition(slot);
        m_shortcutStates[i].targetX = pt.x;
        m_shortcutStates[i].targetY = pt.y;
    }
    UpdateAddShortcutTarget(compactPendingDelete, false);
}

void ShortcutPage::DeleteShortcuts(const std::vector<int>& sortedIndices)
{
    if (!m_pageData) return;
    CancelBatchFaviconFetches();

    for (auto it = sortedIndices.rbegin(); it != sortedIndices.rend(); ++it)
    {
        int index = *it;

        if (index < (int)m_pageData->iconBitmaps.size())
        {
            if (m_pageData->iconBitmaps[index])
            {
                m_pageData->iconBitmaps[index]->Release();
            }
            m_pageData->iconBitmaps.erase(m_pageData->iconBitmaps.begin() + index);
        }

        if (index < (int)m_pageData->shortcuts.size())
        {
            if (m_pageData->shortcuts[index].hIcon)
            {
                DestroyIcon(m_pageData->shortcuts[index].hIcon);
                m_pageData->shortcuts[index].hIcon = nullptr;
            }
            m_pageData->shortcuts.erase(m_pageData->shortcuts.begin() + index);
        }

        if (index < (int)m_shortcutStates.size())
        {
            m_shortcutStates.erase(m_shortcutStates.begin() + index);
        }
    }

    m_selectionAnchorIndex = -1;
    ResetShortcutTargets();
}

bool ShortcutPage::ConfirmAndDeleteShortcuts(const std::vector<int>& indices, bool& repaint)
{
    if (!m_pageData || m_pageData->isSyncFolder) return false;

    std::vector<int> normalized = NormalizeShortcutIndices(indices);
    if (normalized.empty()) return false;

    std::wstring prompt = ShortcutSelectionModel::BuildDeletePrompt(normalized.size());

    HWND hWnd = m_owner->GetWindowHWND();
    if (!ConfirmWindow::Show(hWnd, L"确认删除", prompt.c_str(), m_owner->GetAppContext()))
    {
        return false;
    }

    m_owner->RecordShortcutHistoryCheckpoint();
    DeleteShortcuts(normalized);

    m_animating = true;
    m_owner->StartAnimation();
    m_owner->NotifyConfigChanged();
    repaint = true;
    return true;
}

void ShortcutPage::CancelBatchFaviconFetches()
{
    if (m_faviconFetcher)
    {
        m_faviconFetcher->Cancel();
    }
}

void ShortcutPage::FetchSelectedUrlFavicons(const std::vector<int>& indices)
{
    if (!m_faviconFetcher) return;

    FaviconBatchFetcher::HostContext host{
        m_owner,
        m_pageData,
        SharedIconService(),
        [this](const RendShortcutInfo& sc) { return CreateShortcutBitmap(sc); },
        [this](const std::vector<int>& ind) { return NormalizeShortcutIndices(ind); }
    };
    m_faviconFetcher->StartFetch(host, indices);
}

bool ShortcutPage::ConfirmPendingDeleteShortcuts(const std::vector<int>& indices, bool& repaint)
{
    if (!m_pageData || m_pageData->isSyncFolder)
    {
        m_dragController.Reset();
        ResetShortcutTargets();
        repaint = true;
        return false;
    }

    std::vector<int> normalized = NormalizeShortcutIndices(indices);
    if (normalized.empty())
    {
        m_dragController.Reset();
        ResetShortcutTargets();
        repaint = true;
        return false;
    }

    HWND hWnd = m_owner->GetWindowHWND();
    m_pendingDeleteIndices = normalized;
    m_dragController.Reset();
    ResetShortcutTargets(true);

    m_animating = true;
    m_owner->StartAnimation();
    repaint = true;
    InvalidateRect(hWnd, nullptr, FALSE);
    UpdateWindow(hWnd);

    std::wstring prompt = ShortcutSelectionModel::BuildDeletePrompt(normalized.size());

    bool confirmed = ConfirmWindow::Show(hWnd, L"确认删除", prompt.c_str(), m_owner->GetAppContext());
    if (confirmed)
    {
        m_owner->RecordShortcutHistoryCheckpoint();
        DeleteShortcuts(normalized);
        m_owner->NotifyConfigChanged();
    }
    else
    {
        m_pendingDeleteIndices.clear();
        ResetShortcutTargets();
    }

    m_pendingDeleteIndices.clear();
    m_animating = true;
    m_owner->StartAnimation();
    repaint = true;
    InvalidateRect(hWnd, nullptr, FALSE);
    return confirmed;
}

bool ShortcutPage::HasDragExceededThreshold(POINT pt) const
{
    return m_dragController.HasExceededThreshold(pt);
}

void ShortcutPage::AddShortcutFromPath(const std::wstring& filePath)
{
    if (!m_pageData) return;

    std::wstring path = filePath;
    if (!path.empty() && (path.back() == L'\\' || path.back() == L'/'))
    {
        path.pop_back();
    }

    // A dropped directory is itself a launch target.  Do not expand it into its
    // child files: that makes a single folder drop unexpectedly populate the grid.
    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY))
    {
        AddShortcutFromSingleFile(path);
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
    sc.type = Model::ShortcutType::File;
    // A .lnk is stored as the resolved target when available.  Keep its target
    // kind in sync with that stored path so the card uses the real icon instead
    // of treating a valid executable target as a generic link/default icon.
    sc.targetKind = ShortcutManager::InferTargetKind(targetPath);
    sc.iconSource = Model::IconSource::Auto;
    if (sc.targetKind == Model::ShortcutTargetKind::Folder)
    {
        sc.iconSource = Model::IconSource::Builtin;
        sc.builtinIconId = L"folder";
    }
    sc.hIcon = ShortcutManager::GetShortcutIcon(sc, false, SharedIconService());

    m_pageData->shortcuts.push_back(sc);

    ID2D1Bitmap* bmp = CreateShortcutBitmap(sc);
    m_pageData->iconBitmaps.push_back(bmp);
}

void ShortcutPage::NotifyShortcutListChanged(bool snap)
{
    EnsureShortcutStates();
    UpdateAddShortcutTarget(!m_pendingDeleteIndices.empty(), snap);
    if (!snap)
    {
        m_animating = true;
        if (m_owner)
        {
            m_owner->StartAnimation();
        }
    }
}


int ShortcutPage::HitTestShortcut(POINT pt)
{
    if (!m_pageData) return -1;
    return ShortcutSelectionModel::HitTestGrid(pt, m_scrollY, (int)m_pageData->shortcuts.size());
}

bool ShortcutPage::HitTestAddShortcut(POINT pt)
{
    if (!m_pageData || m_pageData->isSyncFolder) return false;
    UpdateAddShortcutTarget(!m_pendingDeleteIndices.empty(), !m_addCardInitialized);
    return ShortcutSelectionModel::HitTestAddCard(pt, m_scrollY, m_addCardCurrentX, m_addCardCurrentY);
}

void ShortcutPage::EditShortcut(int index, bool& repaint)
{
    ShortcutDialogController::EditShortcut(BuildDialogHostContext(), index, repaint);
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
