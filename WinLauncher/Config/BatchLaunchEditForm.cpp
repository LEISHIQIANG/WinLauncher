#define NOMINMAX
#include "BatchLaunchEditForm.h"
#include "ConfirmWindow.h"
#include "UIStyle.h"
#include "../DpiHelper.h"
#include "../ShortcutManager.h"
#include <windowsx.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <algorithm>
#include <cstring>
#include <vector>
#include <cmath>

#pragma comment(lib, "comdlg32.lib")
#include "../UI/Controls/IconRenderer.h"

struct BatchBrushCacheEntry
{
    D2D1_COLOR_F color;
    ComPtr<ID2D1SolidColorBrush> brush;
};
static std::vector<BatchBrushCacheEntry> g_batchBrushCache;

static ComPtr<ID2D1SolidColorBrush> GetOrCreateBrush(ID2D1HwndRenderTarget* rt, const D2D1_COLOR_F& color)
{
    for (auto& entry : g_batchBrushCache)
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
            g_batchBrushCache.push_back({ color, brush });
    }
    return brush;
}

static const float Y_LBL_NAME        = 8.0f;
static const float Y_BOX_NAME        = 24.0f;
static const float Y_LBL_LISTS       = 56.0f;
static const float Y_BOX_LISTS       = 72.0f;
static const float H_LIST_BOX        = 200.0f;
static const float Y_LBL_ICON        = 276.0f;
static const float Y_BOX_ICON        = 292.0f;
static const float Y_PREVIEW         = 280.0f;
static const float Y_INVERT_LIGHT    = 326.0f;
static const float Y_INVERT_DARK     = 326.0f;

static const float QUEUE_STEP_H      = 46.0f;
static const float DEL_BTN_SIZE      = 18.0f;

BatchLaunchEditForm::BatchLaunchEditForm()
{
}

BatchLaunchEditForm::~BatchLaunchEditForm()
{
    Destroy();
}

bool BatchLaunchEditForm::Create(HWND parentHWND, IDWriteFactory* dwriteFactory, const D2D1_RECT_F& logicalBounds, const BatchLaunchEditFormInitParams& init, AppContext* ctx)
{
    m_ctx = ctx;
    m_parentHWND = parentHWND;
    m_bounds = logicalBounds;
    m_init = init;
    m_iconInvertLight = init.iconInvertLight;
    m_iconInvertDark = init.iconInvertDark;
    m_steps = BatchHelper::Parse(init.arguments);
    EnsureFonts(dwriteFactory);

    float W = m_bounds.right - m_bounds.left;
    float R = m_bounds.left + W - 20;

    UIStyle::TextBoxStyle style;
    style.fontSize    = 11;
    style.paddingTop  = 4.0f;
    style.paddingBottom = 4.0f;

    m_nameBox.SetStyle(style);
    m_nameBox.Create(parentHWND, dwriteFactory, D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_NAME, R, m_bounds.top + Y_BOX_NAME + 22), m_init.name);

    m_iconBox.SetStyle(style);
    m_iconBox.Create(parentHWND, dwriteFactory, D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_ICON, R - 101, m_bounds.top + Y_BOX_ICON + 22), m_init.iconPath);

    SyncQueueStates();
    m_hoveredRightIdx = -1;
    m_hoveredDeleteIdx = -1;

    m_focusedBox = &m_nameBox;
    m_focusedBox->SetFocus(true);
    return true;
}

void BatchLaunchEditForm::Destroy()
{
    m_nameBox.Destroy();
    m_iconBox.Destroy();
    if (m_previewIcon) { DestroyIcon(m_previewIcon); m_previewIcon = nullptr; }
    if (m_previewBitmap) { m_previewBitmap->Release(); m_previewBitmap = nullptr; }
    for (auto& item : m_availableItems)
    {
        if (item.bitmap) { item.bitmap->Release(); item.bitmap = nullptr; }
    }
    m_availableItems.clear();
    g_batchBrushCache.clear();
}

void BatchLaunchEditForm::PopulateAvailableShortcuts(ID2D1HwndRenderTarget* rt)
{
    if (!m_availableItems.empty()) return;

    m_availableItems.clear();
    std::wstring configDir = ShortcutManager::FindConfigDir();
    auto pages = ShortcutManager::LoadConfig(configDir);
    for (auto& page : pages)
    {
        for (auto& sc : page.shortcuts)
        {
            if (sc.type == Model::ShortcutType::Batch)
                continue;
            SimpleShortcutItem item;
            item.id = sc.id;
            item.name = sc.name;
            item.type = sc.type;
            item.hIcon = sc.hIcon;
            if (item.hIcon && rt)
            {
                auto bmp = IconRenderer::HicontoD2D(rt, item.hIcon, 24, false);
                if (bmp) item.bitmap = bmp.Detach();
            }
            m_availableItems.push_back(item);
        }
    }
}

void BatchLaunchEditForm::EnsureQueueStates()
{
    size_t count = m_steps.size();
    while (m_queueStates.size() < count)
    {
        QueueVisualState vs;
        vs.currentY = (float)(m_queueStates.size() * QUEUE_STEP_H);
        vs.targetY = vs.currentY;
        m_queueStates.push_back(vs);
    }
    while (m_queueStates.size() > count)
        m_queueStates.pop_back();
}

void BatchLaunchEditForm::SyncQueueStates()
{
    EnsureQueueStates();
    for (size_t i = 0; i < m_queueStates.size(); ++i)
        m_queueStates[i].targetY = (float)(i * QUEUE_STEP_H);
}

bool BatchLaunchEditForm::TickAnimation()
{
    if (!m_animating && m_dragIndex < 0) return false;

    const float dt = 0.016f;
    bool anyMoving = false;

    for (size_t i = 0; i < m_queueStates.size(); ++i)
    {
        float dy = m_queueStates[i].targetY - m_queueStates[i].currentY;
        if (std::abs(dy) > 0.1f)
        {
            m_queueStates[i].currentY += dy * (1.0f - std::exp(-15.0f * dt));
            anyMoving = true;
        }
        else
        {
            m_queueStates[i].currentY = m_queueStates[i].targetY;
        }
    }

    if (!anyMoving && m_dragIndex < 0) m_animating = false;
    return anyMoving || m_dragIndex >= 0;
}

