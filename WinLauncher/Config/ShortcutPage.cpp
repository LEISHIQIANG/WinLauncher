#define NOMINMAX
#include "ShortcutPage.h"
#include "IConfigWindow.h"
#include "UIStyle.h"
#include "ContextMenu.h"
#include "PromptWindow.h"
#include "ConfirmWindow.h"
#include "ShortcutDialog.h"
#include "HotkeyDialog.h"
#include "UrlDialog.h"
#include "CommandDialog.h"
#include "MacroDialog.h"
#include "BatchLaunchDialog.h"
#include "BuiltinIconDialog.h"
#include "SystemIconDialog.h"
#include "../DpiHelper.h"
#include "../resource.h"
#include "../Services/SyncFolderService.h"
#include "../UI/Controls/IconRenderer.h"
#include <windowsx.h>
#include <shlobj.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <commdlg.h>
#include <algorithm>
#include <cmath>
#include <cstring>

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
    , m_dragActive(false)
    , m_dragDeleteCursorShown(false)
    , m_deleteCursor(nullptr)
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
    if (m_deleteCursor)
    {
        DestroyCursor(m_deleteCursor);
        m_deleteCursor = nullptr;
    }
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
    m_dragActive = false;
    m_dragDeleteCursorShown = false;
    m_pendingDeleteIndices.clear();
    m_addCardInitialized = false;
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
    if (ShortcutDialog::Show(hWnd, L"添加快捷方式", result, nullptr, m_owner->GetAppContext()))
    {
        if (!result.targetPath.empty())
        {
            RendShortcutInfo sc;
            sc.name            = result.name;
            sc.targetPath      = result.targetPath;
            sc.arguments       = result.arguments;
            sc.iconPath        = result.iconPath;
            sc.runAsAdmin      = result.runAsAdmin;
            sc.iconInvertLight = result.iconInvertLight;
            sc.iconInvertDark  = result.iconInvertDark;
            sc.type            = Model::ShortcutType::File;
            sc.targetKind      = ShortcutManager::InferTargetKind(result.targetPath);
            sc.iconSource      = result.iconPath.empty() ? Model::IconSource::Auto : Model::IconSource::CustomPath;
            sc.hIcon           = ShortcutManager::GetShortcutIcon(sc);

            m_owner->RecordShortcutHistoryCheckpoint();
            m_pageData->shortcuts.push_back(sc);
            ID2D1Bitmap* bmp = CreateShortcutBitmap(sc);
            m_pageData->iconBitmaps.push_back(bmp);

            m_owner->NotifyConfigChanged();
            InvalidateRect(hWnd, nullptr, FALSE);
        }
    }
}

void ShortcutPage::ShowAddHotkeyDialog()
{
    if (!m_pageData) return;
    HWND hWnd = m_owner->GetWindowHWND();
    HotkeyDialogResult result;
    if (HotkeyDialog::Show(hWnd, L"添加快捷键", result, nullptr, m_owner->GetAppContext()))
    {
        RendShortcutInfo sc;
        sc.name        = result.name;
        sc.targetPath  = result.hotkey;
        sc.arguments   = L"";
        sc.iconPath    = result.iconPath;
        sc.runAsAdmin  = result.afterClose;
        sc.type        = Model::ShortcutType::Hotkey;
        sc.iconInvertLight = result.iconInvertLight;
        sc.iconInvertDark  = result.iconInvertDark;
        sc.iconSource  = result.iconPath.empty() ? Model::IconSource::Auto : Model::IconSource::CustomPath;
        sc.hIcon       = ShortcutManager::GetShortcutIcon(sc);

        m_owner->RecordShortcutHistoryCheckpoint();
        m_pageData->shortcuts.push_back(sc);
        ID2D1Bitmap* bmp = CreateShortcutBitmap(sc);
        m_pageData->iconBitmaps.push_back(bmp);

        m_owner->NotifyConfigChanged();
        InvalidateRect(hWnd, nullptr, FALSE);
    }
}

void ShortcutPage::ShowAddUrlDialog()
{
    if (!m_pageData) return;
    HWND hWnd = m_owner->GetWindowHWND();
    UrlDialogResult result;
    if (UrlDialog::Show(hWnd, L"添加打开网址", result, nullptr, m_owner->GetAppContext()))
    {
        RendShortcutInfo sc;
        sc.name        = result.name;
        sc.targetPath  = result.url;
        sc.arguments   = result.browserPath + L"|||" + result.browserArgs;
        sc.iconPath    = result.iconPath;
        sc.runAsAdmin  = false;
        sc.type        = Model::ShortcutType::Url;
        sc.iconInvertLight = result.iconInvertLight;
        sc.iconInvertDark  = result.iconInvertDark;
        sc.iconSource  = result.iconPath.empty() ? Model::IconSource::Auto : Model::IconSource::CustomPath;
        sc.hIcon       = ShortcutManager::GetShortcutIcon(sc);

        m_owner->RecordShortcutHistoryCheckpoint();
        m_pageData->shortcuts.push_back(sc);
        ID2D1Bitmap* bmp = CreateShortcutBitmap(sc);
        m_pageData->iconBitmaps.push_back(bmp);

        m_owner->NotifyConfigChanged();
        InvalidateRect(hWnd, nullptr, FALSE);
    }
}

