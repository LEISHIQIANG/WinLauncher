#define NOMINMAX
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "PopupIconPresenter.h"
#include "../PopupWindow.h"
#include "../UI/Controls/IconRenderer.h"
#include "../Config/UIStyle.h"
#include "../App/Logger.h"
#include "../App/UiDispatcher.h"
#include "PopupClock.h"
#include "PopupWindowMessages.h"
#include <algorithm>
#include <cmath>

bool PopupIconPresenter::IsFlashing() const noexcept
{
    return m_iconFlashStart != 0 && (GetTickCount64() - m_iconFlashStart < 120);
}

ID2D1Bitmap* PopupIconPresenter::GetOrCreateFlashBitmap(const std::wstring& name, ID2D1HwndRenderTarget* rt,
                                                        IDWriteFactory* dwFactory, int iconSize, ID2D1Bitmap* fallback)
{
    if (!IsFlashing())
        return fallback;

    auto& flash = m_iconFlashBitmaps[name];
    if (!flash && rt && dwFactory)
    {
        flash = IconRenderer::CreateDefaultIcon(rt, dwFactory, name,
            IconRenderer::GetRecommendedBitmapSize(rt, static_cast<float>(iconSize)));
    }
    return flash ? flash.Get() : fallback;
}

bool PopupIconPresenter::OnFlashTimerTick() noexcept
{
    if (!m_iconFlashStart || GetTickCount64() - m_iconFlashStart >= 120)
    {
        m_iconFlashStart = 0;
        m_iconFlashBitmaps.clear();
        return true; // timer should be killed
    }
    return false;
}

void PopupIconPresenter::ClearFlash() noexcept
{
    m_iconFlashStart = 0;
    m_iconFlashBitmaps.clear();
}

void PopupIconPresenter::Reset()
{
    m_lastRt = nullptr;
    m_lastDpi = 96.0f;
    m_lastIconBitmapSize = 0;
    m_bmpBrushCache.clear();
    m_iconFlashStart = 0;
    m_iconFlashBitmaps.clear();
    for (const auto& task : m_iconRefreshTasks) task.Cancel();
    m_iconRefreshTasks.clear();
}

ID2D1BitmapBrush* PopupIconPresenter::GetOrCreateBrush(ID2D1Bitmap* bmp, ID2D1HwndRenderTarget* rt)
{
    if (!bmp || !rt) return nullptr;
    auto it = m_bmpBrushCache.find(bmp);
    if (it != m_bmpBrushCache.end() && it->second)
        return it->second.Get();
    ComPtr<ID2D1BitmapBrush> brush;
    if (SUCCEEDED(rt->CreateBitmapBrush(bmp, &brush)))
    {
        auto* ptr = brush.Get();
        m_bmpBrushCache[bmp] = std::move(brush);
        return ptr;
    }
    return nullptr;
}