void BatchLaunchEditForm::UpdateQueueDragAndSortState(POINT pt)
{
    float boxTop = m_bounds.top + Y_BOX_LISTS;
    float mouseY = (float)pt.y - boxTop + m_rightScrollY;
    float dragPos = mouseY - m_grabOffsetY;
    int count = (int)m_steps.size();

    // Step 1: Dragged item follows mouse
    m_queueStates[m_dragIndex].currentY = dragPos;
    m_queueStates[m_dragIndex].targetY = dragPos;

    // Step 2: Find closest slot
    float dragCenter = dragPos + QUEUE_STEP_H * 0.5f;
    int insertIndex = count - 1;
    float minDist = FLT_MAX;
    for (int j = 0; j < count; ++j)
    {
        if (j == m_dragIndex) continue;
        float slotCenter = (float)j * QUEUE_STEP_H + QUEUE_STEP_H * 0.5f;
        float d = std::abs(dragCenter - slotCenter);
        if (d < minDist) { minDist = d; insertIndex = j; }
    }
    float endY = (float)(count - 1) * QUEUE_STEP_H + QUEUE_STEP_H;
    if (std::abs(dragCenter - endY) < minDist)
        insertIndex = count;
    m_dragInsertIndex = insertIndex;

    // Step 3: Shift other items' targetY to make room
    for (int i = 0; i < count; ++i)
    {
        if (i == m_dragIndex) continue;
        int targetSlot = i;
        if (i < m_dragIndex)
        {
            if (i >= insertIndex) targetSlot = i + 1;
        }
        else if (i > m_dragIndex)
        {
            if (i <= insertIndex) targetSlot = i - 1;
        }
        m_queueStates[i].targetY = (float)(targetSlot * QUEUE_STEP_H);
    }

    // Auto-scroll when dragging near edges
    float dragViewY = dragPos - m_rightScrollY;
    float boxH = H_LIST_BOX;
    if (dragViewY < 20.0f && m_rightScrollY > 0.0f)
    {
        m_rightScrollY = (std::max)(0.0f, m_rightScrollY - 4.0f);
    }
    else if (dragViewY + QUEUE_STEP_H > boxH - 20.0f && m_rightScrollY < m_rightScrollMax)
    {
        m_rightScrollY = (std::min)(m_rightScrollMax, m_rightScrollY + 4.0f);
    }
}

