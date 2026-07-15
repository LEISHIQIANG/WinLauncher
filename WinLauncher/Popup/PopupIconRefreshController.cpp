#include "PopupIconRefreshController.h"

PopupIconRefreshController::State::State()
    : completionEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr))
{
}

PopupIconRefreshController::State::~State()
{
    for (const auto& result : results)
        if (result.icon) DestroyIcon(result.icon);
    if (completionEvent) CloseHandle(completionEvent);
}

std::shared_ptr<PopupIconRefreshController::State> PopupIconRefreshController::Begin()
{
    if (m_refreshing) { m_pending = true; return {}; }
    m_refreshing = true;
    auto state = std::make_shared<State>();
    state->generation = ++m_generation;
    m_state = state;
    return state;
}

void PopupIconRefreshController::Cancel()
{
    if (m_state)
    {
        m_state->cancelled = true;
        if (m_state->completionEvent) SetEvent(m_state->completionEvent);
    }
    m_state.reset();
    m_refreshing = false;
    m_pending = false;
    ++m_generation;
}

bool PopupIconRefreshController::IsCurrent(const std::shared_ptr<State>& state) const
{
    return state && !state->cancelled && state == m_state && state->generation == m_generation;
}

std::vector<PopupIconRefreshController::Result> PopupIconRefreshController::Take(const std::shared_ptr<State>& state)
{
    if (!IsCurrent(state)) return {};
    std::lock_guard<std::mutex> lock(state->mutex);
    std::vector<Result> result;
    result.swap(state->results);
    return result;
}

bool PopupIconRefreshController::WaitForCompletion(const std::shared_ptr<State>& state, DWORD timeoutMs) const noexcept
{
    if (!IsCurrent(state)) return false;
    if (!state->completionEvent) return true;
    return WaitForSingleObject(state->completionEvent, timeoutMs) == WAIT_OBJECT_0 && IsCurrent(state);
}
