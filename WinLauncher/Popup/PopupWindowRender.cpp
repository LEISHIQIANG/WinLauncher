// Paint presentation for the popup window: text formats and every Draw*
// pass (top bar, search results, shortcut icons, pages, paint entry, dock),
// split from PopupWindow.cpp as one responsibility in one file. All
// functions are PopupWindow members moved verbatim; signatures and the
// class definition are unchanged.

#define NOMINMAX
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "../PopupWindow.h"
#include "PopupRenderHelper.h"
#include "PopupClock.h"
#include "../Config/UIStyle.h"
#include "../UI/Controls/IconRenderer.h"
#include <shellapi.h>
#include <cmath>

void PopupWindow::UpdateTextFormat()
{
    if (!m_dw) return;

    m_popupTextFormat.Reset();
    m_searchTextFormat.Reset();
    m_tabTextFormat.Reset();

    float fontSize = GetFontSize();
    float searchFontSize = GetSearchFontSize();

    UIStyle::Typography::CreateTextFormat(
        m_dw.Get(),
        &m_popupTextFormat,
        fontSize,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_TEXT_ALIGNMENT_CENTER,
        DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    UIStyle::Typography::CreateTextFormat(
        m_dw.Get(),
        &m_searchTextFormat,
        searchFontSize,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_TEXT_ALIGNMENT_LEADING,
        DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    UIStyle::Typography::CreateTextFormat(
        m_dw.Get(),
        &m_tabTextFormat,
        GetHeaderLayout().textSize,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_TEXT_ALIGNMENT_CENTER,
        DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}
void PopupWindow::DrawTopBar(ID2D1HwndRenderTarget* rt)
{
    int wndPad = GetWndPadding();
    HeaderLayout header = GetHeaderLayout();
    RECT cr; GetClientRect(GetHWND(), &cr);
    float scale = GetWindowScale(GetHWND());
    float w = (float)cr.right / scale;
    
    D2D1_RECT_F topRect = D2D1::RectF(
        (float)wndPad,
        (float)wndPad,
        w - wndPad,
        (float)wndPad + header.controlHeight
    );

    if (m_searchActive)
    {
        // Draw textbox background, border, selection, text, and caret
        m_searchTextBox.Paint(rt, scale);

        // Keep the search glyph in proportion with the configured header.
        float cx = topRect.left + header.searchTextInset * 0.55f;
        float cy = topRect.top + header.controlHeight * 0.5f;
        float iconRadius = std::max(3.0f, header.controlHeight * 0.18f);
        auto iconBrush = GetOrCreateBrush(UIStyle::ThemeColor::TextMuted().d2d);
        if (iconBrush)
        {
            rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), iconRadius, iconRadius), iconBrush.Get(), UIStyle::Metrics::IconStroke());
            rt->DrawLine(D2D1::Point2F(cx + iconRadius * 0.7f, cy + iconRadius * 0.7f),
                D2D1::Point2F(cx + iconRadius * 1.6f, cy + iconRadius * 1.6f), iconBrush.Get(), UIStyle::Metrics::IconStroke());
        }

        // 3. Draw placeholder if empty
        if (m_searchTextBox.IsEmpty())
        {
            D2D1_COLOR_F placeholderColor = UIStyle::ThemeColor::TextNormal().d2d;
            placeholderColor.a = 0.4f;
            auto placeholderBrush = GetOrCreateBrush(placeholderColor);
            if (placeholderBrush && m_searchTextFormat)
            {
                rt->DrawTextW(L"搜索...", 5, m_searchTextFormat.Get(),
                    D2D1::RectF(topRect.left + header.searchTextInset, topRect.top, topRect.right - 8.0f, topRect.bottom),
                    placeholderBrush.Get());
            }
        }
    }
    else
    {
        // Draw tabs
        int numPages = (int)m_pages.size();
        if (numPages > 0)
        {
            float totalWidth = topRect.right - topRect.left;
            float tabWidth = totalWidth / numPages;
            for (int i = 0; i < numPages; i++)
            {
                D2D1_RECT_F tabRect = D2D1::RectF(
                    topRect.left + i * tabWidth,
                    topRect.top,
                    topRect.left + (i + 1) * tabWidth,
                    topRect.bottom
                );

                if (i == m_hoveredTab)
                {
                    D2D1_ROUNDED_RECT roundedTab = D2D1::RoundedRect(tabRect, header.tabHoverRadius, header.tabHoverRadius);
                    auto hoverBg = GetOrCreateBrush(UIStyle::ThemeColor::ButtonBgHover().d2d);
                    if (hoverBg) rt->FillRoundedRectangle(roundedTab, hoverBg.Get());
                }

                // Dynamic opacity transition based on m_scrollPosition distance
                float dist = std::abs(std::remainder((float)i - m_scrollPosition, static_cast<float>(m_pages.size())));
                if (numPages > 1)
                {
                    float halfN = (float)numPages / 2.0f;
                    if (dist > halfN) dist = (float)numPages - dist;
                }
                float factor = 1.0f - dist;
                if (factor < 0.0f) factor = 0.0f;
                if (factor > 1.0f) factor = 1.0f;
                float alpha = 0.6f + factor * 0.4f;

                D2D1_COLOR_F textColor = UIStyle::ThemeColor::TextNormal().d2d;
                textColor.a = alpha;

                auto tabTextBrush = GetOrCreateBrush(textColor);
                if (tabTextBrush && m_tabTextFormat)
                {
                    rt->DrawTextW(m_pages[i].name.c_str(), (UINT32)m_pages[i].name.size(), m_tabTextFormat.Get(),
                        tabRect, tabTextBrush.Get());
                }
            }

            float lineW = std::min(header.selectionIndicatorWidth, std::max(10.0f, tabWidth - 4.0f));
            float lineH = std::max(1.2f, header.controlHeight * 0.06f);
            float lineX = topRect.left + (m_scrollPosition - std::floor(m_scrollPosition / m_pages.size()) * m_pages.size()) * tabWidth + (tabWidth - lineW) * 0.5f;
            float lineY = topRect.bottom - lineH;
            D2D1_ROUNDED_RECT indicatorLine = D2D1::RoundedRect(
                D2D1::RectF(lineX, lineY, lineX + lineW, lineY + lineH),
                lineH * 0.5f, lineH * 0.5f);
            D2D1_COLOR_F accentLine = UIStyle::ThemeColor::Accent().d2d;
            accentLine.a = 0.82f;
            auto accentBrush = GetOrCreateBrush(accentLine);
            if (accentBrush) rt->FillRoundedRectangle(indicatorLine, accentBrush.Get());
        }
    }
}

