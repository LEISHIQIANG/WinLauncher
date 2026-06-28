#define NOMINMAX
#include "HotkeyEditForm.h"
#include "UIStyle.h"
#include "../DpiHelper.h"
#include "../ShortcutManager.h"
#include "../KeyboardHook.h"
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

struct HotkeyBrushCacheEntry
{
    D2D1_COLOR_F color;
    ComPtr<ID2D1SolidColorBrush> brush;
};
static std::vector<HotkeyBrushCacheEntry> g_hotkeyBrushCache;

static ComPtr<ID2D1SolidColorBrush> GetOrCreateBrush(ID2D1HwndRenderTarget* rt, const D2D1_COLOR_F& color)
{
    for (auto& entry : g_hotkeyBrushCache)
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
            g_hotkeyBrushCache.push_back({ color, brush });
        }
    }
    return brush;
}

// Layout Y coordinates relative to m_bounds.top
static const float Y_SEC_BASIC     = 0.0f;
static const float Y_LBL_NAME      = 16.0f;
static const float Y_BOX_NAME      = 32.0f;
static const float Y_LBL_HOTKEY    = 60.0f;
static const float Y_BOX_HOTKEY    = 76.0f;
static const float Y_LBL_ICON      = 104.0f;
static const float Y_BOX_ICON      = 120.0f;
static const float Y_SEC_ADV       = 148.0f;
static const float Y_AFTERCLOSE_CB = 164.0f; // trigger mode checkbox
static const float Y_INVERT_LIGHT  = Y_AFTERCLOSE_CB; // light invert checkbox
static const float Y_INVERT_DARK   = Y_AFTERCLOSE_CB; // dark invert checkbox
static const float Y_PREVIEW       = 174.0f; // preview on the right (x range W - 70 to W - 20)

static std::wstring GetKeyName(WPARAM vk)
{
    // Letters and digits
    if (vk >= 'A' && vk <= 'Z') return std::wstring(1, (wchar_t)vk);
    if (vk >= '0' && vk <= '9') return std::wstring(1, (wchar_t)vk);
    // Function keys F1-F24
    if (vk >= VK_F1 && vk <= VK_F12) return L"F" + std::to_wstring(vk - VK_F1 + 1);
    if (vk >= VK_F13 && vk <= VK_F24) return L"F" + std::to_wstring(vk - VK_F1 + 1);

    switch (vk)
    {
    // Navigation
    case VK_SPACE:    return L"Space";
    case VK_RETURN:   return L"Enter";
    case VK_TAB:      return L"Tab";
    case VK_ESCAPE:   return L"Esc";
    case VK_BACK:     return L"Backspace";
    case VK_INSERT:   return L"Insert";
    case VK_DELETE:   return L"Delete";
    case VK_HOME:     return L"Home";
    case VK_END:      return L"End";
    case VK_PRIOR:    return L"PageUp";
    case VK_NEXT:     return L"PageDown";
    case VK_UP:       return L"Up";
    case VK_DOWN:     return L"Down";
    case VK_LEFT:     return L"Left";
    case VK_RIGHT:    return L"Right";
    case VK_SNAPSHOT: return L"PrintScrn";
    case VK_PAUSE:    return L"Pause";
    case VK_SCROLL:   return L"ScrollLock";
    case VK_NUMLOCK:  return L"NumLock";
    case VK_CAPITAL:  return L"CapsLock";
    // Numpad
    case VK_NUMPAD0:  return L"Num0";
    case VK_NUMPAD1:  return L"Num1";
    case VK_NUMPAD2:  return L"Num2";
    case VK_NUMPAD3:  return L"Num3";
    case VK_NUMPAD4:  return L"Num4";
    case VK_NUMPAD5:  return L"Num5";
    case VK_NUMPAD6:  return L"Num6";
    case VK_NUMPAD7:  return L"Num7";
    case VK_NUMPAD8:  return L"Num8";
    case VK_NUMPAD9:  return L"Num9";
    case VK_MULTIPLY: return L"Num*";
    case VK_ADD:      return L"Num+";
    case VK_SUBTRACT: return L"Num-";
    case VK_DECIMAL:  return L"Num.";
    case VK_DIVIDE:   return L"Num/";
    // Symbol keys (US layout)
    case 0xBA:        return L";";
    case 0xBB:        return L"=";
    case 0xBC:        return L",";
    case 0xBD:        return L"-";
    case 0xBE:        return L".";
    case 0xBF:        return L"/";
    case 0xC0:        return L"`";
    case 0xDB:        return L"[";
    case 0xDC:        return L"\\";
    case 0xDD:        return L"]";
    case 0xDE:        return L"'";
    // Media keys
    case VK_VOLUME_MUTE:   return L"VolMute";
    case VK_VOLUME_DOWN:   return L"VolDown";
    case VK_VOLUME_UP:     return L"VolUp";
    case VK_MEDIA_NEXT_TRACK: return L"MediaNext";
    case VK_MEDIA_PREV_TRACK: return L"MediaPrev";
    case VK_MEDIA_STOP:       return L"MediaStop";
    case VK_MEDIA_PLAY_PAUSE: return L"MediaPlay";
    // Browser/app keys
    case VK_BROWSER_BACK:     return L"BrowserBack";
    case VK_BROWSER_FORWARD:  return L"BrowserFwd";
    case VK_BROWSER_REFRESH:  return L"BrowserRefresh";
    case VK_BROWSER_SEARCH:   return L"BrowserSearch";
    }
    return L"";
}

