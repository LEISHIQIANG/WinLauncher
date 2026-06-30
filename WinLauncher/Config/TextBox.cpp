#define NOMINMAX
#include "TextBox.h"
#include "../DpiHelper.h"
#include <commctrl.h>
#include <vector>
#include <cwctype>
#include <imm.h>
#include <algorithm>

#pragma comment(lib, "imm32.lib")

std::atomic<int> TextBox::s_focusedTextBoxCount{0};

TextBox::TextBox()
    : m_parent(nullptr)
    , m_dwFactory(nullptr)
    , m_caretIndex(0)
    , m_selStart(0)
    , m_selEnd(0)
    , m_bounds{}
    , m_hasFocus(false)
    , m_caretVisible(false)
    , m_dragSelecting(false)
    , m_style()
    , m_clickCount(0)
    , m_lastClickTime(0)
    , m_tripleClickLineStart(0)
    , m_tripleClickLineEnd(0)
{
}

TextBox::~TextBox()
{
    Destroy();
}

bool TextBox::Create(HWND parentHWND, IDWriteFactory* dwriteFactory, const D2D1_RECT_F& logicalBounds, const std::wstring& defaultText)
{
    SetFocus(false);
    m_parent = parentHWND;
    m_dwFactory = dwriteFactory;
    m_bounds = logicalBounds;
    m_text = defaultText;
    m_caretIndex = m_text.size();
    m_selStart = m_caretIndex;
    m_selEnd = m_caretIndex;
    m_caretVisible = false;
    m_dragSelecting = false;
    RecreateTextLayout();
    return true;
}

void TextBox::Destroy()
{
    SetFocus(false);
    m_textLayout.Reset();
}

void TextBox::SetFocus(bool focus)
{
    if (m_hasFocus == focus)
    {
        if (focus) ResetCaretBlink();
        return;
    }

    m_hasFocus = focus;
    if (focus)
    {
        s_focusedTextBoxCount.fetch_add(1, std::memory_order_relaxed);
    }
    else
    {
        int current = s_focusedTextBoxCount.load(std::memory_order_relaxed);
        while (current > 0 &&
               !s_focusedTextBoxCount.compare_exchange_weak(
                   current,
                   current - 1,
                   std::memory_order_relaxed,
                   std::memory_order_relaxed))
        {
        }
    }
    ResetCaretBlink();
}

bool TextBox::IsAnyTextBoxFocused()
{
    return s_focusedTextBoxCount.load(std::memory_order_relaxed) > 0;
}

void TextBox::RecreateTextLayout()
{
    m_textLayout.Reset();
    if (!m_dwFactory) return;

    DWRITE_WORD_WRAPPING wrapMode = m_multiline ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP;

    ComPtr<IDWriteTextFormat> tf;
    HRESULT hr = UIStyle::Typography::CreateTextFormat(
        m_dwFactory,
        &tf,
        m_style.fontSize,
        m_style.fontWeight,
        DWRITE_TEXT_ALIGNMENT_LEADING,
        DWRITE_PARAGRAPH_ALIGNMENT_NEAR,
        wrapMode);
    if (SUCCEEDED(hr))
    {
        float maxW = m_bounds.right - m_bounds.left - (m_style.paddingLeft + m_style.paddingRight);
        // For multiline, use a very large height so the layout measures full content
        float maxH = m_multiline ? 1e6f : (m_bounds.bottom - m_bounds.top - (m_style.paddingTop + m_style.paddingBottom));

        size_t safeCaret = std::min(m_caretIndex, m_text.size());
        std::wstring displayText = m_text.substr(0, safeCaret) + m_compText + m_text.substr(safeCaret);

        m_dwFactory->CreateTextLayout(
            displayText.c_str(),
            (UINT32)displayText.size(),
            tf.Get(),
            maxW > 0 ? maxW : 1000.0f,
            maxH > 0 ? maxH : 100.0f,
            &m_textLayout
        );

        if (!m_compText.empty() && m_textLayout)
        {
            m_textLayout->SetUnderline(TRUE, DWRITE_TEXT_RANGE{ (UINT32)safeCaret, (UINT32)m_compText.size() });
        }

        UpdateScrollRange();
        if (m_multiline) EnsureCaretVisible();
    }
}

