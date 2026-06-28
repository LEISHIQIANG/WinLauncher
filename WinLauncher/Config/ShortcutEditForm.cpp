#define NOMINMAX

#include "ShortcutEditForm.h"

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



#pragma comment(lib, "comdlg32.lib")

#include "../UI/Controls/IconRenderer.h"



// 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€

//  Brush Cache Entry mapping helper (defined at top to avoid compiler errors)

// 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€

struct D2DBrushCacheEntry

{

    D2D1_COLOR_F color;

    ComPtr<ID2D1SolidColorBrush> brush;

};

static std::vector<D2DBrushCacheEntry> g_formBrushCache;



static ComPtr<ID2D1SolidColorBrush> GetOrCreateBrush(ID2D1HwndRenderTarget* rt, const D2D1_COLOR_F& color)

{

    for (auto& entry : g_formBrushCache)

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

            g_formBrushCache.push_back({ color, brush });

        }

    }

    return brush;

}



// 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€

//  Form Y coordinates relative to m_bounds.top

// 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€

static const float Y_SEC_BASIC  = 0.0f;    // "鍩烘湰淇℃伅" separator

static const float Y_LBL_NAME   = 16.0f;

static const float Y_BOX_NAME   = 32.0f;

static const float Y_LBL_TARGET = 60.0f;

static const float Y_BOX_TARGET = 76.0f;

static const float Y_LBL_ICON   = 104.0f;  // Custom Icon label

static const float Y_BOX_ICON   = 120.0f;  // Custom Icon textbox

static const float Y_LBL_ARGS   = 148.0f;

static const float Y_BOX_ARGS   = 164.0f;

static const float Y_SEC_ADV    = 192.0f;  // "楂樼骇閫夐」" separator

static const float Y_LBL_WDIR   = 208.0f;

static const float Y_BOX_WDIR   = 224.0f;

static const float Y_ADMIN_CB   = 260.0f;  // run-as-admin checkbox row

static const float Y_INVERT_LIGHT = Y_ADMIN_CB;  // light invert checkbox

static const float Y_INVERT_DARK  = Y_ADMIN_CB;  // dark invert checkbox

static const float Y_PREVIEW    = 253.0f;  // icon preview (right side)



// 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€

ShortcutEditForm::ShortcutEditForm()

{

}



ShortcutEditForm::~ShortcutEditForm()

{

    Destroy();

}



bool ShortcutEditForm::Create(HWND parentHWND, IDWriteFactory* dwriteFactory, const D2D1_RECT_F& logicalBounds, const ShortcutEditFormInitParams& init)

{

    m_parentHWND = parentHWND;

    m_bounds = logicalBounds;

    m_init = init;

    m_runAsAdmin = init.runAsAdmin;

    m_iconInvertLight = init.iconInvertLight;

    m_iconInvertDark = init.iconInvertDark;



    UIStyle::TextBoxStyle style;

    style.fontSize    = 11;

    style.paddingTop  = 4.0f;

    style.paddingBottom = 4.0f;



    EnsureFonts(dwriteFactory);



    float W = m_bounds.right - m_bounds.left;



    // Name (Full width: x range [20, W - 20])

    m_nameBox.SetStyle(style);

    m_nameBox.Create(parentHWND, dwriteFactory, D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_NAME, m_bounds.left + W - 20, m_bounds.top + Y_BOX_NAME + 24), m_init.name);



    // Target (With browse button: x range [20, W - 85])

    m_targetBox.SetStyle(style);

    m_targetBox.Create(parentHWND, dwriteFactory, D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_TARGET, m_bounds.left + W - 85, m_bounds.top + Y_BOX_TARGET + 24), m_init.targetPath);



    // Icon (With browse button: x range [20, W - 85])

    m_iconBox.SetStyle(style);

    m_iconBox.Create(parentHWND, dwriteFactory, D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_ICON, m_bounds.left + W - 85, m_bounds.top + Y_BOX_ICON + 24), m_init.iconPath);



    // Args (Full width: x range [20, W - 20])

    m_argsBox.SetStyle(style);

    m_argsBox.Create(parentHWND, dwriteFactory, D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_ARGS, m_bounds.left + W - 20, m_bounds.top + Y_BOX_ARGS + 24), m_init.arguments);



    // Workdir (With browse button: x range [20, W - 85])

    m_workdirBox.SetStyle(style);

    m_workdirBox.Create(parentHWND, dwriteFactory, D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_WDIR, m_bounds.left + W - 85, m_bounds.top + Y_BOX_WDIR + 24), m_init.workingDir);



    m_focusedBox = &m_nameBox;

    m_nameBox.SetFocus(true);



    // Load preview icon

    std::wstring previewPath = m_init.iconPath.empty() ? m_init.targetPath : m_init.iconPath;

    if (!previewPath.empty())

    {

        m_previewIcon = GetFileIconForPreview(previewPath);

    }



    return true;

}



