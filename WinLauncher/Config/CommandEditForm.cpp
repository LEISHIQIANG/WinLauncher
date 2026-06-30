#define NOMINMAX
#include "CommandEditForm.h"
#include "ConfirmWindow.h"
#include "UIStyle.h"
#include "../DpiHelper.h"
#include "../ShortcutManager.h"
#include "ContextMenu.h"
#include "DropDownMenu.h"
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

struct CommandBrushCacheEntry
{
    D2D1_COLOR_F color;
    ComPtr<ID2D1SolidColorBrush> brush;
};
static std::vector<CommandBrushCacheEntry> g_commandBrushCache;

static ComPtr<ID2D1SolidColorBrush> GetOrCreateBrush(ID2D1HwndRenderTarget* rt, const D2D1_COLOR_F& color)
{
    for (auto& entry : g_commandBrushCache)
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
            g_commandBrushCache.push_back({ color, brush });
        }
    }
    return brush;
}

// Layout Y coordinates relative to m_bounds.top
static const float Y_SEC_BASIC       = 0.0f;
static const float Y_LBL_NAME        = 16.0f;
static const float Y_BOX_NAME        = 32.0f;
static const float Y_LBL_TYPE        = 60.0f; // Command type row
static const float Y_BOX_TYPE        = 76.0f; // Textbox + Select button
static const float Y_LBL_CMD         = 104.0f; // Command text
static const float Y_BOX_CMD         = 120.0f; // Multi-line Textbox (height 100)
static const float Y_SEC_ADV         = 228.0f; // "高级选项" separator
static const float Y_CB_ROW1         = 244.0f; // "显示控制台窗口" and "捕获命令行输出"
static const float Y_LBL_ICON        = 276.0f; // Icon Path
static const float Y_BOX_ICON        = 292.0f; // Textbox + Select button
static const float Y_ICON_INVERT     = 322.0f; // Theme inversion checkboxes
static const float Y_PREVIEW         = 280.0f; // Preview bottom aligns with icon box bottom

CommandEditForm::CommandEditForm()
{
}

CommandEditForm::~CommandEditForm()
{
    Destroy();
}

bool CommandEditForm::Create(HWND parentHWND, IDWriteFactory* dwriteFactory, const D2D1_RECT_F& logicalBounds, const CommandEditFormInitParams& init, AppContext* ctx)
{
    m_ctx = ctx;
    m_parentHWND = parentHWND;
    m_bounds = logicalBounds;
    m_init = init;
    m_commandType = init.commandType.empty() ? L"cmd" : init.commandType;
    m_builtinCmd = init.builtinCmd;
    m_runAsAdmin = init.runAsAdmin;
    m_showWindow = init.showWindow;
    m_captureOutput = init.captureOutput;
    m_iconInvertLight = init.iconInvertLight;
    m_iconInvertDark = init.iconInvertDark;
    m_timeoutSeconds = init.timeoutSeconds;
    m_maxChars = init.maxChars;

    UIStyle::TextBoxStyle style;
    style.fontSize    = 11;
    style.paddingTop  = 4.0f;
    style.paddingBottom = 4.0f;

    EnsureFonts(dwriteFactory);

    float W = m_bounds.right - m_bounds.left;

    // Name Box
    m_nameBox.SetStyle(style);
    m_nameBox.Create(parentHWND, dwriteFactory, D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_NAME, m_bounds.left + W - 20, m_bounds.top + Y_BOX_NAME + 24), m_init.name);

    // Command Type Display Box
    std::wstring typeDisp = L"CMD";
    if (m_commandType == L"powershell") typeDisp = L"PowerShell";
    else if (m_commandType == L"python") typeDisp = L"Python";
    else if (m_commandType == L"gitbash") typeDisp = L"GitBash";

    m_typeBox.SetStyle(style);
    m_typeBox.Create(parentHWND, dwriteFactory, D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_TYPE, m_bounds.left + W - 125, m_bounds.top + Y_BOX_TYPE + 24), typeDisp);

    // Command Box (6 lines height)
    m_commandBox.SetStyle(style);
    m_commandBox.SetMultiline(true);
    m_commandBox.Create(parentHWND, dwriteFactory, D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_CMD, m_bounds.left + W - 20, m_bounds.top + Y_BOX_CMD + 100.0f), m_init.command);

    // Icon Box
    m_iconBox.SetStyle(style);
    m_iconBox.Create(parentHWND, dwriteFactory, D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_ICON, m_bounds.left + W - 121, m_bounds.top + Y_BOX_ICON + 24), m_init.iconPath);

    m_focusedBox = &m_nameBox;
    m_nameBox.SetFocus(true);

    if (!m_init.iconPath.empty())
    {
        m_previewIcon = GetFileIconForPreview(m_init.iconPath);
    }

    return true;
}