void TextBox::Paint(ID2D1HwndRenderTarget* rt, float scale)
{
    D2D1_ROUNDED_RECT roundedEdit = D2D1::RoundedRect(m_bounds, m_style.cornerRadius, m_style.cornerRadius);

    ComPtr<ID2D1SolidColorBrush> bgBrush;
    rt->CreateSolidColorBrush(m_style.bgNormal.d2d, &bgBrush);
    if (bgBrush) rt->FillRoundedRectangle(roundedEdit, bgBrush.Get());

    float textX = m_bounds.left + m_style.paddingLeft;
    float textY = m_bounds.top + m_style.paddingTop;
    float padR = m_multiline ? m_style.paddingRight + 6.0f : m_style.paddingRight; // extra room for scrollbar
    D2D1_POINT_2F textOrigin = D2D1::Point2F(textX, textY - m_scrollOffset);

    // Push clip rect to prevent text/selection/caret drawing outside the textbox padding bounds
    D2D1_RECT_F clipRect = D2D1::RectF(
        m_bounds.left + m_style.paddingLeft,
        m_bounds.top + m_style.paddingTop,
        m_bounds.right - padR,
        m_bounds.bottom - m_style.paddingBottom
    );
    rt->PushAxisAlignedClip(clipRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    if (m_selStart != m_selEnd && m_textLayout)
    {
        size_t start = std::min(m_selStart, m_selEnd);
        size_t len = std::max(m_selStart, m_selEnd) - start;
        UINT32 actualCount = 0;
        m_textLayout->HitTestTextRange((UINT32)start, (UINT32)len, 0, 0, nullptr, 0, &actualCount);
        if (actualCount > 0)
        {
            std::vector<DWRITE_HIT_TEST_METRICS> metrics(actualCount);
            m_textLayout->HitTestTextRange((UINT32)start, (UINT32)len, 0, 0, metrics.data(), actualCount, &actualCount);

            D2D1_COLOR_F selColor = UIStyle::ThemeColor::Accent().d2d;
            selColor.a = 0.22f;
            ComPtr<ID2D1SolidColorBrush> selBrush;
            rt->CreateSolidColorBrush(selColor, &selBrush);
            if (selBrush)
            {
                for (const auto& m : metrics)
                {
                    D2D1_RECT_F r = D2D1::RectF(
                        textX + m.left, textY + m.top - m_scrollOffset,
                        textX + m.left + m.width, textY + m.top + m.height - m_scrollOffset
                    );
                    rt->FillRectangle(r, selBrush.Get());
                }
            }
        }
    }

    if (m_textLayout)
    {
        size_t safeCaret = std::min(m_caretIndex, m_text.size());
        std::wstring displayText = m_text.substr(0, safeCaret) + m_compText + m_text.substr(safeCaret);

        m_textLayout->SetDrawingEffect(nullptr, DWRITE_TEXT_RANGE{ 0, (UINT32)displayText.size() });

        size_t pos = 0;
        ComPtr<ID2D1SolidColorBrush> accentBrush;
        rt->CreateSolidColorBrush(UIStyle::ThemeColor::Accent().d2d, &accentBrush);

        while (accentBrush)
        {
            size_t start = displayText.find(L"{{", pos);
            if (start == std::wstring::npos)
                break;
            size_t end = displayText.find(L"}}", start);
            if (end == std::wstring::npos)
                break;

            UINT32 rangeStart = (UINT32)start;
            UINT32 rangeLength = (UINT32)(end + 2 - start);
            m_textLayout->SetDrawingEffect(accentBrush.Get(), DWRITE_TEXT_RANGE{ rangeStart, rangeLength });
            
            pos = end + 2;
        }

        ComPtr<ID2D1SolidColorBrush> textBrush;
        rt->CreateSolidColorBrush(m_style.textNormal.d2d, &textBrush);
        if (textBrush) rt->DrawTextLayout(textOrigin, m_textLayout.Get(), textBrush.Get());
    }

    if (m_hasFocus && m_caretVisible && m_textLayout)
    {
        float caretX = 0, caretY = 0;
        DWRITE_HIT_TEST_METRICS metrics;
        BOOL isTrailing = FALSE;
        size_t drawCaretIndex = m_caretIndex + m_compCaretOffset;
        size_t displayTextSize = m_text.size() + m_compText.size();
        if (drawCaretIndex > displayTextSize) drawCaretIndex = displayTextSize;
        m_textLayout->HitTestTextPosition((UINT32)drawCaretIndex, isTrailing, &caretX, &caretY, &metrics);

        D2D1_RECT_F caretRect = D2D1::RectF(
            textX + caretX - 0.75f, textY + caretY - m_scrollOffset,
            textX + caretX + 0.75f, textY + caretY - m_scrollOffset + metrics.height
        );

        ComPtr<ID2D1SolidColorBrush> caretBrush;
        rt->CreateSolidColorBrush(m_style.textNormal.d2d, &caretBrush);
        if (caretBrush) rt->FillRectangle(caretRect, caretBrush.Get());
    }

    rt->PopAxisAlignedClip();

    // Draw scrollbar if multiline and content overflows
    if (m_multiline && m_scrollMax > 0.0f)
    {
        DrawScrollbar(rt, scale);
    }

    ComPtr<ID2D1SolidColorBrush> borderBrush;
    if (m_hasFocus)
        rt->CreateSolidColorBrush(m_style.borderFocused.d2d, &borderBrush);
    else
        rt->CreateSolidColorBrush(m_style.borderNormal.d2d, &borderBrush);

    if (borderBrush)
        rt->DrawRoundedRectangle(roundedEdit, borderBrush.Get(), m_style.borderThickness);
}

void TextBox::UpdateLayout(float scale)
{
    RecreateTextLayout();
}

static bool IsWordBoundary(wchar_t ch)
{
    if (iswspace(ch)) return true;
    if ((ch >= 0x4E00 && ch <= 0x9FFF) ||
        (ch >= 0x3400 && ch <= 0x4DBF) ||
        (ch >= 0x2E80 && ch <= 0x2EFF) ||
        (ch >= 0x3000 && ch <= 0x303F) ||
        (ch >= 0xFF00 && ch <= 0xFFEF) ||
        (ch >= 0xF900 && ch <= 0xFAFF) ||
        (ch >= 0xAC00 && ch <= 0xD7AF))
        return true;
    if (ispunct(ch)) return true;
    return false;
}

void TextBox::OnKeyDown(HWND hWnd, WPARAM wParam, LPARAM lParam, bool& repaint)
{
    if (!m_hasFocus) return;

    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

    if (ctrl)
    {
        if (wParam == 'A' || wParam == 'a')
        {
            m_selStart = 0;
            m_selEnd = m_text.size();
            m_caretIndex = m_selEnd;
            ResetCaretBlink();
            repaint = true;
        }
        else if (wParam == 'C' || wParam == 'c') { CopyToClipboard(hWnd); }
        else if (wParam == 'V' || wParam == 'v') { PasteFromClipboard(hWnd, repaint); }
        else if (wParam == 'X' || wParam == 'x') { CutToClipboard(hWnd, repaint); }
        else if (wParam == VK_LEFT)
        {
            if (m_caretIndex > 0)
            {
                size_t pos = m_caretIndex - 1;
                while (pos > 0 && !IsWordBoundary(m_text[pos]))
                    pos--;
                if (pos > 0) pos++;
                m_caretIndex = pos;
            }
            m_selEnd = m_caretIndex;
            if (!shift) m_selStart = m_selEnd;
            ResetCaretBlink();
            if (m_multiline) EnsureCaretVisible();
            UpdateImeWindowPosition(hWnd, DpiHelper::GetWindowScale(hWnd));
            repaint = true;
        }
        else if (wParam == VK_RIGHT)
        {
            if (m_caretIndex < m_text.size())
            {
                size_t pos = m_caretIndex + 1;
                while (pos < m_text.size() && !IsWordBoundary(m_text[pos]))
                    pos++;
                m_caretIndex = pos;
            }
            m_selEnd = m_caretIndex;
            if (!shift) m_selStart = m_selEnd;
            ResetCaretBlink();
            if (m_multiline) EnsureCaretVisible();
            UpdateImeWindowPosition(hWnd, DpiHelper::GetWindowScale(hWnd));
            repaint = true;
        }
        return;
    }

    if (wParam == VK_LEFT)
    {
        if (m_caretIndex > 0)
        {
            m_caretIndex--;
            m_selEnd = m_caretIndex;
            if (!shift) m_selStart = m_selEnd;
            ResetCaretBlink();
            UpdateImeWindowPosition(hWnd, DpiHelper::GetWindowScale(hWnd));
            if (m_multiline) EnsureCaretVisible();
            repaint = true;
        }
        else if (!shift && m_selStart != m_selEnd)
        {
            m_caretIndex = std::min(m_selStart, m_selEnd);
            m_selStart = m_selEnd = m_caretIndex;
            ResetCaretBlink();
            UpdateImeWindowPosition(hWnd, DpiHelper::GetWindowScale(hWnd));
            repaint = true;
        }
    }
    else if (wParam == VK_RIGHT)
    {
        if (m_caretIndex < m_text.size())
        {
            m_caretIndex++;
            m_selEnd = m_caretIndex;
            if (!shift) m_selStart = m_selEnd;
            ResetCaretBlink();
            UpdateImeWindowPosition(hWnd, DpiHelper::GetWindowScale(hWnd));
            if (m_multiline) EnsureCaretVisible();
            repaint = true;
        }
        else if (!shift && m_selStart != m_selEnd)
        {
            m_caretIndex = std::max(m_selStart, m_selEnd);
            m_selStart = m_selEnd = m_caretIndex;
            ResetCaretBlink();
            UpdateImeWindowPosition(hWnd, DpiHelper::GetWindowScale(hWnd));
            repaint = true;
        }
    }
    else if (wParam == VK_UP && m_multiline)
    {
        MoveCaretVertical(hWnd, -1, shift, repaint);
    }
    else if (wParam == VK_DOWN && m_multiline)
    {
        MoveCaretVertical(hWnd, 1, shift, repaint);
    }
    else if (wParam == VK_HOME)
    {
        // In multiline, Home goes to line start first; double-tap goes to text start
        if (m_multiline && m_textLayout)
        {
            float caretX = 0, caretY = 0;
            DWRITE_HIT_TEST_METRICS cm;
            BOOL trail = FALSE;
            m_textLayout->HitTestTextPosition((UINT32)m_caretIndex, trail, &caretX, &caretY, &cm);

            // Find the start of current line
            BOOL isInside = FALSE;
            DWRITE_HIT_TEST_METRICS lm;
            m_textLayout->HitTestPoint(caretX, caretY, &trail, &isInside, &lm);

            size_t lineStart = lm.textPosition;
            if (m_caretIndex == lineStart && lineStart > 0)
            {
                // Already at line start, go to text start
                m_caretIndex = 0;
            }
            else
            {
                m_caretIndex = lineStart;
            }
        }
        else
        {
            m_caretIndex = 0;
        }
        m_selEnd = m_caretIndex;
        if (!shift) m_selStart = m_selEnd;
        ResetCaretBlink();
        UpdateImeWindowPosition(hWnd, DpiHelper::GetWindowScale(hWnd));
        if (m_multiline) EnsureCaretVisible();
        repaint = true;
    }
    else if (wParam == VK_END)
    {
        // In multiline, End goes to line end
        if (m_multiline && m_textLayout)
        {
            float caretX = 0, caretY = 0;
            DWRITE_HIT_TEST_METRICS cm;
            BOOL trail = FALSE;
            m_textLayout->HitTestTextPosition((UINT32)m_caretIndex, trail, &caretX, &caretY, &cm);

            // Find a point near the right edge of this line to get its last character
            BOOL isInside = FALSE;
            DWRITE_HIT_TEST_METRICS lm;
            m_textLayout->HitTestPoint(caretX + 10000.0f, caretY + cm.height * 0.5f, &trail, &isInside, &lm);
            size_t lineEnd = lm.textPosition;
            if (isInside && trail) lineEnd++;
            if (lineEnd > m_text.size()) lineEnd = m_text.size();

            m_caretIndex = lineEnd;
        }
        else
        {
            m_caretIndex = m_text.size();
        }
        m_selEnd = m_caretIndex;
        if (!shift) m_selStart = m_selEnd;
        ResetCaretBlink();
        UpdateImeWindowPosition(hWnd, DpiHelper::GetWindowScale(hWnd));
        if (m_multiline) EnsureCaretVisible();
        repaint = true;
    }
    else if (wParam == VK_BACK)
    {
        if (!DeleteSelection(repaint))
        {
            if (m_caretIndex > 0)
            {
                m_text.erase(m_caretIndex - 1, 1);
                m_caretIndex--;
                m_selStart = m_caretIndex;
                m_selEnd = m_caretIndex;
                RecreateTextLayout();
                ResetCaretBlink();
                UpdateImeWindowPosition(hWnd, DpiHelper::GetWindowScale(hWnd));
                repaint = true;
            }
        }
    }
    else if (wParam == VK_DELETE)
    {
        if (!DeleteSelection(repaint))
        {
            if (m_caretIndex < m_text.size())
            {
                m_text.erase(m_caretIndex, 1);
                RecreateTextLayout();
                ResetCaretBlink();
                UpdateImeWindowPosition(hWnd, DpiHelper::GetWindowScale(hWnd));
                repaint = true;
            }
        }
    }
    else if (wParam == VK_RETURN)
    {
        if (m_multiline)
        {
            DeleteSelection(repaint);
            m_text.insert(m_caretIndex, L"\r\n");
            m_caretIndex += 2;
            m_selStart = m_caretIndex;
            m_selEnd = m_caretIndex;
            RecreateTextLayout();
            ResetCaretBlink();
            UpdateImeWindowPosition(hWnd, DpiHelper::GetWindowScale(hWnd));
            repaint = true;
        }
        else
        {
            PostMessageW(m_parent, WM_COMMAND, MAKEWPARAM(IDOK, 0), 0);
        }
    }
    else if (wParam == VK_ESCAPE)
    {
        PostMessageW(m_parent, WM_COMMAND, MAKEWPARAM(IDCANCEL, 0), 0);
    }
}

void TextBox::OnChar(HWND hWnd, WPARAM wParam, bool& repaint)
{
    if (!m_hasFocus) return;

    wchar_t ch = (wchar_t)wParam;
    if (ch >= 32 && ch != 127)
    {
        if (GetKeyState(VK_CONTROL) & 0x8000) return;

        DeleteSelection(repaint);
        m_text.insert(m_caretIndex, 1, ch);
        m_caretIndex++;
        m_selStart = m_caretIndex;
        m_selEnd = m_caretIndex;
        RecreateTextLayout();
        ResetCaretBlink();
        UpdateImeWindowPosition(hWnd, DpiHelper::GetWindowScale(hWnd));
        repaint = true;
    }
}

void TextBox::OnLButtonDown(HWND hWnd, POINT pt, float scale, bool& repaint)
{
    POINT logicalPt{ (long)(pt.x / scale), (long)(pt.y / scale) };

    // Check scrollbar drag first (multiline only)
    if (m_multiline && m_scrollMax > 0.0f && HitTestScrollbar(pt, scale))
    {
        m_draggingScrollbar = true;
        m_sbDragStartY = pt.y / scale;
        m_sbDragStartOff = m_scrollOffset;
        SetCapture(hWnd);
        return;
    }

    if (!HitTest(logicalPt))
        return;

    // Triple-click tracking
    DWORD now = GetTickCount();
    if (now - m_lastClickTime > GetDoubleClickTime())
    {
        m_clickCount = 0;
    }
    m_lastClickTime = now;

    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

    m_dragSelecting = true;
    SetCapture(hWnd);
    m_caretIndex = GetCaretIndexFromPoint(pt, scale);

    if (shift)
    {
        m_selEnd = m_caretIndex;
        if (m_selStart == m_selEnd)
        {
            m_selStart = m_caretIndex;
        }
    }
    else
    {
        m_selStart = m_caretIndex;
        m_selEnd = m_caretIndex;
    }

    ResetCaretBlink();
    UpdateImeWindowPosition(hWnd, scale);
    repaint = true;
}

void TextBox::OnLButtonDblClk(HWND hWnd, POINT pt, float scale, bool& repaint)
{
    POINT logicalPt{ (long)(pt.x / scale), (long)(pt.y / scale) };
    if (!HitTest(logicalPt))
        return;

    m_clickCount = 2;
    m_caretIndex = GetCaretIndexFromPoint(pt, scale);

    size_t start = m_caretIndex;
    if (start < m_text.size())
    {
        // If at word boundary, expand differently
        if (IsWordBoundary(m_text[start]))
        {
            // Include consecutive word boundaries
            while (start > 0 && IsWordBoundary(m_text[start - 1]))
                start--;
            size_t end = m_caretIndex;
            while (end < m_text.size() && IsWordBoundary(m_text[end]))
                end++;
            m_selStart = start;
            m_selEnd = end;
        }
        else
        {
            // Non-boundary: find word extent
            while (start > 0 && !IsWordBoundary(m_text[start - 1]))
                start--;
            size_t end = m_caretIndex;
            while (end < m_text.size() && !IsWordBoundary(m_text[end]))
                end++;
            m_selStart = start;
            m_selEnd = end;
        }
    }
    else
    {
        m_selStart = m_caretIndex;
        m_selEnd = m_caretIndex;
    }
    m_caretIndex = m_selEnd;
    m_dragSelecting = false;
    ResetCaretBlink();
    UpdateImeWindowPosition(hWnd, scale);
    repaint = true;
}

void TextBox::OnMouseMove(HWND hWnd, POINT pt, float scale, bool& repaint)
{
    if (m_draggingScrollbar)
    {
        float trackH = m_bounds.bottom - m_bounds.top - 4.0f; // 2px margin each side
        float visibleH = trackH;
        float contentH = visibleH + m_scrollMax;
        float thumbH = std::max(20.0f, visibleH * visibleH / contentH);
        float trackRange = trackH - thumbH;
        float mouseY = pt.y / scale;
        float dy = mouseY - m_sbDragStartY;
        if (trackRange > 0.0f)
            m_scrollOffset = std::max(0.0f, std::min(m_scrollMax, m_sbDragStartOff + dy * (m_scrollMax / trackRange)));
        repaint = true;
        return;
    }

    if (m_dragSelecting)
    {
        m_caretIndex = GetCaretIndexFromPoint(pt, scale);
        m_selEnd = m_caretIndex;
        ResetCaretBlink();
        UpdateImeWindowPosition(hWnd, scale);
        repaint = true;
    }
}

void TextBox::OnLButtonUp(HWND hWnd, POINT pt, float scale, bool& repaint)
{
    if (m_draggingScrollbar)
    {
        m_draggingScrollbar = false;
        ReleaseCapture();
        repaint = true;
        return;
    }

    if (m_dragSelecting)
    {
        m_dragSelecting = false;
        ReleaseCapture();
        repaint = true;
    }

    // Triple-click: select entire line
    if (m_clickCount >= 2 && m_multiline && m_textLayout)
    {
        DWORD now = GetTickCount();
        if (now - m_lastClickTime <= GetDoubleClickTime())
        {
            size_t clickIndex = GetCaretIndexFromPoint(pt, scale);

            DWRITE_HIT_TEST_METRICS metrics;
            BOOL isTrailing = FALSE;
            BOOL isInside = FALSE;
            float localX = pt.x / scale - (m_bounds.left + m_style.paddingLeft);
            float localY = pt.y / scale - (m_bounds.top + m_style.paddingTop) + m_scrollOffset;
            m_textLayout->HitTestPoint(localX, localY, &isTrailing, &isInside, &metrics);

            size_t lineStart = metrics.textPosition;
            // Find beginning of this line
            if (lineStart > 0)
            {
                // Walk backwards from lineStart to find start of line
                float testX = 0, testY = 0;
                BOOL trail2 = FALSE;
                DWRITE_HIT_TEST_METRICS lm;
                m_textLayout->HitTestTextPosition((UINT32)lineStart, trail2, &testX, &testY, &lm);
                if (testX > 0)
                {
                    // This isn't the start of the line, find line start by hitting at x=0
                    m_textLayout->HitTestPoint(0, testY + lm.height * 0.5f, &trail2, &isInside, &lm);
                    lineStart = lm.textPosition;
                }
            }

            // Find end of this line by hitting at a point far right
            DWRITE_HIT_TEST_METRICS rm;
            m_textLayout->HitTestPoint(100000.0f, metrics.top + metrics.height * 0.5f, &isTrailing, &isInside, &rm);
            size_t lineEnd = rm.textPosition;
            if (isInside && isTrailing) lineEnd++;
            if (lineEnd > m_text.size()) lineEnd = m_text.size();

            m_selStart = lineStart;
            m_selEnd = lineEnd;
            m_caretIndex = lineEnd;
            m_clickCount = 0;
            ResetCaretBlink();
            repaint = true;
        }
    }
}

void TextBox::BlinkCaret()
{
    if (!m_hasFocus) return;
    m_caretVisible = !m_caretVisible;
}

void TextBox::ResetCaretBlink()
{
    m_caretVisible = true;
}

void TextBox::UpdateImeWindowPosition(HWND hWnd, float scale)
{
    HIMC hIMC = ImmGetContext(hWnd);
    if (hIMC)
    {
        float caretX = 0, caretY = 0;
        float caretH = (float)m_style.fontSize;
        if (m_textLayout)
        {
            DWRITE_HIT_TEST_METRICS metrics;
            BOOL isTrailing = FALSE;
            m_textLayout->HitTestTextPosition((UINT32)m_caretIndex, isTrailing, &caretX, &caretY, &metrics);
            caretH = metrics.height;
        }

        float localX = m_bounds.left + m_style.paddingLeft + caretX;
        float localY = m_bounds.top + m_style.paddingTop + caretY - m_scrollOffset;

        POINT pt = { (int)(localX * scale), (int)(localY * scale) };

        COMPOSITIONFORM cof;
        cof.dwStyle = CFS_POINT;
        cof.ptCurrentPos = pt;
        ImmSetCompositionWindow(hIMC, &cof);

        LOGFONTW lf{};
        lf.lfHeight = -(int)(m_style.fontSize * scale);
        lf.lfWeight = FW_NORMAL;
        lf.lfCharSet = DEFAULT_CHARSET;
        wcscpy_s(lf.lfFaceName, m_style.fontFamily.c_str());
        ImmSetCompositionFontW(hIMC, &lf);

        CANDIDATEFORM cf;
        cf.dwIndex = 0;
        cf.dwStyle = CFS_CANDIDATEPOS;
        cf.ptCurrentPos.x = pt.x;
        cf.ptCurrentPos.y = pt.y + (int)(caretH * scale);
        ImmSetCandidateWindow(hIMC, &cf);

        ImmReleaseContext(hWnd, hIMC);
    }
}

void TextBox::CopyToClipboard(HWND hWnd)
{
    size_t start = std::min(m_selStart, m_selEnd);
    size_t end = std::max(m_selStart, m_selEnd);
    if (start == end) return;

    std::wstring selectedText = m_text.substr(start, end - start);
    if (OpenClipboard(hWnd))
    {
        EmptyClipboard();
        size_t sizeInBytes = (selectedText.size() + 1) * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, sizeInBytes);
        if (hMem)
        {
            void* pMem = GlobalLock(hMem);
            if (pMem)
            {
                memcpy(pMem, selectedText.c_str(), sizeInBytes);
                GlobalUnlock(hMem);
                SetClipboardData(CF_UNICODETEXT, hMem);
            }
        }
        CloseClipboard();
    }
}

