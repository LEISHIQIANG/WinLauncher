#pragma once
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl.h>
#include <atomic>
#include <string>
#include "UIStyle.h"

using Microsoft::WRL::ComPtr;

class TextBox
{
public:
    TextBox();
    ~TextBox();

    bool Create(HWND parentHWND, IDWriteFactory* dwriteFactory, const D2D1_RECT_F& logicalBounds, const std::wstring& defaultText = L"");
    void Destroy();

    void Paint(ID2D1HwndRenderTarget* rt, float scale);
    void UpdateLayout(float scale);

    void OnKeyDown(HWND hWnd, WPARAM wParam, LPARAM lParam, bool& repaint);
    void OnChar(HWND hWnd, WPARAM wParam, bool& repaint);

    void OnLButtonDown(HWND hWnd, POINT pt, float scale, bool& repaint);
    void OnLButtonDblClk(HWND hWnd, POINT pt, float scale, bool& repaint);
    void OnLButtonUp(HWND hWnd, POINT pt, float scale, bool& repaint);
    void OnMouseMove(HWND hWnd, POINT pt, float scale, bool& repaint);
    void OnMouseWheel(HWND hWnd, short zDelta, POINT pt, float scale, bool& repaint);
    void OnRButtonUp(HWND hWnd, bool& repaint);

    void BlinkCaret();
    void ResetCaretBlink();

    void UpdateImeWindowPosition(HWND hWnd, float scale);
    bool HandleImeMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, bool& repaint);

    void CopyToClipboard(HWND hWnd);
    void CutToClipboard(HWND hWnd, bool& repaint);
    void PasteFromClipboard(HWND hWnd, bool& repaint);

    std::wstring GetText() const { return m_text; }
    void SetText(const std::wstring& text);
    void SetCompositionText(const std::wstring& compText, size_t caretOffset);
    bool IsEmpty() const { return m_text.empty() && m_compText.empty(); }

    void SetBounds(const D2D1_RECT_F& bounds) { m_bounds = bounds; }
    D2D1_RECT_F GetBounds() const { return m_bounds; }

    void SetFocus(bool focus);
    bool HasFocus() const { return m_hasFocus; }
    static bool IsAnyTextBoxFocused();

    bool HitTest(POINT pt) const
    {
        return (pt.x >= m_bounds.left && pt.x <= m_bounds.right &&
                pt.y >= m_bounds.top && pt.y <= m_bounds.bottom);
    }

    void SetStyle(const UIStyle::TextBoxStyle& style);
    const UIStyle::TextBoxStyle& GetStyle() const { return m_style; }

    void SetMultiline(bool ml) { m_multiline = ml; }
    bool IsMultiline() const { return m_multiline; }

private:
    void RecreateTextLayout();
    void UpdateScrollRange();
    void DrawScrollbar(ID2D1HwndRenderTarget* rt, float scale);
    void EnsureCaretVisible();
    bool HitTestScrollbar(POINT pt, float scale) const;
    bool DeleteSelection(bool& repaint);
    size_t GetCaretIndexFromPoint(POINT pt, float scale) const;
    void MoveCaretVertical(HWND hWnd, int direction, bool shift, bool& repaint); // direction: -1 up, +1 down

    HWND m_parent = nullptr;
    IDWriteFactory* m_dwFactory = nullptr;
    ComPtr<IDWriteTextLayout> m_textLayout;

    std::wstring m_text;
    size_t m_caretIndex = 0;
    size_t m_selStart = 0;
    size_t m_selEnd = 0;

    D2D1_RECT_F m_bounds = {};
    bool m_hasFocus = false;
    bool m_caretVisible = false;
    bool m_dragSelecting = false;
    bool m_multiline = false;
    bool m_draggingScrollbar = false;
    UIStyle::TextBoxStyle m_style;
    std::wstring m_compText;
    size_t m_compCaretOffset = 0;

    // Scroll state (multiline only)
    float m_scrollOffset = 0.0f;   // current scroll position in pixels
    float m_scrollMax    = 0.0f;   // max scroll range in pixels
    float m_sbDragStartY = 0.0f;   // mouse Y when scrollbar drag started
    float m_sbDragStartOff = 0.0f; // scrollOffset when drag started

    // Triple-click tracking
    int     m_clickCount = 0;
    DWORD   m_lastClickTime = 0;
    size_t  m_tripleClickLineStart = 0;
    size_t  m_tripleClickLineEnd = 0;

    static std::atomic<int> s_focusedTextBoxCount;
};
