#include "PluginDialogRegistry.h"

uint64_t PluginDialogRegistry::Register(const std::wstring& pluginId, const std::wstring& message, bool cancelable, uint64_t total)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    uint64_t handle = m_nextHandle++;
    PluginDialogState state;
    state.pluginId = pluginId;
    state.message = message;
    state.cancelable = cancelable;
    state.total = total;
    m_states[handle] = std::move(state);
    return handle;
}

bool PluginDialogRegistry::Update(uint64_t handle, const std::wstring& message, uint64_t current)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_states.find(handle);
    if (it == m_states.end())
        return false;
    if (!message.empty())
        it->second.message = message;
    it->second.current = current;
    return true;
}

bool PluginDialogRegistry::Remove(uint64_t handle)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_states.erase(handle) > 0;
}

bool PluginDialogRegistry::IsCancelled(uint64_t handle, bool* outCancelled) const
{
    if (!outCancelled)
        return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_states.find(handle);
    if (it == m_states.end())
        return false;
    *outCancelled = it->second.cancelled;
    return true;
}
