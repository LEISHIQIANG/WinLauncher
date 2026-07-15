#pragma once

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

class PopupIconRefreshController
{
public:
    struct Result
    {
        bool dock = false;
        size_t pageIndex = 0;
        size_t shortcutIndex = 0;
        HICON icon = nullptr;
    };

    struct State
    {
        State();
        ~State();
        std::mutex mutex;
        std::vector<Result> results;
        std::atomic_bool cancelled{ false };
        uint64_t generation = 0;
        HANDLE completionEvent = nullptr;
    };

    std::shared_ptr<State> Begin();
    void Cancel();
    bool IsCurrent(const std::shared_ptr<State>& state) const;
    std::vector<Result> Take(const std::shared_ptr<State>& state);
    bool WaitForCompletion(const std::shared_ptr<State>& state) const noexcept;
    std::shared_ptr<State> Current() const { return m_state; }
    bool IsRefreshing() const noexcept { return m_refreshing; }
    void Complete() noexcept { m_refreshing = false; }
    void MarkPending() noexcept { m_pending = true; }
    bool TakePending() noexcept { const bool value = m_pending; m_pending = false; return value; }
    uint64_t Generation() const noexcept { return m_generation; }

private:
    bool m_refreshing = false;
    bool m_pending = false;
    uint64_t m_generation = 0;
    std::shared_ptr<State> m_state;
};