void TextBox::CutToClipboard(HWND hWnd, bool& repaint)
{
    CopyToClipboard(hWnd);
    DeleteSelection(repaint);
}

void TextBox::PasteFromClipboard(HWND hWnd, bool& repaint)
{
    if (IsClipboardFormatAvailable(CF_UNICODETEXT))
    {
        if (OpenClipboard(hWnd))
        {
            HANDLE hData = GetClipboardData(CF_UNICODETEXT);
            if (hData)
            {
                wchar_t* pText = (wchar_t*)GlobalLock(hData);
                if (pText)
                {
                    std::wstring pastedText(pText);
                    GlobalUnlock(hData);

                    DeleteSelection(repaint);
                    m_text.insert(m_caretIndex, pastedText);
                    m_caretIndex += pastedText.size();
                    m_selStart = m_caretIndex;
                    m_selEnd = m_caretIndex;
                    RecreateTextLayout();
                    ResetCaretBlink();
                    UpdateImeWindowPosition(hWnd, DpiHelper::GetWindowScale(hWnd));
                    repaint = true;
                }
            }
            CloseClipboard();
        }
    }
}

void TextBox::OnRButtonUp(HWND hWnd, bool& repaint)
{
    // Placeholder for future context menu
}

void TextBox::SetText(const std::wstring& text)
{
    m_text = text;
    m_caretIndex = m_text.size();
    m_selStart = m_caretIndex;
    m_selEnd = m_caretIndex;
    m_compText.clear();
    m_compCaretOffset = 0;
    RecreateTextLayout();
    ResetCaretBlink();
}