void CommandEditForm::Destroy()
{
    m_nameBox.Destroy();
    m_typeBox.Destroy();
    m_commandBox.Destroy();
    m_iconBox.Destroy();

    if (m_previewBitmap) { m_previewBitmap->Release(); m_previewBitmap = nullptr; }
    if (m_previewIcon)   { DestroyIcon(m_previewIcon); m_previewIcon = nullptr; }

    g_commandBrushCache.clear();
}

void CommandEditForm::UpdateLayout(const D2D1_RECT_F& logicalBounds, float scale)
{
    m_bounds = logicalBounds;
    float W = m_bounds.right - m_bounds.left;

    m_nameBox.SetBounds(D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_NAME, m_bounds.left + W - 20, m_bounds.top + Y_BOX_NAME + 24));
    m_nameBox.UpdateLayout(scale);

    m_typeBox.SetBounds(D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_TYPE, m_bounds.left + W - 125, m_bounds.top + Y_BOX_TYPE + 24));
    m_typeBox.UpdateLayout(scale);

    m_commandBox.SetBounds(D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_CMD, m_bounds.left + W - 20, m_bounds.top + Y_BOX_CMD + 100.0f));
    m_commandBox.UpdateLayout(scale);

    m_iconBox.SetBounds(D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_ICON, m_bounds.left + W - 121, m_bounds.top + Y_BOX_ICON + 24));
    m_iconBox.UpdateLayout(scale);
}

void CommandEditForm::EnsureFonts(IDWriteFactory* dwriteFactory)
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

bool CommandEditForm::HitTestRect(POINT pt, const D2D1_RECT_F& rect)
{
    return (pt.x >= rect.left && pt.x <= rect.right && pt.y >= rect.top && pt.y <= rect.bottom);
}

bool CommandEditForm::HitTestSelectTypeButton(POINT pt)
{
    float W = m_bounds.right - m_bounds.left;
    return HitTestRect(pt, D2D1::RectF(m_bounds.left + W - 120, m_bounds.top + Y_BOX_TYPE, m_bounds.left + W - 20, m_bounds.top + Y_BOX_TYPE + 24));
}

bool CommandEditForm::HitTestBrowseIconButton(POINT pt)
{
    float W = m_bounds.right - m_bounds.left;
    return HitTestRect(pt, D2D1::RectF(m_bounds.left + W - 116, m_bounds.top + Y_BOX_ICON, m_bounds.left + W - 61, m_bounds.top + Y_BOX_ICON + 24));
}

bool CommandEditForm::HitTestShowWindowCheckbox(POINT pt)
{
    return HitTestRect(pt, D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_CB_ROW1, m_bounds.left + 175, m_bounds.top + Y_CB_ROW1 + 22));
}

bool CommandEditForm::HitTestCaptureOutputCheckbox(POINT pt)
{
    return HitTestRect(pt, D2D1::RectF(m_bounds.left + 185, m_bounds.top + Y_CB_ROW1, m_bounds.left + 340, m_bounds.top + Y_CB_ROW1 + 22));
}