void ShortcutEditForm::Destroy()

{

    m_nameBox.Destroy();

    m_targetBox.Destroy();

    m_iconBox.Destroy();

    m_argsBox.Destroy();

    m_workdirBox.Destroy();



    if (m_previewBitmap) { m_previewBitmap->Release(); m_previewBitmap = nullptr; }

    if (m_previewIcon)   { DestroyIcon(m_previewIcon); m_previewIcon = nullptr; }



    g_formBrushCache.clear();

}



void ShortcutEditForm::UpdateLayout(const D2D1_RECT_F& logicalBounds, float scale)

{

    m_bounds = logicalBounds;

    float W = m_bounds.right - m_bounds.left;



    m_nameBox.SetBounds(D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_NAME, m_bounds.left + W - 20, m_bounds.top + Y_BOX_NAME + 24));

    m_nameBox.UpdateLayout(scale);



    m_targetBox.SetBounds(D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_TARGET, m_bounds.left + W - 85, m_bounds.top + Y_BOX_TARGET + 24));

    m_targetBox.UpdateLayout(scale);



    m_iconBox.SetBounds(D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_ICON, m_bounds.left + W - 85, m_bounds.top + Y_BOX_ICON + 24));

    m_iconBox.UpdateLayout(scale);



    m_argsBox.SetBounds(D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_ARGS, m_bounds.left + W - 20, m_bounds.top + Y_BOX_ARGS + 24));

    m_argsBox.UpdateLayout(scale);



    m_workdirBox.SetBounds(D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_WDIR, m_bounds.left + W - 85, m_bounds.top + Y_BOX_WDIR + 24));

    m_workdirBox.UpdateLayout(scale);

}



void ShortcutEditForm::EnsureFonts(IDWriteFactory* dwriteFactory)