HotkeyEditForm::HotkeyEditForm()
{
}

HotkeyEditForm::~HotkeyEditForm()
{
    Destroy();
}

bool HotkeyEditForm::Create(HWND parentHWND, IDWriteFactory* dwriteFactory, const D2D1_RECT_F& logicalBounds, const HotkeyEditFormInitParams& init)
{
    m_parentHWND = parentHWND;
    m_bounds = logicalBounds;
    m_init = init;
    m_afterClose = init.afterClose;
    m_iconInvertLight = init.iconInvertLight;
    m_iconInvertDark = init.iconInvertDark;

    UIStyle::TextBoxStyle style;
    style.fontSize    = 11;
    style.paddingTop  = 4.0f;
    style.paddingBottom = 4.0f;

    EnsureFonts(dwriteFactory);

    float W = m_bounds.right - m_bounds.left;

    // Name Box
    m_nameBox.SetStyle(style);
    m_nameBox.Create(parentHWND, dwriteFactory, D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_NAME, m_bounds.left + W - 20, m_bounds.top + Y_BOX_NAME + 24), m_init.name);

    // Hotkey Box (Read-only, customized recording behavior)
    m_hotkeyBox.SetStyle(style);
    m_hotkeyBox.Create(parentHWND, dwriteFactory, D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_HOTKEY, m_bounds.left + W - 145, m_bounds.top + Y_BOX_HOTKEY + 24), m_init.hotkey);

    // Icon Box
    m_iconBox.SetStyle(style);
    m_iconBox.Create(parentHWND, dwriteFactory, D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_ICON, m_bounds.left + W - 85, m_bounds.top + Y_BOX_ICON + 24), m_init.iconPath);

    m_focusedBox = &m_nameBox;
    m_nameBox.SetFocus(true);

    if (!m_init.iconPath.empty())
    {
        m_previewIcon = GetFileIconForPreview(m_init.iconPath);
    }

    return true;
}

void HotkeyEditForm::Destroy()
{
    m_nameBox.Destroy();
    m_hotkeyBox.Destroy();
    m_iconBox.Destroy();

    if (m_previewBitmap) { m_previewBitmap->Release(); m_previewBitmap = nullptr; }
    if (m_previewIcon)   { DestroyIcon(m_previewIcon); m_previewIcon = nullptr; }

    g_hotkeyBrushCache.clear();
}

