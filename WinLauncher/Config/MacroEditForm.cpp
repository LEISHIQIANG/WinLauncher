#define NOMINMAX
#include "MacroEditForm.h"
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

#pragma comment(lib, "comdlg32.lib")
#include "../UI/Controls/IconRenderer.h"

struct MacroBrushCacheEntry
{
    D2D1_COLOR_F color;
    ComPtr<ID2D1SolidColorBrush> brush;
};
static std::vector<MacroBrushCacheEntry> g_macroBrushCache;

static ComPtr<ID2D1SolidColorBrush> GetOrCreateBrush(ID2D1HwndRenderTarget* rt, const D2D1_COLOR_F& color)
{
    for (auto& entry : g_macroBrushCache)
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
            g_macroBrushCache.push_back({ color, brush });
    }
    return brush;
}

static const float Y_LBL_NAME        = 10.0f;
static const float Y_BOX_NAME        = 25.0f;
static const float Y_LBL_EVENTS      = 58.0f;
static const float Y_BOX_EVENTS      = 73.0f;  // h=120 → bottom=193
static const float Y_REC_BTN         = 202.0f; // 4 buttons, h=24 → bottom=226
static const float Y_AFTERCLOSE_CB   = 229.0f; // checkbox row → bottom=251
static const float Y_SPEED           = 255.0f; // 4 speed radio buttons, h=22 → bottom=277
static const float Y_LBL_ICON        = 287.0f;
static const float Y_BOX_ICON        = 302.0f; // h=24 → bottom=326
static const float Y_PREVIEW         = 290.0f;
static const float Y_INVERT_LIGHT    = 338.0f;
static const float Y_INVERT_DARK     = 338.0f;

static const wchar_t* SPEED_LABELS[4] = { L"1x", L"2x", L"4x", L"8x" };
static const double   SPEED_VALUES[4] = { 1.0, 2.0, 4.0, 8.0 };

MacroEditForm::MacroEditForm()
{
}

MacroEditForm::~MacroEditForm()
{
    Destroy();
}

bool MacroEditForm::Create(HWND parentHWND, IDWriteFactory* dwriteFactory, const D2D1_RECT_F& logicalBounds, const MacroEditFormInitParams& init, AppContext* ctx)
{
    m_ctx = ctx;
    m_parentHWND = parentHWND;
    m_bounds = logicalBounds;
    m_init = init;
    m_iconInvertLight = init.iconInvertLight;
    m_iconInvertDark = init.iconInvertDark;

    double speed = 1.0;
    std::wstring triggerMode = L"immediate";
    std::vector<MacroEvent> events;
    MacroHelper::Parse(init.arguments, speed, triggerMode, events);
    m_triggerMode = triggerMode;

    m_speedIdx = 0;
    for (int i = 0; i < 4; ++i)
    {
        double diff = speed - SPEED_VALUES[i];
        if (diff < 0.0) diff = -diff;
        if (diff < 0.01) { m_speedIdx = i; break; }
    }

    EnsureFonts(dwriteFactory);

    UIStyle::TextBoxStyle style;
    style.fontSize    = 11;
    style.paddingTop  = 4.0f;
    style.paddingBottom = 4.0f;

    float W = m_bounds.right - m_bounds.left;
    float R = m_bounds.left + W - 20;

    m_nameBox.SetStyle(style);
    m_nameBox.Create(parentHWND, dwriteFactory, D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_NAME, R, m_bounds.top + Y_BOX_NAME + 24), m_init.name);

    UIStyle::TextBoxStyle multilineStyle = style;
    m_eventsBox.SetStyle(multilineStyle);
    m_eventsBox.SetMultiline(true);
    std::wstring scriptText = MacroHelper::ToScript(events);
    m_eventsBox.Create(parentHWND, dwriteFactory, D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_EVENTS, R, m_bounds.top + Y_BOX_EVENTS + 120), scriptText);

    m_iconBox.SetStyle(style);
    m_iconBox.Create(parentHWND, dwriteFactory, D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_ICON, R - 101, m_bounds.top + Y_BOX_ICON + 24), m_init.iconPath);

    m_focusedBox = &m_nameBox;
    m_focusedBox->SetFocus(true);

    return true;
}

