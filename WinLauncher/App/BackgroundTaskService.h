#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class Logger;

class BackgroundTaskService
{
public:
    enum class Priority { Interactive, High, Normal };

    class CancellationToken
    {
    public:
        bool IsCancellationRequested() const noexcept { return m_cancelled.load(); }

    private:
        friend class BackgroundTaskService;
        void Cancel() noexcept { m_cancelled.store(true); }
        std::atomic_bool m_cancelled{ false };
    };

    using Task = std::function<void(const std::shared_ptr<CancellationToken>&)>;

    struct TaskHandle
    {
        uint64_t id = 0;
        std::shared_ptr<CancellationToken> cancellation;
        explicit operator bool() const noexcept { return id != 0 && cancellation != nullptr; }
        void Cancel() const noexcept { if (cancellation) cancellation->Cancel(); }
    };

    explicit BackgroundTaskService(std::shared_ptr<Logger> logger);
    ~BackgroundTaskService();

    TaskHandle Submit(const std::wstring& name, Priority priority, Task task);
    void Shutdown(std::chrono::milliseconds timeout = std::chrono::milliseconds(1500));
    bool IsStopping() const noexcept;
    size_t ActiveTaskCount() const;
    std::vector<std::wstring> ActiveTaskDescriptions() const;
    static bool IsCurrentTaskCancellationRequested() noexcept;

private:
    struct QueuedTask;
    struct State;
    static void WorkerLoop(const std::shared_ptr<State>& state, bool interactiveWorker, unsigned int workerIndex);

    std::shared_ptr<State> m_state;
    std::vector<std::thread> m_threads;
    std::atomic_bool m_shutdownStarted{ false };
};
