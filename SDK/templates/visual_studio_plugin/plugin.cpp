#include <WinLauncher/WinLauncherPluginABI.h>
#include <cwchar>
#include <string>

namespace
{
    struct TemplatePlugin
    {
        const WLHostApiV1* host = nullptr;
    };

    bool CopyResult(const std::wstring& value, WLStringResultV1* outResult)
    {
        if (!outResult || outResult->size < sizeof(WLStringResultV1))
            return false;

        outResult->requiredLength = (uint32_t)value.size() + 1;
        if (!outResult->buffer || outResult->bufferLength < outResult->requiredLength)
            return true;

        wcscpy_s(outResult->buffer, outResult->bufferLength, value.c_str());
        return true;
    }

    bool WL_CALL OnLoad(void* userData)
    {
        auto* plugin = static_cast<TemplatePlugin*>(userData);
        if (plugin && plugin->host && plugin->host->log)
            plugin->host->log(plugin->host->hostContext, L"Template plugin loaded.");
        return true;
    }

    void WL_CALL OnUnload(void* userData)
    {
        auto* plugin = static_cast<TemplatePlugin*>(userData);
        if (plugin && plugin->host && plugin->host->log)
            plugin->host->log(plugin->host->hostContext, L"Template plugin unloaded.");
    }

    bool WL_CALL ExecuteCommand(void*, const WLCommandContextV1*, WLStringResultV1* outMessage)
    {
        return CopyResult(L"Hello from a WinLauncher plugin command.", outMessage);
    }

    bool WL_CALL ExecuteSlashCommand(void*, const WLSlashCommandContextV1* context, WLStringResultV1* outMessage)
    {
        std::wstring args = (context && context->args) ? context->args : L"";
        std::wstring message = args.empty()
            ? L"Hello from a WinLauncher slash command."
            : L"Hello from a WinLauncher slash command: " + args;
        return CopyResult(message, outMessage);
    }
}

WL_EXPORT uint32_t WL_CALL WinLauncherPlugin_GetAbiVersion()
{
    return WINLAUNCHER_PLUGIN_ABI_VERSION;
}

WL_EXPORT bool WL_CALL WinLauncherPlugin_Create(const WLHostApiV1* host, WLPluginInstanceV1** outInstance)
{
    if (!host || !outInstance)
        return false;

    auto* plugin = new TemplatePlugin();
    plugin->host = host;

    auto* instance = new WLPluginInstanceV1();
    instance->size = sizeof(WLPluginInstanceV1);
    instance->userData = plugin;
    instance->onLoad = &OnLoad;
    instance->onUnload = &OnUnload;
    instance->executeCommand = &ExecuteCommand;
    instance->executeSlashCommand = &ExecuteSlashCommand;
    instance->onPopupShown = nullptr;
    instance->onPopupHidden = nullptr;
    instance->search = nullptr;
    instance->requestShutdown = nullptr;
    instance->isShutdownComplete = nullptr;

    *outInstance = instance;
    return true;
}

WL_EXPORT void WL_CALL WinLauncherPlugin_Destroy(WLPluginInstanceV1* instance)
{
    if (!instance)
        return;
    delete static_cast<TemplatePlugin*>(instance->userData);
    delete instance;
}
