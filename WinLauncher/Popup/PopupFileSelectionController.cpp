#include "PopupFileSelectionController.h"

bool PopupFileSelectionController::IsValid(double elapsed, int validitySeconds)
{
    return validitySeconds < 0 || (elapsed >= 0.0 && elapsed <= validitySeconds);
}

void PopupFileSelectionController::Begin(const std::shared_ptr<Services::SelectionRequest>& request, HWND sourceHwnd, double capturedTime)
{
    Cancel();
    std::lock_guard<std::mutex> lock(m_mutex);
    ++m_generation;
    m_context = {};
    m_context.sourceHwnd = sourceHwnd;
    m_context.capturedTime = capturedTime;
    m_context.isPending = true;
    m_request = request;
}

void PopupFileSelectionController::Cancel()
{
    std::shared_ptr<Services::SelectionRequest> request;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ++m_generation;
        request = std::move(m_request);
        m_context.isPending = false;
    }
    if (request) request->Cancel();
}

bool PopupFileSelectionController::Poll()
{
    std::shared_ptr<Services::SelectionRequest> request;
    uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        request = m_request;
        generation = m_generation;
    }
    Services::SelectionContext result;
    if (!request || !request->TryGetResult(result)) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (generation != m_generation || request != m_request || result.sourceHwnd != m_context.sourceHwnd) return false;
    m_context = std::move(result);
    m_request.reset();
    return true;
}

bool PopupFileSelectionController::IsPending() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_context.isPending;
}

bool PopupFileSelectionController::Peek(double now, int validitySeconds, std::vector<std::wstring>& files, double* elapsed) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const double value = now - m_context.capturedTime;
    if (elapsed) *elapsed = value;
    if (m_context.isPending || m_context.filePaths.empty() || !IsValid(value, validitySeconds)) return false;
    files = m_context.filePaths;
    return true;
}

bool PopupFileSelectionController::Consume(double now, int validitySeconds, std::vector<std::wstring>& files)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const double elapsed = now - m_context.capturedTime;
    if (m_context.isPending || m_context.filePaths.empty() || !IsValid(elapsed, validitySeconds)) return false;
    files = std::move(m_context.filePaths);
    return true;
}

void PopupFileSelectionController::Clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_context.filePaths.clear();
    m_context.isPending = false;
}

bool PopupFileSelectionController::ExpireIfNeeded(double now, int validitySeconds)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_context.isPending || m_context.filePaths.empty() || IsValid(now - m_context.capturedTime, validitySeconds)) return false;
    m_context.filePaths.clear();
    return true;
}
