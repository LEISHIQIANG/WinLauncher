#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include <memory>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <functional>
#include "../App/AppContext.h"
#include "../ShortcutManager.h"
#include "../ViewModel/ConfigViewModel.h"

// Unique owner of a backfilled HICON while it travels from the
// background extraction task to the UI-thread apply step. Destroying the
// result without transferring hIcon out (apply or drop) destroys the
// icon, so no code path can leak it.
struct IconBackfillResult
{
    size_t pageIndex = 0;
    std::wstring shortcutId;
    std::wstring targetPath;
    HICON hIcon = nullptr;

    IconBackfillResult() = default;
    IconBackfillResult(IconBackfillResult&& other) noexcept
        : pageIndex(other.pageIndex)
        , shortcutId(std::move(other.shortcutId))
        , targetPath(std::move(other.targetPath))
        , hIcon(other.hIcon)
    {
        other.hIcon = nullptr;
    }
    IconBackfillResult& operator=(IconBackfillResult&& other) noexcept
    {
        if (this != &other)
        {
            if (hIcon) DestroyIcon(hIcon);
            pageIndex = other.pageIndex;
            shortcutId = std::move(other.shortcutId);
            targetPath = std::move(other.targetPath);
            hIcon = other.hIcon;
            other.hIcon = nullptr;
        }
        return *this;
    }
    IconBackfillResult(const IconBackfillResult&) = delete;
    IconBackfillResult& operator=(const IconBackfillResult&) = delete;
    ~IconBackfillResult()
    {
        if (hIcon) DestroyIcon(hIcon);
    }
};

// Shared with background backfill tasks; the generation counter lets the
// UI thread invalidate in-flight work whenever pages are reloaded or the
// window is destroyed. failedUntil keeps targets whose extraction failed
// from being retried on every config reload.
struct IconBackfillState
{
    std::atomic_uint64_t generation{ 0 };
    std::mutex failedMutex;
    std::unordered_map<std::wstring, ULONGLONG> failedUntil;
};

class IconBackfillCoordinator
{
public:
    IconBackfillCoordinator();
    ~IconBackfillCoordinator();

    void Invalidate();

    void Schedule(HWND hWnd, AppContext* appCtx, const std::vector<RendPopupPage>& pages,
                  std::function<void(uint64_t generation, std::vector<IconBackfillResult>& results, ULONGLONG elapsedMs)> onApply);

    static int ApplyResults(std::vector<RendPopupPage>& pages, std::vector<IconBackfillResult>& results, ULONGLONG elapsedMs, uint64_t generation);

    std::shared_ptr<IconBackfillState> GetState() const { return m_state; }

private:
    std::shared_ptr<IconBackfillState> m_state;
};