void BatchLaunchEditForm::Paint(ID2D1HwndRenderTarget* rt, float scale)
{
    m_d2dFactoryCache = nullptr;
    rt->GetFactory(&m_d2dFactoryCache);
    PopulateAvailableShortcuts(rt);

    float W = m_bounds.right - m_bounds.left;
    float R = m_bounds.left + W - 20;
    float leftR  = m_bounds.left + 170;
    float rightL = m_bounds.left + 180;

    auto textBrush = GetOrCreateBrush(rt, UIStyle::ThemeColor::TextNormal().d2d);

    DrawSectionLabel(rt, L"名称", D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_LBL_NAME, R, m_bounds.top + Y_LBL_NAME + 15));
    DrawSectionLabel(rt, L"快捷方式", D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_LBL_LISTS, leftR, m_bounds.top + Y_LBL_LISTS + 15));
    DrawSectionLabel(rt, L"启动队列", D2D1::RectF(rightL, m_bounds.top + Y_LBL_LISTS, R, m_bounds.top + Y_LBL_LISTS + 15));
    DrawSectionLabel(rt, L"图标路径 (留空为默认)", D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_LBL_ICON, R - 101, m_bounds.top + Y_LBL_ICON + 15));

    m_nameBox.Paint(rt, scale);
    m_iconBox.Paint(rt, scale);

    auto borderBrush = GetOrCreateBrush(rt, UIStyle::ThemeColor::ButtonBorderNormal().d2d);
    auto bgBrush = GetOrCreateBrush(rt, UIStyle::ThemeColor::EditBg().d2d);

    // ──── 1. Available Shortcuts Grid ────
    D2D1_RECT_F leftBox = D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_LISTS, leftR, m_bounds.top + Y_BOX_LISTS + H_LIST_BOX);
    if (bgBrush) rt->FillRectangle(D2D1::RectF(leftBox.left * scale, leftBox.top * scale, leftBox.right * scale, leftBox.bottom * scale), bgBrush.Get());
    if (borderBrush) rt->DrawRectangle(D2D1::RectF(leftBox.left * scale, leftBox.top * scale, leftBox.right * scale, leftBox.bottom * scale), borderBrush.Get(), 1.0f);

    int cols = 3;
    float gridW = (leftBox.right - 8) - (leftBox.left + 4);
    float cellW = gridW / cols;
    float cellH = 48.0f;
    int rows = (int)ceil((float)m_availableItems.size() / cols);
    m_leftScrollMax = (float)(std::max)(0.0f, (rows * cellH) - H_LIST_BOX);

    rt->PushAxisAlignedClip(D2D1::RectF((leftBox.left + 1) * scale, (leftBox.top + 1) * scale, (leftBox.right - 6) * scale, (leftBox.bottom - 1) * scale), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    for (size_t i = 0; i < m_availableItems.size(); ++i)
    {
        int row = (int)i / cols;
        int col = (int)i % cols;
        float cx = leftBox.left + 4 + col * cellW;
        float cy = leftBox.top + row * cellH - m_leftScrollY;
        D2D1_RECT_F cellRect = D2D1::RectF(cx + 2, cy + 2, cx + cellW - 2, cy + cellH - 2);
        if (cellRect.bottom > leftBox.top && cellRect.top < leftBox.bottom)
        {
            if ((int)i == m_hoveredLeftIdx)
            {
                auto hoverBrush = GetOrCreateBrush(rt, D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.08f));
                if (hoverBrush) rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cellRect.left * scale, cellRect.top * scale, cellRect.right * scale, cellRect.bottom * scale), 4.0f, 4.0f), hoverBrush.Get());
            }
            float iconSize = 24.0f;
            float iconX = cx + (cellW - iconSize) * 0.5f;
            float iconY = cy + 3;
            if (m_availableItems[i].bitmap)
                rt->DrawBitmap(m_availableItems[i].bitmap, D2D1::RectF(iconX * scale, iconY * scale, (iconX + iconSize) * scale, (iconY + iconSize) * scale));
            if (textBrush && m_tfSmallCenter)
            {
                D2D1_RECT_F txtR = D2D1::RectF(cellRect.left * scale, (iconY + iconSize + 1) * scale, cellRect.right * scale, cellRect.bottom * scale);
                rt->DrawTextW(m_availableItems[i].name.c_str(), (UINT32)m_availableItems[i].name.size(), m_tfSmallCenter.Get(), txtR, textBrush.Get());
            }
        }
    }
    rt->PopAxisAlignedClip();
    DrawScrollbar(rt, leftBox, m_leftScrollY, m_leftScrollMax, scale);

    // ──── 2. Queue List (right) — cards with delete button ────
    D2D1_RECT_F rightBox = D2D1::RectF(rightL, m_bounds.top + Y_BOX_LISTS, R, m_bounds.top + Y_BOX_LISTS + H_LIST_BOX);
    if (bgBrush) rt->FillRectangle(D2D1::RectF(rightBox.left * scale, rightBox.top * scale, rightBox.right * scale, rightBox.bottom * scale), bgBrush.Get());
    if (borderBrush) rt->DrawRectangle(D2D1::RectF(rightBox.left * scale, rightBox.top * scale, rightBox.right * scale, rightBox.bottom * scale), borderBrush.Get(), 1.0f);

    float listH = H_LIST_BOX;
    m_rightScrollMax = (float)(std::max)(0.0f, (m_steps.size() * QUEUE_STEP_H) - listH);

    rt->PushAxisAlignedClip(D2D1::RectF((rightBox.left + 1) * scale, (rightBox.top + 1) * scale, (rightBox.right - 6) * scale, (rightBox.bottom - 1) * scale), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // Draw non-dragged items first
    for (size_t i = 0; i < m_steps.size(); ++i)
    {
        if ((int)i == m_dragIndex) continue;
        float cy = rightBox.top + m_queueStates[i].currentY - m_rightScrollY;
        D2D1_RECT_F itemRect = D2D1::RectF(rightBox.left + 4, cy + 2, rightBox.right - 8, cy + QUEUE_STEP_H - 2);
        if (itemRect.bottom > rightBox.top && itemRect.top < rightBox.bottom)
        {
            auto cardBg = GetOrCreateBrush(rt, UIStyle::ThemeColor::ButtonBgNormal().d2d);
            D2D1_ROUNDED_RECT cardRR = D2D1::RoundedRect(D2D1::RectF(itemRect.left * scale, itemRect.top * scale, itemRect.right * scale, itemRect.bottom * scale), 4.0f, 4.0f);
            if (cardBg) rt->FillRoundedRectangle(cardRR, cardBg.Get());
            float borderA = ((int)i == m_hoveredRightIdx) ? 0.12f : 0.06f;
            auto cardBorder = GetOrCreateBrush(rt, D2D1::ColorF(1.0f, 1.0f, 1.0f, borderA));
            if (cardBorder) rt->DrawRoundedRectangle(cardRR, cardBorder.Get(), 0.5f);

            std::wstring scName = L"丢失项目";
            ID2D1Bitmap* bmp = nullptr;
            for (const auto& item : m_availableItems)
            {
                if (item.id == m_steps[i].shortcutId) { scName = item.name; bmp = item.bitmap; break; }
            }
            float iconSize = 24.0f;
            float iconY = cy + (QUEUE_STEP_H - iconSize) * 0.5f;
            if (bmp)
                rt->DrawBitmap(bmp, D2D1::RectF((itemRect.left + 6) * scale, iconY * scale, (itemRect.left + 6 + iconSize) * scale, (iconY + iconSize) * scale));
            if (textBrush && m_tfNormal)
                rt->DrawTextW(scName.c_str(), (UINT32)scName.size(), m_tfNormal.Get(),
                              D2D1::RectF((itemRect.left + 6 + iconSize + 8) * scale, itemRect.top * scale, (itemRect.right - DEL_BTN_SIZE - 8) * scale, itemRect.bottom * scale), textBrush.Get());

            // Delete button (right side)
            float delX = itemRect.right - DEL_BTN_SIZE - 4;
            float delY = cy + (QUEUE_STEP_H - DEL_BTN_SIZE) * 0.5f;
            D2D1_RECT_F delBtnRect = D2D1::RectF(delX, delY, delX + DEL_BTN_SIZE, delY + DEL_BTN_SIZE);
            bool delHov = ((int)i == m_hoveredDeleteIdx);
            D2D1_ROUNDED_RECT delRR = D2D1::RoundedRect(D2D1::RectF(delBtnRect.left * scale, delBtnRect.top * scale, delBtnRect.right * scale, delBtnRect.bottom * scale), 3.0f, 3.0f);
            if (delHov)
            {
                auto delBg = GetOrCreateBrush(rt, D2D1::ColorF(1.0f, 0.3f, 0.3f, 0.5f));
                if (delBg) rt->FillRoundedRectangle(delRR, delBg.Get());
            }
            else
            {
                auto delBg = GetOrCreateBrush(rt, D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.08f));
                if (delBg) rt->FillRoundedRectangle(delRR, delBg.Get());
            }
            auto delBorder = GetOrCreateBrush(rt, D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.15f));
            if (delBorder) rt->DrawRoundedRectangle(delRR, delBorder.Get(), 0.5f);
            if (m_tfBtn)
            {
                auto delText = GetOrCreateBrush(rt, UIStyle::ThemeColor::TextNormal().d2d);
                if (delText) rt->DrawTextW(L"－", 1, m_tfBtn.Get(), D2D1::RectF(delBtnRect.left * scale, delBtnRect.top * scale, delBtnRect.right * scale, delBtnRect.bottom * scale), delText.Get());
            }
        }
    }

    // Draw dragged item on top
    if (m_dragIndex >= 0 && m_dragIndex < (int)m_steps.size())
    {
        float cy = rightBox.top + m_queueStates[m_dragIndex].currentY - m_rightScrollY;
        D2D1_RECT_F itemRect = D2D1::RectF(rightBox.left + 4, cy + 2, rightBox.right - 8, cy + QUEUE_STEP_H - 2);
        if (itemRect.bottom > rightBox.top && itemRect.top < rightBox.bottom)
        {
            auto cardBg = GetOrCreateBrush(rt, D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.12f));
            D2D1_ROUNDED_RECT cardRR = D2D1::RoundedRect(D2D1::RectF(itemRect.left * scale, itemRect.top * scale, itemRect.right * scale, itemRect.bottom * scale), 4.0f, 4.0f);
            if (cardBg) rt->FillRoundedRectangle(cardRR, cardBg.Get());
            auto cardBorder = GetOrCreateBrush(rt, D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.25f));
            if (cardBorder) rt->DrawRoundedRectangle(cardRR, cardBorder.Get(), 1.0f);

            std::wstring scName = L"丢失项目";
            ID2D1Bitmap* bmp = nullptr;
            for (const auto& item : m_availableItems)
            {
                if (item.id == m_steps[m_dragIndex].shortcutId) { scName = item.name; bmp = item.bitmap; break; }
            }
            float iconSize = 24.0f;
            float iconY = cy + (QUEUE_STEP_H - iconSize) * 0.5f;
            if (bmp)
                rt->DrawBitmap(bmp, D2D1::RectF((itemRect.left + 6) * scale, iconY * scale, (itemRect.left + 6 + iconSize) * scale, (iconY + iconSize) * scale));
            if (textBrush && m_tfNormal)
                rt->DrawTextW(scName.c_str(), (UINT32)scName.size(), m_tfNormal.Get(),
                              D2D1::RectF((itemRect.left + 6 + iconSize + 8) * scale, itemRect.top * scale, (itemRect.right - DEL_BTN_SIZE - 8) * scale, itemRect.bottom * scale), textBrush.Get());
        }
    }

    rt->PopAxisAlignedClip();
    DrawScrollbar(rt, rightBox, m_rightScrollY, m_rightScrollMax, scale);

    // Browse button
    DrawButton(rt, L"浏览...", D2D1::RectF(R - 96, m_bounds.top + Y_BOX_ICON, R - 41, m_bounds.top + Y_BOX_ICON + 22), m_hoveredBrowseIcon);
    DrawIconPreview(rt);

    DrawCheckbox(rt, D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_INVERT_LIGHT, m_bounds.left + 130, m_bounds.top + Y_INVERT_LIGHT + 22), m_iconInvertLight, m_hoveredInvertLight, L"浅色主题反色");
    DrawCheckbox(rt, D2D1::RectF(m_bounds.left + 145, m_bounds.top + Y_INVERT_DARK, m_bounds.left + 260, m_bounds.top + Y_INVERT_DARK + 22), m_iconInvertDark, m_hoveredInvertDark, L"深色主题反色");
}