{

    auto makeFormat = [&](ComPtr<IDWriteTextFormat>& tf,

                          float size,

                          DWRITE_FONT_WEIGHT weight,

                          DWRITE_TEXT_ALIGNMENT ha,

                          DWRITE_PARAGRAPH_ALIGNMENT va)

    {

        if (!dwriteFactory || tf) return;

        UIStyle::Typography::CreateTextFormat(
            dwriteFactory,
            &tf,
            size,
            weight,
            ha,
            va);

    };



    makeFormat(m_tfLabel, 10.0f, DWRITE_FONT_WEIGHT_NORMAL,

               DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    makeFormat(m_tfBtn,   10.0f, DWRITE_FONT_WEIGHT_NORMAL,

               DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    makeFormat(m_tfSmall, 9.0f, DWRITE_FONT_WEIGHT_NORMAL,

               DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

}



// 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€

//  Hit-tests

// 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€

bool ShortcutEditForm::HitTestRect(POINT pt, const D2D1_RECT_F& rect)

{

    return (pt.x >= rect.left && pt.x <= rect.right && pt.y >= rect.top && pt.y <= rect.bottom);

}



bool ShortcutEditForm::HitTestBrowseTargetButton(POINT pt)

{

    float W = m_bounds.right - m_bounds.left;

    return HitTestRect(pt, D2D1::RectF(m_bounds.left + W - 80, m_bounds.top + Y_BOX_TARGET, m_bounds.left + W - 20, m_bounds.top + Y_BOX_TARGET + 24));

}



bool ShortcutEditForm::HitTestBrowseIconButton(POINT pt)

{

    float W = m_bounds.right - m_bounds.left;

    return HitTestRect(pt, D2D1::RectF(m_bounds.left + W - 80, m_bounds.top + Y_BOX_ICON, m_bounds.left + W - 20, m_bounds.top + Y_BOX_ICON + 24));

}



bool ShortcutEditForm::HitTestBrowseWorkdirButton(POINT pt)

{

    float W = m_bounds.right - m_bounds.left;

    return HitTestRect(pt, D2D1::RectF(m_bounds.left + W - 80, m_bounds.top + Y_BOX_WDIR, m_bounds.left + W - 20, m_bounds.top + Y_BOX_WDIR + 24));

}



bool ShortcutEditForm::HitTestRunAsAdminCheckbox(POINT pt)

{

    return HitTestRect(pt, D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_ADMIN_CB, m_bounds.left + 112, m_bounds.top + Y_ADMIN_CB + 22));

}



bool ShortcutEditForm::HitTestInvertLightCheckbox(POINT pt)

{

    return HitTestRect(pt, D2D1::RectF(m_bounds.left + 118, m_bounds.top + Y_INVERT_LIGHT, m_bounds.left + 198, m_bounds.top + Y_INVERT_LIGHT + 22));

}



bool ShortcutEditForm::HitTestInvertDarkCheckbox(POINT pt)

{

    return HitTestRect(pt, D2D1::RectF(m_bounds.left + 204, m_bounds.top + Y_INVERT_DARK, m_bounds.left + 275, m_bounds.top + Y_INVERT_DARK + 22));

}



// 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€

//  Event Handlers

// 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€

void ShortcutEditForm::OnMouseMove(HWND hWnd, POINT pt, float scale, bool& repaint)

{

    POINT rawPt{ (int)(pt.x * scale), (int)(pt.y * scale) };

    m_nameBox.OnMouseMove(hWnd, rawPt, scale, repaint);

    m_targetBox.OnMouseMove(hWnd, rawPt, scale, repaint);

    m_iconBox.OnMouseMove(hWnd, rawPt, scale, repaint);

    m_argsBox.OnMouseMove(hWnd, rawPt, scale, repaint);

    m_workdirBox.OnMouseMove(hWnd, rawPt, scale, repaint);



    auto update = [&](bool& flag, bool newVal){ if (flag != newVal){ flag = newVal; repaint = true; } };

    update(m_hoveredBrowseTarget,  HitTestBrowseTargetButton(pt));

    update(m_hoveredBrowseIcon,    HitTestBrowseIconButton(pt));

    update(m_hoveredBrowseWorkdir, HitTestBrowseWorkdirButton(pt));

    update(m_hoveredRunAsAdmin,    HitTestRunAsAdminCheckbox(pt));

    update(m_hoveredInvertLight,   HitTestInvertLightCheckbox(pt));

    update(m_hoveredInvertDark,    HitTestInvertDarkCheckbox(pt));

}



void ShortcutEditForm::OnLButtonDown(HWND hWnd, POINT pt, float scale, bool& repaint)

{

    POINT rawPt{ (int)(pt.x * scale), (int)(pt.y * scale) };



    if (HitTestBrowseTargetButton(pt))  { BrowseTargetFile(hWnd);  return; }

    if (HitTestBrowseIconButton(pt))    { BrowseIconFile(hWnd);    return; }

    if (HitTestBrowseWorkdirButton(pt)) { BrowseWorkingDir(hWnd);  return; }



    if (HitTestRunAsAdminCheckbox(pt))

    {

        m_runAsAdmin = !m_runAsAdmin;

        repaint = true;

        return;

    }

    if (HitTestInvertLightCheckbox(pt))

    {

        m_iconInvertLight = !m_iconInvertLight;

        repaint = true;

        return;

    }

    if (HitTestInvertDarkCheckbox(pt))

    {

        m_iconInvertDark = !m_iconInvertDark;

        repaint = true;

        return;

    }



    auto tryFocus = [&](TextBox& tb) -> bool {

        if (tb.HitTest(pt))

        {

            SetFocus(hWnd);

            if (m_focusedBox && m_focusedBox != &tb) m_focusedBox->SetFocus(false);

            m_focusedBox = &tb;

            tb.SetFocus(true);

            tb.OnLButtonDown(hWnd, rawPt, scale, repaint);

            return true;

        }

        return false;

    };



    if (tryFocus(m_nameBox))    return;

    if (tryFocus(m_targetBox))  return;

    if (tryFocus(m_iconBox))    return;

    if (tryFocus(m_argsBox))    return;

    if (tryFocus(m_workdirBox)) return;



    if (m_focusedBox) { m_focusedBox->SetFocus(false); m_focusedBox = nullptr; repaint = true; }

}



void ShortcutEditForm::OnLButtonUp(HWND hWnd, POINT pt, float scale, bool& repaint)

{

    POINT rawPt{ (int)(pt.x * scale), (int)(pt.y * scale) };

    m_nameBox.OnLButtonUp(hWnd, rawPt, scale, repaint);

    m_targetBox.OnLButtonUp(hWnd, rawPt, scale, repaint);

    m_iconBox.OnLButtonUp(hWnd, rawPt, scale, repaint);

    m_argsBox.OnLButtonUp(hWnd, rawPt, scale, repaint);

    m_workdirBox.OnLButtonUp(hWnd, rawPt, scale, repaint);

}



void ShortcutEditForm::OnChar(HWND hWnd, WPARAM wParam, bool& repaint)

{

    if (m_focusedBox)

    {

        m_focusedBox->OnChar(hWnd, wParam, repaint);



        if (m_focusedBox == &m_targetBox || m_focusedBox == &m_iconBox)

        {

            std::wstring tp = m_iconBox.GetText();

            if (tp.empty()) tp = m_targetBox.GetText();



            if (m_previewIcon)  { DestroyIcon(m_previewIcon);   m_previewIcon = nullptr; }

            if (m_previewBitmap){ m_previewBitmap->Release(); m_previewBitmap = nullptr; }

            if (!tp.empty())

                m_previewIcon = GetFileIconForPreview(tp);

            repaint = true;

        }

    }

}



void ShortcutEditForm::OnKeyDown(HWND hWnd, WPARAM wParam, LPARAM lParam, bool& repaint)

{

    if (wParam == VK_TAB)

    {

        TextBox* order[] = { &m_nameBox, &m_targetBox, &m_iconBox, &m_argsBox, &m_workdirBox };

        const int N = 5;

        for (int i = 0; i < N; i++)

        {

            if (m_focusedBox == order[i])

            {

                m_focusedBox->SetFocus(false);

                bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

                m_focusedBox = order[(i + (shift ? N - 1 : 1)) % N];

                m_focusedBox->SetFocus(true);

                repaint = true;

                return;

            }

        }

        return;

    }



    if (m_focusedBox)

    {

        m_focusedBox->OnKeyDown(hWnd, wParam, lParam, repaint);

    }

}



void ShortcutEditForm::BlinkCaret()

{

    m_nameBox.BlinkCaret();

    m_targetBox.BlinkCaret();

    m_iconBox.BlinkCaret();

    m_argsBox.BlinkCaret();

    m_workdirBox.BlinkCaret();

}



bool ShortcutEditForm::IsInputFocused() const

{

    return m_focusedBox != nullptr;

}



void ShortcutEditForm::ResetFocus()

{

    if (m_focusedBox)

    {

        m_focusedBox->SetFocus(false);

        m_focusedBox = nullptr;

    }

}



ShortcutEditFormResult ShortcutEditForm::GetResult() const

{

    ShortcutEditFormResult res;

    res.name            = m_nameBox.GetText();

    res.targetPath      = m_targetBox.GetText();

    res.arguments       = m_argsBox.GetText();

    res.workingDir      = m_workdirBox.GetText();

    res.iconPath        = m_iconBox.GetText();

    res.runAsAdmin      = m_runAsAdmin;

    res.iconInvertLight = m_iconInvertLight;

    res.iconInvertDark  = m_iconInvertDark;

    return res;

}



bool ShortcutEditForm::Validate(HWND hWnd)

{

    std::wstring name = m_nameBox.GetText();

    std::wstring target = m_targetBox.GetText();



    while (!name.empty() && (name.back() == L' ')) name.pop_back();

    while (!target.empty() && (target.back() == L' ')) target.pop_back();



    if (name.empty()) { m_focusedBox = &m_nameBox; m_nameBox.SetFocus(true); InvalidateRect(hWnd, nullptr, FALSE); return false; }

    if (target.empty()) { m_focusedBox = &m_targetBox; m_targetBox.SetFocus(true); InvalidateRect(hWnd, nullptr, FALSE); return false; }



    return true;

}



// 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€

//  Button Actions

// 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€

void ShortcutEditForm::BrowseTargetFile(HWND hWnd)

{

    wchar_t fileBuf[MAX_PATH]{};

    OPENFILENAMEW ofn{};

    ofn.lStructSize  = sizeof(ofn);

    ofn.hwndOwner    = hWnd;

    ofn.lpstrFilter  = L"所有文件 (*.*)\0*.*\0应用程序 (*.exe)\0*.exe\0快捷方式 (*.lnk)\0*.lnk\0";

    ofn.lpstrFile    = fileBuf;

    ofn.nMaxFile     = MAX_PATH;

    ofn.Flags        = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;



    std::wstring cur = m_targetBox.GetText();

    if (!cur.empty() && cur.size() < MAX_PATH)

        wcscpy_s(fileBuf, cur.c_str());



    if (GetOpenFileNameW(&ofn))

    {

        std::wstring targetPath = fileBuf;

        m_targetBox.SetText(targetPath);



        if (m_nameBox.GetText().empty())

            AutoFillNameFromTarget(targetPath);



        if (m_iconBox.GetText().empty())

        {

            if (m_previewIcon)   { DestroyIcon(m_previewIcon);   m_previewIcon = nullptr; }

            if (m_previewBitmap) { m_previewBitmap->Release(); m_previewBitmap = nullptr; }

            m_previewIcon = GetFileIconForPreview(targetPath);

        }



        InvalidateRect(hWnd, nullptr, FALSE);

    }

}



void ShortcutEditForm::BrowseIconFile(HWND hWnd)

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

        m_previewIcon = GetFileIconForPreview(iconPath);



        InvalidateRect(hWnd, nullptr, FALSE);

    }

}