bool CommandEditForm::HitTestInvertLightCheckbox(POINT pt)
{
    return HitTestRect(pt, D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_ICON_INVERT, m_bounds.left + 104, m_bounds.top + Y_ICON_INVERT + 22));
}

bool CommandEditForm::HitTestInvertDarkCheckbox(POINT pt)
{
    return HitTestRect(pt, D2D1::RectF(m_bounds.left + 108, m_bounds.top + Y_ICON_INVERT, m_bounds.left + 192, m_bounds.top + Y_ICON_INVERT + 22));
}

void CommandEditForm::OnMouseMove(HWND hWnd, POINT pt, float scale, bool& repaint)
{
    POINT rawPt{ (int)(pt.x * scale), (int)(pt.y * scale) };
    m_nameBox.OnMouseMove(hWnd, rawPt, scale, repaint);
    m_typeBox.OnMouseMove(hWnd, rawPt, scale, repaint);
    m_commandBox.OnMouseMove(hWnd, rawPt, scale, repaint);
    m_iconBox.OnMouseMove(hWnd, rawPt, scale, repaint);

    auto update = [&](bool& flag, bool newVal){ if (flag != newVal){ flag = newVal; repaint = true; } };
    update(m_hoveredSelectType,    HitTestSelectTypeButton(pt));
    update(m_hoveredBrowseIcon,    HitTestBrowseIconButton(pt));
    update(m_hoveredShowWindow,    HitTestShowWindowCheckbox(pt));
    update(m_hoveredCaptureOutput, HitTestCaptureOutputCheckbox(pt));
    update(m_hoveredInvertLight,   HitTestInvertLightCheckbox(pt));
    update(m_hoveredInvertDark,    HitTestInvertDarkCheckbox(pt));
}

void CommandEditForm::OnLButtonDown(HWND hWnd, POINT pt, float scale, bool& repaint)
{
    POINT rawPt{ (int)(pt.x * scale), (int)(pt.y * scale) };

    if (HitTestSelectTypeButton(pt))
    {
        SelectCommandType(hWnd);
        repaint = true;
        return;
    }

    if (HitTestBrowseIconButton(pt))
    {
        BrowseIconFile(hWnd);
        repaint = true;
        return;
    }

    if (HitTestShowWindowCheckbox(pt))
    {
        m_showWindow = !m_showWindow;
        repaint = true;
        return;
    }

    if (HitTestCaptureOutputCheckbox(pt))
    {
        m_captureOutput = !m_captureOutput;
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
            if (m_focusedBox && m_focusedBox != &tb) m_focusedBox->SetFocus(false);
            m_focusedBox = &tb;
            tb.SetFocus(true);
            tb.OnLButtonDown(hWnd, rawPt, scale, repaint);
            return true;
        }
        return false;
    };

    if (tryFocus(m_nameBox))    return;
    if (tryFocus(m_commandBox)) return;
    if (tryFocus(m_iconBox))    return;

    if (m_focusedBox) { m_focusedBox->SetFocus(false); m_focusedBox = nullptr; repaint = true; }
}

void CommandEditForm::OnLButtonDblClk(HWND hWnd, POINT pt, float scale, bool& repaint)
{
    POINT rawPt{ (int)(pt.x * scale), (int)(pt.y * scale) };

    auto tryWordSelect = [&](TextBox& tb) -> bool {
        if (tb.HitTest(pt))
        {
            if (m_focusedBox && m_focusedBox != &tb) m_focusedBox->SetFocus(false);
            m_focusedBox = &tb;
            tb.SetFocus(true);
            tb.OnLButtonDblClk(hWnd, rawPt, scale, repaint);
            return true;
        }
        return false;
    };

    if (tryWordSelect(m_nameBox))    return;
    if (tryWordSelect(m_commandBox)) return;
    if (tryWordSelect(m_iconBox))    return;
}

