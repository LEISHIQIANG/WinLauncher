#include "IconBackfillCoordinator.h"
#include "../App/Logger.h"

IconBackfillCoordinator::IconBackfillCoordinator()
    : m_state(std::make_shared<IconBackfillState>())
{
}

IconBackfillCoordinator::~IconBackfillCoordinator()
{
    Invalidate();
}

void IconBackfillCoordinator::Invalidate()
{
    if (m_state)
    {
        m_state->generation.fetch_add(1);
    }
}

void IconBackfillCoordinator::Schedule(HWND hWnd, AppContext* appCtx, const std::vector<RendPopupPage>& pages,
                                      std::function<void(uint64_t generation, std::vector<IconBackfillResult>& results, ULONGLONG elapsedMs)> onApply)
{
    if (!hWnd || !IsWindow(hWnd))
        return;
    if (!appCtx || !appCtx->backgroundTasks || !appCtx->uiDispatcher)
        return;

    auto state = m_state;
    if (!state)
        return;

    struct BackfillItem
    {
        size_t pageIndex;
        std::wstring targetPath;
        Model::ShortcutInfo info;
    };
    std::vector<BackfillItem> items;
    {
        const ULONGLONG now = GetTickCount64();
        std::lock_guard<std::mutex> lock(state->failedMutex);
        for (size_t p = 0; p < pages.size(); ++p)
        {
            for (const auto& sc : pages[p].shortcuts)
            {
                if (sc.hIcon)
                    continue;
                auto failed = state->failedUntil.find(sc.targetPath);
                if (failed != state->failedUntil.end())
                {
                    if (now < failed->second)
                        continue;
                    state->failedUntil.erase(failed);
                }
                Model::ShortcutInfo info;
                info.id = sc.id;
                info.name = sc.name;
                info.targetPath = sc.targetPath;
                info.arguments = sc.arguments;
                info.iconPath = sc.iconPath;
                info.runAsAdmin = sc.runAsAdmin;
                info.type = sc.type;
                info.targetKind = sc.targetKind;
                info.iconSource = sc.iconSource;
                info.builtinIconId = sc.builtinIconId;
                info.iconInvertLight = sc.iconInvertLight;
                info.iconInvertDark = sc.iconInvertDark;
                items.push_back({ p, sc.targetPath, std::move(info) });
            }
        }
    }
    if (items.empty())
        return;

    const uint64_t generation = state->generation.load();
    auto iconService = appCtx->iconService;
    LOG_G_INFO_NODE(L"ui.config", L"icon_backfill",
        L"status=scheduled items=%d generation=%llu",
        static_cast<int>(items.size()),
        static_cast<unsigned long long>(generation));

    appCtx->backgroundTasks->Submit(L"config.icon_backfill", BackgroundTaskService::Priority::Normal,
        [state, generation, items = std::move(items), iconService, appCtx, hWnd, onApply = std::move(onApply)](const std::shared_ptr<BackgroundTaskService::CancellationToken>& cancellation) {
            std::vector<IconBackfillResult> results;
            const ULONGLONG start = GetTickCount64();
            for (const auto& item : items)
            {
                if (cancellation->IsCancellationRequested() ||
                    state->generation.load(std::memory_order_relaxed) != generation)
                    break;
                RendShortcutInfo probe;
                probe.id = item.info.id;
                probe.name = item.info.name;
                probe.targetPath = item.info.targetPath;
                probe.arguments = item.info.arguments;
                probe.iconPath = item.info.iconPath;
                probe.runAsAdmin = item.info.runAsAdmin;
                probe.type = item.info.type;
                probe.targetKind = item.info.targetKind;
                probe.iconSource = item.info.iconSource;
                probe.builtinIconId = item.info.builtinIconId;
                probe.iconInvertLight = item.info.iconInvertLight;
                probe.iconInvertDark = item.info.iconInvertDark;
                HICON hIcon = ShortcutManager::GetShortcutIcon(probe, /*fastOnly=*/false, iconService.get());
                if (hIcon)
                {
                    IconBackfillResult result;
                    result.pageIndex = item.pageIndex;
                    result.shortcutId = std::move(item.info.id);
                    result.targetPath = std::move(item.targetPath);
                    result.hIcon = hIcon;
                    results.push_back(std::move(result));
                }
                else
                {
                    // Negative cache: skip re-extracting known-unresolvable
                    // targets on every subsequent config reload.
                    std::lock_guard<std::mutex> lock(state->failedMutex);
                    state->failedUntil[item.targetPath] = GetTickCount64() + 5 * 60 * 1000;
                }
            }
            if (results.empty())
                return;
            if (state->generation.load(std::memory_order_relaxed) != generation)
                return; // Results are RAII; dropping the vector destroys the icons.
            const ULONGLONG elapsed = GetTickCount64() - start;
            if (!appCtx || !appCtx->uiDispatcher)
                return;
            auto posted = std::make_shared<std::vector<IconBackfillResult>>(std::move(results));
            if (!appCtx->uiDispatcher->IsStopping() &&
                appCtx->uiDispatcher->Post(L"config.icon_backfill.apply",
                    [state, generation, posted, elapsed, hWnd, onApply]() {
                        if (state->generation.load() != generation)
                            return; // RAII results destroy any unapplied icons.
                        if (!IsWindow(hWnd))
                            return;
                        onApply(generation, *posted, elapsed);
                    }))
            {
                return;
            }
            // The dispatcher rejected the post while stopping; the shared
            // vector is released and its RAII results destroy the icons.
        });
}

int IconBackfillCoordinator::ApplyResults(std::vector<RendPopupPage>& pages, std::vector<IconBackfillResult>& results, ULONGLONG elapsedMs, uint64_t generation)
{
    int applied = 0;
    int skipped = 0;
    for (auto& r : results)
    {
        bool matched = false;
        if (r.pageIndex < pages.size() && r.hIcon)
        {
            auto& shortcuts = pages[r.pageIndex].shortcuts;
            for (auto it = shortcuts.begin(); it != shortcuts.end(); ++it)
            {
                if (it->id != r.shortcutId || it->targetPath != r.targetPath)
                    continue;
                matched = true;
                if (it->hIcon)
                {
                    ++skipped; // Slot was filled by a user edit; the RAII result keeps the new icon.
                    break;
                }
                it->hIcon = r.hIcon; // Transfer ownership out of the result.
                r.hIcon = nullptr;
                ++applied;
                auto& bmps = pages[r.pageIndex].iconBitmaps;
                const size_t idx = static_cast<size_t>(it - shortcuts.begin());
                if (idx < bmps.size() && bmps[idx])
                {
                    bmps[idx]->Release();
                    bmps[idx] = nullptr;
                }
                break;
            }
        }
        if (!matched)
            ++skipped;
    }
    LOG_G_INFO_NODE(L"ui.config", L"icon_backfill",
        L"status=applied applied=%d skipped=%d elapsed_ms=%llu generation=%llu",
        applied, skipped,
        static_cast<unsigned long long>(elapsedMs),
        static_cast<unsigned long long>(generation));
    return applied;
}