void ShortcutEditForm::BrowseWorkingDir(HWND hWnd)

{

    wchar_t dirBuf[MAX_PATH]{};



    std::wstring cur = m_workdirBox.GetText();

    if (!cur.empty() && cur.size() < MAX_PATH)

        wcscpy_s(dirBuf, cur.c_str());

    else

    {

        std::wstring target = m_targetBox.GetText();

        if (!target.empty())

        {

            wchar_t tmp[MAX_PATH]{};

            wcscpy_s(tmp, target.c_str());

            PathRemoveFileSpecW(tmp);

            wcscpy_s(dirBuf, tmp);

        }

    }



    BROWSEINFOW bi{};

    bi.hwndOwner      = hWnd;

    bi.pszDisplayName = dirBuf;

    bi.lpszTitle      = L"选择工作目录";

    bi.ulFlags        = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;



    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);

    if (pidl)

    {

        wchar_t path[MAX_PATH]{};

        if (SHGetPathFromIDListW(pidl, path))

            m_workdirBox.SetText(path);

        CoTaskMemFree(pidl);

        InvalidateRect(hWnd, nullptr, FALSE);

    }

}



void ShortcutEditForm::AutoFillNameFromTarget(const std::wstring& targetPath)

{

    wchar_t nameBuf[MAX_PATH]{};

    wcscpy_s(nameBuf, PathFindFileNameW(targetPath.c_str()));

    PathRemoveExtensionW(nameBuf);

    std::wstring name = nameBuf;

    if (name.size() > 6) name = name.substr(0, 6);

    m_nameBox.SetText(name);

}



