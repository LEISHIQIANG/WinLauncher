#pragma once
#include "EventBus.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include "../Model/ShortcutInfo.h"

class IPlugin
{
public:
    virtual ~IPlugin() = default;

    virtual const wchar_t* GetName() const = 0;
    virtual const wchar_t* GetVersion() const = 0;

    virtual void OnLoad(std::shared_ptr<EventBus> eventBus) {}
    virtual void OnUnload() {}
    virtual void OnShortcutLaunched(const Model::ShortcutInfo& shortcut) {}
    virtual void OnPopupShown() {}
    virtual void OnPopupHidden() {}
};

class PluginHost
{
public:
    explicit PluginHost(std::shared_ptr<EventBus> eventBus)
        : m_eventBus(std::move(eventBus))
    {
    }

    void RegisterPlugin(std::unique_ptr<IPlugin> plugin)
    {
        if (!plugin) return;
        plugin->OnLoad(m_eventBus);
        m_plugins.push_back(std::move(plugin));
    }

    void UnloadAll()
    {
        for (auto& p : m_plugins)
        {
            if (p) p->OnUnload();
        }
        m_plugins.clear();
    }

    size_t GetPluginCount() const { return m_plugins.size(); }

    void NotifyShortcutLaunched(const Model::ShortcutInfo& shortcut)
    {
        for (auto& p : m_plugins)
        {
            if (p) p->OnShortcutLaunched(shortcut);
        }
    }

    void NotifyPopupShown()
    {
        for (auto& p : m_plugins)
        {
            if (p) p->OnPopupShown();
        }
    }

    void NotifyPopupHidden()
    {
        for (auto& p : m_plugins)
        {
            if (p) p->OnPopupHidden();
        }
    }

private:
    std::shared_ptr<EventBus> m_eventBus;
    std::vector<std::unique_ptr<IPlugin>> m_plugins;
};