void HotkeyEditForm::UpdateLayout(const D2D1_RECT_F& logicalBounds, float scale)
{
    m_bounds = logicalBounds;
    float W = m_bounds.right - m_bounds.left;

    m_nameBox.SetBounds(D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_NAME, m_bounds.left + W - 20, m_bounds.top + Y_BOX_NAME + 24));
    m_nameBox.UpdateLayout(scale);

    m_hotkeyBox.SetBounds(D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_HOTKEY, m_bounds.left + W - 145, m_bounds.top + Y_BOX_HOTKEY + 24));
    m_hotkeyBox.UpdateLayout(scale);

    m_iconBox.SetBounds(D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_ICON, m_bounds.left + W - 85, m_bounds.top + Y_BOX_ICON + 24));
    m_iconBox.UpdateLayout(scale);
}

void HotkeyEditForm::EnsureFonts(IDWriteFactory* dwriteFactory)
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

bool HotkeyEditForm::HitTestRect(POINT pt, const D2D1_RECT_F& rect)
{
    return (pt.x >= rect.left && pt.x <= rect.right && pt.y >= rect.top && pt.y <= rect.bottom);
}

bool HotkeyEditForm::HitTestRecordButton(POINT pt)
{
    float W = m_bounds.right - m_bounds.left;
    return HitTestRect(pt, D2D1::RectF(m_bounds.left + W - 135, m_bounds.top + Y_BOX_HOTKEY, m_bounds.left + W - 80, m_bounds.top + Y_BOX_HOTKEY + 24));
}

bool HotkeyEditForm::HitTestClearButton(POINT pt)
{
    float W = m_bounds.right - m_bounds.left;
    return HitTestRect(pt, D2D1::RectF(m_bounds.left + W - 75, m_bounds.top + Y_BOX_HOTKEY, m_bounds.left + W - 20, m_bounds.top + Y_BOX_HOTKEY + 24));
}

bool HotkeyEditForm::HitTestBrowseIconButton(POINT pt)
{
    float W = m_bounds.right - m_bounds.left;
    return HitTestRect(pt, D2D1::RectF(m_bounds.left + W - 80, m_bounds.top + Y_BOX_ICON, m_bounds.left + W - 20, m_bounds.top + Y_BOX_ICON + 24));
}

bool HotkeyEditForm::HitTestAfterCloseCheckbox(POINT pt)
{
    return HitTestRect(pt, D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_AFTERCLOSE_CB, m_bounds.left + 138, m_bounds.top + Y_AFTERCLOSE_CB + 22));
}

bool HotkeyEditForm::HitTestInvertLightCheckbox(POINT pt)
{
    return HitTestRect(pt, D2D1::RectF(m_bounds.left + 144, m_bounds.top + Y_INVERT_LIGHT, m_bounds.left + 209, m_bounds.top + Y_INVERT_LIGHT + 22));
}

bool HotkeyEditForm::HitTestInvertDarkCheckbox(POINT pt)
{
    return HitTestRect(pt, D2D1::RectF(m_bounds.left + 215, m_bounds.top + Y_INVERT_DARK, m_bounds.left + 275, m_bounds.top + Y_INVERT_DARK + 22));
}

void HotkeyEditForm::OnMouseMove(HWND hWnd, POINT pt, float scale, bool& repaint)
{
    POINT rawPt{ (int)(pt.x * scale), (int)(pt.y * scale) };
    if (!m_recording)
    {
        m_nameBox.OnMouseMove(hWnd, rawPt, scale, repaint);
        m_hotkeyBox.OnMouseMove(hWnd, rawPt, scale, repaint);
        m_iconBox.OnMouseMove(hWnd, rawPt, scale, repaint);
    }

    auto update = [&](bool& flag, bool newVal){ if (flag != newVal){ flag = newVal; repaint = true; } };
    update(m_hoveredRecord,       HitTestRecordButton(pt));
    update(m_hoveredClear,        HitTestClearButton(pt));
    update(m_hoveredBrowseIcon,    HitTestBrowseIconButton(pt));
    update(m_hoveredAfterClose,    HitTestAfterCloseCheckbox(pt));
    update(m_hoveredInvertLight,   HitTestInvertLightCheckbox(pt));
    update(m_hoveredInvertDark,    HitTestInvertDarkCheckbox(pt));
}