// ─────────────────────────────────────────────────────────────────────────────

//  Drawing Helpers

// ─────────────────────────────────────────────────────────────────────────────

void ShortcutEditForm::DrawSectionLabel(ID2D1HwndRenderTarget* rt, const wchar_t* text, const D2D1_RECT_F& rect)

{

    if (!m_tfSmall) return;



    D2D1_COLOR_F sepClr = UIStyle::ThemeColor::TextNormal().d2d;

    sepClr.a = 0.28f;

    auto sepBrush = GetOrCreateBrush(rt, sepClr);

    if (sepBrush)

    {

        float midY = (rect.top + rect.bottom) * 0.5f;

        float lineStart = rect.left + (float)wcslen(text) * 8.5f + 10.0f;

        lineStart = std::min(lineStart, rect.right);

        rt->DrawLine(D2D1::Point2F(lineStart, midY),

                     D2D1::Point2F(rect.right, midY), sepBrush.Get(), 0.35f);

    }



    D2D1_COLOR_F labelClr = UIStyle::ThemeColor::TextNormal().d2d;

    labelClr.a = 0.5f;

    auto labelBrush = GetOrCreateBrush(rt, labelClr);

    if (labelBrush)

        rt->DrawTextW(text, (UINT32)wcslen(text), m_tfSmall.Get(), rect, labelBrush.Get());

}