void TextBox::SetCompositionText(const std::wstring& compText, size_t caretOffset)
{
    m_compText = compText;
    m_compCaretOffset = caretOffset;
    RecreateTextLayout();
}

bool TextBox::HandleImeMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, bool& repaint)
{
    if (!m_hasFocus) return false;

    switch (uMsg)
    {
    case WM_IME_STARTCOMPOSITION:
    {
        SetCompositionText(L"", 0);
        UpdateImeWindowPosition(hWnd, DpiHelper::GetWindowScale(hWnd));
        return true;
    }
    case WM_IME_COMPOSITION:
    {
        HIMC hIMC = ImmGetContext(hWnd);
        if (hIMC)
        {
            if (lParam & GCS_RESULTSTR)
            {
                SetCompositionText(L"", 0);
                ImmReleaseContext(hWnd, hIMC);
                return false; // Let DefWindowProc translate GCS_RESULTSTR into WM_CHAR!
            }
            if (lParam & GCS_COMPSTR)
            {
                int lenBytes = ImmGetCompositionStringW(hIMC, GCS_COMPSTR, nullptr, 0);
                std::wstring compStr;
                int caretOffset = 0;
                if (lenBytes > 0)
                {
                    std::vector<wchar_t> buf(lenBytes / sizeof(wchar_t) + 1);
                    ImmGetCompositionStringW(hIMC, GCS_COMPSTR, buf.data(), lenBytes);
                    compStr = std::wstring(buf.data(), lenBytes / sizeof(wchar_t));
                    
                    int pos = ImmGetCompositionStringW(hIMC, GCS_CURSORPOS, nullptr, 0);
                    if (pos >= 0)
                    {
                        caretOffset = pos;
                    }
                }
                SetCompositionText(compStr, caretOffset);
                UpdateImeWindowPosition(hWnd, DpiHelper::GetWindowScale(hWnd));
                repaint = true;
                ImmReleaseContext(hWnd, hIMC);
                return true; // Suppress default composition window
            }
            ImmReleaseContext(hWnd, hIMC);
        }
        break;
    }
    case WM_IME_ENDCOMPOSITION:
    {
        SetCompositionText(L"", 0);
        repaint = true;
        return true;
    }
    }
    return false;
}

