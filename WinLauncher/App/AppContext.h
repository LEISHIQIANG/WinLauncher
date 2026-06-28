#pragma once
#include <Windows.h>
#include <memory>
#include "EventBus.h"
#include "Logger.h"
#include "PluginHost.h"
#include "../Services/IConfigService.h"
#include "../Services/IIconService.h"
#include "../Services/ConfigPath.h"

struct AppContext
{
    std::shared_ptr<EventBus> eventBus;
    std::shared_ptr<Logger> logger;
    std::unique_ptr<PluginHost> pluginHost;

    std::unique_ptr<IConfigService> configService;
    std::unique_ptr<IIconService> iconService;

    HWND hMainWnd = nullptr;
    HINSTANCE hInstance = nullptr;

    AppContext()
        : eventBus(std::make_shared<EventBus>())
        , logger(std::make_shared<Logger>(ConfigPath::PrepareUserConfigDirectory() + L"\\winlauncher.log"))
        , pluginHost(std::make_unique<PluginHost>(eventBus))
    {
    }

    ~AppContext() = default;
};