void ShortcutPage::ShowAddCommandDialog()
{
    if (!m_pageData) return;
    HWND hWnd = m_owner->GetWindowHWND();
    CommandDialogResult result;
    if (CommandDialog::Show(hWnd, L"添加运行命令", result, nullptr, m_owner->GetAppContext()))
    {
        RendShortcutInfo sc;
        sc.name        = result.name;
        sc.targetPath  = result.command;
        sc.arguments   = result.commandType + L"||||||" +
                         std::to_wstring((result.showWindow && !result.captureOutput) ? 1 : 0) + L"|||" +
                         std::to_wstring(result.captureOutput ? 1 : 0) + L"|||" + 
                         std::to_wstring(result.timeoutSeconds) + L"|||" + 
                         std::to_wstring(result.maxChars);
        sc.iconPath    = result.iconPath;
        sc.runAsAdmin  = result.runAsAdmin;
        sc.type        = Model::ShortcutType::Command;
        sc.iconInvertLight = result.iconInvertLight;
        sc.iconInvertDark  = result.iconInvertDark;
        sc.iconSource  = result.iconPath.empty() ? Model::IconSource::Auto : Model::IconSource::CustomPath;
        sc.hIcon       = ShortcutManager::GetShortcutIcon(sc);

        m_owner->RecordShortcutHistoryCheckpoint();
        m_pageData->shortcuts.push_back(sc);
        ID2D1Bitmap* bmp = CreateShortcutBitmap(sc);
        m_pageData->iconBitmaps.push_back(bmp);

        m_owner->NotifyConfigChanged();
        InvalidateRect(hWnd, nullptr, FALSE);
    }
}

void ShortcutPage::ShowAddMacroDialog()
{
    if (!m_pageData) return;
    HWND hWnd = m_owner->GetWindowHWND();
    MacroDialogResult result;
    if (MacroDialog::Show(hWnd, L"添加宏", result, nullptr, m_owner->GetAppContext()))
    {
        RendShortcutInfo sc;
        sc.name        = result.name;
        sc.targetPath  = L"";
        sc.arguments   = result.arguments;
        sc.iconPath    = result.iconPath;
        sc.runAsAdmin  = false;
        sc.type        = Model::ShortcutType::Macro;
        sc.iconInvertLight = result.iconInvertLight;
        sc.iconInvertDark  = result.iconInvertDark;
        sc.iconSource  = result.iconPath.empty() ? Model::IconSource::Auto : Model::IconSource::CustomPath;
        sc.hIcon       = ShortcutManager::GetShortcutIcon(sc);

        m_owner->RecordShortcutHistoryCheckpoint();
        m_pageData->shortcuts.push_back(sc);
        ID2D1Bitmap* bmp = CreateShortcutBitmap(sc);
        m_pageData->iconBitmaps.push_back(bmp);

        m_owner->NotifyConfigChanged();
        InvalidateRect(hWnd, nullptr, FALSE);
    }
}

void ShortcutPage::ShowAddBatchDialog()
{
    if (!m_pageData) return;
    HWND hWnd = m_owner->GetWindowHWND();
    BatchLaunchDialogResult result;
    if (BatchLaunchDialog::Show(hWnd, L"添加批量启动", result, nullptr, m_owner->GetAppContext()))
    {
        RendShortcutInfo sc;
        sc.name        = result.name;
        sc.targetPath  = L"";
        sc.arguments   = result.arguments;
        sc.iconPath    = result.iconPath;
        sc.runAsAdmin  = false;
        sc.type        = Model::ShortcutType::Batch;
        sc.iconInvertLight = result.iconInvertLight;
        sc.iconInvertDark  = result.iconInvertDark;
        sc.iconSource  = result.iconPath.empty() ? Model::IconSource::Auto : Model::IconSource::CustomPath;
        sc.hIcon       = ShortcutManager::GetShortcutIcon(sc);

        m_owner->RecordShortcutHistoryCheckpoint();
        m_pageData->shortcuts.push_back(sc);
        ID2D1Bitmap* bmp = CreateShortcutBitmap(sc);
        m_pageData->iconBitmaps.push_back(bmp);

        m_owner->NotifyConfigChanged();
        InvalidateRect(hWnd, nullptr, FALSE);
    }
}

void ShortcutPage::ShowBuiltinIconDialog()
{
    if (!m_pageData) return;
    HWND hWnd = m_owner->GetWindowHWND();
    std::vector<RendShortcutInfo> results;
    if (BuiltinIconDialog::Show(hWnd, results, m_owner->GetAppContext()))
    {
        m_owner->RecordShortcutHistoryCheckpoint();
        for (auto& sc : results)
        {
            sc.hIcon = ShortcutManager::GetShortcutIcon(sc);

            m_pageData->shortcuts.push_back(sc);
            ID2D1Bitmap* bmp = CreateShortcutBitmap(sc);
            m_pageData->iconBitmaps.push_back(bmp);
        }

        m_owner->NotifyConfigChanged();
        InvalidateRect(hWnd, nullptr, FALSE);
    }
}