void HotkeyEditForm::OnLButtonDown(HWND hWnd, POINT pt, float scale, bool& repaint)
{
    POINT rawPt{ (int)(pt.x * scale), (int)(pt.y * scale) };

    if (HitTestRecordButton(pt))
    {
        if (m_recording) StopRecording(hWnd);
        else StartRecording(hWnd);
        repaint = true;
        return;
    }
    if (HitTestClearButton(pt))
    {
        ClearHotkey();
        repaint = true;
        return;
    }
    if (HitTestBrowseIconButton(pt))    { BrowseIconFile(hWnd);    return; }

    if (HitTestAfterCloseCheckbox(pt))
    {
        m_afterClose = !m_afterClose;
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

    if (m_recording)
    {
        // While recording, mouse interaction with text boxes must not move focus
        // to an editable field. Otherwise the captured key can leak into it once
        // recording completes.
        if (m_nameBox.HitTest(pt) || m_hotkeyBox.HitTest(pt) || m_iconBox.HitTest(pt))
        {
            repaint = true;
            return;
        }
    }

    auto tryFocus = [&](TextBox& tb) -> bool {
        if (tb.HitTest(pt))
        {
            if (m_focusedBox && m_focusedBox != &tb) m_focusedBox->SetFocus(false);
            m_focusedBox = &tb;
            tb.SetFocus(true);
            tb.OnLButtonDown(hWnd, rawPt, scale, repaint);
            return true;
        }
        return false;
    };

    if (tryFocus(m_nameBox))    return;
    if (tryFocus(m_hotkeyBox))  return;
    if (tryFocus(m_iconBox))    return;

    if (m_focusedBox) { m_focusedBox->SetFocus(false); m_focusedBox = nullptr; repaint = true; }
}

void HotkeyEditForm::OnLButtonUp(HWND hWnd, POINT pt, float scale, bool& repaint)
{
    if (m_recording)
    {
        return;
    }

    POINT rawPt{ (int)(pt.x * scale), (int)(pt.y * scale) };
    m_nameBox.OnLButtonUp(hWnd, rawPt, scale, repaint);
    m_hotkeyBox.OnLButtonUp(hWnd, rawPt, scale, repaint);
    m_iconBox.OnLButtonUp(hWnd, rawPt, scale, repaint);
}

void HotkeyEditForm::OnChar(HWND hWnd, WPARAM wParam, bool& repaint)
{
    if (m_recording)
    {
        // Don't input standard character when recording hotkey
        return;
    }

    if (m_focusedBox && m_focusedBox != &m_hotkeyBox)
    {
        m_focusedBox->OnChar(hWnd, wParam, repaint);

        if (m_focusedBox == &m_iconBox)
        {
            std::wstring tp = m_iconBox.GetText();
            if (m_previewIcon)  { DestroyIcon(m_previewIcon);   m_previewIcon = nullptr; }
            if (m_previewBitmap){ m_previewBitmap->Release(); m_previewBitmap = nullptr; }
            if (!tp.empty())
                m_previewIcon = GetFileIconForPreview(tp);
            repaint = true;
        }
    }
}

// ---------------------------------------------------------------------------
// HandleHookMessage
// Processes KeyboardHookMsg messages posted by KeyboardHook and updates the
// displayed hotkey string.  Called from HotkeyDialog::HandleMessage.
// ---------------------------------------------------------------------------
bool HotkeyEditForm::HandleHookMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, bool& repaint)
{
    if (uMsg == KeyboardHookMsg::KeyCaptured)
    {
        if (!m_recording) return false;

        // Build current modifier prefix for display
        DWORD mods = (DWORD)lParam;
        std::wstring modStr;
        if (mods & RecordModifiers::Ctrl)  modStr += L"Ctrl + ";
        if (mods & RecordModifiers::Alt)   modStr += L"Alt + ";
        if (mods & RecordModifiers::Shift) modStr += L"Shift + ";
        if (mods & RecordModifiers::Win)   modStr += L"Win + ";

        DWORD vk = (DWORD)wParam;
        if (vk == 0)
        {
            // Modifier-only update: show preview
            m_hotkeyBox.SetText(modStr.empty() ? L"\u5f55\u5236\u4e2d..." : modStr + L"...");
        }
        else
        {
            std::wstring keyName = GetKeyName(vk);
            if (!keyName.empty())
            {
                m_hotkeyBox.SetText(modStr + keyName);
                m_recordingCapturedHotkey = true;
            }
        }
        repaint = true;
        return true;
    }

    if (uMsg == KeyboardHookMsg::ChordComplete)
    {
        if (!m_recording) return false;

        if (!m_recordingCapturedHotkey)
        {
            m_hotkeyBox.SetText(m_hotkeyBeforeRecording);
        }
        m_recording = false;
        m_recordingCapturedHotkey = false;
        m_hotkeyBeforeRecording.clear();
        m_hotkeyBox.SetFocus(false);
        m_focusedBox = nullptr;
        repaint = true;
        return true;
    }

    return false;
}

