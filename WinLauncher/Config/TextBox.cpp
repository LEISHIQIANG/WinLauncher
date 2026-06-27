#define NOMINMAX
#include "TextBox.h"
#include "../DpiHelper.h"
#include <commctrl.h>
#include <vector>
#include <imm.h>
#include <algorithm>

#pragma comment(lib, "imm32.lib")

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
{
}

TextBox::~TextBox()
{
    Destroy();
}

bool TextBox::Create(HWND parentHWND, IDWriteFactory* dwriteFactory, const D2D1_RECT_F& logicalBounds, const std::wstring& defaultText)
{
    m_parent = parentHWND;
    m_dwFactory = dwriteFactory;
    m_bounds = logicalBounds;
    m_text = defaultText;
    m_caretIndex = m_text.size();
    m_selStart = m_caretIndex;
    m_selEnd = m_caretIndex;
    m_hasFocus = false;
    m_caretVisible = false;
    m_dragSelecting = false;
    RecreateTextLayout();
    return true;
}

void TextBox::Destroy()
{
    m_textLayout.Reset();
}

void TextBox::RecreateTextLayout()
{
    m_textLayout.Reset();
    if (!m_dwFactory) return;

    ComPtr<IDWriteTextFormat> tf;
    HRESULT hr = m_dwFactory->CreateTextFormat(
        m_style.fontFamily.c_str(),
        nullptr,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        (FLOAT)m_style.fontSize,
        L"",
        &tf
    );
    if (SUCCEEDED(hr))
    {
        tf->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

        float maxW = m_bounds.right - m_bounds.left - (m_style.paddingLeft + m_style.paddingRight);
        float maxH = m_bounds.bottom - m_bounds.top - (m_style.paddingTop + m_style.paddingBottom);

        m_dwFactory->CreateTextLayout(
            m_text.c_str(),
            (UINT32)m_text.size(),
            tf.Get(),
            maxW > 0 ? maxW : 1000.0f,
            maxH > 0 ? maxH : 100.0f,
            &m_textLayout
        );
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
    D2D1_POINT_2F textOrigin = D2D1::Point2F(textX, textY);

    // Push clip rect to prevent text/selection/caret drawing outside the textbox padding bounds
    D2D1_RECT_F clipRect = D2D1::RectF(
        m_bounds.left + m_style.paddingLeft,
        m_bounds.top + m_style.paddingTop,
        m_bounds.right - m_style.paddingRight,
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
                        textX + m.left, textY + m.top,
                        textX + m.left + m.width, textY + m.top + m.height
                    );
                    rt->FillRectangle(r, selBrush.Get());
                }
            }
        }
    }

    if (m_textLayout)
    {
        ComPtr<ID2D1SolidColorBrush> textBrush;
        rt->CreateSolidColorBrush(m_style.textNormal.d2d, &textBrush);
        if (textBrush) rt->DrawTextLayout(textOrigin, m_textLayout.Get(), textBrush.Get());
    }

    if (m_hasFocus && m_caretVisible && m_textLayout)
    {
        float caretX = 0, caretY = 0;
        DWRITE_HIT_TEST_METRICS metrics;
        BOOL isTrailing = FALSE;
        m_textLayout->HitTestTextPosition((UINT32)m_caretIndex, isTrailing, &caretX, &caretY, &metrics);

        D2D1_RECT_F caretRect = D2D1::RectF(
            textX + caretX - 0.75f, textY + caretY,
            textX + caretX + 0.75f, textY + caretY + metrics.height
        );

        ComPtr<ID2D1SolidColorBrush> caretBrush;
        rt->CreateSolidColorBrush(m_style.textNormal.d2d, &caretBrush);
        if (caretBrush) rt->FillRectangle(caretRect, caretBrush.Get());
    }

    rt->PopAxisAlignedClip();

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

void TextBox::OnKeyDown(HWND hWnd, WPARAM wParam, LPARAM lParam, bool& repaint)
{
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
    else if (wParam == VK_HOME)
    {
        m_caretIndex = 0;
        m_selEnd = m_caretIndex;
        if (!shift) m_selStart = m_selEnd;
        ResetCaretBlink();
        UpdateImeWindowPosition(hWnd, DpiHelper::GetWindowScale(hWnd));
        repaint = true;
    }
    else if (wParam == VK_END)
    {
        m_caretIndex = m_text.size();
        m_selEnd = m_caretIndex;
        if (!shift) m_selStart = m_selEnd;
        ResetCaretBlink();
        UpdateImeWindowPosition(hWnd, DpiHelper::GetWindowScale(hWnd));
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
        PostMessageW(m_parent, WM_COMMAND, MAKEWPARAM(IDOK, 0), 0);
    }
    else if (wParam == VK_ESCAPE)
    {
        PostMessageW(m_parent, WM_COMMAND, MAKEWPARAM(IDCANCEL, 0), 0);
    }
}

void TextBox::OnChar(HWND hWnd, WPARAM wParam, bool& repaint)
{
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
    m_dragSelecting = true;
    SetCapture(hWnd);
    m_caretIndex = GetCaretIndexFromPoint(pt, scale);
    m_selStart = m_caretIndex;
    m_selEnd = m_caretIndex;
    ResetCaretBlink();
    UpdateImeWindowPosition(hWnd, scale);
    repaint = true;
}

void TextBox::OnMouseMove(HWND hWnd, POINT pt, float scale, bool& repaint)
{
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
    if (m_dragSelecting)
    {
        m_dragSelecting = false;
        ReleaseCapture();
        repaint = true;
    }
}

void TextBox::BlinkCaret()
{
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
        float localY = m_bounds.top + m_style.paddingTop + caretY;

        POINT pt = { (int)(localX * scale), (int)(localY * scale) };

        COMPOSITIONFORM cof;
        cof.dwStyle = CFS_POINT;
        cof.ptCurrentPos = pt;
        ImmSetCompositionWindow(hIMC, &cof);

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

void TextBox::SetText(const std::wstring& text)
{
    m_text = text;
    m_caretIndex = m_text.size();
    m_selStart = m_caretIndex;
    m_selEnd = m_caretIndex;
    RecreateTextLayout();
    ResetCaretBlink();
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

size_t TextBox::GetCaretIndexFromPoint(POINT pt, float scale) const
{
    if (!m_textLayout) return 0;
    float localX = pt.x / scale - (m_bounds.left + m_style.paddingLeft);
    float localY = pt.y / scale - (m_bounds.top + m_style.paddingTop);

    BOOL isTrailing = FALSE;
    BOOL isInside = FALSE;
    DWRITE_HIT_TEST_METRICS metrics;
    m_textLayout->HitTestPoint(localX, localY, &isTrailing, &isInside, &metrics);

    size_t index = metrics.textPosition;
    if (isTrailing) index++;
    return index;
}