void CommandEditForm::OnLButtonUp(HWND hWnd, POINT pt, float scale, bool& repaint)
{
    POINT rawPt{ (int)(pt.x * scale), (int)(pt.y * scale) };
    m_nameBox.OnLButtonUp(hWnd, rawPt, scale, repaint);
    m_typeBox.OnLButtonUp(hWnd, rawPt, scale, repaint);
    m_commandBox.OnLButtonUp(hWnd, rawPt, scale, repaint);
    m_iconBox.OnLButtonUp(hWnd, rawPt, scale, repaint);
}

void CommandEditForm::OnChar(HWND hWnd, WPARAM wParam, bool& repaint)
{
    if (m_focusedBox && m_focusedBox != &m_typeBox)
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

void CommandEditForm::OnKeyDown(HWND hWnd, WPARAM wParam, LPARAM lParam, bool& repaint)
{
    if (wParam == VK_TAB)
    {
        TextBox* order[] = { &m_nameBox, &m_commandBox, &m_iconBox };
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

void CommandEditForm::OnMouseWheel(HWND hWnd, short zDelta, POINT pt, float scale, bool& repaint)
{
    m_commandBox.OnMouseWheel(hWnd, zDelta, pt, scale, repaint);
}

void CommandEditForm::BlinkCaret()
{
    m_nameBox.BlinkCaret();
    m_commandBox.BlinkCaret();
    m_iconBox.BlinkCaret();
}

bool CommandEditForm::IsInputFocused() const
{
    return m_focusedBox != nullptr;
}

bool CommandEditForm::HandleImeMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, bool& repaint)
{
    if (m_focusedBox)
    {
        return m_focusedBox->HandleImeMessage(hWnd, uMsg, wParam, lParam, repaint);
    }
    return false;
}

void CommandEditForm::ResetFocus()
{
    if (m_focusedBox)
    {
        m_focusedBox->SetFocus(false);
        m_focusedBox = nullptr;
    }
}

CommandEditFormResult CommandEditForm::GetResult() const
{
    CommandEditFormResult res;
    res.name = m_nameBox.GetText();
    res.command = m_commandBox.GetText();
    res.iconPath = m_iconBox.GetText();
    res.runAsAdmin = m_runAsAdmin;
    res.iconInvertLight = m_iconInvertLight;
    res.iconInvertDark = m_iconInvertDark;
    res.commandType = m_commandType;
    res.builtinCmd = m_builtinCmd;
    res.showWindow = m_showWindow;
    res.captureOutput = m_captureOutput;
    res.timeoutSeconds = m_timeoutSeconds;
    res.maxChars = m_maxChars;
    return res;
}

bool CommandEditForm::Validate(HWND hWnd)
{
    std::wstring name = m_nameBox.GetText();
    std::wstring cmd = m_commandBox.GetText();

    if (name.empty())
    {
        ConfirmWindow::Show(hWnd, L"验证失败", L"请输入名称！", m_ctx, false);
        return false;
    }

    if (cmd.empty())
    {
        ConfirmWindow::Show(hWnd, L"验证失败", L"请输入命令行内容！", m_ctx, false);
        return false;
    }

    return true;
}

static bool CheckExecutableAvailable(const wchar_t* name)
{
    wchar_t dummy[MAX_PATH];
    return SearchPathW(nullptr, name, nullptr, MAX_PATH, dummy, nullptr) > 0;
}

static bool CheckGitBashAvailable()
{
    wchar_t foundPath[MAX_PATH]{};
    DWORD len = SearchPathW(nullptr, L"git.exe", nullptr, MAX_PATH, foundPath, nullptr);
    if (len > 0 && len < MAX_PATH)
    {
        std::wstring gp(foundPath);
        size_t cmdPos = gp.rfind(L"\\cmd\\");
        if (cmdPos != std::wstring::npos)
        {
            std::wstring root = gp.substr(0, cmdPos);
            std::wstring candidate = root + L"\\bin\\bash.exe";
            if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES)
                return true;
            candidate = root + L"\\usr\\bin\\bash.exe";
            if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES)
                return true;
        }
    }

    static const wchar_t* commonPaths[] = {
        L"C:\\Program Files\\Git\\bin\\bash.exe",
        L"C:\\Program Files\\Git\\usr\\bin\\bash.exe",
        L"C:\\Program Files (x86)\\Git\\bin\\bash.exe",
        L"C:\\Program Files (x86)\\Git\\usr\\bin\\bash.exe",
    };
    for (auto p : commonPaths)
    {
        if (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES)
            return true;
    }

    return false;
}

