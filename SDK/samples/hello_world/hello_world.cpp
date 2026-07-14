#include <WinLauncher/WinLauncherPluginABI.h>
#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <string>

namespace
{
    struct HelloPlugin
    {
        const WLHostApiV1* host = nullptr;
    };

    bool WL_CALL OnLoad(void* userData)
    {
        auto* plugin = static_cast<HelloPlugin*>(userData);
        if (plugin && plugin->host && plugin->host->log)
            plugin->host->log(plugin->host->hostContext, L"Hello World plugin loaded.");
        return true;
    }

    void WL_CALL OnUnload(void* userData)
    {
        auto* plugin = static_cast<HelloPlugin*>(userData);
        if (plugin && plugin->host && plugin->host->log)
            plugin->host->log(plugin->host->hostContext, L"Hello World plugin unloaded.");
    }

    bool WL_CALL ExecuteCommand(void*, const WLCommandContextV1* context, WLStringResultV1* outMessage)
    {
        const wchar_t* commandId = (context && context->commandId) ? context->commandId : L"";
        const wchar_t* text = (wcscmp(commandId, L"example.hello_world.hello") == 0)
            ? L"Hello from a WinLauncher plugin icon."
            : L"Hello from a WinLauncher plugin command.";

        uint32_t required = (uint32_t)wcslen(text) + 1;
        if (outMessage)
        {
            outMessage->requiredLength = required;
            if (outMessage->buffer && outMessage->bufferLength >= required)
                wcscpy_s(outMessage->buffer, outMessage->bufferLength, text);
        }
        return true;
    }

    std::wstring ReadConfigValue(const WLHostApiV1* host, const wchar_t* key, const wchar_t* defaultValue)
    {
        if (!host || !host->getPluginConfig)
            return defaultValue ? defaultValue : L"";

        wchar_t buffer[512]{};
        WLStringResultV1 result{};
        result.size = sizeof(result);
        result.buffer = buffer;
        result.bufferLength = (uint32_t)_countof(buffer);
        if (!host->getPluginConfig(host->hostContext, key, defaultValue, &result))
            return defaultValue ? defaultValue : L"";
        return buffer;
    }

    bool WL_CALL ExecuteSlashCommand(void* userData, const WLSlashCommandContextV1* context, WLStringResultV1* outMessage)
    {
        auto* plugin = static_cast<HelloPlugin*>(userData);
        std::wstring args = (context && context->args) ? context->args : L"";
        std::wstring greeting = ReadConfigValue(plugin ? plugin->host : nullptr, L"greeting", L"Hello from a WinLauncher slash command.");
        std::wstring text = args.empty()
            ? greeting
            : greeting + L" Args: " + args;

        uint32_t required = (uint32_t)text.size() + 1;
        if (outMessage)
        {
            outMessage->requiredLength = required;
            if (outMessage->buffer && outMessage->bufferLength >= required)
                wcscpy_s(outMessage->buffer, outMessage->bufferLength, text.c_str());
        }
        return true;
    }

    bool ContainsInsensitive(const std::wstring& value, const std::wstring& query)
    {
        if (query.empty())
            return true;

        std::wstring haystack = value;
        std::wstring needle = query;
        std::transform(haystack.begin(), haystack.end(), haystack.begin(), [](wchar_t c) {
            return (wchar_t)towlower(c);
        });
        std::transform(needle.begin(), needle.end(), needle.begin(), [](wchar_t c) {
            return (wchar_t)towlower(c);
        });
        return haystack.find(needle) != std::wstring::npos;
    }

    bool WL_CALL Search(void*, const WLSearchRequestV1* request, WLSearchResponseV1* response)
    {
        if (!request || !response || !response->addResult || request->slashMode)
            return true;

        std::wstring query = request->query ? request->query : L"";
        if (!ContainsInsensitive(L"hello world plugin sample", query))
            return true;

        WLSearchResultV1 result{};
        result.size = sizeof(result);
        result.id = L"example.hello_world.search.hello";
        result.title = L"Hello Search Result";
        result.description = L"Dynamic result returned by the sample plugin.";
        result.commandId = L"example.hello_world.hello";
        result.score = 90;
        response->addResult(response->hostContext, &result);
        return true;
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

    auto* plugin = new HelloPlugin();
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
    instance->search = &Search;
    instance->requestShutdown = nullptr;
    instance->isShutdownComplete = nullptr;

    *outInstance = instance;
    return true;
}

WL_EXPORT void WL_CALL WinLauncherPlugin_Destroy(WLPluginInstanceV1* instance)
{
    if (!instance)
        return;
    delete static_cast<HelloPlugin*>(instance->userData);
    delete instance;
}