void ShortcutEditForm::DrawButton(ID2D1HwndRenderTarget* rt, const wchar_t* text, const D2D1_RECT_F& rect, bool hovered)

{

    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(rect, 4.0f, 4.0f);



    bool isLight = (UIStyle::GetThemeMode() == UIStyle::ThemeMode::Light);

    D2D1_COLOR_F base = isLight ? D2D1::ColorF(0.f, 0.f, 0.f) : D2D1::ColorF(1.f, 1.f, 1.f);

    float bgA     = hovered ? 0.09f : 0.04f;

    float borderA = hovered ? 0.16f : 0.075f;



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



void ShortcutEditForm::DrawCheckbox(ID2D1HwndRenderTarget* rt, const D2D1_RECT_F& rect, bool checked, bool hovered, const wchar_t* labelText)

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

        float bgA     = hovered ? 0.06f : 0.03f;

        float borderA = hovered ? 0.20f : 0.12f;

        auto bgBrush = GetOrCreateBrush(rt, D2D1::ColorF(base.r, base.g, base.b, bgA));

        if (bgBrush) rt->FillRoundedRectangle(rrBox, bgBrush.Get());

        auto borderBrush = GetOrCreateBrush(rt, D2D1::ColorF(base.r, base.g, base.b, borderA));

        if (borderBrush) rt->DrawRoundedRectangle(rrBox, borderBrush.Get(), UIStyle::Metrics::ControlStroke());

    }



    if (m_tfLabel && labelText)

    {

        D2D1_RECT_F labelRect = D2D1::RectF(boxRect.right + 6, rect.top, rect.right, rect.bottom);

        D2D1_COLOR_F tc = UIStyle::ThemeColor::TextNormal().d2d;

        tc.a = hovered ? 1.0f : 0.85f;

        auto tb = GetOrCreateBrush(rt, tc);

        if (tb) rt->DrawTextW(labelText, (UINT32)wcslen(labelText), m_tfLabel.Get(), labelRect, tb.Get());

    }

}



void ShortcutEditForm::DrawIconPreview(ID2D1HwndRenderTarget* rt)