void PopupIconPresenter::EnsureIcons(PopupWindow* window)
{
    if (!window) return;
    window->UpdateTextFormat();

    if (window->m_pages.empty() || !window->m_rt) return;

    float currentDpi = 96.0f;
    if (window->m_rt)
    {
        float dpiY = 96.0f;
        window->m_rt->GetDpi(&currentDpi, &dpiY);
    }

    const int iconBitmapSize = IconRenderer::GetRecommendedBitmapSize(window->m_rt.Get(), static_cast<float>(window->GetIconSize()));
    bool rtChanged = (window->m_rt.Get() != m_lastRt) || (currentDpi != m_lastDpi) || (iconBitmapSize != m_lastIconBitmapSize);
    if (rtChanged)
    {
        m_iconFlashBitmaps.clear();
        m_lastRt = window->m_rt.Get();
        m_lastDpi = currentDpi;
        m_lastIconBitmapSize = iconBitmapSize;
        m_bmpBrushCache.clear();
    }

    bool anyRecreated = false;
    auto iconSvc = window->m_appCtx && window->m_appCtx->iconService ? window->m_appCtx->iconService.get() : window->m_iconService.get();

    const int pageCount = static_cast<int>(window->m_pages.size());
    for (int pageIndex = 0; pageIndex < pageCount; ++pageIndex)
    {
        int distance = std::abs(pageIndex - window->m_currentPage);
        if (pageCount > 1) distance = (std::min)(distance, pageCount - distance);
        if (distance > 1) continue;
        auto& page = window->m_pages[pageIndex];
        int n = (int)page.shortcuts.size();
        bool needRecreate = rtChanged || (page.iconBitmaps.size() != (size_t)n);
        if (needRecreate)
        {
            anyRecreated = true;
            for (auto* bmp : page.iconBitmaps)
            {
                if (bmp) bmp->Release();
            }
            page.iconBitmaps.clear();
            page.iconBitmaps.resize(n, nullptr);
            m_bmpBrushCache.clear();
        }
        for (int i = 0; i < n; i++)
        {
            if (page.iconBitmaps[i]) continue;
            anyRecreated = true;
            bool invert = (UIStyle::GetThemeMode() == UIStyle::ThemeMode::Light) ? page.shortcuts[i].iconInvertLight : page.shortcuts[i].iconInvertDark;
            if (page.shortcuts[i].hIcon == nullptr)
            {
                page.iconBitmaps[i] = IconRenderer::CreateDefaultIcon(window->m_rt.Get(), window->GetDWFactory(), page.shortcuts[i].name, iconBitmapSize).Detach();
            }
            else
            {
                page.iconBitmaps[i] = iconSvc->IconToBitmap(window->m_rt.Get(), page.shortcuts[i].hIcon, iconBitmapSize, invert);
            }
        }
    }
    // Recreate dock page bitmaps
    {
        int dn = (int)window->m_dockPage.shortcuts.size();
        bool needRecreate = rtChanged || (window->m_dockPage.iconBitmaps.size() != (size_t)dn);
        if (needRecreate)
        {
            anyRecreated = true;
            for (auto* bmp : window->m_dockPage.iconBitmaps)
                if (bmp) bmp->Release();
            window->m_dockPage.iconBitmaps.clear();
            window->m_dockPage.iconBitmaps.resize(dn, nullptr);
            m_bmpBrushCache.clear();
        }
        for (int i = 0; i < dn; i++)
        {
            if (window->m_dockPage.iconBitmaps[i]) continue;
            anyRecreated = true;
            bool invert = (UIStyle::GetThemeMode() == UIStyle::ThemeMode::Light) ? window->m_dockPage.shortcuts[i].iconInvertLight : window->m_dockPage.shortcuts[i].iconInvertDark;
            if (window->m_dockPage.shortcuts[i].hIcon == nullptr)
            {
                window->m_dockPage.iconBitmaps[i] = IconRenderer::CreateDefaultIcon(window->m_rt.Get(), window->GetDWFactory(), window->m_dockPage.shortcuts[i].name, iconBitmapSize).Detach();
            }
            else
            {
                window->m_dockPage.iconBitmaps[i] = iconSvc->IconToBitmap(window->m_rt.Get(), window->m_dockPage.shortcuts[i].hIcon, iconBitmapSize, invert);
            }
        }
    }

    if (anyRecreated && window->m_searchActive && !window->m_searchQuery.empty())
    {
        window->UpdateSearch();
    }
}