void PopupWindow::DrawSearchResults(ID2D1HwndRenderTarget* rt)
{
    int n = (int)m_searchResults.size();
    RECT cr; GetClientRect(GetHWND(), &cr);
    float scale = GetWindowScale(GetHWND());
    float w = (float)cr.right / scale;

    int topBarHeight = GetHeaderLayout().topBarHeight;

    if (n == 0)
    {
        auto textBrush = GetOrCreateBrush(UIStyle::ThemeColor::TextMuted().d2d);
        if (textBrush && m_popupTextFormat)
        {
            rt->DrawTextW(L"无匹配结果", 5, m_popupTextFormat.Get(),
                D2D1::RectF(0.0f, (float)topBarHeight + 40.0f, w, (float)topBarHeight + 100.0f),
                textBrush.Get());
        }
        return;
    }

    int cols = GetColumns();
    int rows = GetRows();
    int maxCells = cols * rows;
    if (n > maxCells) n = maxCells;

    int cw = CellWidth(), ch = CellHeight();
    int wndPad = GetWndPadding();
    int iconGap = GetIconGap();
    float iconRad = (float)GetIconRadius();
    float cardRad = iconRad + 2.0f;

    // Card backgrounds
    D2D1_COLOR_F selectedBorderColor = UIStyle::ThemeColor::AccentHover().d2d;
    selectedBorderColor.a = 0.42f;

    PopupRenderHelper::ShortcutCellMetrics cellMetrics;
    cellMetrics.count = n;
    cellMetrics.columns = cols;
    cellMetrics.cellWidth = cw;
    cellMetrics.cellHeight = ch;
    cellMetrics.padding = wndPad;
    cellMetrics.gridTop = wndPad + topBarHeight;
    cellMetrics.gap = iconGap;
    cellMetrics.cardCornerRadius = cardRad;

    PopupRenderHelper::ShortcutCellBrushes cellBrushes;
    cellBrushes.bgSelected = GetOrCreateBrush(UIStyle::ThemeColor::AccentSubtle().d2d).Get();
    cellBrushes.bgHover = GetOrCreateBrush(UIStyle::ThemeColor::ButtonBgHover().d2d).Get();
    cellBrushes.bgNormal = GetOrCreateBrush(UIStyle::ThemeColor::ButtonBgNormal().d2d).Get();
    cellBrushes.borderSelected = GetOrCreateBrush(selectedBorderColor).Get();
    cellBrushes.borderHover = GetOrCreateBrush(UIStyle::ThemeColor::ButtonBorderHover().d2d).Get();
    cellBrushes.borderNormal = GetOrCreateBrush(UIStyle::ThemeColor::ButtonBorderNormal().d2d).Get();
    cellBrushes.stroke = UIStyle::Metrics::ControlStroke();

    PopupRenderHelper::RenderShortcutCards(rt, cellMetrics, cellBrushes,
        [this](int i) { return i == m_hovered; },
        [this](int i) { return i == m_selectedSearchResult; });

    // Icons
    int cellMarginX = GetCellMarginX();
    int cellMarginY = GetCellMarginY();
    int iconSize = GetIconSize();

    for (int i = 0; i < n; i++)
    {
        float ix = (float)(wndPad + (i % cols) * cw);
        float iy = (float)(wndPad + (i / cols) * ch + topBarHeight);
        const auto& item = m_searchResults[i];
        auto* bmp = item.bitmap;
        ComPtr<ID2D1Bitmap> generatedBitmap;
        bool commandLike =
            item.kind == SearchResultItem::Kind::PluginCommand ||
            item.kind == SearchResultItem::Kind::PluginSearchResult ||
            item.kind == SearchResultItem::Kind::SlashCommand;
        if (!bmp && commandLike)
        {
            HICON hIcon = nullptr;
            if (!item.iconPath.empty())
            {
                ExtractIconExW(item.iconPath.c_str(), 0, &hIcon, nullptr, 1);
            }
            if (hIcon)
            {
                generatedBitmap = IconRenderer::HicontoD2D(rt, hIcon, IconRenderer::GetRecommendedBitmapSize(rt, static_cast<float>(iconSize)));
                DestroyIcon(hIcon);
            }
            if (!generatedBitmap)
            {
                generatedBitmap = IconRenderer::CreateDefaultIcon(rt, GetDWFactory(), item.shortcut.name, IconRenderer::GetRecommendedBitmapSize(rt, static_cast<float>(iconSize)));
            }
            bmp = generatedBitmap.Get();
        }
        if (bmp)
        {
            float iconX = ix + cellMarginX;
            float iconY = iy + cellMarginY;
            D2D1_RECT_F iconRect = IconRenderer::AlignToPixels(rt, iconX, iconY, (float)iconSize, (float)iconSize);
            DrawShortcutIcon(rt, bmp, iconRect, item.shortcut.name);
        }
    }

    // Labels
    if (m_popupTextFormat)
    {
        for (int i = 0; i < n; i++)
        {
            int col = i % cols, row = i / cols;
            float lx = (float)(wndPad + col * cw);
            bool commandLike = (m_searchResults[i].kind == SearchResultItem::Kind::PluginCommand ||
                m_searchResults[i].kind == SearchResultItem::Kind::PluginSearchResult ||
                m_searchResults[i].kind == SearchResultItem::Kind::SlashCommand);
            float ly = (float)(wndPad + row * ch + cellMarginY + iconSize + 2 + topBarHeight);
            auto& nm = m_searchResults[i].shortcut.name;
            
            auto tb = GetOrCreateBrush(UIStyle::ThemeColor::TextNormal().d2d);
            if (tb)
            {
                rt->DrawTextW(nm.c_str(), (UINT32)nm.size(), m_popupTextFormat.Get(),
                    D2D1::RectF(lx + 2, ly, lx + cw - iconGap - 2, ly + GetLabelHeight()),
                    tb.Get());
            }
            if (commandLike &&
                m_searchResults[i].kind != SearchResultItem::Kind::SlashCommand &&
                !m_searchResults[i].subtitle.empty())
            {
                auto subBrush = GetOrCreateBrush(UIStyle::ThemeColor::TextMuted().d2d);
                if (subBrush)
                {
                    const auto& subtitle = m_searchResults[i].subtitle;
                    rt->DrawTextW(subtitle.c_str(), (UINT32)subtitle.size(), m_popupTextFormat.Get(),
                        D2D1::RectF(lx + 2, ly + GetLabelHeight() + 2, lx + cw - iconGap - 2, ly + GetLabelHeight() * 2 + 4),
                        subBrush.Get());
                }
            }

        }
    }
}

