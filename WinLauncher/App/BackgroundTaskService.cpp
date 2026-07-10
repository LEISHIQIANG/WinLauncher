#include "BackgroundTaskService.h"
#include "Logger.h"
#include "CrashReporter.h"
#include <algorithm>
#include <exception>
#include <sstream>
#include <Windows.h>

namespace
{
    constexpr size_t MaxQueuedTasks = 128;
    thread_local std::weak_ptr<BackgroundTaskService::CancellationToken> t_currentCancellation;

    void SetCurrentThreadName(const std::wstring& name)
    {
        HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
        if (!kernel) return;
        using SetThreadDescriptionFn = HRESULT(WINAPI*)(HANDLE, PCWSTR);
        auto fn = reinterpret_cast<SetThreadDescriptionFn>(GetProcAddress(kernel, "SetThreadDescription"));
        if (fn) fn(GetCurrentThread(), name.c_str());
    }
}

struct BackgroundTaskService::QueuedTask
{
    uint64_t id = 0;
    std::wstring name;
    Priority priority = Priority::Normal;
    Task callback;
    std::shared_ptr<CancellationToken> cancellation;
    std::chrono::steady_clock::time_point queuedAt;
};

struct BackgroundTaskService::State
{
    explicit State(std::shared_ptr<Logger> value) : logger(std::move(value)) {}

    std::shared_ptr<Logger> logger;
    mutable std::mutex mutex;
    std::condition_variable workCv;
    std::condition_variable stoppedCv;
    std::deque<QueuedTask> interactive;
    std::deque<QueuedTask> high;
    std::deque<QueuedTask> normal;
    std::map<uint64_t, QueuedTask> active;
    uint64_t nextId = 1;
    bool stopping = false;
    size_t liveWorkers = 0;
};

BackgroundTaskService::BackgroundTaskService(std::shared_ptr<Logger> logger)
    : m_state(std::make_shared<State>(std::move(logger)))
{
    unsigned int hardware = std::thread::hardware_concurrency();
    unsigned int generalWorkers = (std::max)(2u, (std::min)(4u, hardware > 1 ? hardware / 2 : 2u));
    {
        std::lock_guard<std::mutex> lock(m_state->mutex);
        m_state->liveWorkers = static_cast<size_t>(generalWorkers) + 1;
    }

    m_threads.emplace_back(&BackgroundTaskService::WorkerLoop, m_state, true, 0);
    for (unsigned int i = 0; i < generalWorkers; ++i)
        m_threads.emplace_back(&BackgroundTaskService::WorkerLoop, m_state, false, i);
}

BackgroundTaskService::~BackgroundTaskService()
{
    Shutdown();
}

BackgroundTaskService::TaskHandle BackgroundTaskService::Submit(const std::wstring& name, Priority priority, Task task)
{
    if (!task) return {};

    QueuedTask queued;
    queued.name = name.empty() ? L"unnamed" : name;
    queued.priority = priority;
    queued.callback = std::move(task);
    queued.cancellation = std::make_shared<CancellationToken>();
    queued.queuedAt = std::chrono::steady_clock::now();

    {
        std::lock_guard<std::mutex> lock(m_state->mutex);
        const size_t queuedCount = m_state->interactive.size() + m_state->high.size() + m_state->normal.size();
        if (m_state->stopping || queuedCount >= MaxQueuedTasks)
        {
            queued.cancellation->Cancel();
            LOG_WARNING_NODE(m_state->logger, L"async.runtime", L"task_rejected", L"name=%ls stopping=%d queued=%zu",
                queued.name.c_str(), m_state->stopping ? 1 : 0, queuedCount);
            return {};
        }
        queued.id = m_state->nextId++;
        if (priority == Priority::Interactive)
            m_state->interactive.push_back(queued);
        else if (priority == Priority::High)
            m_state->high.push_back(queued);
        else
            m_state->normal.push_back(queued);
    }
    m_state->workCv.notify_all();
    return { queued.id, queued.cancellation };
}