void TextBox::SetStyle(const UIStyle::TextBoxStyle& style)
{
    m_style = style;
    RecreateTextLayout();
}

bool TextBox::DeleteSelection(bool& repaint)
{
    size_t start = std::min(m_selStart, m_selEnd);
    size_t end = std::max(m_selStart, m_selEnd);
    if (start != end)
    {
        m_text.erase(start, end - start);
        m_caretIndex = start;
        m_selStart = m_caretIndex;
        m_selEnd = m_caretIndex;
        RecreateTextLayout();
        ResetCaretBlink();
        repaint = true;
        return true;
    }
    return false;
}

void TextBox::MoveCaretVertical(HWND hWnd, int direction, bool shift, bool& repaint)
{
    if (!m_textLayout || !m_multiline) return;

    // Get current caret position in layout coordinates
    float caretX = 0, caretY = 0;
    DWRITE_HIT_TEST_METRICS cm;
    BOOL trail = FALSE;
    m_textLayout->HitTestTextPosition((UINT32)m_caretIndex, trail, &caretX, &caretY, &cm);

    // Target Y: move one line-height up or down from the baseline-ish position
    float lineH = cm.height > 0 ? cm.height : (float)m_style.fontSize * 1.4f;
    float targetY = caretY + direction * lineH * 1.1f; // slight overshoot to cross line boundary

    // Clamp to layout bounds
    DWRITE_TEXT_METRICS tm;
    m_textLayout->GetMetrics(&tm);
    if (targetY < 0) targetY = 0;
    if (targetY > tm.height) targetY = tm.height - 1.0f;

    // Hit-test at target Y to find new text position
    BOOL isInside = FALSE;
    DWRITE_HIT_TEST_METRICS hm;
    m_textLayout->HitTestPoint(caretX, targetY, &trail, &isInside, &hm);

    if (isInside || hm.textPosition < m_text.size())
    {
        size_t newIndex = hm.textPosition;
        if (trail && newIndex < m_text.size()) newIndex++;
        if (newIndex > m_text.size()) newIndex = m_text.size();

        m_caretIndex = newIndex;
        m_selEnd = m_caretIndex;
        if (!shift) m_selStart = m_selEnd;
        ResetCaretBlink();
        EnsureCaretVisible();
        UpdateImeWindowPosition(hWnd, DpiHelper::GetWindowScale(hWnd));
        repaint = true;
    }
}