void HotkeyEditForm::OnKeyDown(HWND hWnd, WPARAM wParam, LPARAM lParam, bool& repaint)
{
    if (m_recording)
    {
        // Recording input is owned by KeyboardHook. The focused text boxes stay
        // visually inert here so normal WM_KEYDOWN/WM_CHAR cannot edit them.
        repaint = true;
        return;
    }

    if (wParam == VK_TAB)
    {
        TextBox* order[] = { &m_nameBox, &m_hotkeyBox, &m_iconBox };
        const int N = 3;
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

void HotkeyEditForm::BlinkCaret()
{
    m_nameBox.BlinkCaret();
    m_hotkeyBox.BlinkCaret();
    m_iconBox.BlinkCaret();
}

bool HotkeyEditForm::IsInputFocused() const
{
    return m_focusedBox != nullptr || m_recording;
}

void HotkeyEditForm::ResetFocus()
{
    if (m_recording)
    {
        StopRecording(m_parentHWND);
    }
    if (m_focusedBox)
    {
        m_focusedBox->SetFocus(false);
        m_focusedBox = nullptr;
    }
}

HotkeyEditFormResult HotkeyEditForm::GetResult() const
{
    HotkeyEditFormResult res;
    res.name = m_nameBox.GetText();
    res.hotkey = m_hotkeyBox.GetText();
    res.iconPath = m_iconBox.GetText();
    res.afterClose = m_afterClose;
    res.iconInvertLight = m_iconInvertLight;
    res.iconInvertDark = m_iconInvertDark;
    return res;
}

bool HotkeyEditForm::Validate(HWND hWnd)
{
    std::wstring name = m_nameBox.GetText();
    std::wstring hotkey = m_hotkeyBox.GetText();

    if (name.empty())
    {
        MessageBoxW(hWnd, L"请输入名称！", L"验证失败", MB_OK | MB_ICONWARNING);
        return false;
    }
    if (hotkey.empty())
    {
        MessageBoxW(hWnd, L"请设置快捷键组合！", L"验证失败", MB_OK | MB_ICONWARNING);
        return false;
    }
    return true;
}

void HotkeyEditForm::StartRecording(HWND hWnd)
{
    m_hotkeyBeforeRecording = m_hotkeyBox.GetText();
    m_recordingCapturedHotkey = false;

    if (!KeyboardHook::IsInstalled() && !KeyboardHook::Install())
    {
        m_hotkeyBeforeRecording.clear();
        return;
    }

    m_hotkeyBox.SetText(L"\u5f55\u5236\u4e2d\uff0c\u8bf7\u6309\u4e0b\u7ec4\u5408\u952e...");
    m_nameBox.SetFocus(false);
    m_hotkeyBox.SetFocus(false);
    m_iconBox.SetFocus(false);
    m_focusedBox = nullptr;
    m_recording = true;

    if (!KeyboardHook::StartRecording(hWnd, 10000))
    {
        m_recording = false;
        m_hotkeyBox.SetText(m_hotkeyBeforeRecording);
        m_hotkeyBeforeRecording.clear();
    }
}

void HotkeyEditForm::StopRecording(HWND hWnd)
{
    if (m_recording && !m_recordingCapturedHotkey)
    {
        m_hotkeyBox.SetText(m_hotkeyBeforeRecording);
    }
    m_recording = false;
    m_recordingCapturedHotkey = false;
    m_hotkeyBeforeRecording.clear();
    if (KeyboardHook::IsRecording())
        KeyboardHook::StopRecording();
}

void HotkeyEditForm::ClearHotkey()
{
    if (m_recording && KeyboardHook::IsRecording())
        KeyboardHook::StopRecording();
    m_hotkeyBox.SetText(L"");
    m_recording = false;
    m_recordingCapturedHotkey = false;
    m_hotkeyBeforeRecording.clear();
}

void HotkeyEditForm::BrowseIconFile(HWND hWnd)
{
    wchar_t filename[MAX_PATH]{};
    OPENFILENAMEW ofn{ sizeof(ofn) };
    ofn.hwndOwner = hWnd;
    ofn.lpstrFilter = L"图标文件 (*.ico;*.exe;*.dll;*.png)\0*.ico;*.exe;*.dll;*.png\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&ofn))
    {
        m_iconBox.SetText(filename);
        if (m_previewIcon)   { DestroyIcon(m_previewIcon);   m_previewIcon = nullptr; }
        if (m_previewBitmap) { m_previewBitmap->Release(); m_previewBitmap = nullptr; }
        m_previewIcon = GetFileIconForPreview(filename);
    }
}

void HotkeyEditForm::Paint(ID2D1HwndRenderTarget* rt, float scale)
{
    float W = m_bounds.right - m_bounds.left;

    // "基本信息" Separator
    DrawSectionLabel(rt, L"基本信息", D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_SEC_BASIC, m_bounds.left + W - 20, m_bounds.top + Y_SEC_BASIC + 16));

    // Name Label
    if (m_tfLabel)
    {
        auto lblBrush = GetOrCreateBrush(rt, UIStyle::ThemeColor::TextMuted().d2d);
        if (lblBrush) rt->DrawTextW(L"名称 (最多6个字符)", 11, m_tfLabel.Get(), D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_LBL_NAME, m_bounds.left + W - 20, m_bounds.top + Y_LBL_NAME + 16), lblBrush.Get());
    }
    m_nameBox.Paint(rt, scale);

    // Hotkey Label
    if (m_tfLabel)
    {
        auto lblBrush = GetOrCreateBrush(rt, UIStyle::ThemeColor::TextMuted().d2d);
        if (lblBrush) rt->DrawTextW(L"快捷键组合", 5, m_tfLabel.Get(), D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_LBL_HOTKEY, m_bounds.left + W - 20, m_bounds.top + Y_LBL_HOTKEY + 16), lblBrush.Get());
    }
    m_hotkeyBox.Paint(rt, scale);

    // Record and Clear Buttons
    DrawButton(rt, m_recording ? L"停止" : L"录制", D2D1::RectF(m_bounds.left + W - 135, m_bounds.top + Y_BOX_HOTKEY, m_bounds.left + W - 80, m_bounds.top + Y_BOX_HOTKEY + 24), m_hoveredRecord);
    DrawButton(rt, L"清空", D2D1::RectF(m_bounds.left + W - 75, m_bounds.top + Y_BOX_HOTKEY, m_bounds.left + W - 20, m_bounds.top + Y_BOX_HOTKEY + 24), m_hoveredClear);

    // Custom Icon Label
    if (m_tfLabel)
    {
        auto lblBrush = GetOrCreateBrush(rt, UIStyle::ThemeColor::TextMuted().d2d);
        if (lblBrush) rt->DrawTextW(L"自定义图标路径 (可选)", 13, m_tfLabel.Get(), D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_LBL_ICON, m_bounds.left + W - 20, m_bounds.top + Y_LBL_ICON + 16), lblBrush.Get());
    }
    m_iconBox.Paint(rt, scale);
    DrawButton(rt, L"浏览...", D2D1::RectF(m_bounds.left + W - 80, m_bounds.top + Y_BOX_ICON, m_bounds.left + W - 20, m_bounds.top + Y_BOX_ICON + 24), m_hoveredBrowseIcon);

    // "高级选项" Separator
    DrawSectionLabel(rt, L"高级选项", D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_SEC_ADV, m_bounds.left + W - 20, m_bounds.top + Y_SEC_ADV + 16));

    // Checkboxes
    DrawCheckbox(rt, D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_AFTERCLOSE_CB, m_bounds.left + 138, m_bounds.top + Y_AFTERCLOSE_CB + 22), m_afterClose, m_hoveredAfterClose);
    if (m_tfLabel)
    {
        auto txtBrush = GetOrCreateBrush(rt, UIStyle::ThemeColor::TextNormal().d2d);
        if (txtBrush) rt->DrawTextW(L"窗口关闭触发", 6, m_tfLabel.Get(), D2D1::RectF(m_bounds.left + 45, m_bounds.top + Y_AFTERCLOSE_CB, m_bounds.left + 138, m_bounds.top + Y_AFTERCLOSE_CB + 22), txtBrush.Get());
    }

    DrawCheckbox(rt, D2D1::RectF(m_bounds.left + 144, m_bounds.top + Y_INVERT_LIGHT, m_bounds.left + 209, m_bounds.top + Y_INVERT_LIGHT + 22), m_iconInvertLight, m_hoveredInvertLight);
    if (m_tfLabel)
    {
        auto txtBrush = GetOrCreateBrush(rt, UIStyle::ThemeColor::TextNormal().d2d);
        if (txtBrush) rt->DrawTextW(L"浅色反转", 4, m_tfLabel.Get(), D2D1::RectF(m_bounds.left + 164, m_bounds.top + Y_INVERT_LIGHT, m_bounds.left + 209, m_bounds.top + Y_INVERT_LIGHT + 22), txtBrush.Get());
    }

    DrawCheckbox(rt, D2D1::RectF(m_bounds.left + 215, m_bounds.top + Y_INVERT_DARK, m_bounds.left + 275, m_bounds.top + Y_INVERT_DARK + 22), m_iconInvertDark, m_hoveredInvertDark);
    if (m_tfLabel)
    {
        auto txtBrush = GetOrCreateBrush(rt, UIStyle::ThemeColor::TextNormal().d2d);
        if (txtBrush) rt->DrawTextW(L"深色反转", 4, m_tfLabel.Get(), D2D1::RectF(m_bounds.left + 235, m_bounds.top + Y_INVERT_DARK, m_bounds.left + 275, m_bounds.top + Y_INVERT_DARK + 22), txtBrush.Get());
    }

    // Draw preview icon
    DrawIconPreview(rt);
}