void PopupIconPresenter::RefreshIcons(PopupWindow* window, bool forceRefresh, bool showFeedback)
{
    if (!window || !window->m_appCtx || !window->m_appCtx->backgroundTasks) return;
    if (forceRefresh && showFeedback && IsWindowVisible(window->GetHWND()))
    {
        m_iconFlashStart = GetTickCount64();
        SetTimer(window->GetHWND(), PopupWindowMessages::IconFlashTimer, 16, nullptr);
        InvalidateRect(window->GetHWND(), nullptr, FALSE);
    }
    auto state = window->m_iconRefresh.Begin(forceRefresh);
    if (!state) return;
    state->layoutGeneration = m_iconLayoutGeneration;
    HWND hwnd = window->GetHWND();
    std::shared_ptr<UiDispatcher> dispatcher = window->m_appCtx->uiDispatcher;
    if (hwnd) SetTimer(hwnd, PopupWindowMessages::IconProgressTimer, 16, nullptr);

    std::vector<std::tuple<bool, size_t, size_t, RendShortcutInfo>> jobs;
    for (size_t pageIndex = 0; pageIndex < window->m_pages.size(); ++pageIndex)
    {
        for (size_t shortcutIndex = 0; shortcutIndex < window->m_pages[pageIndex].shortcuts.size(); ++shortcutIndex)
        {
            auto& sc = window->m_pages[pageIndex].shortcuts[shortcutIndex];
            if (!forceRefresh && sc.hIcon != nullptr) continue;
            jobs.emplace_back(false, pageIndex, shortcutIndex, sc);
        }
    }
    for (size_t shortcutIndex = 0; shortcutIndex < window->m_dockPage.shortcuts.size(); ++shortcutIndex)
    {
        auto& sc = window->m_dockPage.shortcuts[shortcutIndex];
        if (!forceRefresh && sc.hIcon != nullptr) continue;
        jobs.emplace_back(true, 0, shortcutIndex, sc);
    }

    if (jobs.empty())
    {
        window->m_iconRefresh.Complete();
        if (hwnd) KillTimer(hwnd, PopupWindowMessages::IconProgressTimer);
        return;
    }

    const int currentPage = window->m_currentPage;
    const size_t pageCount = window->m_pages.size();
    std::stable_sort(jobs.begin(), jobs.end(), [currentPage, pageCount](const auto& a, const auto& b) {
        auto rank = [currentPage, pageCount](const auto& job) {
            if (std::get<0>(job)) return 0;
            const int count = static_cast<int>(pageCount);
            const int distance = std::abs(static_cast<int>(std::get<1>(job)) - currentPage);
            return (std::min)(distance, count - distance);
        };
        return rank(a) < rank(b);
    });

    const auto refreshPriority = forceRefresh
        ? BackgroundTaskService::Priority::Normal
        : BackgroundTaskService::Priority::High;
    constexpr size_t MaximumIconWorkers = 4;
    const size_t workerCount = (std::min)(MaximumIconWorkers, jobs.size());
    auto sharedJobs = std::make_shared<
        std::vector<std::tuple<bool, size_t, size_t, RendShortcutInfo>>>(std::move(jobs));
    auto nextJob = std::make_shared<std::atomic_size_t>(0);

    state->pendingWorkers.store(workerCount);
    m_iconRefreshTasks.clear();
    m_iconRefreshTasks.reserve(workerCount);
    auto sharedIconService = window->m_appCtx ? window->m_appCtx->iconService : nullptr;
    auto finishWorker = [state, hwnd, dispatcher]() {
        if (state->pendingWorkers.fetch_sub(1) != 1)
            return;
        if (state->cancelled)
            return;
        SetEvent(state->completionEvent);
        if (dispatcher && dispatcher->Post(L"popup.icon_preload_complete", [state]() {
            PopupWindow::OnAnyIconPreloadCompleted(state);
        }))
        {
            return;
        }
        if (IsWindow(hwnd))
            PostMessageW(hwnd, PopupWindowMessages::RefreshIcons, 0, 0);
    };

    for (size_t workerIndex = 0; workerIndex < workerCount; ++workerIndex)
    {
        auto handle = window->m_appCtx->backgroundTasks->Submit(
            L"popup.icon_refresh." + std::to_wstring(workerIndex),
            refreshPriority,
            [state, sharedJobs, nextJob, finishWorker, sharedIconService](
                const std::shared_ptr<BackgroundTaskService::CancellationToken>& cancellation) mutable {
            const auto completion = std::shared_ptr<void>(nullptr, [finishWorker](void*) { finishWorker(); });
            const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            while (!state->cancelled && !cancellation->IsCancellationRequested())
            {
                const size_t jobIndex = nextJob->fetch_add(1);
                if (jobIndex >= sharedJobs->size())
                    break;
                auto& job = (*sharedJobs)[jobIndex];
                auto& shortcut = std::get<3>(job);
                HICON icon = ShortcutManager::GetShortcutIcon(shortcut, false, sharedIconService.get());
                if (state->cancelled || cancellation->IsCancellationRequested())
                {
                    if (icon) DestroyIcon(icon);
                    break;
                }
                std::lock_guard<std::mutex> lock(state->mutex);
                state->results.push_back({ std::get<0>(job), std::get<1>(job), std::get<2>(job), icon, PopupIconCache::Key(shortcut), state->layoutGeneration });
            }
            if (SUCCEEDED(comResult)) CoUninitialize();
        });
        if (handle)
        {
            m_iconRefreshTasks.push_back(handle);
        }
        else
        {
            finishWorker();
            LOG_G_WORNING(L"PopupWindow perf: icon refresh worker was not queued");
        }
    }
}

