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
        std::vector<Token> tokens;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_handlers.find(type);
            if (it == m_handlers.end()) return;
            for (const auto& entry : it->second) tokens.push_back(entry.first);
        }
        for (Token token : tokens)
        {
            Handler handler;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                auto it = m_handlers.find(type);
                if (it == m_handlers.end()) continue;
                for (const auto& entry : it->second)
                {
                    if (entry.first == token) { handler = entry.second; break; }
                }
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
