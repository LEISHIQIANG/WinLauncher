#pragma once
#include <functional>
#include <map>
#include <vector>
#include <memory>

enum class EventType
{
    ConfigChanged,
    ShortcutLaunched,
    PopupShown,
    PopupHidden,
    AppQuit,
    ThemeChanged
};


class EventBus
{
public:
    using Handler = std::function<void()>;
    using Token = size_t;

    Token Subscribe(EventType type, Handler handler)
    {
        Token token = ++m_nextToken;
        m_handlers[type].push_back({ token, std::move(handler) });
        return token;
    }

    void Unsubscribe(EventType type, Token token)
    {
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
        auto it = m_handlers.find(type);
        if (it == m_handlers.end()) return;
        // Copy handlers in case one handler unsubscribes another
        auto handlers = it->second;
        for (auto& entry : handlers)
        {
            if (entry.second) entry.second();
        }
    }

    void UnsubscribeAll(EventType type)
    {
        m_handlers.erase(type);
    }

private:
    using Entry = std::pair<Token, Handler>;
    std::map<EventType, std::vector<Entry>> m_handlers;
    Token m_nextToken = 0;
};
