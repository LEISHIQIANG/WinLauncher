#pragma once
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl.h>
#include <string>
#include <vector>
#include "TextBox.h"
#include "../Model/ShortcutInfo.h"
#include "../Services/BatchLaunchService.h"

using Microsoft::WRL::ComPtr;

struct AppContext;

struct BatchLaunchEditFormResult
{
    std::wstring name;
    std::wstring arguments; // Serialized BatchStep list
    std::wstring iconPath;
    bool         iconInvertLight = false;
    bool         iconInvertDark = false;
};

struct BatchLaunchEditFormInitParams
{
    std::wstring name;
    std::wstring arguments;
    std::wstring iconPath;
    bool         iconInvertLight = false;
    bool         iconInvertDark = false;
};

class BatchLaunchEditForm
{
public:
    BatchLaunchEditForm();
    ~BatchLaunchEditForm();

    static constexpr float PreferredContentHeight() { return 366.0f; }

    bool Create(HWND parentHWND, IDWriteFactory* dwriteFactory, const D2D1_RECT_F& logicalBounds, const BatchLaunchEditFormInitParams& init, AppContext* ctx = nullptr);
    void Destroy();

    void Paint(ID2D1HwndRenderTarget* rt, float scale);
    void UpdateLayout(const D2D1_RECT_F& logicalBounds, float scale);

    // Event handlers
    void OnMouseMove(HWND hWnd, POINT pt, float scale, bool& repaint);
    void OnLButtonDown(HWND hWnd, POINT pt, float scale, bool& repaint);
    void OnLButtonDblClk(HWND hWnd, POINT pt, float scale, bool& repaint);
    void OnLButtonUp(HWND hWnd, POINT pt, float scale, bool& repaint);
    void OnChar(HWND hWnd, WPARAM wParam, bool& repaint);
    void OnKeyDown(HWND hWnd, WPARAM wParam, LPARAM lParam, bool& repaint);
    void OnMouseWheel(HWND hWnd, short zDelta, POINT pt, float scale, bool& repaint);

    void BlinkCaret();
    bool TickAnimation(); // returns true if still animating

    bool IsInputFocused() const { return m_focusedBox != nullptr; }
    bool HandleImeMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, bool& repaint);

    // Data validation and results
    BatchLaunchEditFormResult GetResult() const;
    bool Validate(HWND hWnd);

private:
    struct SimpleShortcutItem
    {
        std::wstring id;
        std::wstring name;
        Model::ShortcutType type;
        HICON hIcon = nullptr;
        ID2D1Bitmap* bitmap = nullptr;
    };

    struct QueueVisualState
    {
        float currentY = 0.0f;
        float targetY = 0.0f;
    };

    void EnsureFonts(IDWriteFactory* dwriteFactory);
    void BrowseIconFile(HWND hWnd);
    void PopulateAvailableShortcuts(ID2D1HwndRenderTarget* rt);
    void DrawScrollbar(ID2D1HwndRenderTarget* rt, const D2D1_RECT_F& rect, float scrollOffset, float scrollMax, float scale);
    void SyncQueueStates();
    void UpdateQueueDragAndSortState(POINT pt);
    void EnsureQueueStates();

    bool HitTestRect(POINT pt, const D2D1_RECT_F& rect);
    bool HitTestBrowseIconButton(POINT pt);
    bool HitTestInvertLightCheckbox(POINT pt);
    bool HitTestInvertDarkCheckbox(POINT pt);

    void DrawSectionLabel(ID2D1HwndRenderTarget* rt, const wchar_t* text, const D2D1_RECT_F& rect);
    void DrawButton(ID2D1HwndRenderTarget* rt, const wchar_t* text, const D2D1_RECT_F& rect, bool hovered, bool disabled = false);
    void DrawCheckbox(ID2D1HwndRenderTarget* rt, const D2D1_RECT_F& rect, bool checked, bool hovered, const wchar_t* labelText = nullptr);
    void DrawIconPreview(ID2D1HwndRenderTarget* rt);
    HICON GetFileIconForPreview(const std::wstring& path);

    AppContext* m_ctx = nullptr;
    HWND m_parentHWND = nullptr;
    D2D1_RECT_F m_bounds = {};
    BatchLaunchEditFormInitParams m_init;

    // UI Controls
    TextBox m_nameBox;
    TextBox m_iconBox;
    TextBox* m_focusedBox = nullptr;

    bool m_iconInvertLight = false;
    bool m_iconInvertDark = false;

    // Available shortcuts (left) and configured steps (right)
    std::vector<SimpleShortcutItem> m_availableItems;
    std::vector<BatchStep> m_steps;

    // Hover states for global items
    bool m_hoveredBrowseIcon = false;
    bool m_hoveredInvertLight = false;
    bool m_hoveredInvertDark = false;

    // Left (available) grid scrolling states
    float m_leftScrollY = 0.0f;
    float m_leftScrollMax = 0.0f;
    int m_hoveredLeftIdx = -1;

    // Right (queue) list states
    float m_rightScrollY = 0.0f;
    float m_rightScrollMax = 0.0f;
    int m_hoveredRightIdx = -1;
    int m_hoveredDeleteIdx = -1; // which queue item's delete button is hovered

    // Queue drag-to-reorder state
    std::vector<QueueVisualState> m_queueStates;
    int m_dragIndex = -1;
    int m_dragInsertIndex = -1;
    float m_grabOffsetY = 0.0f;
    bool m_animating = false;

    // Icon Preview cache
    HICON m_previewIcon = nullptr;
    ID2D1Bitmap* m_previewBitmap = nullptr;
    std::wstring m_lastPreviewPath;
    std::wstring m_lastPreviewText;
    ID2D1Factory* m_d2dFactoryCache = nullptr;

    // Fonts
    ComPtr<IDWriteTextFormat> m_tfLabel;
    ComPtr<IDWriteTextFormat> m_tfBtn;
    ComPtr<IDWriteTextFormat> m_tfSmall;
    ComPtr<IDWriteTextFormat> m_tfSmallCenter;
    ComPtr<IDWriteTextFormat> m_tfNormal;
};