{

    if (m_previewIcon)

    {

        if (!m_previewBitmap)

        {

            auto bmp = IconRenderer::HicontoD2D(rt, m_previewIcon, 36);

            if (bmp)

            {

                m_previewBitmap = bmp.Detach();

            }

        }

    }

    else

    {

        std::wstring currentName = m_nameBox.GetText();

        if (!m_previewBitmap || m_lastPreviewName != currentName)

        {

            if (m_previewBitmap)

            {

                m_previewBitmap->Release();

                m_previewBitmap = nullptr;

            }

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

    D2D1_RECT_F previewRect = D2D1::RectF(m_bounds.left + W - 20 - previewSize, m_bounds.top + Y_PREVIEW, m_bounds.left + W - 20, m_bounds.top + Y_PREVIEW + previewSize);

    D2D1_ROUNDED_RECT rrPreview = D2D1::RoundedRect(previewRect, 6.0f, 6.0f);



    bool isLight = (UIStyle::GetThemeMode() == UIStyle::ThemeMode::Light);

    D2D1_COLOR_F bgClr = isLight ? D2D1::ColorF(0.f, 0.f, 0.f, 0.05f) : D2D1::ColorF(1.f, 1.f, 1.f, 0.07f);

    auto bgBrush = GetOrCreateBrush(rt, bgClr);

    if (bgBrush) rt->FillRoundedRectangle(rrPreview, bgBrush.Get());



    D2D1_COLOR_F borderClr = isLight ? D2D1::ColorF(0.f, 0.f, 0.f, 0.10f) : D2D1::ColorF(1.f, 1.f, 1.f, 0.10f);

    auto borderBrush = GetOrCreateBrush(rt, borderClr);

    if (borderBrush) rt->DrawRoundedRectangle(rrPreview, borderBrush.Get(), UIStyle::Metrics::ControlStroke());



    if (m_previewBitmap)

    {

        ComPtr<ID2D1BitmapBrush> bmpBrush;

        rt->CreateBitmapBrush(m_previewBitmap, &bmpBrush);

        if (bmpBrush)

        {

            bmpBrush->SetExtendModeX(D2D1_EXTEND_MODE_CLAMP);

            bmpBrush->SetExtendModeY(D2D1_EXTEND_MODE_CLAMP);

            bmpBrush->SetTransform(D2D1::Matrix3x2F::Translation(previewRect.left, previewRect.top));

            rt->FillRoundedRectangle(rrPreview, bmpBrush.Get());

        }

    }

}



HICON ShortcutEditForm::GetFileIconForPreview(const std::wstring& path)

{

    if (path.empty()) return nullptr;

    return ShortcutManager::GetShortcutIcon(path);

}



void ShortcutEditForm::Paint(ID2D1HwndRenderTarget* rt, float scale)

{

    float W = m_bounds.right - m_bounds.left;



    // 鈹€鈹€ Section: 鍩烘湰淇℃伅 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€

    const wchar_t* basicSecText = L"基本信息";

    DrawSectionLabel(rt, basicSecText, D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_SEC_BASIC, m_bounds.left + W - 20, m_bounds.top + Y_SEC_BASIC + 12));



    // Label: 鍚嶇О

    if (m_tfLabel)

    {

        D2D1_COLOR_F lc = UIStyle::ThemeColor::TextNormal().d2d; lc.a = 0.75f;

        auto lb = GetOrCreateBrush(rt, lc);

        const wchar_t* lblText = L"名称 (最多6个字符)";

        if (lb) rt->DrawTextW(lblText, (UINT32)wcslen(lblText), m_tfLabel.Get(),

                              D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_LBL_NAME, m_bounds.left + 200, m_bounds.top + Y_LBL_NAME + 14), lb.Get());

    }

    m_nameBox.Paint(rt, scale);



    // Label: 鐩爣

    if (m_tfLabel)

    {

        D2D1_COLOR_F lc = UIStyle::ThemeColor::TextNormal().d2d; lc.a = 0.75f;

        auto lb = GetOrCreateBrush(rt, lc);

        const wchar_t* lblText = L"目标路径";

        if (lb) rt->DrawTextW(lblText, (UINT32)wcslen(lblText), m_tfLabel.Get(),

                              D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_LBL_TARGET, m_bounds.left + 200, m_bounds.top + Y_LBL_TARGET + 14), lb.Get());

    }

    m_targetBox.Paint(rt, scale);

    DrawButton(rt, L"浏览...", D2D1::RectF(m_bounds.left + W - 80, m_bounds.top + Y_BOX_TARGET, m_bounds.left + W - 20, m_bounds.top + Y_BOX_TARGET + 24),

               m_hoveredBrowseTarget);



    // Label: 鑷畾涔夊浘鏍?
    if (m_tfLabel)

    {

        D2D1_COLOR_F lc = UIStyle::ThemeColor::TextNormal().d2d; lc.a = 0.75f;

        auto lb = GetOrCreateBrush(rt, lc);

        const wchar_t* lblText = L"自定义图标路径 (可选)";

        if (lb) rt->DrawTextW(lblText, (UINT32)wcslen(lblText), m_tfLabel.Get(),

                              D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_LBL_ICON, m_bounds.left + 200, m_bounds.top + Y_LBL_ICON + 14), lb.Get());

    }

    m_iconBox.Paint(rt, scale);

    DrawButton(rt, L"浏览...", D2D1::RectF(m_bounds.left + W - 80, m_bounds.top + Y_BOX_ICON, m_bounds.left + W - 20, m_bounds.top + Y_BOX_ICON + 24),

               m_hoveredBrowseIcon);



    // Label: 鍙傛暟

    if (m_tfLabel)

    {

        D2D1_COLOR_F lc = UIStyle::ThemeColor::TextNormal().d2d; lc.a = 0.75f;

        auto lb = GetOrCreateBrush(rt, lc);

        const wchar_t* lblText = L"启动参数 (可选)";

        if (lb) rt->DrawTextW(lblText, (UINT32)wcslen(lblText), m_tfLabel.Get(),

                              D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_LBL_ARGS, m_bounds.left + 200, m_bounds.top + Y_LBL_ARGS + 14), lb.Get());

    }

    m_argsBox.Paint(rt, scale);



    // 鈹€鈹€ Section: 楂樼骇閫夐」 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€

    const wchar_t* advSecText = L"高级选项";

    DrawSectionLabel(rt, advSecText, D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_SEC_ADV, m_bounds.left + W - 20, m_bounds.top + Y_SEC_ADV + 12));



    // Label: 宸ヤ綔鐩綍

    if (m_tfLabel)

    {

        D2D1_COLOR_F lc = UIStyle::ThemeColor::TextNormal().d2d; lc.a = 0.75f;

        auto lb = GetOrCreateBrush(rt, lc);

        const wchar_t* lblText = L"工作目录 (可选)";

        if (lb) rt->DrawTextW(lblText, (UINT32)wcslen(lblText), m_tfLabel.Get(),

                              D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_LBL_WDIR, m_bounds.left + 200, m_bounds.top + Y_LBL_WDIR + 14), lb.Get());

    }

    m_workdirBox.Paint(rt, scale);

    DrawButton(rt, L"浏览...", D2D1::RectF(m_bounds.left + W - 80, m_bounds.top + Y_BOX_WDIR, m_bounds.left + W - 20, m_bounds.top + Y_BOX_WDIR + 24),

               m_hoveredBrowseWorkdir);



    // Checkboxes

    DrawCheckbox(rt, D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_ADMIN_CB, m_bounds.left + 112, m_bounds.top + Y_ADMIN_CB + 22), m_runAsAdmin, m_hoveredRunAsAdmin, L"管理员运行");



    DrawCheckbox(rt, D2D1::RectF(m_bounds.left + 118, m_bounds.top + Y_INVERT_LIGHT, m_bounds.left + 198, m_bounds.top + Y_INVERT_LIGHT + 22), m_iconInvertLight, m_hoveredInvertLight, L"浅色反转");

    DrawCheckbox(rt, D2D1::RectF(m_bounds.left + 204, m_bounds.top + Y_INVERT_DARK, m_bounds.left + 275, m_bounds.top + Y_INVERT_DARK + 22), m_iconInvertDark, m_hoveredInvertDark, L"深色反转");



    // Icon preview (right side)

    DrawIconPreview(rt);

}
