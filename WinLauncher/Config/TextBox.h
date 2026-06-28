#pragma once
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl.h>
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
    void OnMouseMove(HWND hWnd, POINT pt, float scale, bool& repaint);
    void OnLButtonUp(HWND hWnd, POINT pt, float scale, bool& repaint);

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

    void SetFocus(bool focus) { m_hasFocus = focus; ResetCaretBlink(); }
    bool HasFocus() const { return m_hasFocus; }

    bool HitTest(POINT pt) const
    {
        return (pt.x >= m_bounds.left && pt.x <= m_bounds.right &&
                pt.y >= m_bounds.top && pt.y <= m_bounds.bottom);
    }

    void SetStyle(const UIStyle::TextBoxStyle& style);
    const UIStyle::TextBoxStyle& GetStyle() const { return m_style; }

private:
    void RecreateTextLayout();
    bool DeleteSelection(bool& repaint);
    size_t GetCaretIndexFromPoint(POINT pt, float scale) const;

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
    UIStyle::TextBoxStyle m_style;
    std::wstring m_compText;
    size_t m_compCaretOffset = 0;
};
