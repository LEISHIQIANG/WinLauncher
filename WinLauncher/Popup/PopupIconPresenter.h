#pragma once

#include "PopupIconRefreshController.h"
#include "PopupIconCache.h"
#include "../App/BackgroundTaskService.h"
#include <d2d1.h>
#include <wrl.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

using Microsoft::WRL::ComPtr;

class PopupWindow;
struct IDWriteFactory;

// Collaborator responsible for Direct2D icon bitmap presentation,
// bitmap brush caching, flash feedback, and background icon refresh tasks.
class PopupIconPresenter
{
public:
    PopupIconPresenter() = default;
    ~PopupIconPresenter() = default;

    void EnsureIcons(PopupWindow* window);
    void RefreshIcons(PopupWindow* window, bool forceRefresh, bool showFeedback);
    void CancelRefresh(PopupWindow* window, bool preservePreload = false);
    void ApplyRefreshedIcons(PopupWindow* window, bool refreshCompleted = true);
    void OnPreloadCompleted(PopupWindow* window, const std::shared_ptr<PopupIconRefreshController::State>& state);

    void InvalidateLayoutGeneration() noexcept { ++m_iconLayoutGeneration; }
    uint64_t LayoutGeneration() const noexcept { return m_iconLayoutGeneration; }

    void ClearBrushCache() { m_bmpBrushCache.clear(); }
    ID2D1BitmapBrush* GetOrCreateBrush(ID2D1Bitmap* bmp, ID2D1HwndRenderTarget* rt);

    bool IsFlashing() const noexcept;
    ID2D1Bitmap* GetOrCreateFlashBitmap(const std::wstring& name, ID2D1HwndRenderTarget* rt,
                                        IDWriteFactory* dwFactory, int iconSize, ID2D1Bitmap* fallback);
    bool OnFlashTimerTick() noexcept;
    void ClearFlash() noexcept;
    void Reset();

private:
    ID2D1HwndRenderTarget* m_lastRt = nullptr;
    float m_lastDpi = 96.0f;
    int m_lastIconBitmapSize = 0;

    std::unordered_map<ID2D1Bitmap*, ComPtr<ID2D1BitmapBrush>> m_bmpBrushCache;
    ULONGLONG m_iconFlashStart = 0;
    std::unordered_map<std::wstring, ComPtr<ID2D1Bitmap>> m_iconFlashBitmaps;
    std::vector<BackgroundTaskService::TaskHandle> m_iconRefreshTasks;
    uint64_t m_iconLayoutGeneration = 0;
};