void MacroEditForm::Destroy()
{
    m_nameBox.Destroy();
    m_iconBox.Destroy();
    m_eventsBox.Destroy();
    if (m_previewIcon) { DestroyIcon(m_previewIcon); m_previewIcon = nullptr; }
    if (m_previewBitmap) { m_previewBitmap->Release(); m_previewBitmap = nullptr; }
    g_macroBrushCache.clear();
}

void MacroEditForm::Paint(ID2D1HwndRenderTarget* rt, float scale)
{
    m_d2dFactoryCache = nullptr;
    rt->GetFactory(&m_d2dFactoryCache);

    float W = m_bounds.right - m_bounds.left;
    float R = m_bounds.left + W - 20;

    auto textBrush = GetOrCreateBrush(rt, UIStyle::ThemeColor::TextNormal().d2d);

    DrawSectionLabel(rt, L"名称",
        D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_LBL_NAME, R, m_bounds.top + Y_LBL_NAME + 15));
    DrawSectionLabel(rt, L"宏事件脚本 (F8 停止录制)",
        D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_LBL_EVENTS, R, m_bounds.top + Y_LBL_EVENTS + 15));
    DrawSectionLabel(rt, L"图标路径 (留空为默认)",
        D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_LBL_ICON, R - 101, m_bounds.top + Y_LBL_ICON + 15));

    m_nameBox.Paint(rt, scale);
    m_eventsBox.Paint(rt, scale);
    m_iconBox.Paint(rt, scale);

    bool isRecording = MacroRecorder::IsRecording();
    float btnW = (W - 40) / 4.0f;
    float bx = m_bounds.left + 20;
    DrawButton(rt, L"开始录制", D2D1::RectF(bx,            m_bounds.top + Y_REC_BTN, bx + btnW - 4,      m_bounds.top + Y_REC_BTN + 24), m_hoveredRecord, isRecording);
    DrawButton(rt, L"停止录制", D2D1::RectF(bx + btnW,      m_bounds.top + Y_REC_BTN, bx + btnW*2 - 4,   m_bounds.top + Y_REC_BTN + 24), m_hoveredStop, !isRecording);
    DrawButton(rt, L"测试播放", D2D1::RectF(bx + btnW*2,    m_bounds.top + Y_REC_BTN, bx + btnW*3 - 4,   m_bounds.top + Y_REC_BTN + 24), m_hoveredPlay, isRecording || MacroPlayer::IsPlaying());
    DrawButton(rt, L"清空",     D2D1::RectF(bx + btnW*3,    m_bounds.top + Y_REC_BTN, R,                  m_bounds.top + Y_REC_BTN + 24), m_hoveredClear, isRecording);

    DrawCheckbox(rt, D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_AFTERCLOSE_CB,
                                  R, m_bounds.top + Y_AFTERCLOSE_CB + 22),
                 m_triggerMode == L"after_close", m_hoveredAfterClose, L"触发后关闭窗口并在前台执行");

    // Speed radio buttons — 4 pills in a row
    float speedUsable = W - 40 - 12;
    float speedBtnW = speedUsable / 4.0f;
    float sx = m_bounds.left + 20;
    for (int i = 0; i < 4; ++i)
    {
        D2D1_RECT_F sr = D2D1::RectF(sx, m_bounds.top + Y_SPEED, sx + speedBtnW, m_bounds.top + Y_SPEED + 22);
        bool sel = (i == m_speedIdx);
        bool hov = (i == m_hoveredSpeedIdx);
        D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(D2D1::RectF(sr.left * scale, sr.top * scale, sr.right * scale, sr.bottom * scale), 4.0f, 4.0f);
        if (sel)
        {
            auto fill = GetOrCreateBrush(rt, UIStyle::ThemeColor::Accent().d2d);
            if (fill) rt->FillRoundedRectangle(rr, fill.Get());
        }
        else
        {
            bool isLight = (UIStyle::GetThemeMode() == UIStyle::ThemeMode::Light);
            D2D1_COLOR_F base = isLight ? D2D1::ColorF(0.f, 0.f, 0.f) : D2D1::ColorF(1.f, 1.f, 1.f);
            float bgA = hov ? 0.09f : 0.04f;
            auto bg = GetOrCreateBrush(rt, D2D1::ColorF(base.r, base.g, base.b, bgA));
            if (bg) rt->FillRoundedRectangle(rr, bg.Get());
            float borderA = hov ? 0.16f : 0.075f;
            auto border = GetOrCreateBrush(rt, D2D1::ColorF(base.r, base.g, base.b, borderA));
            if (border) rt->DrawRoundedRectangle(rr, border.Get(), UIStyle::Metrics::ControlStroke());
        }
        if (m_tfBtn)
        {
            D2D1_COLOR_F tc = sel ? UIStyle::ThemeColor::TextOnAccent().d2d : UIStyle::ThemeColor::TextNormal().d2d;
            auto tb = GetOrCreateBrush(rt, tc);
            if (tb) rt->DrawTextW(SPEED_LABELS[i], 2, m_tfBtn.Get(),
                                  D2D1::RectF(sr.left * scale, sr.top * scale, sr.right * scale, sr.bottom * scale), tb.Get());
        }
        sx += speedBtnW + 4;
    }

    DrawButton(rt, L"浏览...",
        D2D1::RectF(R - 96, m_bounds.top + Y_BOX_ICON, R - 41, m_bounds.top + Y_BOX_ICON + 24),
        m_hoveredBrowseIcon);
    DrawIconPreview(rt);

    DrawCheckbox(rt, D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_INVERT_LIGHT,
                                  m_bounds.left + 130, m_bounds.top + Y_INVERT_LIGHT + 22),
                 m_iconInvertLight, m_hoveredInvertLight, L"浅色主题反色");
    DrawCheckbox(rt, D2D1::RectF(m_bounds.left + 145, m_bounds.top + Y_INVERT_DARK,
                                  m_bounds.left + 260, m_bounds.top + Y_INVERT_DARK + 22),
                 m_iconInvertDark, m_hoveredInvertDark, L"深色主题反色");
}