void BatchLaunchEditForm::UpdateLayout(const D2D1_RECT_F& logicalBounds, float scale)
{
    m_bounds = logicalBounds;
    float W = m_bounds.right - m_bounds.left;
    float R = m_bounds.left + W - 20;
    m_nameBox.SetBounds(D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_NAME, R, m_bounds.top + Y_BOX_NAME + 22));
    m_iconBox.SetBounds(D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_ICON, R - 101, m_bounds.top + Y_BOX_ICON + 22));
    m_nameBox.UpdateLayout(scale);
    m_iconBox.UpdateLayout(scale);
}

void BatchLaunchEditForm::OnMouseMove(HWND hWnd, POINT pt, float scale, bool& repaint)
{
    POINT logicalPt{ (long)(pt.x / scale), (long)(pt.y / scale) };
    float W = m_bounds.right - m_bounds.left;
    float R = m_bounds.left + W - 20;
    float leftR  = m_bounds.left + 170;
    float rightL = m_bounds.left + 180;

    // If dragging, update drag state
    if (m_dragIndex >= 0)
    {
        UpdateQueueDragAndSortState(logicalPt);
        repaint = true;
        return;
    }

    bool b = HitTestBrowseIconButton(logicalPt);
    if (b != m_hoveredBrowseIcon) { m_hoveredBrowseIcon = b; repaint = true; }

    bool il = HitTestInvertLightCheckbox(logicalPt);
    if (il != m_hoveredInvertLight) { m_hoveredInvertLight = il; repaint = true; }

    bool id = HitTestInvertDarkCheckbox(logicalPt);
    if (id != m_hoveredInvertDark) { m_hoveredInvertDark = id; repaint = true; }

    m_nameBox.OnMouseMove(hWnd, pt, scale, repaint);
    m_iconBox.OnMouseMove(hWnd, pt, scale, repaint);

    // Available grid hit test
    D2D1_RECT_F leftBox = D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_LISTS, leftR, m_bounds.top + Y_BOX_LISTS + H_LIST_BOX);
    if (HitTestRect(logicalPt, leftBox))
    {
        float gridW = (leftBox.right - 8) - (leftBox.left + 4);
        int col = (int)((logicalPt.x - leftBox.left - 4) / (gridW / 3));
        int row = (int)((logicalPt.y - leftBox.top + m_leftScrollY) / 48.0f);
        int idx = row * 3 + col;
        if (col >= 0 && row >= 0 && idx >= 0 && idx < (int)m_availableItems.size())
        {
            if (idx != m_hoveredLeftIdx) { m_hoveredLeftIdx = idx; repaint = true; }
        }
        else if (m_hoveredLeftIdx != -1) { m_hoveredLeftIdx = -1; repaint = true; }
    }
    else if (m_hoveredLeftIdx != -1) { m_hoveredLeftIdx = -1; repaint = true; }

    // Queue list hit test
    D2D1_RECT_F rightBox = D2D1::RectF(rightL, m_bounds.top + Y_BOX_LISTS, R, m_bounds.top + Y_BOX_LISTS + H_LIST_BOX);
    int newHover = -1;
    int newDel = -1;
    if (HitTestRect(logicalPt, rightBox))
    {
        float cy = rightBox.top - m_rightScrollY;
        for (size_t i = 0; i < m_queueStates.size(); ++i)
        {
            float itemY = rightBox.top + m_queueStates[i].currentY - m_rightScrollY;
            D2D1_RECT_F itemRect = D2D1::RectF(rightBox.left + 4, itemY + 2, rightBox.right - 8, itemY + QUEUE_STEP_H - 2);
            if (HitTestRect(logicalPt, itemRect))
            {
                newHover = (int)i;
                // Check delete button
                float delX = itemRect.right - DEL_BTN_SIZE - 4;
                float delY = itemY + (QUEUE_STEP_H - DEL_BTN_SIZE) * 0.5f;
                if (HitTestRect(logicalPt, D2D1::RectF(delX, delY, delX + DEL_BTN_SIZE, delY + DEL_BTN_SIZE)))
                    newDel = (int)i;
                break;
            }
        }
    }
    if (newHover != m_hoveredRightIdx) { m_hoveredRightIdx = newHover; repaint = true; }
    if (newDel != m_hoveredDeleteIdx) { m_hoveredDeleteIdx = newDel; repaint = true; }
}

