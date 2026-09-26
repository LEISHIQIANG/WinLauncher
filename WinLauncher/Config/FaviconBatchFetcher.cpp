#include "FaviconBatchFetcher.h"
#include "IConfigWindow.h"
#include "../App/AppContext.h"
#include "../Services/FaviconFetcher.h"
#include "../ToastWindow.h"
#include <mutex>

struct FaviconBatchFetcher::State
{
    std::mutex mutex;
    FaviconBatchFetcher* owner = nullptr;
    uint64_t generation = 0;
};

FaviconBatchFetcher::FaviconBatchFetcher()
{
    m_state = std::make_shared<State>();
    m_state->owner = this;
}

FaviconBatchFetcher::~FaviconBatchFetcher()
{
    Detach();
}

void FaviconBatchFetcher::Detach()
{
    Cancel();
    if (m_state)
    {
        std::lock_guard<std::mutex> lock(m_state->mutex);
        m_state->owner = nullptr;
        ++m_state->generation;
    }
}

void FaviconBatchFetcher::Cancel()
{
    for (const auto& task : m_tasks)
    {
        task.Cancel();
    }
    m_tasks.clear();
    m_pending = 0;
    m_applied = 0;
    m_changed = false;
    m_historyRecorded = false;

    if (m_state)
    {
        std::lock_guard<std::mutex> lock(m_state->mutex);
        ++m_state->generation;
    }
}

void FaviconBatchFetcher::StartFetch(const HostContext& host, const std::vector<int>& indices)
{
    m_activeHost = host;
    if (!m_activeHost.pageData || m_activeHost.pageData->isSyncFolder || !m_activeHost.owner) return;

    AppContext* context = m_activeHost.owner->GetAppContext();
    if (!context || !context->backgroundTasks || !context->uiDispatcher || !m_state) return;

    struct UrlJob { int index; std::wstring shortcutId; std::wstring url; };
    std::vector<UrlJob> jobs;
    std::vector<int> normalized = host.normalizeIndices ? host.normalizeIndices(indices) : indices;
    for (int index : normalized)
    {
        if (index >= 0 && index < (int)m_activeHost.pageData->shortcuts.size())
        {
            const RendShortcutInfo& shortcut = m_activeHost.pageData->shortcuts[index];
            if (shortcut.type == Model::ShortcutType::Url && !shortcut.targetPath.empty())
            {
                jobs.push_back({ index, shortcut.id, shortcut.targetPath });
            }
        }
    }
    if (jobs.empty())
    {
        ToastWindow::Show(L"选中的项目中没有可获取图标的网址", 1800);
        return;
    }

    Cancel();
    const auto state = m_state;
    uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        generation = ++state->generation;
    }
    m_generation = generation;

    auto dispatcher = context->uiDispatcher;
    for (const UrlJob& job : jobs)
    {
        BackgroundTaskService::TaskHandle task = context->backgroundTasks->Submit(
            L"config.url_favicon.batch", BackgroundTaskService::Priority::Normal,
            [state, dispatcher, generation, index = job.index, shortcutId = job.shortcutId, url = job.url]
            (const std::shared_ptr<BackgroundTaskService::CancellationToken>& cancellation) {
                std::wstring iconPath;
                try
                {
                    iconPath = FaviconFetcher::FetchFavicon(url, /*forceRefresh=*/true);
                }
                catch (...) {}
                if (cancellation->IsCancellationRequested()) return;
                dispatcher->Post(L"config.url_favicon.batch.complete", [state, generation, index, shortcutId, url, iconPath]() {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    if (state->owner)
                        state->owner->ApplyResult(generation, index, shortcutId, url, iconPath);
                });
            });
        if (task)
        {
            m_tasks.push_back(task);
            ++m_pending;
        }
    }

    if (m_pending == 0)
    {
        ToastWindow::Show(L"后台任务繁忙，未开始获取图标", 1800);
        return;
    }

    ToastWindow::Show(L"正在并行获取 " + std::to_wstring(m_pending) + L" 个网站图标...", 1600);
}

void FaviconBatchFetcher::ApplyResult(uint64_t generation, int index, const std::wstring& shortcutId,
                                      const std::wstring& url, const std::wstring& iconPath)
{
    if (generation != m_generation || m_pending <= 0) return;

    if (!iconPath.empty() && m_activeHost.pageData && index >= 0 && index < (int)m_activeHost.pageData->shortcuts.size())
    {
        RendShortcutInfo& shortcut = m_activeHost.pageData->shortcuts[index];
        if (shortcut.type == Model::ShortcutType::Url && shortcut.id == shortcutId && shortcut.targetPath == url)
        {
            bool changed = shortcut.iconPath != iconPath || shortcut.iconSource != Model::IconSource::CustomPath;
            if (changed && !m_historyRecorded && m_activeHost.owner)
            {
                m_activeHost.owner->RecordShortcutHistoryCheckpoint();
                m_historyRecorded = true;
            }
            shortcut.iconPath = iconPath;
            shortcut.iconSource = Model::IconSource::CustomPath;

            if (shortcut.hIcon) { DestroyIcon(shortcut.hIcon); shortcut.hIcon = nullptr; }
            shortcut.hIcon = ShortcutManager::GetShortcutIcon(shortcut, false, m_activeHost.iconService);
            if (index < (int)m_activeHost.pageData->iconBitmaps.size() && m_activeHost.pageData->iconBitmaps[index])
            {
                m_activeHost.pageData->iconBitmaps[index]->Release();
                m_activeHost.pageData->iconBitmaps[index] = nullptr;
            }
            if (index < (int)m_activeHost.pageData->iconBitmaps.size() && m_activeHost.createBitmap)
                m_activeHost.pageData->iconBitmaps[index] = m_activeHost.createBitmap(shortcut);

            ++m_applied;
            m_changed = m_changed || changed;
        }
    }

    --m_pending;
    if (m_pending == 0)
        FinishFetch();
}

void FaviconBatchFetcher::FinishFetch()
{
    m_tasks.clear();
    if (m_changed && m_activeHost.owner)
        m_activeHost.owner->NotifyConfigChanged();

    if (m_applied > 0)
    {
        ToastWindow::Show(L"已获取并应用 " + std::to_wstring(m_applied) + L" 个网站图标", 2200);
    }
    else
    {
        ToastWindow::Show(L"未获取到选中网站的图标", 2200);
    }
    HWND hWnd = m_activeHost.owner ? m_activeHost.owner->GetWindowHWND() : nullptr;
    if (hWnd && IsWindow(hWnd)) InvalidateRect(hWnd, nullptr, FALSE);
}
