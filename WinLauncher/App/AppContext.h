#pragma once
#include <Windows.h>
#include <memory>
#include "EventBus.h"
#include "Logger.h"
#include "PluginManager.h"
#include "BackgroundTaskService.h"
#include "UiDispatcher.h"
#include "CrashReporter.h"
#include "../Services/IConfigService.h"
#include "../Services/IIconService.h"
#include "../Services/ConfigPath.h"
#include "../Services/UsageHistoryStore.h"
#include "../Services/DiagnosticService.h"

struct AppContext
{
    std::shared_ptr<CrashReporter> crashReporter;
    std::shared_ptr<Logger> logger;
    std::shared_ptr<EventBus> eventBus;
    std::shared_ptr<BackgroundTaskService> backgroundTasks;
    std::shared_ptr<UiDispatcher> uiDispatcher;
    std::shared_ptr<PluginManager> pluginManager;
    std::shared_ptr<UsageHistoryStore> usageHistory;
    std::shared_ptr<DiagnosticService> diagnostics;

    std::unique_ptr<IConfigService> configService;
    std::unique_ptr<IIconService> iconService;

    HWND hMainWnd = nullptr;
    HINSTANCE hInstance = nullptr;

    AppContext()
        : crashReporter(std::make_shared<CrashReporter>(ConfigPath::GetUserDataDirectory() + L"\\crash"))
        , logger(std::make_shared<Logger>(ConfigPath::PrepareUserConfigDirectory() + L"\\winlauncher.log"))
        , eventBus(std::make_shared<EventBus>(logger))
        , backgroundTasks(std::make_shared<BackgroundTaskService>(logger))
        , uiDispatcher(std::make_shared<UiDispatcher>(logger))
        , pluginManager(std::make_shared<PluginManager>(eventBus, logger, uiDispatcher, backgroundTasks))
        , usageHistory(std::make_shared<UsageHistoryStore>(ConfigPath::GetUserDataDirectory() + L"\\usage_history.json"))
        , diagnostics(std::make_shared<DiagnosticService>(logger.get()))
    {
    }

    ~AppContext() = default;
};