void CommandEditForm::SelectCommandType(HWND hWnd)
{
    float W = m_bounds.right - m_bounds.left;
    POINT clientPt = { (int)(m_bounds.left + W - 120), (int)(m_bounds.top + Y_BOX_TYPE + 24) };
    POINT screenPt = DpiHelper::LogicalClientToScreen(hWnd, clientPt);

    bool cmdOK = CheckExecutableAvailable(L"cmd.exe");
    bool psOK = CheckExecutableAvailable(L"powershell.exe");
    bool pyOK = CheckExecutableAvailable(L"python.exe");
    bool gitOK = CheckGitBashAvailable();

    std::vector<DropDownMenu::Item> items = {
        { L"CMD", [this, hWnd]() { m_commandType = L"cmd"; m_typeBox.SetText(L"CMD"); InvalidateRect(hWnd, nullptr, FALSE); }, !cmdOK },
        { L"PowerShell", [this, hWnd]() { m_commandType = L"powershell"; m_typeBox.SetText(L"PowerShell"); InvalidateRect(hWnd, nullptr, FALSE); }, !psOK },
        { L"Python", [this, hWnd]() { m_commandType = L"python"; m_typeBox.SetText(L"Python"); InvalidateRect(hWnd, nullptr, FALSE); }, !pyOK },
        { L"GitBash", [this, hWnd]() { m_commandType = L"gitbash"; m_typeBox.SetText(L"GitBash"); InvalidateRect(hWnd, nullptr, FALSE); }, !gitOK }
    };
    DropDownMenu::Show(hWnd, screenPt, items, m_ctx, 100.0f);
}

void CommandEditForm::BrowseIconFile(HWND hWnd)
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

