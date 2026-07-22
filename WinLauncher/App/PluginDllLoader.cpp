#include "PluginDllLoader.h"
#include "Logger.h"

PluginDllLoader::~PluginDllLoader()
{
    Unload();
}

bool PluginDllLoader::Load(const std::wstring& dllPath, const WLHostApiV1* hostApi, std::wstring* errorMessage)
{
    Unload();

    if (dllPath.empty())
    {
        if (errorMessage) *errorMessage = L"DLL path is empty";
        return false;
    }

    HMODULE hMod = LoadLibraryExW(dllPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!hMod)
    {
        DWORD err = GetLastError();
        if (errorMessage) *errorMessage = L"LoadLibraryExW failed with code " + std::to_wstring(err);
        return false;
    }

    auto getAbiVersionFn = (WLGetAbiVersionFn)(void*)GetProcAddress(hMod, "WinLauncherPlugin_GetAbiVersion");
    auto createPluginFn = (WLCreatePluginFn)(void*)GetProcAddress(hMod, "WinLauncherPlugin_Create");
    auto destroyPluginFn = (WLDestroyPluginFn)(void*)GetProcAddress(hMod, "WinLauncherPlugin_Destroy");

    if (!createPluginFn)
    {
        FreeLibrary(hMod);
        if (errorMessage) *errorMessage = L"Export function WinLauncherPlugin_Create not found";
        return false;
    }

    if (getAbiVersionFn)
    {
        uint32_t abiVersion = getAbiVersionFn();
        if (abiVersion != WINLAUNCHER_PLUGIN_ABI_VERSION)
        {
            FreeLibrary(hMod);
            if (errorMessage) *errorMessage = L"Incompatible plugin ABI version: " + std::to_wstring(abiVersion);
            return false;
        }
    }

    WLPluginInstanceV1* instance = nullptr;
    if (!createPluginFn(hostApi, &instance) || !instance)
    {
        FreeLibrary(hMod);
        if (errorMessage) *errorMessage = L"WinLauncherPlugin_Create returned false or null instance";
        return false;
    }

    m_hModule = hMod;
    m_pluginInstance = instance;
    m_destroyFn = destroyPluginFn;
    return true;
}

void PluginDllLoader::Unload()
{
    if (m_pluginInstance && m_destroyFn)
    {
        m_destroyFn(m_pluginInstance);
        m_pluginInstance = nullptr;
    }
    m_destroyFn = nullptr;

    if (m_hModule)
    {
        FreeLibrary(m_hModule);
        m_hModule = nullptr;
    }
}
