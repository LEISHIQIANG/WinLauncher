#define NOMINMAX
#include "SystemIconEditForm.h"
#include "UIStyle.h"
#include "../DpiHelper.h"
#include "../ShortcutManager.h"
#include <windowsx.h>
#include <commdlg.h>
#include <shlwapi.h>
#include <algorithm>
#include <vector>

#pragma comment(lib, "comdlg32.lib")

#include "../UI/Controls/IconRenderer.h"

struct SystemBrushCacheEntry
{
    D2D1_COLOR_F color;
    ComPtr<ID2D1SolidColorBrush> brush;
};
static std::vector<SystemBrushCacheEntry> g_systemFormBrushCache;

static ComPtr<ID2D1SolidColorBrush> GetOrCreateBrush(ID2D1HwndRenderTarget* rt, const D2D1_COLOR_F& color)
{
    for (auto& entry : g_systemFormBrushCache)
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
            g_systemFormBrushCache.push_back({ color, brush });
        }
    }
    return brush;
}

static const float Y_SEC_BASIC  = 0.0f;
static const float Y_LBL_NAME   = 16.0f;
#define Y_BOX_NAME (Y_LBL_NAME + 16.0f)
static const float Y_LBL_ICON   = 68.0f;
#define Y_BOX_ICON (Y_LBL_ICON + 16.0f)
static const float Y_INVERT_LIGHT = 120.0f;
static const float Y_INVERT_DARK  = 120.0f;
static const float Y_PREVIEW      = 72.0f;

SystemIconEditForm::SystemIconEditForm()
{
}

SystemIconEditForm::~SystemIconEditForm()
{
    Destroy();
}

bool SystemIconEditForm::Create(HWND parentHWND, IDWriteFactory* dwriteFactory, const D2D1_RECT_F& logicalBounds, const SystemIconEditFormInitParams& init)
{
    m_parentHWND = parentHWND;
    m_bounds = logicalBounds;
    m_init = init;
    m_iconInvertLight = init.iconInvertLight;
    m_iconInvertDark = init.iconInvertDark;

    UIStyle::TextBoxStyle style;
    style.fontSize    = 11;
    style.paddingTop  = 4.0f;
    style.paddingBottom = 4.0f;

    EnsureFonts(dwriteFactory);

    float W = m_bounds.right - m_bounds.left;

    m_nameBox.SetStyle(style);
    m_nameBox.Create(parentHWND, dwriteFactory, D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_NAME, m_bounds.left + W - 20, m_bounds.top + Y_BOX_NAME + 24), m_init.name);

    m_iconBox.SetStyle(style);
    m_iconBox.Create(parentHWND, dwriteFactory, D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_ICON, m_bounds.left + W - 121, m_bounds.top + Y_BOX_ICON + 24), m_init.iconPath);

    m_focusedBox = &m_nameBox;
    m_nameBox.SetFocus(true);

    m_previewIcon = GetIconForPreview(m_init.iconPath);

    return true;
}

void SystemIconEditForm::Destroy()
{
    m_nameBox.Destroy();
    m_iconBox.Destroy();

    if (m_previewBitmap) { m_previewBitmap->Release(); m_previewBitmap = nullptr; }
    if (m_previewIcon)   { DestroyIcon(m_previewIcon); m_previewIcon = nullptr; }

    g_systemFormBrushCache.clear();
}

void SystemIconEditForm::UpdateLayout(const D2D1_RECT_F& logicalBounds, float scale)
{
    m_bounds = logicalBounds;
    float W = m_bounds.right - m_bounds.left;

    m_nameBox.SetBounds(D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_NAME, m_bounds.left + W - 20, m_bounds.top + Y_BOX_NAME + 24));
    m_nameBox.UpdateLayout(scale);

    m_iconBox.SetBounds(D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_ICON, m_bounds.left + W - 121, m_bounds.top + Y_BOX_ICON + 24));
    m_iconBox.UpdateLayout(scale);
}

