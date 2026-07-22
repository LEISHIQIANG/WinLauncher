#pragma once
#include <Windows.h>
#include <string>
#include "../SDK/include/WinLauncher/WinLauncherPluginABI.h"

class PluginDllLoader
{
public:
    PluginDllLoader() = default;
    ~PluginDllLoader();

    bool Load(const std::wstring& dllPath, const WLHostApiV1* hostApi, std::wstring* errorMessage = nullptr);
    void Unload();
    bool IsLoaded() const { return m_hModule != nullptr; }

    HMODULE GetModuleHandle() const { return m_hModule; }
    WLPluginInstanceV1* GetPluginInstance() const { return m_pluginInstance; }

private:
    HMODULE m_hModule = nullptr;
    WLPluginInstanceV1* m_pluginInstance = nullptr;
    WLDestroyPluginFn m_destroyFn = nullptr;
};
