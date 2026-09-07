#pragma once

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>
#include <string>

class PopupIconRefreshController
{
public:
    struct Result
    {
        bool dock = false;
        size_t pageIndex = 0;
        size_t shortcutIndex = 0;
        HICON icon = nullptr;
        std::wstring identity;
        uint64_t layoutGeneration = 0;
    };

    struct State
    {
        State();
        ~State();
        std::mutex mutex;
        std::vector<Result> results;
        std::atomic_bool cancelled{ false };
        std::atomic_size_t pendingWorkers{ 0 };
        uint64_t generation = 0;
        uint64_t layoutGeneration = 0;
        HANDLE completionEvent = nullptr;
    };

    std::shared_ptr<State> Begin(bool force = false);
    void Cancel();
    bool IsCurrent(const std::shared_ptr<State>& state) const;
    std::vector<Result> Take(const std::shared_ptr<State>& state);
    // Callers use a finite timeout (zero for a non-blocking readiness poll).
    // Completion is always consumed later on the UI thread.
    bool WaitForCompletion(const std::shared_ptr<State>& state, DWORD timeoutMs) const noexcept;
    std::shared_ptr<State> Current() const { return m_state; }
    bool IsRefreshing() const noexcept { return m_refreshing; }
    void Complete() noexcept { m_refreshing = false; }
    bool TakePendingForce() noexcept { const bool value = m_pendingForce; m_pendingForce = false; return value; }
    void MarkPending() noexcept { m_pending = true; }
    bool TakePending() noexcept { const bool value = m_pending; m_pending = false; return value; }
    uint64_t Generation() const noexcept { return m_generation; }

private:
    bool m_refreshing = false;
    bool m_pending = false;
    bool m_pendingForce = false;
    uint64_t m_generation = 0;
    std::shared_ptr<State> m_state;
};