void CommandEditForm::Paint(ID2D1HwndRenderTarget* rt, float scale)
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

    // Command Type Label
    if (m_tfLabel)
    {
        auto lblBrush = GetOrCreateBrush(rt, UIStyle::ThemeColor::TextMuted().d2d);
        if (lblBrush) rt->DrawTextW(L"命令类型", 4, m_tfLabel.Get(), D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_LBL_TYPE, m_bounds.left + W - 20, m_bounds.top + Y_LBL_TYPE + 16), lblBrush.Get());
    }
    m_typeBox.Paint(rt, scale);
    DrawButton(rt, L"选择...", D2D1::RectF(m_bounds.left + W - 120, m_bounds.top + Y_BOX_TYPE, m_bounds.left + W - 20, m_bounds.top + Y_BOX_TYPE + 24), m_hoveredSelectType);

    // Command Content Label
    if (m_tfLabel)
    {
        auto lblBrush = GetOrCreateBrush(rt, UIStyle::ThemeColor::TextMuted().d2d);
        if (lblBrush) rt->DrawTextW(L"命令行内容", 5, m_tfLabel.Get(), D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_LBL_CMD, m_bounds.left + W - 20, m_bounds.top + Y_LBL_CMD + 16), lblBrush.Get());
    }
    m_commandBox.Paint(rt, scale);

    // "高级选项" Separator
    DrawSectionLabel(rt, L"配置选项", D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_SEC_ADV, m_bounds.left + W - 20, m_bounds.top + Y_SEC_ADV + 16));

    // Checkbox Row 1
    DrawCheckbox(rt, D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_CB_ROW1, m_bounds.left + 175, m_bounds.top + Y_CB_ROW1 + 22), m_showWindow, m_hoveredShowWindow);
    if (m_tfLabel)
    {
        auto txtBrush = GetOrCreateBrush(rt, UIStyle::ThemeColor::TextNormal().d2d);
        if (txtBrush) rt->DrawTextW(L"显示控制台窗口", 7, m_tfLabel.Get(), D2D1::RectF(m_bounds.left + 40, m_bounds.top + Y_CB_ROW1, m_bounds.left + 175, m_bounds.top + Y_CB_ROW1 + 22), txtBrush.Get());
    }

    DrawCheckbox(rt, D2D1::RectF(m_bounds.left + 185, m_bounds.top + Y_CB_ROW1, m_bounds.left + 340, m_bounds.top + Y_CB_ROW1 + 22), m_captureOutput, m_hoveredCaptureOutput);
    if (m_tfLabel)
    {
        auto txtBrush = GetOrCreateBrush(rt, UIStyle::ThemeColor::TextNormal().d2d);
        if (txtBrush) rt->DrawTextW(L"捕获命令行输出", 7, m_tfLabel.Get(), D2D1::RectF(m_bounds.left + 205, m_bounds.top + Y_CB_ROW1, m_bounds.left + 340, m_bounds.top + Y_CB_ROW1 + 22), txtBrush.Get());
    }

    // Icon Path Label
    if (m_tfLabel)
    {
        auto lblBrush = GetOrCreateBrush(rt, UIStyle::ThemeColor::TextMuted().d2d);
        if (lblBrush) rt->DrawTextW(L"自定义图标路径", 7, m_tfLabel.Get(), D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_LBL_ICON, m_bounds.left + W - 150, m_bounds.top + Y_LBL_ICON + 16), lblBrush.Get());
    }
    m_iconBox.Paint(rt, scale);
    DrawButton(rt, L"选择...", D2D1::RectF(m_bounds.left + W - 116, m_bounds.top + Y_BOX_ICON, m_bounds.left + W - 61, m_bounds.top + Y_BOX_ICON + 24), m_hoveredBrowseIcon);

    DrawCheckbox(rt, D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_ICON_INVERT, m_bounds.left + 104, m_bounds.top + Y_ICON_INVERT + 22), m_iconInvertLight, m_hoveredInvertLight);
    if (m_tfLabel)
    {
        auto txtBrush = GetOrCreateBrush(rt, UIStyle::ThemeColor::TextNormal().d2d);
        if (txtBrush) rt->DrawTextW(L"浅色反转", 4, m_tfLabel.Get(), D2D1::RectF(m_bounds.left + 40, m_bounds.top + Y_ICON_INVERT, m_bounds.left + 104, m_bounds.top + Y_ICON_INVERT + 22), txtBrush.Get());
    }

    DrawCheckbox(rt, D2D1::RectF(m_bounds.left + 108, m_bounds.top + Y_ICON_INVERT, m_bounds.left + 192, m_bounds.top + Y_ICON_INVERT + 22), m_iconInvertDark, m_hoveredInvertDark);
    if (m_tfLabel)
    {
        auto txtBrush = GetOrCreateBrush(rt, UIStyle::ThemeColor::TextNormal().d2d);
        if (txtBrush) rt->DrawTextW(L"深色反转", 4, m_tfLabel.Get(), D2D1::RectF(m_bounds.left + 128, m_bounds.top + Y_ICON_INVERT, m_bounds.left + 192, m_bounds.top + Y_ICON_INVERT + 22), txtBrush.Get());
    }

    // Draw preview icon
    DrawIconPreview(rt);
}

void CommandEditForm::DrawSectionLabel(ID2D1HwndRenderTarget* rt, const wchar_t* text, const D2D1_RECT_F& rect)
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

