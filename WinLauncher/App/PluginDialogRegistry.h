#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>

struct PluginDialogState
{
    std::wstring pluginId;
    std::wstring message;
    bool cancelable = false;
    bool cancelled = false;
    uint64_t total = 0;
    uint64_t current = 0;
};

class PluginDialogRegistry
{
public:
    PluginDialogRegistry() = default;

    uint64_t Register(const std::wstring& pluginId, const std::wstring& message, bool cancelable, uint64_t total = 0);
    bool Update(uint64_t handle, const std::wstring& message, uint64_t current = 0);
    bool Remove(uint64_t handle);
    bool IsCancelled(uint64_t handle, bool* outCancelled) const;

private:
    mutable std::mutex m_mutex;
    std::map<uint64_t, PluginDialogState> m_states;
    uint64_t m_nextHandle = 1;
};