void PopupIconPresenter::OnPreloadCompleted(PopupWindow* window, const std::shared_ptr<PopupIconRefreshController::State>& state)
{
    if (!window || !window->m_iconRefresh.IsCurrent(state)) return;
    if (!IsWindowVisible(window->GetHWND()))
        ApplyRefreshedIcons(window, window->m_iconRefresh.WaitForCompletion(state, 0));
    else
        SetTimer(window->GetHWND(), PopupWindowMessages::IconProgressTimer, 16, nullptr);
}

void PopupIconPresenter::CancelRefresh(PopupWindow* window, bool preservePreload)
{
    if (preservePreload) return; // Hide keeps useful preload alive.
    if (window && window->GetHWND()) KillTimer(window->GetHWND(), PopupWindowMessages::IconProgressTimer);
    for (const auto& task : m_iconRefreshTasks) task.Cancel();
    m_iconRefreshTasks.clear();
    if (window) window->m_iconRefresh.Cancel();
}

void PopupIconPresenter::ApplyRefreshedIcons(PopupWindow* window, bool refreshCompleted)
{
    if (!window) return;
    auto state = window->m_iconRefresh.Current();
    if (!window->m_iconRefresh.IsCurrent(state)) return;
    auto results = window->m_iconRefresh.Take(state);
    const double started = PopupClock::NowSeconds();
    int applied = 0;
    for (auto& result : results)
    {
        auto* page = result.dock ? &window->m_dockPage : (result.pageIndex < window->m_pages.size() ? &window->m_pages[result.pageIndex] : nullptr);
        if (!page || result.shortcutIndex >= page->shortcuts.size()) { if (result.icon) DestroyIcon(result.icon); continue; }
        if (result.layoutGeneration != m_iconLayoutGeneration)
        { if (result.icon) DestroyIcon(result.icon); continue; }
        if (PopupIconCache::Key(page->shortcuts[result.shortcutIndex]) != result.identity)
        {
            const auto found = std::find_if(page->shortcuts.begin(), page->shortcuts.end(),
                [&result](const auto& shortcut) { return PopupIconCache::Key(shortcut) == result.identity; });
            if (found == page->shortcuts.end()) { if (result.icon) DestroyIcon(result.icon); continue; }
            result.shortcutIndex = static_cast<size_t>(found - page->shortcuts.begin());
        }
        auto& shortcut = page->shortcuts[result.shortcutIndex];
        if (!result.icon || result.layoutGeneration != m_iconLayoutGeneration ||
            result.identity != PopupIconCache::Key(shortcut))
        {
            if (result.icon) DestroyIcon(result.icon);
            continue;
        }
        if (shortcut.hIcon) DestroyIcon(shortcut.hIcon);
        shortcut.hIcon = result.icon;
        window->m_iconCache.Remember(shortcut);
        if (result.shortcutIndex < page->iconBitmaps.size() && page->iconBitmaps[result.shortcutIndex])
        {
            page->iconBitmaps[result.shortcutIndex]->Release();
            page->iconBitmaps[result.shortcutIndex] = nullptr;
        }
        ++applied;
    }
    if (applied) m_bmpBrushCache.clear();
    if (refreshCompleted)
    {
        window->m_iconRefresh.Complete();
        if (window->GetHWND()) KillTimer(window->GetHWND(), PopupWindowMessages::IconProgressTimer);
        m_iconRefreshTasks.clear();
    }
    if (window->GetHWND() && applied > 0)
    {
        EnsureIcons(window);
        InvalidateRect(window->GetHWND(), nullptr, FALSE);
    }
    if (applied || refreshCompleted) LOG_G_DEBUG(L"PopupWindow perf: icon refresh applied=%d total=%zu ui_ms=%.2f generation=%llu",
               applied, results.size(), (PopupClock::NowSeconds() - started) * 1000.0,
               static_cast<unsigned long long>(window->m_iconRefresh.Generation()));
    if (refreshCompleted && window->m_iconRefresh.TakePending())
    {
        RefreshIcons(window, window->m_iconRefresh.TakePendingForce(), false);
    }
}
