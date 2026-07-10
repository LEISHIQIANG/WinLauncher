#include "UiDispatcher.h"
#include "AppMessages.h"
#include "CallbackGuard.h"
#include "Logger.h"

struct UiDispatcher::Request
{
    ~Request() { if (completed) CloseHandle(completed); }
    std::wstring name;
    std::function<void()> callback;
    HANDLE completed = nullptr;
    std::atomic_bool cancelled{ false };
};

struct UiDispatcher::Envelope
{
    std::weak_ptr<UiDispatcher> owner;
    std::shared_ptr<Request> request;
};

UiDispatcher::UiDispatcher(std::shared_ptr<Logger> logger)
    : m_logger(std::move(logger))
{
}

UiDispatcher::~UiDispatcher()
{
    Shutdown();
}

void UiDispatcher::Bind(HWND hwnd)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_hwnd = hwnd;
    m_uiThreadId = GetCurrentThreadId();
}

bool UiDispatcher::IsUiThread() const noexcept
{
    return m_uiThreadId != 0 && GetCurrentThreadId() == m_uiThreadId;
}

bool UiDispatcher::QueueRequest(const std::shared_ptr<Request>& request)
{
    HWND hwnd = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopping || !m_hwnd || !IsWindow(m_hwnd)) return false;
        hwnd = m_hwnd;
        m_pending.insert(request);
    }

    auto envelope = new Envelope{ weak_from_this(), request };
    if (!PostMessageW(hwnd, AppMessages::UiDispatch, 0, reinterpret_cast<LPARAM>(envelope)))
    {
        delete envelope;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending.erase(request);
        return false;
    }
    return true;
}

bool UiDispatcher::Post(const std::wstring& name, std::function<void()> callback)
{
    if (!callback) return false;
    if (IsUiThread() && !m_stopping)
        return CallbackGuard::Invoke(m_logger.get(), name.c_str(), callback);

    auto request = std::make_shared<Request>();
    request->name = name;
    request->callback = std::move(callback);
    return QueueRequest(request);
}

bool UiDispatcher::InvokeSync(const std::wstring& name, std::function<void()> callback, DWORD timeoutMs)
{
    if (!callback) return false;
    if (IsUiThread() && !m_stopping)
        return CallbackGuard::Invoke(m_logger.get(), name.c_str(), callback);

    auto request = std::make_shared<Request>();
    request->name = name;
    request->callback = std::move(callback);
    request->completed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!request->completed) return false;

    if (!QueueRequest(request))
    {
        return false;
    }

    DWORD wait = WaitForSingleObject(request->completed, timeoutMs);
    if (wait != WAIT_OBJECT_0)
        request->cancelled = true;
    return wait == WAIT_OBJECT_0 && !request->cancelled;
}

bool UiDispatcher::HandleMessage(LPARAM payload)
{
    std::unique_ptr<Envelope> envelope(reinterpret_cast<Envelope*>(payload));
    if (!envelope) return false;
    auto owner = envelope->owner.lock();
    if (!owner) return false;
    auto request = envelope->request;

    if (!request->cancelled && !owner->m_stopping)
        CallbackGuard::Invoke(owner->m_logger.get(), request->name.c_str(), request->callback);

    {
        std::lock_guard<std::mutex> lock(owner->m_mutex);
        owner->m_pending.erase(request);
    }
    if (request->completed) SetEvent(request->completed);
    return true;
}

void UiDispatcher::Shutdown()
{
    if (m_stopping.exchange(true)) return;

    std::set<std::shared_ptr<Request>, std::owner_less<std::shared_ptr<Request>>> pending;
    HWND hwnd = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        hwnd = m_hwnd;
        m_hwnd = nullptr;
        pending.swap(m_pending);
    }
    for (const auto& request : pending)
    {
        request->cancelled = true;
        if (request->completed) SetEvent(request->completed);
    }

    if (hwnd && GetCurrentThreadId() == m_uiThreadId)
    {
        MSG msg{};
        while (PeekMessageW(&msg, hwnd, AppMessages::UiDispatch, AppMessages::UiDispatch, PM_REMOVE))
        {
            std::unique_ptr<Envelope> envelope(reinterpret_cast<Envelope*>(msg.lParam));
        }
    }
}