void BackgroundTaskService::WorkerLoop(const std::shared_ptr<State>& state, bool interactiveWorker, unsigned int workerIndex)
{
    SetCurrentThreadName(interactiveWorker ? L"WinLauncher.Interactive" : L"WinLauncher.Worker." + std::to_wstring(workerIndex));

    while (true)
    {
        QueuedTask task;
        {
            std::unique_lock<std::mutex> lock(state->mutex);
            state->workCv.wait(lock, [&]() {
                if (state->stopping) return true;
                return interactiveWorker ? !state->interactive.empty() : (!state->high.empty() || !state->normal.empty());
            });

            auto* queue = interactiveWorker ? &state->interactive : (!state->high.empty() ? &state->high : &state->normal);
            if (queue->empty())
            {
                if (state->stopping) break;
                continue;
            }

            task = std::move(queue->front());
            queue->pop_front();
            if (task.cancellation->IsCancellationRequested())
                continue;
            state->active.emplace(task.id, task);
        }

        Logger::SetThreadTaskContext(task.id, task.name);
        t_currentCancellation = task.cancellation;
        CrashReporter::RecordBreadcrumb(L"task.start", std::to_wstring(task.id) + L":" + task.name);
        const auto started = std::chrono::steady_clock::now();
        const auto queueMs = std::chrono::duration_cast<std::chrono::milliseconds>(started - task.queuedAt).count();
        LOG_DEBUG_NODE(state->logger, L"async.runtime", L"task_started", L"id=%llu name=%ls queue_ms=%lld",
            static_cast<unsigned long long>(task.id), task.name.c_str(), static_cast<long long>(queueMs));
        try
        {
            task.callback(task.cancellation);
        }
        catch (const std::exception& ex)
        {
            LOG_ERROR_NODE(state->logger, L"async.runtime", L"task_exception", L"id=%llu name=%ls what=%hs",
                static_cast<unsigned long long>(task.id), task.name.c_str(), ex.what());
        }
        catch (...)
        {
            LOG_ERROR_NODE(state->logger, L"async.runtime", L"task_exception_unknown", L"id=%llu name=%ls",
                static_cast<unsigned long long>(task.id), task.name.c_str());
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
        LOG_DEBUG_NODE(state->logger, L"async.runtime", L"task_finished", L"id=%llu name=%ls elapsed_ms=%lld cancelled=%d",
            static_cast<unsigned long long>(task.id), task.name.c_str(), static_cast<long long>(elapsed),
            task.cancellation->IsCancellationRequested() ? 1 : 0);
        Logger::ClearThreadTaskContext();
        t_currentCancellation.reset();
        CrashReporter::RecordBreadcrumb(L"task.finish", std::to_wstring(task.id) + L":" + task.name + L":" + std::to_wstring(elapsed) + L"ms");

        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->active.erase(task.id);
        }
        state->stoppedCv.notify_all();
    }

    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->liveWorkers > 0) --state->liveWorkers;
    }
    state->stoppedCv.notify_all();
}

void BackgroundTaskService::Shutdown(std::chrono::milliseconds timeout)
{
    if (m_shutdownStarted.exchange(true)) return;

    {
        std::lock_guard<std::mutex> lock(m_state->mutex);
        m_state->stopping = true;
        for (auto& task : m_state->interactive) task.cancellation->Cancel();
        for (auto& task : m_state->high) task.cancellation->Cancel();
        for (auto& task : m_state->normal) task.cancellation->Cancel();
        for (auto& entry : m_state->active) entry.second.cancellation->Cancel();
        m_state->interactive.clear();
        m_state->high.clear();
        m_state->normal.clear();
    }
    m_state->workCv.notify_all();

    bool stopped = false;
    {
        std::unique_lock<std::mutex> lock(m_state->mutex);
        stopped = m_state->stoppedCv.wait_for(lock, timeout, [&]() { return m_state->liveWorkers == 0; });
        if (!stopped)
        {
            for (const auto& entry : m_state->active)
            {
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - entry.second.queuedAt).count();
                LOG_WARNING_NODE(m_state->logger, L"async.runtime", L"shutdown_task_timeout", L"id=%llu name=%ls elapsed_ms=%lld",
                    static_cast<unsigned long long>(entry.first), entry.second.name.c_str(), static_cast<long long>(elapsed));
            }
        }
    }

    for (auto& thread : m_threads)
    {
        if (!thread.joinable()) continue;
        if (stopped) thread.join();
        else thread.detach();
    }
    m_threads.clear();
}

bool BackgroundTaskService::IsStopping() const noexcept
{
    std::lock_guard<std::mutex> lock(m_state->mutex);
    return m_state->stopping;
}

size_t BackgroundTaskService::ActiveTaskCount() const
{
    std::lock_guard<std::mutex> lock(m_state->mutex);
    return m_state->active.size();
}

std::vector<std::wstring> BackgroundTaskService::ActiveTaskDescriptions() const
{
    std::vector<std::wstring> result;
    std::lock_guard<std::mutex> lock(m_state->mutex);
    for (const auto& entry : m_state->active)
    {
        std::wstringstream text;
        text << entry.first << L":" << entry.second.name;
        result.push_back(text.str());
    }
    return result;
}

bool BackgroundTaskService::IsCurrentTaskCancellationRequested() noexcept
{
    auto token = t_currentCancellation.lock();
    return token && token->IsCancellationRequested();
}