size_t TextBox::GetCaretIndexFromPoint(POINT pt, float scale) const
{
    if (!m_textLayout) return 0;
    float localX = pt.x / scale - (m_bounds.left + m_style.paddingLeft);
    float localY = pt.y / scale - (m_bounds.top + m_style.paddingTop) + m_scrollOffset;

    BOOL isTrailing = FALSE;
    BOOL isInside = FALSE;
    DWRITE_HIT_TEST_METRICS metrics;
    m_textLayout->HitTestPoint(localX, localY, &isTrailing, &isInside, &metrics);

    size_t index = metrics.textPosition;
    if (isTrailing) index++;
    if (index > m_text.size()) index = m_text.size();
    return index;
}

void TextBox::UpdateScrollRange()
{
    if (!m_textLayout || !m_multiline)
    {
        m_scrollMax = 0.0f;
        return;
    }

    DWRITE_TEXT_METRICS tm;
    m_textLayout->GetMetrics(&tm);
    float visibleH = m_bounds.bottom - m_bounds.top - (m_style.paddingTop + m_style.paddingBottom);
    m_scrollMax = std::max(0.0f, tm.height - visibleH);
    if (m_scrollOffset > m_scrollMax)
        m_scrollOffset = m_scrollMax;
    if (m_scrollOffset < 0.0f)
        m_scrollOffset = 0.0f;
}