void PopupWindow::DrawShortcutIcon(ID2D1HwndRenderTarget* rt, ID2D1Bitmap* bitmap,
    const D2D1_RECT_F& rect, const std::wstring& name)
{
    // Preserve the original text-icon flash without destroying real artwork.
    if (m_iconFlashStart && GetTickCount64() - m_iconFlashStart < 120)
    {
        auto& flash = m_iconFlashBitmaps[name];
        if (!flash)
            flash = IconRenderer::CreateDefaultIcon(rt, GetDWFactory(), name,
                IconRenderer::GetRecommendedBitmapSize(rt, static_cast<float>(GetIconSize())));
        if (flash) bitmap = flash.Get();
    }
    if (bitmap) rt->DrawBitmap(bitmap, rect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}

void PopupWindow::DrawPage(ID2D1HwndRenderTarget* rt, int pageIndex)
{
    if (pageIndex < 0 || pageIndex >= (int)m_pages.size()) return;

    const auto& page = m_pages[pageIndex];
    int n = (int)page.shortcuts.size();
    if (n == 0) return;

    int cols = GetColumns();
    int rows = GetRows();
    int maxCells = cols * rows;
    if (n > maxCells) n = maxCells;

    int cw = CellWidth(), ch = CellHeight();
    int wndPad = GetWndPadding();
    int iconGap = GetIconGap();
    float iconRad = (float)GetIconRadius();
    float cardRad = iconRad + 2.0f;
    int topBarHeight = GetHeaderLayout().topBarHeight;

    // Card backgrounds / icons / labels via the shared grid helpers
    int cellMarginX = GetCellMarginX();
    int cellMarginY = GetCellMarginY();
    int iconSize = GetIconSize();

    PopupRenderHelper::ShortcutCellMetrics cellMetrics;
    cellMetrics.count = n;
    cellMetrics.columns = cols;
    cellMetrics.cellWidth = cw;
    cellMetrics.cellHeight = ch;
    cellMetrics.padding = wndPad;
    cellMetrics.gridTop = wndPad + topBarHeight;
    cellMetrics.gap = iconGap;
    cellMetrics.cardCornerRadius = cardRad;

    PopupRenderHelper::ShortcutCellBrushes cellBrushes;
    cellBrushes.bgHover = GetOrCreateBrush(UIStyle::ThemeColor::ButtonBgHover().d2d).Get();
    cellBrushes.bgNormal = GetOrCreateBrush(UIStyle::ThemeColor::ButtonBgNormal().d2d).Get();
    cellBrushes.borderHover = GetOrCreateBrush(UIStyle::ThemeColor::ButtonBorderHover().d2d).Get();
    cellBrushes.borderNormal = GetOrCreateBrush(UIStyle::ThemeColor::ButtonBorderNormal().d2d).Get();
    cellBrushes.stroke = UIStyle::Metrics::ControlStroke();

    PopupRenderHelper::RenderShortcutCards(rt, cellMetrics, cellBrushes,
        [this, pageIndex](int i) { return pageIndex == m_currentPage && i == m_hovered; });

    PopupRenderHelper::RenderCellIcons(rt, cellMetrics, cellMarginX, cellMarginY, iconSize,
        [&page](int i) { return (i < (int)page.iconBitmaps.size()) ? page.iconBitmaps[i] : nullptr; },
        [this, rt, &page](int i, const D2D1_RECT_F& iconRect) {
            DrawShortcutIcon(rt, page.iconBitmaps[i], iconRect, page.shortcuts[i].name);
        });

    if (m_popupTextFormat)
    {
        auto tb = GetOrCreateBrush(UIStyle::ThemeColor::TextNormal().d2d);
        if (tb)
        {
            PopupRenderHelper::RenderShortcutLabels(rt, cellMetrics, m_popupTextFormat.Get(), tb.Get(),
                cellMarginY, iconSize, GetLabelHeight(),
                [&page](int i) -> const std::wstring& { return page.shortcuts[i].name; });
        }
    }
}

void PopupWindow::OnPaintContent(ID2D1HwndRenderTarget* rt)
{
    // Pin indicator
    if (m_pinned)
    {
        RECT cr; GetClientRect(GetHWND(), &cr);
        float scale = GetWindowScale(GetHWND());
        float w = (float)cr.right / scale;
        auto pb = GetOrCreateBrush(D2D1::ColorF(0, 0.8f, 0, 1));
        if (pb)
        {
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(w - 10, 10), 4, 4), pb.Get());
        }
    }

    EnsureIcons();

    DrawTopBar(rt);

    if (m_pages.empty()) return;

    RECT cr; GetClientRect(GetHWND(), &cr);
    float scale = GetWindowScale(GetHWND());
    float w = (float)cr.right / scale;

    if (m_searchActive && !m_searchQuery.empty())
    {
        DrawSearchResults(rt);
    }
    else
    {
        D2D1_MATRIX_3X2_F originalTransform;
        rt->GetTransform(&originalTransform);

        int numPages = (int)m_pages.size();
        for (int i = 0; i < numPages; i++)
        {
            float diff = (float)i - m_scrollPosition;
            if (numPages > 1)
            {
                diff = std::remainder(diff, static_cast<float>(numPages));
            }

            float offsetX = diff * w;
            if (std::abs(offsetX) < w)
            {
                rt->SetTransform(D2D1::Matrix3x2F::Translation(offsetX, 0.0f) * originalTransform);
                DrawPage(rt, i);
            }
        }
        rt->SetTransform(originalTransform);
    }

    DrawDock(rt);
}

