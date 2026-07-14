#pragma once

#include "../Services/FileSelectionService.h"
#include <memory>
#include <mutex>
#include <vector>

class PopupFileSelectionController
{
public:
    void Begin(const std::shared_ptr<Services::SelectionRequest>& request, HWND sourceHwnd, double capturedTime);
    void Cancel();
    bool Poll();
    bool IsPending() const;
    bool Peek(double now, int validitySeconds, std::vector<std::wstring>& files, double* elapsed = nullptr) const;
    bool Consume(double now, int validitySeconds, std::vector<std::wstring>& files);
    void Clear();
    bool ExpireIfNeeded(double now, int validitySeconds);

private:
    static bool IsValid(double elapsed, int validitySeconds);
    mutable std::mutex m_mutex;
    Services::SelectionContext m_context;
    std::shared_ptr<Services::SelectionRequest> m_request;
    uint64_t m_generation = 0;
};