void ShortcutPage::UpdateTheme()
{
    m_brushCache.clear();
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
    bool allowGeneratedDefault = !m_pageData || !m_pageData->isSyncFolder;
    HICON hIcon = (allowGeneratedDefault && ShortcutManager::UsesGeneratedDefaultIcon(shortcut))
        ? nullptr
        : shortcut.hIcon;
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
    if (m_dragActive && m_dragIndex >= 0 && m_dragCurrentInsertIndex >= 0 && m_dragCurrentInsertIndex < n)
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
        if (m_dragActive && m_shortcutStates[i].selected) continue;
        if (IsShortcutPendingDelete(i)) continue;
        if (i >= (int)m_shortcutStates.size()) continue;

        float X = std::roundf(m_shortcutStates[i].currentX);
        float Y = std::roundf(m_shortcutStates[i].currentY - m_scrollY);

        bool isHovered = (i == m_hoveredShortcut) && !m_dragActive;
        D2D1_RECT_F cardRect = D2D1::RectF(X, Y, X + 62, Y + 62);
        D2D1_ROUNDED_RECT roundedCard = D2D1::RoundedRect(cardRect, 8.0f, 8.0f);

        bool isSelected = m_shortcutStates[i].selected;
        D2D1_COLOR_F baseClr = UIStyle::ThemeColor::ThemeBase().d2d;
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
            D2D1_RECT_F iconRect = IconRenderer::AlignToPixels(rt, iconX, iconY, (float)ICON_SIZE, (float)ICON_SIZE);
            rt->DrawBitmap(m_pageData->iconBitmaps[i], iconRect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
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
        UpdateAddShortcutTarget(!m_pendingDeleteIndices.empty(), !m_addCardInitialized);
        float X = std::roundf(m_addCardCurrentX);
        float Y = std::roundf(m_addCardCurrentY - m_scrollY);

        D2D1_RECT_F addCardRect = D2D1::RectF(X, Y, X + 62, Y + 62);
        D2D1_ROUNDED_RECT roundedAdd = D2D1::RoundedRect(addCardRect, 8.0f, 8.0f);

        D2D1_COLOR_F baseClr = UIStyle::ThemeColor::ThemeBase().d2d;
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
    if (m_dragActive && m_dragIndex >= 0)
    {
        for (int i = 0; i < n; i++)
        {
            if (!m_shortcutStates[i].selected) continue;
            if (IsShortcutPendingDelete(i)) continue;
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
                D2D1_RECT_F iconRect = IconRenderer::AlignToPixels(rt, iconX, iconY, (float)ICON_SIZE, (float)ICON_SIZE);
                rt->DrawBitmap(m_pageData->iconBitmaps[i], iconRect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
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
        if (!m_dragActive)
        {
            if (HasDragExceededThreshold(pt))
            {
                StartShortcutDrag(pt);
                UpdateDragDeleteCursor(pt);
            }
        }
        else
        {
            UpdateDragAndSortState(pt);
            UpdateDragDeleteCursor(pt);
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

        if (m_shortcutStates[hs].selected)
        {
            m_dragIndex = hs;
            m_dragCurrentInsertIndex = hs;
            m_dragActive = false;
            m_dragStartPt = pt;
            SetCapture(hWnd);
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
        UpdateDragDeleteCursor(POINT{ 0, 0 });

        POINT mousePt = pt;
        int dx = mousePt.x - m_dragStartPt.x;
        int dy = mousePt.y - m_dragStartPt.y;
        bool ctrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

        if (!m_dragActive && HasDragExceededThreshold(pt))
        {
            StartShortcutDrag(pt);
        }

        if (!m_dragActive)
        {
            if (std::abs(dx) <= 3 && std::abs(dy) <= 3 && !ctrlPressed && !shiftPressed)
            {
                // Simple click without modifiers.
                // Clear all selections except the one that was clicked.
                for (int i = 0; i < (int)m_shortcutStates.size(); i++)
                {
                    m_shortcutStates[i].selected = (i == m_dragIndex);
                }
            }

            m_dragIndex = -1;
            m_dragCurrentInsertIndex = -1;
            m_dragActive = false;
            repaint = true;
            return;
        }

        if (IsPointOutsideWindow(pt))
        {
            std::vector<int> selectedIndices = GetSelectedShortcutIndices();
            if (selectedIndices.empty())
            {
                selectedIndices.push_back(m_dragIndex);
            }

            ConfirmPendingDeleteShortcuts(selectedIndices, repaint);
            return;
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
                        m_owner->RecordShortcutHistoryCheckpoint();

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
            m_dragActive = false;
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
                    m_owner->RecordShortcutHistoryCheckpoint();

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
        m_dragActive = false;
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
        POINT screenPt = DpiHelper::LogicalClientToScreen(hWnd, pt);

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
        if (m_dragActive && m_dragIndex >= 0)
        {
            POINT mousePt;
            GetCursorPos(&mousePt);
            ScreenToClient(hWnd, &mousePt);
            float scale = DpiHelper::GetWindowScale(hWnd);
            mousePt.x = (int)(mousePt.x / scale);
            mousePt.y = (int)(mousePt.y / scale);
            UpdateDragAndSortState(mousePt);
        }

        for (auto& state : m_shortcutStates)
        {
            state.currentX = state.targetX;
            state.currentY = state.targetY;
        }
        m_addCardCurrentX = m_addCardTargetX;
        m_addCardCurrentY = m_addCardTargetY;

        bool scrollAnimating = (std::abs(m_targetScrollY - m_scrollY) > 0.2f || std::abs(m_scrollVelocity) > 1.0f);
        bool dragging = m_dragActive;
        if (!scrollAnimating && !dragging)
        {
            m_animating = false;
        }
        repaint = true;
        return;
    }

    // 1. Drag auto-scroll logic
    if (m_dragActive && m_dragIndex >= 0 && m_pageData)
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
    if (m_dragActive && m_dragIndex >= 0)
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
    if (m_addCardInitialized)
    {
        float dx = m_addCardTargetX - m_addCardCurrentX;
        float dy = m_addCardTargetY - m_addCardCurrentY;
        if (std::abs(dx) > 0.1f || std::abs(dy) > 0.1f)
        {
            m_addCardCurrentX += dx * (1.0f - std::exp(-15.0f * dt));
            m_addCardCurrentY += dy * (1.0f - std::exp(-15.0f * dt));
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
    bool dragging = m_dragActive;

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
            m_pageData->iconBitmaps[i] = CreateShortcutBitmap(m_pageData->shortcuts[i]);
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
    std::vector<int> indices;
    for (int i = 0; i < (int)m_shortcutStates.size(); i++)
    {
        if (m_shortcutStates[i].selected)
        {
            indices.push_back(i);
        }
    }
    return indices;
}

std::vector<int> ShortcutPage::NormalizeShortcutIndices(const std::vector<int>& indices) const
{
    std::vector<int> normalized = indices;
    std::sort(normalized.begin(), normalized.end());
    normalized.erase(std::unique(normalized.begin(), normalized.end()), normalized.end());

    int shortcutCount = m_pageData ? (int)m_pageData->shortcuts.size() : 0;
    normalized.erase(
        std::remove_if(normalized.begin(), normalized.end(),
            [shortcutCount](int index) { return index < 0 || index >= shortcutCount; }),
        normalized.end());
    return normalized;
}

bool ShortcutPage::IsShortcutPendingDelete(int index) const
{
    return std::binary_search(m_pendingDeleteIndices.begin(), m_pendingDeleteIndices.end(), index);
}

int ShortcutPage::CountVisibleShortcuts() const
{
    if (!m_pageData) return 0;
    int count = 0;
    for (int i = 0; i < (int)m_pageData->shortcuts.size(); i++)
    {
        if (!IsShortcutPendingDelete(i))
        {
            count++;
        }
    }
    return count;
}

void ShortcutPage::UpdateAddShortcutTarget(bool compactPendingDelete, bool snap)
{
    int slot = compactPendingDelete ? CountVisibleShortcuts() : (m_pageData ? (int)m_pageData->shortcuts.size() : 0);
    m_addCardTargetX = (float)(160 + (slot % 5) * 72);
    m_addCardTargetY = (float)(72 + (slot / 5) * 72);

    if (snap || !m_addCardInitialized)
    {
        m_addCardCurrentX = m_addCardTargetX;
        m_addCardCurrentY = m_addCardTargetY;
        m_addCardInitialized = true;
    }
}

bool ShortcutPage::IsPointOutsideWindow(POINT pt) const
{
    HWND hWnd = m_owner ? m_owner->GetWindowHWND() : nullptr;
    if (!hWnd) return false;

    RECT cr{};
    if (!GetClientRect(hWnd, &cr)) return false;

    float scale = DpiHelper::GetWindowScale(hWnd);
    float w = (float)(cr.right - cr.left) / scale;
    float h = (float)(cr.bottom - cr.top) / scale;

    return pt.x < 0 || pt.y < 0 || (float)pt.x >= w || (float)pt.y >= h;
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
        m_shortcutStates[i].targetX = (float)(160 + (slot % 5) * 72);
        m_shortcutStates[i].targetY = (float)(72 + (slot / 5) * 72);
    }
    UpdateAddShortcutTarget(compactPendingDelete, false);
}

void ShortcutPage::DeleteShortcuts(const std::vector<int>& sortedIndices)
{
    if (!m_pageData) return;

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

    std::wstring prompt = normalized.size() == 1
        ? L"确定要删除该快捷方式吗？"
        : L"确定要删除选中的 " + std::to_wstring(normalized.size()) + L" 个快捷方式吗？";

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

bool ShortcutPage::ConfirmPendingDeleteShortcuts(const std::vector<int>& indices, bool& repaint)
{
    if (!m_pageData || m_pageData->isSyncFolder)
    {
        m_dragIndex = -1;
        m_dragCurrentInsertIndex = -1;
        m_dragActive = false;
        ResetShortcutTargets();
        repaint = true;
        return false;
    }

    std::vector<int> normalized = NormalizeShortcutIndices(indices);
    if (normalized.empty())
    {
        m_dragIndex = -1;
        m_dragCurrentInsertIndex = -1;
        m_dragActive = false;
        ResetShortcutTargets();
        repaint = true;
        return false;
    }

    HWND hWnd = m_owner->GetWindowHWND();
    m_pendingDeleteIndices = normalized;
    m_dragIndex = -1;
    m_dragCurrentInsertIndex = -1;
    m_dragActive = false;
    m_dragDeleteCursorShown = false;
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    ResetShortcutTargets(true);

    m_animating = true;
    m_owner->StartAnimation();
    repaint = true;
    InvalidateRect(hWnd, nullptr, FALSE);
    UpdateWindow(hWnd);

    std::wstring prompt = normalized.size() == 1
        ? L"确定要删除该快捷方式吗？"
        : L"确定要删除选中的 " + std::to_wstring(normalized.size()) + L" 个快捷方式吗？";

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
    int dx = pt.x - m_dragStartPt.x;
    int dy = pt.y - m_dragStartPt.y;
    return std::abs(dx) > 4 || std::abs(dy) > 4;
}

void ShortcutPage::UpdateDragDeleteCursor(POINT pt)
{
    bool showDeleteCursor = m_dragActive && IsPointOutsideWindow(pt);
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

HCURSOR ShortcutPage::GetDeleteCursor()
{
    if (m_deleteCursor) return m_deleteCursor;

    constexpr int size = 32;
    std::vector<DWORD> pixels(size * size, 0);

    auto setPixel = [&pixels](int x, int y, BYTE a, BYTE r, BYTE g, BYTE b)
    {
        if (x < 0 || x >= size || y < 0 || y >= size) return;
        pixels[y * size + x] = ((DWORD)a << 24) | ((DWORD)r << 16) | ((DWORD)g << 8) | (DWORD)b;
    };

    auto drawLine = [&setPixel](int x0, int y0, int x1, int y1, BYTE a, BYTE r, BYTE g, BYTE b)
    {
        int dx = std::abs(x1 - x0);
        int sx = x0 < x1 ? 1 : -1;
        int dy = -std::abs(y1 - y0);
        int sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;
        while (true)
        {
            setPixel(x0, y0, a, r, g, b);
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    };

    const wchar_t* arrow[] = {
        L"X...............",
        L"XX..............",
        L"XWX.............",
        L"XWWX............",
        L"XWWWX...........",
        L"XWWWWX..........",
        L"XWWWWWX.........",
        L"XWWWWWWX........",
        L"XWWWWWWWX.......",
        L"XWWWWXXXXX......",
        L"XWWXWXX.........",
        L"XWX.XWX.........",
        L"XX..XWX.........",
        L"X....XWX........",
        L".....XWX........",
        L"......XX........",
    };
    for (int y = 0; y < 16; y++)
    {
        for (int x = 0; x < 16; x++)
        {
            wchar_t ch = arrow[y][x];
            if (ch == L'X') setPixel(x + 1, y + 1, 255, 18, 18, 18);
            else if (ch == L'W') setPixel(x + 1, y + 1, 255, 255, 255, 255);
        }
    }

    constexpr int badgeSize = 14;
    constexpr int badgeLeft = 17;
    constexpr int badgeTop = 15;
    HICON trashIcon = (HICON)LoadImageW(
        GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(IDI_DELETE_TRASH_ICON),
        IMAGE_ICON,
        badgeSize,
        badgeSize,
        LR_DEFAULTCOLOR);

    if (trashIcon)
    {
        BITMAPINFO badgeBmi{};
        badgeBmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        badgeBmi.bmiHeader.biWidth = badgeSize;
        badgeBmi.bmiHeader.biHeight = -badgeSize;
        badgeBmi.bmiHeader.biPlanes = 1;
        badgeBmi.bmiHeader.biBitCount = 32;
        badgeBmi.bmiHeader.biCompression = BI_RGB;

        void* badgeBitsRaw = nullptr;
        HBITMAP badgeBitmap = CreateDIBSection(nullptr, &badgeBmi, DIB_RGB_COLORS, &badgeBitsRaw, nullptr, 0);
        if (badgeBitmap && badgeBitsRaw)
        {
            HDC screenDc = GetDC(nullptr);
            HDC memDc = CreateCompatibleDC(screenDc);
            HGDIOBJ oldBitmap = SelectObject(memDc, badgeBitmap);
            DrawIconEx(memDc, 0, 0, trashIcon, badgeSize, badgeSize, 0, nullptr, DI_NORMAL);
            SelectObject(memDc, oldBitmap);
            DeleteDC(memDc);
            ReleaseDC(nullptr, screenDc);

            DWORD* badgeBits = static_cast<DWORD*>(badgeBitsRaw);
            for (int y = 0; y < badgeSize; y++)
            {
                for (int x = 0; x < badgeSize; x++)
                {
                    DWORD src = badgeBits[y * badgeSize + x];
                    BYTE srcB = (BYTE)(src & 0xFF);
                    BYTE srcG = (BYTE)((src >> 8) & 0xFF);
                    BYTE srcR = (BYTE)((src >> 16) & 0xFF);
                    BYTE srcA = (BYTE)((src >> 24) & 0xFF);
                    int luminance = (srcR * 30 + srcG * 59 + srcB * 11) / 100;
                    bool visible = srcA > 8 || luminance > 8;
                    if (!visible) continue;

                    BYTE alpha = srcA > 8 ? srcA : 255;
                    setPixel(badgeLeft + x + 1, badgeTop + y + 1, (BYTE)(alpha / 3), 0, 0, 0);

                    BYTE red = (BYTE)std::min(255, 150 + luminance / 2);
                    BYTE green = (BYTE)std::min(90, 12 + luminance / 6);
                    BYTE blue = (BYTE)std::min(90, 18 + luminance / 7);
                    setPixel(badgeLeft + x, badgeTop + y, alpha, red, green, blue);
                }
            }
        }
        if (badgeBitmap) DeleteObject(badgeBitmap);
        DestroyIcon(trashIcon);
    }
    else
    {
        drawLine(20, 17, 26, 17, 255, 220, 38, 38);
        drawLine(18, 19, 28, 19, 255, 185, 28, 28);
        drawLine(19, 20, 19, 28, 255, 239, 68, 68);
        drawLine(27, 20, 27, 28, 255, 185, 28, 28);
        drawLine(19, 28, 27, 28, 255, 185, 28, 28);
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = size;
    bmi.bmiHeader.biHeight = -size;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* colorBits = nullptr;
    HBITMAP colorBitmap = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &colorBits, nullptr, 0);
    if (!colorBitmap || !colorBits)
    {
        if (colorBitmap) DeleteObject(colorBitmap);
        return LoadCursorW(nullptr, IDC_ARROW);
    }
    memcpy(colorBits, pixels.data(), pixels.size() * sizeof(DWORD));

    std::vector<BYTE> maskBits((size * size + 7) / 8, 0);
    HBITMAP maskBitmap = CreateBitmap(size, size, 1, 1, maskBits.data());
    if (!maskBitmap)
    {
        DeleteObject(colorBitmap);
        return LoadCursorW(nullptr, IDC_ARROW);
    }

    ICONINFO iconInfo{};
    iconInfo.fIcon = FALSE;
    iconInfo.xHotspot = 2;
    iconInfo.yHotspot = 2;
    iconInfo.hbmMask = maskBitmap;
    iconInfo.hbmColor = colorBitmap;

    m_deleteCursor = CreateIconIndirect(&iconInfo);
    DeleteObject(maskBitmap);
    DeleteObject(colorBitmap);

    return m_deleteCursor ? m_deleteCursor : LoadCursorW(nullptr, IDC_ARROW);
}

void ShortcutPage::StartShortcutDrag(POINT pt)
{
    if (!m_pageData || m_dragIndex < 0 || m_dragIndex >= (int)m_shortcutStates.size()) return;
    if (!m_shortcutStates[m_dragIndex].selected) return;

    m_dragActive = true;
    m_dragCurrentInsertIndex = m_dragIndex;

    float leaderCurrentX = m_shortcutStates[m_dragIndex].currentX;
    float leaderCurrentY = m_shortcutStates[m_dragIndex].currentY;
    m_grabOffsetX = (float)m_dragStartPt.x - leaderCurrentX;
    m_grabOffsetY = (float)(m_dragStartPt.y + m_scrollY) - leaderCurrentY;

    for (auto& s : m_shortcutStates)
    {
        if (s.selected)
        {
            s.dragOffsetX = s.currentX - leaderCurrentX;
            s.dragOffsetY = s.currentY - leaderCurrentY;
        }
    }

    UpdateDragAndSortState(pt);
    m_animating = true;
    m_owner->StartAnimation();
}

void ShortcutPage::UpdateDragAndSortState(POINT clientPt)
{
    if (!m_dragActive || m_dragIndex < 0) return;

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
    sc.type = Model::ShortcutType::File;
    sc.targetKind = ShortcutManager::InferTargetKind(path);
    sc.iconSource = Model::IconSource::Auto;
    sc.hIcon = ShortcutManager::GetShortcutIcon(sc);

    m_pageData->shortcuts.push_back(sc);

    ID2D1Bitmap* bmp = CreateShortcutBitmap(sc);
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
    UpdateAddShortcutTarget(!m_pendingDeleteIndices.empty(), !m_addCardInitialized);
    float localAddY = m_addCardCurrentY - 72.0f;
    return (pt.x >= m_addCardCurrentX && pt.x <= m_addCardCurrentX + 62 &&
        scrolledY >= localAddY && scrolledY <= localAddY + 62);
}

void ShortcutPage::EditShortcut(int index, bool& repaint)
{
    HWND hWnd = m_owner->GetWindowHWND();
    if (!m_pageData || index < 0 || index >= (int)m_pageData->shortcuts.size()) return;

    RendShortcutInfo& sc = m_pageData->shortcuts[index];
    RendShortcutInfo originalSc = sc;
    bool changed = false;

    if (sc.type == Model::ShortcutType::Hotkey)
    {
        HotkeyDialog::InitParams init;
        init.name = sc.name;
        init.hotkey = sc.targetPath;
        init.iconPath = sc.iconPath;
        init.afterClose = sc.runAsAdmin;
        init.iconInvertLight = sc.iconInvertLight;
        init.iconInvertDark = sc.iconInvertDark;

        HotkeyDialogResult result;
        if (HotkeyDialog::Show(hWnd, L"编辑快捷键", result, &init, m_owner->GetAppContext()))
        {
            if (sc.name != result.name) { sc.name = result.name; changed = true; }
            if (sc.targetPath != result.hotkey) { sc.targetPath = result.hotkey; changed = true; }
            if (sc.iconPath != result.iconPath) { sc.iconPath = result.iconPath; changed = true; }
            Model::IconSource newIconSource = result.iconPath.empty() ? Model::IconSource::Auto : Model::IconSource::CustomPath;
            if (sc.iconSource != newIconSource) { sc.iconSource = newIconSource; changed = true; }
            if (sc.runAsAdmin != result.afterClose) { sc.runAsAdmin = result.afterClose; changed = true; }
            if (sc.iconInvertLight != result.iconInvertLight) { sc.iconInvertLight = result.iconInvertLight; changed = true; }
            if (sc.iconInvertDark != result.iconInvertDark) { sc.iconInvertDark = result.iconInvertDark; changed = true; }
        }
    }
    else if (sc.type == Model::ShortcutType::Url)
    {
        UrlDialog::InitParams init;
        init.name = sc.name;
        init.url = sc.targetPath;
        
        std::wstring browserPath, browserArgs;
        size_t sep = sc.arguments.find(L"|||");
        if (sep != std::wstring::npos)
        {
            browserPath = sc.arguments.substr(0, sep);
            browserArgs = sc.arguments.substr(sep + 3);
        }
        else
        {
            browserPath = sc.arguments;
        }
        init.browserPath = browserPath;
        init.browserArgs = browserArgs;
        init.iconPath = sc.iconPath;
        init.iconInvertLight = sc.iconInvertLight;
        init.iconInvertDark = sc.iconInvertDark;

        UrlDialogResult result;
        if (UrlDialog::Show(hWnd, L"编辑打开网址", result, &init, m_owner->GetAppContext()))
        {
            if (sc.name != result.name) { sc.name = result.name; changed = true; }
            if (sc.targetPath != result.url) { sc.targetPath = result.url; changed = true; }
            
            std::wstring newArgs = result.browserPath + L"|||" + result.browserArgs;
            if (sc.arguments != newArgs) { sc.arguments = newArgs; changed = true; }
            
            if (sc.iconPath != result.iconPath) { sc.iconPath = result.iconPath; changed = true; }
            Model::IconSource newIconSource = result.iconPath.empty() ? Model::IconSource::Auto : Model::IconSource::CustomPath;
            if (sc.iconSource != newIconSource) { sc.iconSource = newIconSource; changed = true; }
            if (sc.iconInvertLight != result.iconInvertLight) { sc.iconInvertLight = result.iconInvertLight; changed = true; }
            if (sc.iconInvertDark != result.iconInvertDark) { sc.iconInvertDark = result.iconInvertDark; changed = true; }
        }
    }
    else if (sc.type == Model::ShortcutType::Command)
    {
        CommandDialog::InitParams init;
        init.name = sc.name;
        init.command = sc.targetPath;
        init.iconPath = sc.iconPath;
        init.runAsAdmin = sc.runAsAdmin;
        init.iconInvertLight = sc.iconInvertLight;
        init.iconInvertDark = sc.iconInvertDark;

        std::vector<std::wstring> segments;
        std::wstring s = sc.arguments;
        size_t pos = 0;
        while ((pos = s.find(L"|||")) != std::wstring::npos)
        {
            segments.push_back(s.substr(0, pos));
            s.erase(0, pos + 3);
        }
        segments.push_back(s);
        
        std::wstring type = L"cmd";
        bool showWindow = false, captureOutput = false;
        int timeout = 300, maxChars = 50000;
        
        if (segments.size() > 0) type = segments[0];
        // segments[1] was builtinCmd — now unused, kept for backward compat
        if (segments.size() > 2) showWindow = (segments[2] == L"1");
        if (segments.size() > 3) captureOutput = (segments[3] == L"1");
        if (captureOutput) showWindow = false;
        if (segments.size() > 4) { try { timeout = std::stoi(segments[4]); } catch(...) {} }
        if (segments.size() > 5) { try { maxChars = std::stoi(segments[5]); } catch(...) {} }

        init.commandType = type;
        init.showWindow = showWindow;
        init.captureOutput = captureOutput;
        init.timeoutSeconds = timeout;
        init.maxChars = maxChars;

        CommandDialogResult result;
        if (CommandDialog::Show(hWnd, L"编辑运行命令", result, &init, m_owner->GetAppContext()))
        {
            if (sc.name != result.name) { sc.name = result.name; changed = true; }
            if (sc.targetPath != result.command) { sc.targetPath = result.command; changed = true; }
            if (sc.iconPath != result.iconPath) { sc.iconPath = result.iconPath; changed = true; }
            Model::IconSource newIconSource = result.iconPath.empty() ? Model::IconSource::Auto : Model::IconSource::CustomPath;
            if (sc.iconSource != newIconSource) { sc.iconSource = newIconSource; changed = true; }
            if (sc.runAsAdmin != result.runAsAdmin) { sc.runAsAdmin = result.runAsAdmin; changed = true; }
            if (sc.iconInvertLight != result.iconInvertLight) { sc.iconInvertLight = result.iconInvertLight; changed = true; }
            if (sc.iconInvertDark != result.iconInvertDark) { sc.iconInvertDark = result.iconInvertDark; changed = true; }
            
            std::wstring newArgs = result.commandType + L"||||||" +
                                   std::to_wstring((result.showWindow && !result.captureOutput) ? 1 : 0) + L"|||" +
                                   std::to_wstring(result.captureOutput ? 1 : 0) + L"|||" + 
                                   std::to_wstring(result.timeoutSeconds) + L"|||" + 
                                   std::to_wstring(result.maxChars);
            if (sc.arguments != newArgs) { sc.arguments = newArgs; changed = true; }
        }
    }
    else if (sc.type == Model::ShortcutType::System)
    {
        SystemIconDialog::InitParams init;
        init.name = sc.name;
        init.targetPath = sc.targetPath;
        init.iconPath = sc.iconPath;
        init.targetKind = sc.targetKind;
        init.iconSource = sc.iconSource;
        init.builtinIconId = sc.builtinIconId;
        init.iconInvertLight = sc.iconInvertLight;
        init.iconInvertDark = sc.iconInvertDark;

        SystemIconDialogResult result;
        if (SystemIconDialog::Show(hWnd, L"编辑系统图标", result, &init, m_owner->GetAppContext()))
        {
            if (sc.name != result.name) { sc.name = result.name; changed = true; }
            if (sc.iconPath != result.iconPath) { sc.iconPath = result.iconPath; changed = true; }
            Model::IconSource newIconSource = result.iconPath.empty() ? Model::IconSource::Auto : Model::IconSource::CustomPath;
            if (result.iconPath.empty() && !sc.builtinIconId.empty())
            {
                newIconSource = Model::IconSource::Builtin;
            }
            if (sc.iconSource != newIconSource) { sc.iconSource = newIconSource; changed = true; }
            if (sc.iconInvertLight != result.iconInvertLight) { sc.iconInvertLight = result.iconInvertLight; changed = true; }
            if (sc.iconInvertDark != result.iconInvertDark) { sc.iconInvertDark = result.iconInvertDark; changed = true; }
        }
    }
    else if (sc.type == Model::ShortcutType::Macro)
    {
        MacroDialog::InitParams init;
        init.name = sc.name;
        init.arguments = sc.arguments;
        init.iconPath = sc.iconPath;
        init.iconInvertLight = sc.iconInvertLight;
        init.iconInvertDark = sc.iconInvertDark;

        MacroDialogResult result;
        if (MacroDialog::Show(hWnd, L"编辑宏脚本", result, &init, m_owner->GetAppContext()))
        {
            if (sc.name != result.name) { sc.name = result.name; changed = true; }
            if (sc.arguments != result.arguments) { sc.arguments = result.arguments; changed = true; }
            if (sc.iconPath != result.iconPath) { sc.iconPath = result.iconPath; changed = true; }
            Model::IconSource newIconSource = result.iconPath.empty() ? Model::IconSource::Auto : Model::IconSource::CustomPath;
            if (sc.iconSource != newIconSource) { sc.iconSource = newIconSource; changed = true; }
            if (sc.iconInvertLight != result.iconInvertLight) { sc.iconInvertLight = result.iconInvertLight; changed = true; }
            if (sc.iconInvertDark != result.iconInvertDark) { sc.iconInvertDark = result.iconInvertDark; changed = true; }
        }
    }
    else if (sc.type == Model::ShortcutType::Batch)
    {
        BatchLaunchDialog::InitParams init;
        init.name = sc.name;
        init.arguments = sc.arguments;
        init.iconPath = sc.iconPath;
        init.iconInvertLight = sc.iconInvertLight;
        init.iconInvertDark = sc.iconInvertDark;

        BatchLaunchDialogResult result;
        if (BatchLaunchDialog::Show(hWnd, L"编辑批量启动队列", result, &init, m_owner->GetAppContext()))
        {
            if (sc.name != result.name) { sc.name = result.name; changed = true; }
            if (sc.arguments != result.arguments) { sc.arguments = result.arguments; changed = true; }
            if (sc.iconPath != result.iconPath) { sc.iconPath = result.iconPath; changed = true; }
            Model::IconSource newIconSource = result.iconPath.empty() ? Model::IconSource::Auto : Model::IconSource::CustomPath;
            if (sc.iconSource != newIconSource) { sc.iconSource = newIconSource; changed = true; }
            if (sc.iconInvertLight != result.iconInvertLight) { sc.iconInvertLight = result.iconInvertLight; changed = true; }
            if (sc.iconInvertDark != result.iconInvertDark) { sc.iconInvertDark = result.iconInvertDark; changed = true; }
        }
    }
    else
    {
        ShortcutDialog::InitParams init;
        init.name            = sc.name;
        init.targetPath      = sc.targetPath;
        init.arguments       = sc.arguments;
        init.iconPath        = sc.iconPath;
        init.runAsAdmin      = sc.runAsAdmin;
        init.iconInvertLight = sc.iconInvertLight;
        init.iconInvertDark  = sc.iconInvertDark;

        ShortcutDialogResult result;
        if (ShortcutDialog::Show(hWnd, L"编辑快捷方式", result, &init, m_owner->GetAppContext()))
        {
            if (sc.name != result.name) { sc.name = result.name; changed = true; }
            if (sc.arguments != result.arguments) { sc.arguments = result.arguments; changed = true; }
            if (sc.iconPath != result.iconPath) { sc.iconPath = result.iconPath; changed = true; }
            Model::IconSource newIconSource = result.iconPath.empty() ? Model::IconSource::Auto : Model::IconSource::CustomPath;
            if (sc.iconSource != newIconSource) { sc.iconSource = newIconSource; changed = true; }
            if (sc.runAsAdmin != result.runAsAdmin) { sc.runAsAdmin = result.runAsAdmin; changed = true; }
            if (sc.targetPath != result.targetPath) { sc.targetPath = result.targetPath; changed = true; }
            Model::ShortcutTargetKind newTargetKind = ShortcutManager::InferTargetKind(result.targetPath);
            if (sc.targetKind != newTargetKind) { sc.targetKind = newTargetKind; changed = true; }
            if (sc.iconInvertLight != result.iconInvertLight) { sc.iconInvertLight = result.iconInvertLight; changed = true; }
            if (sc.iconInvertDark != result.iconInvertDark) { sc.iconInvertDark = result.iconInvertDark; changed = true; }
        }
    }

    if (changed)
    {
        HICON currentIcon = sc.hIcon;
        RendShortcutInfo changedSc = sc;
        sc = originalSc;
        sc.hIcon = currentIcon;
        m_owner->RecordShortcutHistoryCheckpoint();
        sc = changedSc;
        sc.hIcon = currentIcon;

        if (sc.hIcon) { DestroyIcon(sc.hIcon); sc.hIcon = nullptr; }
        sc.hIcon = ShortcutManager::GetShortcutIcon(sc);

        if (index < (int)m_pageData->iconBitmaps.size() && m_pageData->iconBitmaps[index])
        {
            m_pageData->iconBitmaps[index]->Release();
            m_pageData->iconBitmaps[index] = nullptr;
        }
        if (index < (int)m_pageData->iconBitmaps.size())
        {
            m_pageData->iconBitmaps[index] = CreateShortcutBitmap(sc);
        }

        m_owner->NotifyConfigChanged();
        repaint = true;
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