void HotkeyEditForm::DrawSectionLabel(ID2D1HwndRenderTarget* rt, const wchar_t* text, const D2D1_RECT_F& rect)
{
    if (m_tfSmall)
    {
        auto labelBrush = GetOrCreateBrush(rt, UIStyle::ThemeColor::TextMuted().d2d);
        if (labelBrush)
        {
            rt->DrawTextW(text, (UINT32)wcslen(text), m_tfSmall.Get(), rect, labelBrush.Get());
            float lineStart = rect.left + (float)wcslen(text) * 8.5f + 10.0f;
            lineStart = std::min(lineStart, rect.right);
            D2D1_COLOR_F lineClr = UIStyle::ThemeColor::TextNormal().d2d;
            lineClr.a = 0.28f;
            auto lineBrush = GetOrCreateBrush(rt, lineClr);
            if (lineBrush) rt->DrawLine(D2D1::Point2F(lineStart, rect.top + 8), D2D1::Point2F(rect.right, rect.top + 8), lineBrush.Get(), 0.35f);
        }
    }
}

void HotkeyEditForm::DrawButton(ID2D1HwndRenderTarget* rt, const wchar_t* text, const D2D1_RECT_F& rect, bool hovered)
{
    D2D1_ROUNDED_RECT rc = D2D1::RoundedRect(rect, 3.0f, 3.0f);
    D2D1_COLOR_F bgClr = hovered ? UIStyle::ThemeColor::ButtonBgHover().d2d : UIStyle::ThemeColor::ButtonBgNormal().d2d;

    auto bg = GetOrCreateBrush(rt, bgClr);
    if (bg) rt->FillRoundedRectangle(rc, bg.Get());

    D2D1_COLOR_F bdrClr = hovered ? UIStyle::ThemeColor::ButtonBorderHover().d2d : UIStyle::ThemeColor::ButtonBorderNormal().d2d;

    auto bdr = GetOrCreateBrush(rt, bdrClr);
    if (bdr) rt->DrawRoundedRectangle(rc, bdr.Get(), UIStyle::Metrics::ControlStroke());

    if (m_tfBtn)
    {
        auto txt = GetOrCreateBrush(rt, UIStyle::ThemeColor::TextNormal().d2d);
        if (txt) rt->DrawTextW(text, (UINT32)wcslen(text), m_tfBtn.Get(), rect, txt.Get());
    }
}