void TextBox::EnsureCaretVisible()
{
    if (!m_textLayout || !m_multiline) return;

    float caretX = 0, caretY = 0;
    DWRITE_HIT_TEST_METRICS metrics;
    BOOL isTrailing = FALSE;
    size_t drawCaretIndex = m_caretIndex + m_compCaretOffset;
    size_t displayTextSize = m_text.size() + m_compText.size();
    if (drawCaretIndex > displayTextSize) drawCaretIndex = displayTextSize;
    m_textLayout->HitTestTextPosition((UINT32)drawCaretIndex, isTrailing, &caretX, &caretY, &metrics);

    float visibleH = m_bounds.bottom - m_bounds.top - (m_style.paddingTop + m_style.paddingBottom);
    float caretTop = caretY;
    float caretBot = caretY + metrics.height;

    if (caretTop < m_scrollOffset)
        m_scrollOffset = caretTop;
    else if (caretBot > m_scrollOffset + visibleH)
        m_scrollOffset = caretBot - visibleH;

    if (m_scrollOffset < 0.0f) m_scrollOffset = 0.0f;
    if (m_scrollOffset > m_scrollMax) m_scrollOffset = m_scrollMax;
}

bool TextBox::HitTestScrollbar(POINT pt, float scale) const
{
    float sbX = m_bounds.right - 8.0f;
    float sbR = m_bounds.right - 2.0f;
    float localX = pt.x / scale;
    float localY = pt.y / scale;
    return (localX >= sbX && localX <= sbR &&
            localY >= m_bounds.top && localY <= m_bounds.bottom);
}