void CommandEditForm::DrawButton(ID2D1HwndRenderTarget* rt, const wchar_t* text, const D2D1_RECT_F& rect, bool hovered, bool enabled)
{
    D2D1_ROUNDED_RECT rc = D2D1::RoundedRect(rect, 3.0f, 3.0f);
    D2D1_COLOR_F bgClr = hovered && enabled ? UIStyle::ThemeColor::ButtonBgHover().d2d : UIStyle::ThemeColor::ButtonBgNormal().d2d;
    if (!enabled) bgClr.a = 0.03f;

    auto bg = GetOrCreateBrush(rt, bgClr);
    if (bg) rt->FillRoundedRectangle(rc, bg.Get());

    D2D1_COLOR_F bdrClr = hovered && enabled ? UIStyle::ThemeColor::ButtonBorderHover().d2d : UIStyle::ThemeColor::ButtonBorderNormal().d2d;
    if (!enabled) bdrClr.a = 0.2f;

    auto bdr = GetOrCreateBrush(rt, bdrClr);
    if (bdr) rt->DrawRoundedRectangle(rc, bdr.Get(), UIStyle::Metrics::ControlStroke());

    if (m_tfBtn)
    {
        D2D1_COLOR_F textClr = UIStyle::ThemeColor::TextNormal().d2d;
        if (!enabled) textClr.a = 0.4f;
        auto txt = GetOrCreateBrush(rt, textClr);
        if (txt) rt->DrawTextW(text, (UINT32)wcslen(text), m_tfBtn.Get(), rect, txt.Get());
    }
}

void CommandEditForm::DrawCheckbox(ID2D1HwndRenderTarget* rt, const D2D1_RECT_F& rect, bool checked, bool hovered, bool enabled)
{
    D2D1_RECT_F boxRect = D2D1::RectF(rect.left, rect.top + 4.0f, rect.left + 14.0f, rect.top + 18.0f);
    D2D1_ROUNDED_RECT roundedBox = D2D1::RoundedRect(boxRect, 2.0f, 2.0f);

    D2D1_COLOR_F bgClr = checked ? UIStyle::ThemeColor::Accent().d2d : UIStyle::ThemeColor::EditBg().d2d;
    if (checked && hovered && enabled) bgClr = UIStyle::ThemeColor::AccentHover().d2d;
    if (!enabled) bgClr.a = 0.03f;

    auto bg = GetOrCreateBrush(rt, bgClr);
    if (bg) rt->FillRoundedRectangle(roundedBox, bg.Get());

    D2D1_COLOR_F bdrClr = checked ? UIStyle::ThemeColor::Accent().d2d : UIStyle::ThemeColor::EditBorderNormal().d2d;
    if (hovered && enabled) bdrClr = UIStyle::ThemeColor::Accent().d2d;
    if (!enabled) bdrClr.a = 0.2f;

    auto bdr = GetOrCreateBrush(rt, bdrClr);
    if (bdr) rt->DrawRoundedRectangle(roundedBox, bdr.Get(), UIStyle::Metrics::ControlStroke());

    if (checked)
    {
        D2D1_COLOR_F chkClr = UIStyle::ThemeColor::TextOnAccent().d2d;
        if (!enabled) chkClr.a = 0.4f;
        auto chkBrush = GetOrCreateBrush(rt, chkClr);
        if (chkBrush)
        {
            rt->DrawLine(D2D1::Point2F(boxRect.left + 3, boxRect.top + 7), D2D1::Point2F(boxRect.left + 6, boxRect.top + 10), chkBrush.Get(), 1.5f);
            rt->DrawLine(D2D1::Point2F(boxRect.left + 6, boxRect.top + 10), D2D1::Point2F(boxRect.left + 11, boxRect.top + 4), chkBrush.Get(), 1.5f);
        }
    }
}

void CommandEditForm::DrawIconPreview(ID2D1HwndRenderTarget* rt)
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

HICON CommandEditForm::GetFileIconForPreview(const std::wstring& path)
{
    return ShortcutManager::GetShortcutIcon(path);
}