void MacroEditForm::UpdateLayout(const D2D1_RECT_F& logicalBounds, float scale)
{
    m_bounds = logicalBounds;
    float W = m_bounds.right - m_bounds.left;
    float R = m_bounds.left + W - 20;
    m_nameBox.SetBounds(D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_NAME, R, m_bounds.top + Y_BOX_NAME + 24));
    m_eventsBox.SetBounds(D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_EVENTS, R, m_bounds.top + Y_BOX_EVENTS + 120));
    m_iconBox.SetBounds(D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_BOX_ICON, R - 101, m_bounds.top + Y_BOX_ICON + 24));
    m_nameBox.UpdateLayout(scale);
    m_eventsBox.UpdateLayout(scale);
    m_iconBox.UpdateLayout(scale);
}

void MacroEditForm::OnMouseMove(HWND hWnd, POINT pt, float scale, bool& repaint)
{
    POINT logicalPt{ (long)(pt.x / scale), (long)(pt.y / scale) };
    bool isRecording = MacroRecorder::IsRecording();

    bool r = !isRecording && HitTestRecordButton(logicalPt);
    if (r != m_hoveredRecord) { m_hoveredRecord = r; repaint = true; }

    bool s = HitTestStopButton(logicalPt);
    if (s != m_hoveredStop) { m_hoveredStop = s; repaint = true; }

    bool p = !isRecording && HitTestPlayButton(logicalPt);
    if (p != m_hoveredPlay) { m_hoveredPlay = p; repaint = true; }

    bool c = !isRecording && HitTestClearButton(logicalPt);
    if (c != m_hoveredClear) { m_hoveredClear = c; repaint = true; }

    bool b = !isRecording && HitTestBrowseIconButton(logicalPt);
    if (b != m_hoveredBrowseIcon) { m_hoveredBrowseIcon = b; repaint = true; }

    bool ac = !isRecording && HitTestAfterCloseCheckbox(logicalPt);
    if (ac != m_hoveredAfterClose) { m_hoveredAfterClose = ac; repaint = true; }

    int spd = isRecording ? -1 : HitTestSpeedButton(logicalPt);
    if (spd != m_hoveredSpeedIdx) { m_hoveredSpeedIdx = spd; repaint = true; }

    bool il = !isRecording && HitTestInvertLightCheckbox(logicalPt);
    if (il != m_hoveredInvertLight) { m_hoveredInvertLight = il; repaint = true; }

    bool id = !isRecording && HitTestInvertDarkCheckbox(logicalPt);
    if (id != m_hoveredInvertDark) { m_hoveredInvertDark = id; repaint = true; }

    if (isRecording)
        return;

    m_nameBox.OnMouseMove(hWnd, pt, scale, repaint);
    m_eventsBox.OnMouseMove(hWnd, pt, scale, repaint);
    m_iconBox.OnMouseMove(hWnd, pt, scale, repaint);
}

