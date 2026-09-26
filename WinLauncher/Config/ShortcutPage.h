#pragma once
#include "ConfigPage.h"
#include "ShortcutDialogController.h"
#include "ShortcutGridViewHelper.h"
#include "ShortcutSelectionModel.h"
#include "FaviconBatchFetcher.h"
#include "ShortcutDragController.h"
#include "../ShortcutManager.h"
#include "../App/BackgroundTaskService.h"
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
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
    bool IsDragging() const { return m_dragController.IsActive(); }

    using ShortcutVisualState = ::ShortcutVisualState;

private:
    IIconService* SharedIconService() const;

    static void CancelPointerInteractionThunk(void* context);
    void CancelPointerInteraction();

    void EnsureIcons(ID2D1HwndRenderTarget* rt);
    void EnsureShortcutStates();
    bool HasDragExceededThreshold(POINT pt) const;
    int CountVisibleShortcuts() const;
    void UpdateAddShortcutTarget(bool compactPendingDelete = false, bool snap = false);
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
    void NotifyShortcutListChanged(bool snap = false);
    ShortcutDialogController::DialogHostContext BuildDialogHostContext() const;
    ID2D1Bitmap* CreateShortcutBitmap(const RendShortcutInfo& shortcut) const;
    void FetchSelectedUrlFavicons(const std::vector<int>& indices);
    void CancelBatchFaviconFetches();

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

    // Drag-and-drop controller and selection states
    std::vector<ShortcutVisualState> m_shortcutStates;
    std::vector<int> m_pendingDeleteIndices;

    std::unique_ptr<FaviconBatchFetcher> m_faviconFetcher;
    ShortcutDragController m_dragController;

    int m_selectionAnchorIndex;
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
    ShortcutBrushCache m_brushCache;
    ComPtr<ID2D1SolidColorBrush> GetOrCreateBrush(ID2D1HwndRenderTarget* rt, const D2D1_COLOR_F& color)
    {
        return m_brushCache.GetOrCreateSolidBrush(rt, color);
    }
};