void BatchLaunchEditForm::OnLButtonDown(HWND hWnd, POINT pt, float scale, bool& repaint)
{
    POINT logicalPt{ (long)(pt.x / scale), (long)(pt.y / scale) };
    float W = m_bounds.right - m_bounds.left;
    float R = m_bounds.left + W - 20;
    float rightL = m_bounds.left + 180;
    TextBox* clickedBox = nullptr;

    // Focus handling
    if (m_nameBox.HitTest(logicalPt))
    {
        clickedBox = &m_nameBox;
        if (m_focusedBox != &m_nameBox)
        {
            if (m_focusedBox) m_focusedBox->SetFocus(false);
            m_focusedBox = &m_nameBox;
            m_focusedBox->SetFocus(true);
            repaint = true;
        }
    }
    else if (m_iconBox.HitTest(logicalPt))
    {
        clickedBox = &m_iconBox;
        if (m_focusedBox != &m_iconBox)
        {
            if (m_focusedBox) m_focusedBox->SetFocus(false);
            m_focusedBox = &m_iconBox;
            m_focusedBox->SetFocus(true);
            repaint = true;
        }
    }
    else
    {
        if (m_focusedBox)
        {
            m_focusedBox->SetFocus(false);
            m_focusedBox = nullptr;
            repaint = true;
        }
    }

    if (HitTestBrowseIconButton(logicalPt))
    {
        BrowseIconFile(hWnd);
        repaint = true;
    }
    else if (HitTestInvertLightCheckbox(logicalPt))
    {
        m_iconInvertLight = !m_iconInvertLight;
        repaint = true;
    }
    else if (HitTestInvertDarkCheckbox(logicalPt))
    {
        m_iconInvertDark = !m_iconInvertDark;
        repaint = true;
    }

    // Queue interactions
    D2D1_RECT_F rightBox = D2D1::RectF(rightL, m_bounds.top + Y_BOX_LISTS, R, m_bounds.top + Y_BOX_LISTS + H_LIST_BOX);
    if (HitTestRect(logicalPt, rightBox))
    {
        // Check delete button first
        if (m_hoveredDeleteIdx >= 0 && m_hoveredDeleteIdx < (int)m_steps.size())
        {
            m_steps.erase(m_steps.begin() + m_hoveredDeleteIdx);
            SyncQueueStates();
            m_dragIndex = -1;
            m_hoveredDeleteIdx = -1;
            m_hoveredRightIdx = -1;
            m_animating = true;
            repaint = true;
        }
        // Start drag
        else if (m_hoveredRightIdx >= 0 && m_hoveredRightIdx < (int)m_steps.size() && m_steps.size() > 1)
        {
            m_dragIndex = m_hoveredRightIdx;
            m_dragInsertIndex = m_hoveredRightIdx;
            float boxTop = m_bounds.top + Y_BOX_LISTS;
            float itemTop = boxTop + m_queueStates[m_dragIndex].currentY - m_rightScrollY;
            m_grabOffsetY = (float)logicalPt.y - itemTop;
            SetCapture(hWnd);
            m_animating = true;
            repaint = true;
        }
    }

    if (clickedBox)
    {
        clickedBox->OnLButtonDown(hWnd, pt, scale, repaint);
    }
}

void BatchLaunchEditForm::OnLButtonDblClk(HWND hWnd, POINT pt, float scale, bool& repaint)
{
    POINT logicalPt{ (long)(pt.x / scale), (long)(pt.y / scale) };
    float leftR = m_bounds.left + 170;

    D2D1_RECT_F leftBox = D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_LISTS, leftR, m_bounds.top + Y_BOX_LISTS + H_LIST_BOX);
    if (HitTestRect(logicalPt, leftBox) && m_hoveredLeftIdx >= 0 && m_hoveredLeftIdx < (int)m_availableItems.size())
    {
        BatchStep step;
        step.shortcutId = m_availableItems[m_hoveredLeftIdx].id;
        step.delayMs = 0;
        step.stopOnError = true;
        step.enabled = true;
        m_steps.push_back(step);
        SyncQueueStates();
        m_animating = true;
        repaint = true;
    }

    m_nameBox.OnLButtonDblClk(hWnd, pt, scale, repaint);
    m_iconBox.OnLButtonDblClk(hWnd, pt, scale, repaint);
}