void MacroEditForm::OnLButtonDown(HWND hWnd, POINT pt, float scale, bool& repaint)
{
    POINT logicalPt{ (long)(pt.x / scale), (long)(pt.y / scale) };
    TextBox* clickedBox = nullptr;

    if (MacroRecorder::IsRecording())
    {
        if (HitTestStopButton(logicalPt))
        {
            MacroRecorder::Stop(true);
            RefreshScriptText(hWnd);
            if (m_focusedBox)
            {
                m_focusedBox->SetFocus(false);
                m_focusedBox = nullptr;
            }
            repaint = true;
        }
        return;
    }

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
    else if (m_eventsBox.HitTest(logicalPt))
    {
        clickedBox = &m_eventsBox;
        if (m_focusedBox != &m_eventsBox)
        {
            if (m_focusedBox) m_focusedBox->SetFocus(false);
            m_focusedBox = &m_eventsBox;
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

    if (HitTestRecordButton(logicalPt) && !MacroRecorder::IsRecording())
    {
        if (MacroRecorder::Start(hWnd))
        {
            // Clear focus so keyboard events are captured by recorder, not text boxes
            if (m_focusedBox)
            {
                m_focusedBox->SetFocus(false);
                m_focusedBox = nullptr;
            }
            repaint = true;
        }
    }
    else if (HitTestStopButton(logicalPt) && MacroRecorder::IsRecording())
    {
        MacroRecorder::Stop(true);
        RefreshScriptText(hWnd);
        repaint = true;
    }
    else if (HitTestPlayButton(logicalPt) && !MacroRecorder::IsRecording() && !MacroPlayer::IsPlaying())
    {
        std::vector<MacroEvent> events = MacroHelper::FromScript(m_eventsBox.GetText());
        double speed = SPEED_VALUES[m_speedIdx];
        MacroPlayer::Play(events, speed, m_triggerMode, hWnd);
        repaint = true;
    }
    else if (HitTestClearButton(logicalPt))
    {
        MacroRecorder::Clear();
        m_eventsBox.SetText(L"");
        repaint = true;
    }
    else if (HitTestBrowseIconButton(logicalPt))
    {
        BrowseIconFile(hWnd);
        repaint = true;
    }
    else if (HitTestAfterCloseCheckbox(logicalPt))
    {
        m_triggerMode = (m_triggerMode == L"after_close") ? L"immediate" : L"after_close";
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

    int spd = HitTestSpeedButton(logicalPt);
    if (spd >= 0 && spd != m_speedIdx)
    {
        m_speedIdx = spd;
        repaint = true;
    }

    if (clickedBox)
    {
        clickedBox->OnLButtonDown(hWnd, pt, scale, repaint);
    }
}

void MacroEditForm::OnLButtonDblClk(HWND hWnd, POINT pt, float scale, bool& repaint)
{
    if (MacroRecorder::IsRecording())
        return;

    m_nameBox.OnLButtonDblClk(hWnd, pt, scale, repaint);
    m_eventsBox.OnLButtonDblClk(hWnd, pt, scale, repaint);
    m_iconBox.OnLButtonDblClk(hWnd, pt, scale, repaint);
}

void MacroEditForm::OnLButtonUp(HWND hWnd, POINT pt, float scale, bool& repaint)
{
    if (MacroRecorder::IsRecording())
        return;

    m_nameBox.OnLButtonUp(hWnd, pt, scale, repaint);
    m_eventsBox.OnLButtonUp(hWnd, pt, scale, repaint);
    m_iconBox.OnLButtonUp(hWnd, pt, scale, repaint);
}

void MacroEditForm::OnChar(HWND hWnd, WPARAM wParam, bool& repaint)
{
    if (MacroRecorder::IsRecording())
        return;

    if (m_focusedBox) m_focusedBox->OnChar(hWnd, wParam, repaint);
}

void MacroEditForm::OnKeyDown(HWND hWnd, WPARAM wParam, LPARAM lParam, bool& repaint)
{
    if (MacroRecorder::IsRecording())
        return;

    if (m_focusedBox) m_focusedBox->OnKeyDown(hWnd, wParam, lParam, repaint);
}

bool MacroEditForm::HandleImeMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, bool& repaint)
{
    if (MacroRecorder::IsRecording())
        return true;

    if (m_focusedBox)
        return m_focusedBox->HandleImeMessage(hWnd, uMsg, wParam, lParam, repaint);
    return false;
}

void MacroEditForm::OnMouseWheel(HWND hWnd, short zDelta, POINT pt, float scale, bool& repaint)
{
    if (MacroRecorder::IsRecording())
        return;

    POINT logicalPt{ (long)(pt.x / scale), (long)(pt.y / scale) };
    if (m_eventsBox.HitTest(logicalPt))
    {
        m_eventsBox.OnMouseWheel(hWnd, zDelta, pt, scale, repaint);
    }
}

bool MacroEditForm::HandleHookMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, bool& repaint)
{
    if (uMsg == AppMessages::MacroRecordingUpdated)
    {
        RefreshScriptText(hWnd);
        repaint = true;
        return true;
    }

    if (uMsg == AppMessages::MacroRecordingStopped)
    {
        RefreshScriptText(hWnd);
        // Restore focus to name box after recording
        if (m_focusedBox) m_focusedBox->SetFocus(false);
        m_focusedBox = &m_nameBox;
        m_focusedBox->SetFocus(true);
        repaint = true;
        return true;
    }
    return false;
}

void MacroEditForm::BlinkCaret()
{
    if (m_focusedBox) m_focusedBox->BlinkCaret();
}

bool MacroEditForm::IsInputFocused() const
{
    return m_focusedBox != nullptr;
}

void MacroEditForm::ResetFocus()
{
    if (m_focusedBox) m_focusedBox->SetFocus(false);
    m_focusedBox = &m_nameBox;
    m_focusedBox->SetFocus(true);
}

MacroEditFormResult MacroEditForm::GetResult() const
{
    MacroEditFormResult res;
    res.name = m_nameBox.GetText();
    res.iconPath = m_iconBox.GetText();
    res.iconInvertLight = m_iconInvertLight;
    res.iconInvertDark = m_iconInvertDark;

    double speed = SPEED_VALUES[m_speedIdx];
    std::vector<MacroEvent> events = MacroHelper::FromScript(m_eventsBox.GetText());
    res.arguments = MacroHelper::Serialize(speed, m_triggerMode, events);
    return res;
}

bool MacroEditForm::Validate(HWND hWnd)
{
    std::wstring name = m_nameBox.GetText();
    if (name.empty())
    {
        ConfirmWindow::Show(hWnd, L"验证失败", L"宏名称不能为空！", m_ctx, false);
        return false;
    }
    return true;
}

void MacroEditForm::EnsureFonts(IDWriteFactory* dwriteFactory)
{
    if (!m_tfLabel)
    {
        UIStyle::Typography::CreateTextFormat(dwriteFactory, &m_tfLabel, 10.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        UIStyle::Typography::CreateTextFormat(dwriteFactory, &m_tfBtn, 10.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        UIStyle::Typography::CreateTextFormat(dwriteFactory, &m_tfSmall, 9.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
}

void MacroEditForm::BrowseIconFile(HWND hWnd)
{
    wchar_t filename[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFilter = L"Icons/Images\0*.ico;*.png;*.jpg;*.jpeg;*.bmp;*.dll;*.exe\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) m_iconBox.SetText(filename);
}

void MacroEditForm::RefreshScriptText(HWND hWnd)
{
    std::vector<MacroEvent> events = MacroRecorder::GetEvents();
    std::wstring scriptText = MacroHelper::ToScript(events);
    m_eventsBox.SetText(scriptText);
}

bool MacroEditForm::HitTestRect(POINT pt, const D2D1_RECT_F& rect)
{
    return (pt.x >= rect.left && pt.x <= rect.right && pt.y >= rect.top && pt.y <= rect.bottom);
}

bool MacroEditForm::HitTestRecordButton(POINT pt)
{
    float W = m_bounds.right - m_bounds.left;
    float btnW = (W - 40) / 4.0f;
    float bx = m_bounds.left + 20;
    return HitTestRect(pt, D2D1::RectF(bx, m_bounds.top + Y_REC_BTN, bx + btnW - 4, m_bounds.top + Y_REC_BTN + 24));
}

bool MacroEditForm::HitTestStopButton(POINT pt)
{
    float W = m_bounds.right - m_bounds.left;
    float btnW = (W - 40) / 4.0f;
    float bx = m_bounds.left + 20;
    return HitTestRect(pt, D2D1::RectF(bx + btnW, m_bounds.top + Y_REC_BTN, bx + btnW*2 - 4, m_bounds.top + Y_REC_BTN + 24));
}

bool MacroEditForm::HitTestPlayButton(POINT pt)
{
    float W = m_bounds.right - m_bounds.left;
    float btnW = (W - 40) / 4.0f;
    float bx = m_bounds.left + 20;
    return HitTestRect(pt, D2D1::RectF(bx + btnW*2, m_bounds.top + Y_REC_BTN, bx + btnW*3 - 4, m_bounds.top + Y_REC_BTN + 24));
}

bool MacroEditForm::HitTestClearButton(POINT pt)
{
    float W = m_bounds.right - m_bounds.left;
    float R = m_bounds.left + W - 20;
    float btnW = (W - 40) / 4.0f;
    float bx = m_bounds.left + 20;
    return HitTestRect(pt, D2D1::RectF(bx + btnW*3, m_bounds.top + Y_REC_BTN, R, m_bounds.top + Y_REC_BTN + 24));
}

bool MacroEditForm::HitTestBrowseIconButton(POINT pt)
{
    float W = m_bounds.right - m_bounds.left;
    float R = m_bounds.left + W - 20;
    return HitTestRect(pt, D2D1::RectF(R - 96, m_bounds.top + Y_BOX_ICON, R - 41, m_bounds.top + Y_BOX_ICON + 24));
}

bool MacroEditForm::HitTestAfterCloseCheckbox(POINT pt)
{
    float W = m_bounds.right - m_bounds.left;
    float R = m_bounds.left + W - 20;
    return HitTestRect(pt, D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_AFTERCLOSE_CB, R, m_bounds.top + Y_AFTERCLOSE_CB + 22));
}

int MacroEditForm::HitTestSpeedButton(POINT pt)
{
    float W = m_bounds.right - m_bounds.left;
    float speedUsable = W - 40 - 12;
    float speedBtnW = speedUsable / 4.0f;
    float sx = m_bounds.left + 20;
    for (int i = 0; i < 4; ++i)
    {
        if (HitTestRect(pt, D2D1::RectF(sx, m_bounds.top + Y_SPEED, sx + speedBtnW, m_bounds.top + Y_SPEED + 22)))
            return i;
        sx += speedBtnW + 4;
    }
    return -1;
}

bool MacroEditForm::HitTestInvertLightCheckbox(POINT pt)
{
    return HitTestRect(pt, D2D1::RectF(m_bounds.left + 20, m_bounds.top + Y_INVERT_LIGHT, m_bounds.left + 130, m_bounds.top + Y_INVERT_LIGHT + 22));
}

bool MacroEditForm::HitTestInvertDarkCheckbox(POINT pt)
{
    return HitTestRect(pt, D2D1::RectF(m_bounds.left + 145, m_bounds.top + Y_INVERT_DARK, m_bounds.left + 260, m_bounds.top + Y_INVERT_DARK + 22));
}

void MacroEditForm::DrawSectionLabel(ID2D1HwndRenderTarget* rt, const wchar_t* text, const D2D1_RECT_F& rect)
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

void MacroEditForm::DrawButton(ID2D1HwndRenderTarget* rt, const wchar_t* text, const D2D1_RECT_F& rect, bool hovered, bool disabled)
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

void MacroEditForm::DrawCheckbox(ID2D1HwndRenderTarget* rt, const D2D1_RECT_F& rect, bool checked, bool hovered, const wchar_t* labelText)
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

HICON MacroEditForm::GetFileIconForPreview(const std::wstring& path)
{
    if (path.empty()) return nullptr;
    SHFILEINFOW sfi{};
    DWORD_PTR hr = SHGetFileInfoW(path.c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON);
    if (hr) return sfi.hIcon;
    return nullptr;
}

void MacroEditForm::DrawIconPreview(ID2D1HwndRenderTarget* rt)
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