void TextBox::DrawScrollbar(ID2D1HwndRenderTarget* rt, float scale)
{
    float trackT = m_bounds.top + 2.0f;
    float trackH = m_bounds.bottom - m_bounds.top - 4.0f;
    float visibleH = trackH;
    float contentH = visibleH + m_scrollMax;
    float thumbH = std::max(20.0f, visibleH * visibleH / contentH);
    float thumbY = trackT + (trackH - thumbH) * (m_scrollOffset / m_scrollMax);

    float sbX = m_bounds.right - 6.0f;
    D2D1_RECT_F thumbRect = D2D1::RectF(sbX, thumbY, sbX + 4.0f, thumbY + thumbH);
    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(thumbRect, 2.0f, 2.0f);

    D2D1_COLOR_F sbColor = m_draggingScrollbar
        ? D2D1::ColorF(0.45f, 0.45f, 0.45f, 0.55f)
        : D2D1::ColorF(0.55f, 0.55f, 0.55f, 0.28f);

    ComPtr<ID2D1SolidColorBrush> sbBrush;
    rt->CreateSolidColorBrush(sbColor, &sbBrush);
    if (sbBrush) rt->FillRoundedRectangle(rr, sbBrush.Get());
}

void TextBox::OnMouseWheel(HWND hWnd, short zDelta, POINT pt, float scale, bool& repaint)
{
    if (m_scrollMax <= 0.0f) return;

    float lineH = (float)m_style.fontSize * 1.4f;
    float delta = -(zDelta / (float)WHEEL_DELTA) * lineH * 3.0f;
    m_scrollOffset = std::max(0.0f, std::min(m_scrollMax, m_scrollOffset + delta));
    repaint = true;
}