void PopupWindow::DrawDock(ID2D1HwndRenderTarget* rt)
{
    int dockRows = GetDockHeight();  // dockHeight stores row count
    int cols    = GetColumns();
    int cw      = CellWidth();
    int ch      = CellHeight();
    int wndPad  = GetWndPadding();
    int iconGap = GetIconGap();
    int iconSize = GetIconSize();
    int cellMarginX = GetCellMarginX();
    int cellMarginY = GetCellMarginY();
    float iconRad = (float)GetIconRadius();
    float cardRad = iconRad + 2.0f;

    // Gap between upper section and dock = 2 * wndPad, dividing line in the middle
    const PopupLayout::WindowMetrics windowMetrics = ComputeWindowMetrics();
    const int lineY = windowMetrics.lineY;
    const int dockTopY = windowMetrics.dockTopY;

    // Separator line
    RECT cr2; GetClientRect(GetHWND(), &cr2);
    float scale2 = GetWindowScale(GetHWND());
    float totalW = (float)cr2.right / scale2;

    D2D1_COLOR_F baseClr = UIStyle::ThemeColor::ThemeBase().d2d;
    float lineAlpha = (UIStyle::GetThemeMode() == UIStyle::ThemeMode::Light) ? 1.0f : 0.25f;
    auto lineBrush = GetOrCreateBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, lineAlpha));
    if (lineBrush)
    {
        PopupRenderHelper::RenderDockSeparator(rt, totalW, (float)lineY, lineBrush.Get());
    }

    // Active file selection feedback (timeline)
    double now = PopupClock::NowSeconds();
    double elapsed = 0.0;
    std::vector<std::wstring> selectionPreview;
    const bool hasActiveSelection = m_fileSelection.Peek(now, GetFileSelectionValiditySeconds(), selectionPreview, &elapsed);

    if (hasActiveSelection)
    {
        const int validitySeconds = GetFileSelectionValiditySeconds();
        float progress = validitySeconds < 0
            ? 1.0f
            : (float)(1.0 - (elapsed / validitySeconds));
        if (progress < 0.0f) progress = 0.0f;
        if (progress > 1.0f) progress = 1.0f;

        PopupRenderHelper::RenderFileSelectionTimeline(rt, totalW, (float)lineY, progress);
    }

    int n = (int)m_dockPage.shortcuts.size();
    if (n == 0) return;

    int maxCells = cols * dockRows;
    if (n > maxCells) n = maxCells;

    // Card backgrounds / icons / labels via the shared grid helpers
    PopupRenderHelper::ShortcutCellMetrics cellMetrics;
    cellMetrics.count = n;
    cellMetrics.columns = cols;
    cellMetrics.cellWidth = cw;
    cellMetrics.cellHeight = ch;
    cellMetrics.padding = wndPad;
    cellMetrics.gridTop = dockTopY;
    cellMetrics.gap = iconGap;
    cellMetrics.cardCornerRadius = cardRad;

    PopupRenderHelper::ShortcutCellBrushes cellBrushes;
    cellBrushes.bgHover = GetOrCreateBrush(UIStyle::ThemeColor::ButtonBgHover().d2d).Get();
    cellBrushes.bgNormal = GetOrCreateBrush(UIStyle::ThemeColor::ButtonBgNormal().d2d).Get();
    cellBrushes.borderHover = GetOrCreateBrush(UIStyle::ThemeColor::ButtonBorderHover().d2d).Get();
    cellBrushes.borderNormal = GetOrCreateBrush(UIStyle::ThemeColor::ButtonBorderNormal().d2d).Get();
    cellBrushes.stroke = UIStyle::Metrics::ControlStroke();

    PopupRenderHelper::RenderShortcutCards(rt, cellMetrics, cellBrushes,
        [this](int i) { return i == m_hoveredDock; });

    PopupRenderHelper::RenderCellIcons(rt, cellMetrics, cellMarginX, cellMarginY, iconSize,
        [this](int i) { return (i < (int)m_dockPage.iconBitmaps.size()) ? m_dockPage.iconBitmaps[i] : nullptr; },
        [this, rt](int i, const D2D1_RECT_F& iconRect) {
            DrawShortcutIcon(rt, m_dockPage.iconBitmaps[i], iconRect, m_dockPage.shortcuts[i].name);
        });

    if (m_popupTextFormat)
    {
        auto tb = GetOrCreateBrush(UIStyle::ThemeColor::TextNormal().d2d);
        if (tb)
        {
            PopupRenderHelper::RenderShortcutLabels(rt, cellMetrics, m_popupTextFormat.Get(), tb.Get(),
                cellMarginY, iconSize, GetLabelHeight(),
                [this](int i) -> const std::wstring& { return m_dockPage.shortcuts[i].name; });
        }
    }
}

