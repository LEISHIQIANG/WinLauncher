#pragma once
#include <functional>
#include <map>
#include <vector>
#include <memory>
#include <mutex>
#include "CallbackGuard.h"

enum class EventType
{
    ConfigChanged,
    ShortcutLaunched,
    PopupShown,
    PopupHidden,
    AppQuit,
    ThemeChanged,
    UiScaleChanged,
    BackgroundStyleChanged
};


class EventBus
{
public:
    using Handler = std::function<void()>;
    using Token = size_t;

    explicit EventBus(std::shared_ptr<Logger> logger = nullptr)
        : m_logger(std::move(logger))
    {
    }

    Token Subscribe(EventType type, Handler handler)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        Token token = ++m_nextToken;
        m_handlers[type].push_back({ token, std::move(handler) });
        return token;
    }

    void Unsubscribe(EventType type, Token token)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_handlers.find(type);
        if (it == m_handlers.end()) return;
        auto& handlers = it->second;
        for (auto i = handlers.begin(); i != handlers.end(); ++i)
        {
            if (i->first == token)
            {
                handlers.erase(i);
                if (handlers.empty())
                    m_handlers.erase(it);
                return;
            }
        }
    }

    void Publish(EventType type)
    {
        // 一次性快照当前类型的所有 handler（含 token 和 handler 本身），
        // 避免逐 token 重新锁定 + 重新线性查找（O(n²) → O(n)）。
        // 在分发期间新注册的 handler 不会被本次 Publish 调用到，这是期望语义。
        std::vector<std::pair<Token, Handler>> snapshot;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_handlers.find(type);
            if (it == m_handlers.end()) return;
            snapshot = it->second;
        }
        for (auto& [token, handler] : snapshot)
        {
            // 分发前重新检查 handler 是否仍然有效（支持 Unsubscribe-during-dispatch）
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                auto it = m_handlers.find(type);
                if (it == m_handlers.end()) break;
                bool found = false;
                for (const auto& entry : it->second)
                {
                    if (entry.first == token) { found = true; break; }
                }
                if (!found) continue;
            }
            if (handler)
                CallbackGuard::Invoke(m_logger.get(), L"event_bus", handler);
        }
    }

    void UnsubscribeAll(EventType type)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_handlers.erase(type);
    }

private:
    using Entry = std::pair<Token, Handler>;
    std::map<EventType, std::vector<Entry>> m_handlers;
    Token m_nextToken = 0;
    std::shared_ptr<Logger> m_logger;
    mutable std::mutex m_mutex;
};