void BatchLaunchEditForm::OnLButtonUp(HWND hWnd, POINT pt, float scale, bool& repaint)
{
    if (m_dragIndex >= 0)
    {
        ReleaseCapture();
        int insertIndex = m_dragInsertIndex;
        if (insertIndex >= 0 && insertIndex <= (int)m_steps.size() && insertIndex != m_dragIndex)
        {
            BatchStep dragged = m_steps[m_dragIndex];
            m_steps.erase(m_steps.begin() + m_dragIndex);

            QueueVisualState draggedState = m_queueStates[m_dragIndex];
            m_queueStates.erase(m_queueStates.begin() + m_dragIndex);

            m_steps.insert(m_steps.begin() + insertIndex, dragged);
            m_queueStates.insert(m_queueStates.begin() + insertIndex, draggedState);

            SyncQueueStates();
        }
        else
        {
            SyncQueueStates();
        }
        m_dragIndex = -1;
        m_dragInsertIndex = -1;
        m_animating = true;
        repaint = true;
    }

    m_nameBox.OnLButtonUp(hWnd, pt, scale, repaint);
    m_iconBox.OnLButtonUp(hWnd, pt, scale, repaint);
}

void BatchLaunchEditForm::OnChar(HWND hWnd, WPARAM wParam, bool& repaint)
{
    if (m_focusedBox)
        m_focusedBox->OnChar(hWnd, wParam, repaint);
}

void BatchLaunchEditForm::OnKeyDown(HWND hWnd, WPARAM wParam, LPARAM lParam, bool& repaint)
{
    if (m_focusedBox)
        m_focusedBox->OnKeyDown(hWnd, wParam, lParam, repaint);
}

void BatchLaunchEditForm::OnMouseWheel(HWND hWnd, short zDelta, POINT pt, float scale, bool& repaint)
{
    if (m_dragIndex >= 0) return;

    POINT logicalPt{ (long)(pt.x / scale), (long)(pt.y / scale) };
    float W = m_bounds.right - m_bounds.left;
    float R = m_bounds.left + W - 20;
    float leftR  = m_bounds.left + 170;
    float rightL = m_bounds.left + 180;

    D2D1_RECT_F leftBox = D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_LISTS, leftR, m_bounds.top + Y_BOX_LISTS + H_LIST_BOX);
    if (HitTestRect(logicalPt, leftBox))
    {
        m_leftScrollY -= (zDelta / 120.0f) * 48.0f;
        if (m_leftScrollY < 0.0f) m_leftScrollY = 0.0f;
        if (m_leftScrollY > m_leftScrollMax) m_leftScrollY = m_leftScrollMax;
        repaint = true;
        OnMouseMove(hWnd, pt, scale, repaint);
    }

    D2D1_RECT_F rightBox = D2D1::RectF(rightL, m_bounds.top + Y_BOX_LISTS, R, m_bounds.top + Y_BOX_LISTS + H_LIST_BOX);
    if (HitTestRect(logicalPt, rightBox))
    {
        m_rightScrollY -= (zDelta / 120.0f) * QUEUE_STEP_H;
        if (m_rightScrollY < 0.0f) m_rightScrollY = 0.0f;
        if (m_rightScrollY > m_rightScrollMax) m_rightScrollY = m_rightScrollMax;
        repaint = true;
        OnMouseMove(hWnd, pt, scale, repaint);
    }
}

void BatchLaunchEditForm::BlinkCaret()
{
    if (m_focusedBox) m_focusedBox->BlinkCaret();
}

bool BatchLaunchEditForm::HandleImeMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, bool& repaint)
{
    if (m_focusedBox)
        return m_focusedBox->HandleImeMessage(hWnd, uMsg, wParam, lParam, repaint);
    return false;
}

BatchLaunchEditFormResult BatchLaunchEditForm::GetResult() const
{
    BatchLaunchEditFormResult res;
    res.name = m_nameBox.GetText();
    res.iconPath = m_iconBox.GetText();
    res.iconInvertLight = m_iconInvertLight;
    res.iconInvertDark = m_iconInvertDark;
    res.arguments = BatchHelper::Serialize(m_steps);
    return res;
}

bool BatchLaunchEditForm::Validate(HWND hWnd)
{
    std::wstring name = m_nameBox.GetText();
    if (name.empty())
    {
        ConfirmWindow::Show(hWnd, L"验证失败", L"批量启动名称不能为空！", m_ctx, false);
        return false;
    }
    if (m_steps.empty())
    {
        ConfirmWindow::Show(hWnd, L"验证失败", L"配置队列不能为空，请在左侧双击快捷方式添加项目！", m_ctx, false);
        return false;
    }
    return true;
}