void SystemIconEditForm::EnsureFonts(IDWriteFactory* dwriteFactory)
{
    auto makeFormat = [&](ComPtr<IDWriteTextFormat>& tf, float size, DWRITE_FONT_WEIGHT weight, DWRITE_TEXT_ALIGNMENT ha, DWRITE_PARAGRAPH_ALIGNMENT va) {
        if (!dwriteFactory || tf) return;
        UIStyle::Typography::CreateTextFormat(dwriteFactory, &tf, size, weight, ha, va);
    };

    makeFormat(m_tfLabel, 10.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    makeFormat(m_tfBtn,   10.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    makeFormat(m_tfSmall, 9.0f,  DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

bool SystemIconEditForm::HitTestRect(POINT pt, const D2D1_RECT_F& rect)
{
    return (pt.x >= rect.left && pt.x <= rect.right && pt.y >= rect.top && pt.y <= rect.bottom);
}

bool SystemIconEditForm::HitTestBrowseIconButton(POINT pt)
{
    float W = m_bounds.right - m_bounds.left;
    return HitTestRect(pt, D2D1::RectF(m_bounds.left + W - 116, m_bounds.top + Y_BOX_ICON, m_bounds.left + W - 61, m_bounds.top + Y_BOX_ICON + 24));
}

bool SystemIconEditForm::HitTestInvertLightCheckbox(POINT pt)
{
    return HitTestRect(pt, D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_INVERT_LIGHT, m_bounds.left + 100, m_bounds.top + Y_INVERT_LIGHT + 22));
}

bool SystemIconEditForm::HitTestInvertDarkCheckbox(POINT pt)
{
    return HitTestRect(pt, D2D1::RectF(m_bounds.left + 106, m_bounds.top + Y_INVERT_DARK, m_bounds.left + 186, m_bounds.top + Y_INVERT_DARK + 22));
}

HICON SystemIconEditForm::GetIconForPreview(const std::wstring& iconPath)
{
    RendShortcutInfo sc;
    sc.name = m_nameBox.GetText();
    sc.type = Model::ShortcutType::System;
    sc.targetPath = m_init.targetPath;
    sc.targetKind = m_init.targetKind == Model::ShortcutTargetKind::Unknown
        ? ShortcutManager::InferTargetKind(m_init.targetPath)
        : m_init.targetKind;
    sc.iconSource = m_init.iconSource;
    sc.iconPath = iconPath;
    sc.builtinIconId = m_init.builtinIconId;
    sc.iconInvertLight = m_iconInvertLight;
    sc.iconInvertDark = m_iconInvertDark;

    if (!iconPath.empty())
    {
        sc.iconSource = Model::IconSource::CustomPath;
    }
    else if (sc.iconSource == Model::IconSource::CustomPath)
    {
        sc.iconSource = Model::IconSource::Auto;
    }

    return ShortcutManager::GetShortcutIcon(sc);
}

bool SystemIconEditForm::IsInputFocused() const
{
    return (m_focusedBox != nullptr);
}

void SystemIconEditForm::ResetFocus()
{
    if (m_focusedBox)
    {
        m_focusedBox->SetFocus(false);
        m_focusedBox = nullptr;
    }
}

SystemIconEditFormResult SystemIconEditForm::GetResult() const
{
    SystemIconEditFormResult r;
    r.name = m_nameBox.GetText();
    r.iconPath = m_iconBox.GetText();
    r.iconInvertLight = m_iconInvertLight;
    r.iconInvertDark = m_iconInvertDark;
    return r;
}

bool SystemIconEditForm::Validate(HWND hWnd)
{
    std::wstring name = m_nameBox.GetText();
    while(!name.empty() && (name.back() == L' ' || name.back() == L'\t'))
        name.pop_back();
    size_t start = 0;
    while(start < name.size() && (name[start] == L' ' || name[start] == L'\t'))
        start++;
    if (start > 0) name = name.substr(start);

    if (name.empty())
    {
        MessageBoxW(hWnd, L"请输入名称！", L"提示", MB_OK | MB_ICONWARNING);
        return false;
    }
    return true;
}

void SystemIconEditForm::BrowseIconFile(HWND hWnd)
{
    wchar_t fileBuf[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize  = sizeof(ofn);
    ofn.hwndOwner    = hWnd;
    ofn.lpstrFilter  = L"图标文件 (*.ico)\0*.ico\0可执行程序 (*.exe)\0*.exe\0资源链接库 (*.dll)\0*.dll\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile    = fileBuf;
    ofn.nMaxFile     = MAX_PATH;
    ofn.Flags        = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;

    std::wstring cur = m_iconBox.GetText();
    if (!cur.empty() && cur.size() < MAX_PATH)
        wcscpy_s(fileBuf, cur.c_str());

    if (GetOpenFileNameW(&ofn))
    {
        std::wstring iconPath = fileBuf;
        m_iconBox.SetText(iconPath);

        if (m_previewIcon)   { DestroyIcon(m_previewIcon);   m_previewIcon = nullptr; }
        if (m_previewBitmap) { m_previewBitmap->Release(); m_previewBitmap = nullptr; }

        m_previewIcon = GetIconForPreview(iconPath);
    }
}

void SystemIconEditForm::OnMouseMove(HWND hWnd, POINT pt, float scale, bool& repaint)
{
    m_nameBox.OnMouseMove(hWnd, pt, scale, repaint);
    m_iconBox.OnMouseMove(hWnd, pt, scale, repaint);

    auto update = [&](bool& state, bool newVal) {
        if (state != newVal) { state = newVal; repaint = true; }
    };

    update(m_hoveredBrowseIcon,  HitTestBrowseIconButton(pt));
    update(m_hoveredInvertLight, HitTestInvertLightCheckbox(pt));
    update(m_hoveredInvertDark,  HitTestInvertDarkCheckbox(pt));
}

void SystemIconEditForm::OnLButtonDown(HWND hWnd, POINT pt, float scale, bool& repaint)
{
    TextBox* targetBox = nullptr;
    if (m_nameBox.HitTest(pt)) targetBox = &m_nameBox;
    else if (m_iconBox.HitTest(pt)) targetBox = &m_iconBox;

    if (targetBox != m_focusedBox)
    {
        if (m_focusedBox) m_focusedBox->SetFocus(false);
        m_focusedBox = targetBox;
        if (m_focusedBox) m_focusedBox->SetFocus(true);
        repaint = true;
    }

    if (m_focusedBox)
    {
        m_focusedBox->OnLButtonDown(hWnd, pt, scale, repaint);
    }

    if (HitTestBrowseIconButton(pt))
    {
        BrowseIconFile(hWnd);
        repaint = true;
        return;
    }

    if (HitTestInvertLightCheckbox(pt))
    {
        m_iconInvertLight = !m_iconInvertLight;
        if (m_previewBitmap) { m_previewBitmap->Release(); m_previewBitmap = nullptr; }
        repaint = true;
        return;
    }

    if (HitTestInvertDarkCheckbox(pt))
    {
        m_iconInvertDark = !m_iconInvertDark;
        if (m_previewBitmap) { m_previewBitmap->Release(); m_previewBitmap = nullptr; }
        repaint = true;
        return;
    }
}

void SystemIconEditForm::OnLButtonDblClk(HWND hWnd, POINT pt, float scale, bool& repaint)
{
    if (m_focusedBox && m_focusedBox->HitTest(pt))
    {
        m_focusedBox->OnLButtonDblClk(hWnd, pt, scale, repaint);
    }
}

void SystemIconEditForm::OnLButtonUp(HWND hWnd, POINT pt, float scale, bool& repaint)
{
    if (m_focusedBox)
    {
        m_focusedBox->OnLButtonUp(hWnd, pt, scale, repaint);
    }
}

void SystemIconEditForm::OnChar(HWND hWnd, WPARAM wParam, bool& repaint)
{
    if (m_focusedBox)
    {
        m_focusedBox->OnChar(hWnd, wParam, repaint);
        if (m_focusedBox == &m_iconBox)
        {
            if (m_previewIcon)   { DestroyIcon(m_previewIcon);   m_previewIcon = nullptr; }
            if (m_previewBitmap) { m_previewBitmap->Release(); m_previewBitmap = nullptr; }

            m_previewIcon = GetIconForPreview(m_iconBox.GetText());

            repaint = true;
        }
    }
}

void SystemIconEditForm::OnKeyDown(HWND hWnd, WPARAM wParam, LPARAM lParam, bool& repaint)
{
    if (m_focusedBox)
    {
        m_focusedBox->OnKeyDown(hWnd, wParam, lParam, repaint);
        if (m_focusedBox == &m_iconBox)
        {
            if (m_previewIcon)   { DestroyIcon(m_previewIcon);   m_previewIcon = nullptr; }
            if (m_previewBitmap) { m_previewBitmap->Release(); m_previewBitmap = nullptr; }

            m_previewIcon = GetIconForPreview(m_iconBox.GetText());

            repaint = true;
        }
    }
}

void SystemIconEditForm::BlinkCaret()
{
    if (m_focusedBox)
    {
        m_focusedBox->BlinkCaret();
    }
}

bool SystemIconEditForm::HandleImeMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, bool& repaint)
{
    if (m_focusedBox)
    {
        return m_focusedBox->HandleImeMessage(hWnd, uMsg, wParam, lParam, repaint);
    }
    return false;
}

void SystemIconEditForm::DrawSectionLabel(ID2D1HwndRenderTarget* rt, const wchar_t* text, const D2D1_RECT_F& rect)
{
    if (m_tfLabel)
    {
        auto brush = GetOrCreateBrush(rt, UIStyle::ThemeColor::Accent().d2d);
        if (brush) rt->DrawTextW(text, (UINT32)wcslen(text), m_tfLabel.Get(), rect, brush.Get());
    }
}

void SystemIconEditForm::DrawButton(ID2D1HwndRenderTarget* rt, const wchar_t* text, const D2D1_RECT_F& rect, bool hovered)
{
    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(rect, 4.0f, 4.0f);
    bool isLight = (UIStyle::GetThemeMode() == UIStyle::ThemeMode::Light);
    D2D1_COLOR_F base = isLight ? D2D1::ColorF(0.f, 0.f, 0.f) : D2D1::ColorF(1.f, 1.f, 1.f);

    float bgA = hovered ? 0.08f : 0.03f;
    float borderA = hovered ? 0.15f : 0.08f;

    auto bgBrush = GetOrCreateBrush(rt, D2D1::ColorF(base.r, base.g, base.b, bgA));
    if (bgBrush) rt->FillRoundedRectangle(rr, bgBrush.Get());

    auto borderBrush = GetOrCreateBrush(rt, D2D1::ColorF(base.r, base.g, base.b, borderA));
    if (borderBrush) rt->DrawRoundedRectangle(rr, borderBrush.Get(), UIStyle::Metrics::ControlStroke());

    if (m_tfBtn)
    {
        auto textBrush = GetOrCreateBrush(rt, UIStyle::ThemeColor::TextNormal().d2d);
        if (textBrush) rt->DrawTextW(text, (UINT32)wcslen(text), m_tfBtn.Get(), rect, textBrush.Get());
    }
}

void SystemIconEditForm::DrawCheckbox(ID2D1HwndRenderTarget* rt, const D2D1_RECT_F& rect, bool checked, bool hovered, const wchar_t* labelText)
{
    const float cbSize = 14.0f;
    D2D1_RECT_F boxRect = D2D1::RectF(rect.left, rect.top + (rect.bottom - rect.top - cbSize) * 0.5f,
                                       rect.left + cbSize, rect.top + (rect.bottom - rect.top - cbSize) * 0.5f + cbSize);
    D2D1_ROUNDED_RECT rrBox = D2D1::RoundedRect(boxRect, 3.0f, 3.0f);

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
            float cx = boxRect.left + cbSize * 0.5f;
            float cy = boxRect.top  + cbSize * 0.5f;
            rt->DrawLine(D2D1::Point2F(cx - 4, cy),     D2D1::Point2F(cx - 1, cy + 3.5f), ckBrush.Get(), 1.5f);
            rt->DrawLine(D2D1::Point2F(cx - 1, cy + 3.5f), D2D1::Point2F(cx + 4, cy - 3), ckBrush.Get(), 1.5f);
        }
    }
    else
    {
        float bgA = hovered ? 0.08f : 0.02f;
        float borderA = hovered ? 0.20f : 0.10f;
        auto bgBrush = GetOrCreateBrush(rt, D2D1::ColorF(base.r, base.g, base.b, bgA));
        if (bgBrush) rt->FillRoundedRectangle(rrBox, bgBrush.Get());

        auto borderBrush = GetOrCreateBrush(rt, D2D1::ColorF(base.r, base.g, base.b, borderA));
        if (borderBrush) rt->DrawRoundedRectangle(rrBox, borderBrush.Get(), UIStyle::Metrics::ControlStroke());
    }

    if (m_tfLabel)
    {
        auto textBrush = GetOrCreateBrush(rt, UIStyle::ThemeColor::TextNormal().d2d);
        if (textBrush)
        {
            D2D1_RECT_F textRect = D2D1::RectF(rect.left + cbSize + 6.0f, rect.top, rect.right, rect.bottom);
            rt->DrawTextW(labelText, (UINT32)wcslen(labelText), m_tfLabel.Get(), textRect, textBrush.Get());
        }
    }
}

void SystemIconEditForm::DrawIconPreview(ID2D1HwndRenderTarget* rt)
{
    bool isLight = (UIStyle::GetThemeMode() == UIStyle::ThemeMode::Light);
    bool invert = isLight ? m_iconInvertLight : m_iconInvertDark;

    if (m_previewIcon)
    {
        if (!m_previewBitmap)
        {
            auto bmp = IconRenderer::HicontoD2D(rt, m_previewIcon, 36, invert);
            if (bmp) m_previewBitmap = bmp.Detach();
        }
    }
    else
    {
        std::wstring currentName = m_nameBox.GetText();
        if (!m_previewBitmap || m_lastPreviewName != currentName)
        {
            if (m_previewBitmap) { m_previewBitmap->Release(); m_previewBitmap = nullptr; }
            auto bmp = IconRenderer::CreateDefaultIcon(rt, nullptr, currentName, 36);
            if (bmp)
            {
                m_previewBitmap = bmp.Detach();
                m_lastPreviewName = currentName;
            }
        }
    }

    const float previewSize = 36.0f;
    float W = m_bounds.right - m_bounds.left;
    D2D1_RECT_F previewRect = D2D1::RectF(m_bounds.left + W - 56.0f, m_bounds.top + Y_PREVIEW, m_bounds.left + W - 56.0f + previewSize, m_bounds.top + Y_PREVIEW + previewSize);

    D2D1_ROUNDED_RECT rrIcon = D2D1::RoundedRect(previewRect, 8.0f, 8.0f);
    D2D1_COLOR_F base = isLight ? D2D1::ColorF(0.f, 0.f, 0.f) : D2D1::ColorF(1.f, 1.f, 1.f);
    auto previewBg = GetOrCreateBrush(rt, D2D1::ColorF(base.r, base.g, base.b, 0.03f));
    if (previewBg) rt->FillRoundedRectangle(rrIcon, previewBg.Get());

    auto borderBrush = GetOrCreateBrush(rt, D2D1::ColorF(base.r, base.g, base.b, 0.1f));
    if (borderBrush) rt->DrawRoundedRectangle(rrIcon, borderBrush.Get(), UIStyle::Metrics::ControlStroke());

    if (m_previewBitmap)
    {
        float x = previewRect.left + 2.0f;
        float y = previewRect.top + 2.0f;
        float w = previewRect.right - previewRect.left - 4.0f;
        float h = previewRect.bottom - previewRect.top - 4.0f;
        D2D1_RECT_F alignedRect = IconRenderer::AlignToPixels(rt, x, y, w, h);

        rt->DrawBitmap(m_previewBitmap, alignedRect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, nullptr);
    }
}

void SystemIconEditForm::Paint(ID2D1HwndRenderTarget* rt, float scale)
{
    float W = m_bounds.right - m_bounds.left;

    // Section Header
    DrawSectionLabel(rt, L"系统图标配置", D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_SEC_BASIC, m_bounds.left + W - 20, m_bounds.top + Y_SEC_BASIC + 12));

    // Name Label
    if (m_tfLabel)
    {
        D2D1_COLOR_F lc = UIStyle::ThemeColor::TextNormal().d2d; lc.a = 0.75f;
        auto lb = GetOrCreateBrush(rt, lc);
        if (lb) rt->DrawTextW(L"图标名称", 4, m_tfLabel.Get(), D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_LBL_NAME, m_bounds.left + 200, m_bounds.top + Y_LBL_NAME + 14), lb.Get());
    }
    m_nameBox.Paint(rt, scale);

    // Icon Label
    if (m_tfLabel)
    {
        D2D1_COLOR_F lc = UIStyle::ThemeColor::TextNormal().d2d; lc.a = 0.75f;
        auto lb = GetOrCreateBrush(rt, lc);
        if (lb) rt->DrawTextW(L"自定义图标路径 (留空则自动提取系统图标)", 22, m_tfLabel.Get(), D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_LBL_ICON, m_bounds.left + 300, m_bounds.top + Y_LBL_ICON + 14), lb.Get());
    }
    m_iconBox.Paint(rt, scale);

    // Icon Buttons
    DrawButton(rt, L"浏览", D2D1::RectF(m_bounds.left + W - 116.0f, m_bounds.top + Y_BOX_ICON, m_bounds.left + W - 61.0f, m_bounds.top + Y_BOX_ICON + 24.0f), m_hoveredBrowseIcon);

    // Invert Checkboxes
    DrawCheckbox(rt, D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_INVERT_LIGHT, m_bounds.left + 100, m_bounds.top + Y_INVERT_LIGHT + 22), m_iconInvertLight, m_hoveredInvertLight, L"浅色反转");
    DrawCheckbox(rt, D2D1::RectF(m_bounds.left + 106, m_bounds.top + Y_INVERT_DARK, m_bounds.left + 186, m_bounds.top + Y_INVERT_DARK + 22), m_iconInvertDark, m_hoveredInvertDark, L"深色反转");

    // Preview
    DrawIconPreview(rt);
}
