#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <Windows.h>

class Logger;

class UiDispatcher : public std::enable_shared_from_this<UiDispatcher>
{
public:
    explicit UiDispatcher(std::shared_ptr<Logger> logger);
    ~UiDispatcher();

    void Bind(HWND hwnd);
    bool Post(const std::wstring& name, std::function<void()> callback);
    bool InvokeSync(const std::wstring& name, std::function<void()> callback, DWORD timeoutMs = INFINITE);
    bool HandleMessage(LPARAM payload);
    void Shutdown();
    bool IsUiThread() const noexcept;
    bool IsStopping() const noexcept { return m_stopping.load(); }

private:
    struct Request;
    struct Envelope;
    bool QueueRequest(const std::shared_ptr<Request>& request);

    std::shared_ptr<Logger> m_logger;
    HWND m_hwnd = nullptr;
    DWORD m_uiThreadId = 0;
    std::atomic_bool m_stopping{ false };
    mutable std::mutex m_mutex;
    std::set<std::shared_ptr<Request>, std::owner_less<std::shared_ptr<Request>>> m_pending;
};