void BatchLaunchEditForm::EnsureFonts(IDWriteFactory* dwriteFactory)
{
    if (!m_tfLabel)
    {
        UIStyle::Typography::CreateTextFormat(dwriteFactory, &m_tfLabel, 10.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        UIStyle::Typography::CreateTextFormat(dwriteFactory, &m_tfBtn, 10.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        UIStyle::Typography::CreateTextFormat(dwriteFactory, &m_tfSmall, 9.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        UIStyle::Typography::CreateTextFormat(dwriteFactory, &m_tfSmallCenter, 9.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        UIStyle::Typography::CreateTextFormat(dwriteFactory, &m_tfNormal, 10.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
}

void BatchLaunchEditForm::BrowseIconFile(HWND hWnd)
{
    wchar_t filename[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFilter = L"Icons/Images\0*.ico;*.png;*.jpg;*.jpeg;*.bmp;*.dll;*.exe\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn))
        m_iconBox.SetText(filename);
}

void BatchLaunchEditForm::DrawScrollbar(ID2D1HwndRenderTarget* rt, const D2D1_RECT_F& rect, float scrollOffset, float scrollMax, float scale)
{
    if (scrollMax <= 0.0f) return;
    float barW = 6.0f;
    float barH = rect.bottom - rect.top;
    float visibleRatio = barH / (barH + scrollMax);
    float thumbH = (std::max)(20.0f, barH * visibleRatio);
    float thumbY = rect.top + (scrollOffset / scrollMax) * (barH - thumbH);
    D2D1_RECT_F thumbRect = D2D1::RectF((rect.right - barW - 2) * scale, thumbY * scale, (rect.right - 2) * scale, (thumbY + thumbH) * scale);
    auto thumbBrush = GetOrCreateBrush(rt, D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.2f));
    if (thumbBrush) rt->FillRoundedRectangle(D2D1::RoundedRect(thumbRect, 3.0f, 3.0f), thumbBrush.Get());
}

bool BatchLaunchEditForm::HitTestRect(POINT pt, const D2D1_RECT_F& rect)
{
    return (pt.x >= rect.left && pt.x <= rect.right && pt.y >= rect.top && pt.y <= rect.bottom);
}

bool BatchLaunchEditForm::HitTestBrowseIconButton(POINT pt)
{
    float W = m_bounds.right - m_bounds.left;
    float R = m_bounds.left + W - 20;
    return HitTestRect(pt, D2D1::RectF(R - 96, m_bounds.top + Y_BOX_ICON, R - 41, m_bounds.top + Y_BOX_ICON + 22));
}

bool BatchLaunchEditForm::HitTestInvertLightCheckbox(POINT pt)
{
    return HitTestRect(pt, D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_INVERT_LIGHT, m_bounds.left + 130, m_bounds.top + Y_INVERT_LIGHT + 22));
}

bool BatchLaunchEditForm::HitTestInvertDarkCheckbox(POINT pt)
{
    return HitTestRect(pt, D2D1::RectF(m_bounds.left + 145, m_bounds.top + Y_INVERT_DARK, m_bounds.left + 260, m_bounds.top + Y_INVERT_DARK + 22));
}

void BatchLaunchEditForm::DrawSectionLabel(ID2D1HwndRenderTarget* rt, const wchar_t* text, const D2D1_RECT_F& rect)
{
    if (!m_tfSmall) return;
    float scale = DpiHelper::GetWindowScale(m_parentHWND);
    D2D1_COLOR_F sepClr = UIStyle::ThemeColor::TextNormal().d2d;
    sepClr.a = 0.28f;
    auto sepBrush = GetOrCreateBrush(rt, sepClr);
    if (sepBrush)
    {
        float midY = (rect.top + rect.bottom) * 0.5f;
        float textW = (float)wcslen(text) * 8.5f;
        float lineStart = rect.left + textW + 10.0f;
        lineStart = (std::min)(lineStart, rect.right);
        rt->DrawLine(D2D1::Point2F(lineStart * scale, midY * scale),
                     D2D1::Point2F(rect.right * scale, midY * scale), sepBrush.Get(), 0.35f);
    }
    D2D1_COLOR_F labelClr = UIStyle::ThemeColor::TextNormal().d2d;
    labelClr.a = 0.5f;
    auto labelBrush = GetOrCreateBrush(rt, labelClr);
    if (labelBrush)
        rt->DrawTextW(text, (UINT32)wcslen(text), m_tfSmall.Get(),
                      D2D1::RectF(rect.left * scale, rect.top * scale, rect.right * scale, rect.bottom * scale),
                      labelBrush.Get());
}

void BatchLaunchEditForm::DrawButton(ID2D1HwndRenderTarget* rt, const wchar_t* text, const D2D1_RECT_F& rect, bool hovered, bool disabled)
{
    float scale = DpiHelper::GetWindowScale(m_parentHWND);
    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(D2D1::RectF(rect.left * scale, rect.top * scale, rect.right * scale, rect.bottom * scale), 4.0f, 4.0f);
    bool isLight = (UIStyle::GetThemeMode() == UIStyle::ThemeMode::Light);
    D2D1_COLOR_F base = isLight ? D2D1::ColorF(0.f, 0.f, 0.f) : D2D1::ColorF(1.f, 1.f, 1.f);
    float bgA = disabled ? 0.02f : (hovered ? 0.09f : 0.04f);
    float borderA = disabled ? 0.04f : (hovered ? 0.16f : 0.075f);
    auto bgBrush = GetOrCreateBrush(rt, D2D1::ColorF(base.r, base.g, base.b, bgA));
    if (bgBrush) rt->FillRoundedRectangle(rr, bgBrush.Get());
    auto borderBrush = GetOrCreateBrush(rt, D2D1::ColorF(base.r, base.g, base.b, borderA));
    if (borderBrush) rt->DrawRoundedRectangle(rr, borderBrush.Get(), UIStyle::Metrics::ControlStroke());
    if (m_tfBtn)
    {
        auto textBrush = GetOrCreateBrush(rt, disabled ? UIStyle::ThemeColor::TextMuted().d2d : UIStyle::ThemeColor::TextNormal().d2d);
        if (textBrush) rt->DrawTextW(text, (UINT32)wcslen(text), m_tfBtn.Get(), D2D1::RectF(rect.left * scale, rect.top * scale, rect.right * scale, rect.bottom * scale), textBrush.Get());
    }
}

void BatchLaunchEditForm::DrawCheckbox(ID2D1HwndRenderTarget* rt, const D2D1_RECT_F& rect, bool checked, bool hovered, const wchar_t* labelText)
{
    float scale = DpiHelper::GetWindowScale(m_parentHWND);
    const float cbSize = 14.0f;
    D2D1_RECT_F boxRect = D2D1::RectF(
        rect.left,
        rect.top + (rect.bottom - rect.top - cbSize) * 0.5f,
        rect.left + cbSize,
        rect.top + (rect.bottom - rect.top - cbSize) * 0.5f + cbSize
    );
    D2D1_ROUNDED_RECT rrBox = D2D1::RoundedRect(D2D1::RectF(boxRect.left * scale, boxRect.top * scale, boxRect.right * scale, boxRect.bottom * scale), 3.0f, 3.0f);
    bool isLight = (UIStyle::GetThemeMode() == UIStyle::ThemeMode::Light);
    D2D1_COLOR_F base = isLight ? D2D1::ColorF(0.f, 0.f, 0.f) : D2D1::ColorF(1.f, 1.f, 1.f);

    if (checked)
    {
        D2D1_COLOR_F bg = UIStyle::ThemeColor::Accent().d2d;
        bg.a = hovered ? 0.85f : 0.70f;
        auto bgBrush = GetOrCreateBrush(rt, bg);
        if (bgBrush) rt->FillRoundedRectangle(rrBox, bgBrush.Get());
        auto ckBrush = GetOrCreateBrush(rt, UIStyle::ThemeColor::TextOnAccent().d2d);
        if (ckBrush)
        {
            float cx = (boxRect.left + cbSize * 0.5f) * scale;
            float cy = (boxRect.top + cbSize * 0.5f) * scale;
            rt->DrawLine(D2D1::Point2F(cx - 4, cy), D2D1::Point2F(cx - 1, cy + 3.5f), ckBrush.Get(), 1.5f);
            rt->DrawLine(D2D1::Point2F(cx - 1, cy + 3.5f), D2D1::Point2F(cx + 4, cy - 3), ckBrush.Get(), 1.5f);
        }
    }
    else
    {
        float bgA = hovered ? 0.06f : 0.03f;
        float borderA = hovered ? 0.20f : 0.12f;
        auto bgBrush = GetOrCreateBrush(rt, D2D1::ColorF(base.r, base.g, base.b, bgA));
        if (bgBrush) rt->FillRoundedRectangle(rrBox, bgBrush.Get());
        auto borderBrush = GetOrCreateBrush(rt, D2D1::ColorF(base.r, base.g, base.b, borderA));
        if (borderBrush) rt->DrawRoundedRectangle(rrBox, borderBrush.Get(), UIStyle::Metrics::ControlStroke());
    }

    if (labelText && m_tfLabel)
    {
        D2D1_RECT_F labelRect = D2D1::RectF((boxRect.right + 6) * scale, rect.top * scale, rect.right * scale, rect.bottom * scale);
        D2D1_COLOR_F tc = UIStyle::ThemeColor::TextNormal().d2d;
        tc.a = hovered ? 1.0f : 0.85f;
        auto tb = GetOrCreateBrush(rt, tc);
        if (tb) rt->DrawTextW(labelText, (UINT32)wcslen(labelText), m_tfLabel.Get(), labelRect, tb.Get());
    }
}

HICON BatchLaunchEditForm::GetFileIconForPreview(const std::wstring& path)
{
    if (path.empty()) return nullptr;
    SHFILEINFOW sfi{};
    DWORD_PTR hr = SHGetFileInfoW(path.c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON);
    if (hr) return sfi.hIcon;
    return nullptr;
}

void BatchLaunchEditForm::DrawIconPreview(ID2D1HwndRenderTarget* rt)
{
    float W = m_bounds.right - m_bounds.left;
    const float previewSize = 36.0f;
    float scale = DpiHelper::GetWindowScale(m_parentHWND);
    D2D1_RECT_F previewRect = D2D1::RectF(
        (m_bounds.left + W - 20 - previewSize) * scale,
        (m_bounds.top + Y_PREVIEW) * scale,
        (m_bounds.left + W - 20) * scale,
        (m_bounds.top + Y_PREVIEW + previewSize) * scale
    );

    std::wstring iconPath = m_iconBox.GetText();
    bool isLight = (UIStyle::GetThemeMode() == UIStyle::ThemeMode::Light);
    bool invert = isLight ? m_iconInvertLight : m_iconInvertDark;

    if (!iconPath.empty())
    {
        if (iconPath != m_lastPreviewPath)
        {
            m_lastPreviewPath = iconPath;
            if (m_previewIcon) { DestroyIcon(m_previewIcon); m_previewIcon = nullptr; }
            if (m_previewBitmap) { m_previewBitmap->Release(); m_previewBitmap = nullptr; }
            m_previewIcon = GetFileIconForPreview(iconPath);
        }

        if (m_previewIcon && !m_previewBitmap)
        {
            auto bmp = IconRenderer::HicontoD2D(rt, m_previewIcon, (int)previewSize, invert);
            if (bmp) m_previewBitmap = bmp.Detach();
        }
    }
    else
    {
        if (m_previewIcon) { DestroyIcon(m_previewIcon); m_previewIcon = nullptr; }
        std::wstring name = m_nameBox.GetText();
        if (!m_previewBitmap || m_lastPreviewText != name)
        {
            if (m_previewBitmap) { m_previewBitmap->Release(); m_previewBitmap = nullptr; }
            auto bmp = IconRenderer::CreateDefaultIcon(rt, nullptr, name, (int)previewSize);
            if (bmp)
            {
                m_previewBitmap = bmp.Detach();
                m_lastPreviewText = name;
            }
        }
    }

    D2D1_ROUNDED_RECT rrPreview = D2D1::RoundedRect(previewRect, 6.0f, 6.0f);
    D2D1_COLOR_F bgClr = isLight ? D2D1::ColorF(0.f, 0.f, 0.f, 0.05f) : D2D1::ColorF(1.f, 1.f, 1.f, 0.07f);
    auto bgBrush = GetOrCreateBrush(rt, bgClr);
    if (bgBrush) rt->FillRoundedRectangle(rrPreview, bgBrush.Get());
    D2D1_COLOR_F borderClr = isLight ? D2D1::ColorF(0.f, 0.f, 0.f, 0.10f) : D2D1::ColorF(1.f, 1.f, 1.f, 0.10f);
    auto borderBrush = GetOrCreateBrush(rt, borderClr);
    if (borderBrush) rt->DrawRoundedRectangle(rrPreview, borderBrush.Get(), UIStyle::Metrics::ControlStroke());
    if (m_previewBitmap)
    {
        D2D1_RECT_F alignedRect = IconRenderer::AlignToPixels(rt, previewRect.left, previewRect.top, previewSize, previewSize);
        rt->DrawBitmap(m_previewBitmap, alignedRect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }
}