void HotkeyEditForm::DrawCheckbox(ID2D1HwndRenderTarget* rt, const D2D1_RECT_F& rect, bool checked, bool hovered)
{
    D2D1_RECT_F boxRect = D2D1::RectF(rect.left, rect.top + 4.0f, rect.left + 14.0f, rect.top + 18.0f);
    D2D1_ROUNDED_RECT roundedBox = D2D1::RoundedRect(boxRect, 2.0f, 2.0f);

    D2D1_COLOR_F bgClr = checked ? UIStyle::ThemeColor::Accent().d2d : UIStyle::ThemeColor::EditBg().d2d;
    if (checked && hovered) bgClr = UIStyle::ThemeColor::AccentHover().d2d;

    auto bg = GetOrCreateBrush(rt, bgClr);
    if (bg) rt->FillRoundedRectangle(roundedBox, bg.Get());

    D2D1_COLOR_F bdrClr = checked ? UIStyle::ThemeColor::Accent().d2d : UIStyle::ThemeColor::EditBorderNormal().d2d;
    if (hovered) bdrClr = UIStyle::ThemeColor::Accent().d2d;

    auto bdr = GetOrCreateBrush(rt, bdrClr);
    if (bdr) rt->DrawRoundedRectangle(roundedBox, bdr.Get(), UIStyle::Metrics::ControlStroke());

    if (checked)
    {
        auto chkBrush = GetOrCreateBrush(rt, UIStyle::ThemeColor::TextOnAccent().d2d);
        if (chkBrush)
        {
            rt->DrawLine(D2D1::Point2F(boxRect.left + 3, boxRect.top + 7), D2D1::Point2F(boxRect.left + 6, boxRect.top + 10), chkBrush.Get(), 1.5f);
            rt->DrawLine(D2D1::Point2F(boxRect.left + 6, boxRect.top + 10), D2D1::Point2F(boxRect.left + 11, boxRect.top + 4), chkBrush.Get(), 1.5f);
        }
    }
}

void HotkeyEditForm::DrawIconPreview(ID2D1HwndRenderTarget* rt)
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

HICON HotkeyEditForm::GetFileIconForPreview(const std::wstring& path)
{
    return ShortcutManager::GetShortcutIcon(path);
}
