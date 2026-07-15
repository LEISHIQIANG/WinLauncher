#include "Application.h"
#include "AppMessages.h"
#include "PluginManager.h"
#include "../AutoStartHelper.h"
#include "../Config/ConfigWindow.h"
#include "../Config/ConfirmWindow.h"
#include "../Services/UpdateService.h"
#include "../Config/UIStyle.h"
#include "../DpiHelper.h"
#include "../KeyboardHook.h"
#include "../MouseHook.h"
#include "../PopupWindow.h"
#include "../ToastWindow.h"
#include "../resource.h"
#include "../Services/EnvironmentDetector.h"
#include "../Services/IniConfigRepository.h"
#include "../Services/FolderWatcher.h"
#include "../Services/SystemIconService.h"
#include "../Services/CommandExecutionService.h"
#include "../UI/UserInteractionService.h"
#include "../UI/WindowCoordinator.h"
#include "../TrayMenuWindow.h"
#include "../Services/BatchLaunchService.h"
#include "../Services/MacroService.h"
#include <CommCtrl.h>
#include <ole2.h>
#include <shellapi.h>
#include <timeapi.h>
#include <chrono>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "winmm.lib")

static UINT g_uShellRestart = 0;
static constexpr UINT_PTR UI_HEARTBEAT_TIMER_ID = 0xA11;

Application::Application(HINSTANCE hInstance)
    : m_hInstance(hInstance)
{
}

Application::~Application()
{
    Shutdown();
}

int Application::Run()
{
    if (!InitializeProcess())
        return 1;

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool justUpdated = false;
    if (argv)
    {
        for (int i = 0; i < argc; ++i)
        {
            if (wcscmp(argv[i], L"--updated") == 0)
            {
                justUpdated = true;
                break;
            }
        }
        LocalFree(argv);
    }

    if (HandleHelperCommandLine())
        return 0;

    // Check if an instance is already running using a named Mutex
    m_hSingleInstanceMutex = CreateMutexW(nullptr, TRUE, L"Local\\WinLauncher_SingleInstance_Mutex");
    if (m_hSingleInstanceMutex && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        HWND hExisting = FindWindowW(L"WinLauncherMain", nullptr);
        if (hExisting)
        {
            PostMessageW(hExisting, AppMessages::ShowConfigWindow, 0, 0);
        }
        CloseHandle(m_hSingleInstanceMutex);
        m_hSingleInstanceMutex = nullptr;
        return 0;
    }

    m_appCtx = std::make_shared<AppContext>();
    m_appCtx->hInstance = m_hInstance;

    LOG_INFO(m_appCtx->logger, L"Application::Run: process initialized, creating main window");

    if (!CreateMainWindow())
    {
        LOG_ERROR(m_appCtx->logger, L"Application::Run: CreateMainWindow failed! GetLastError()=%d", GetLastError());
        return 1;
    }
    m_appCtx->uiDispatcher->Bind(m_hMainWnd);

    if (!InitializeServices())
    {
        LOG_ERROR(m_appCtx->logger, L"Application::Run: InitializeServices failed!");
        return 1;
    }

    // Check GPU crash recovery marker from previous session
    {
        const std::wstring markerPath = ConfigPath::GetGpuCrashMarkerPath();
        HANDLE hMarker = CreateFileW(markerPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hMarker != INVALID_HANDLE_VALUE)
        {
            CloseHandle(hMarker);
            DeleteFileW(markerPath.c_str());
            LOG_INFO(m_appCtx->logger, L"Application::Run: GPU crash recovery marker found — disabling hardware acceleration");
            if (m_appCtx->configService)
            {
                m_appCtx->configService->SetHardwareAccelerationEnabled(false);
            }
            UIStyle::Performance::SetHardwareAccelerationEnabled(false);
        }
    }

    // Start background environment detection (executors like python, git bash)
    EnvironmentDetector::StartDetection(m_appCtx->backgroundTasks);

    if (!LoadRuntimeSettings())
    {
        LOG_ERROR(m_appCtx->logger, L"Application::Run: LoadRuntimeSettings failed!");
        return 1;
    }

    UpdateTrayIconState();

    if (!InstallHooks())
    {
        LOG_ERROR(m_appCtx->logger, L"Application::Run: InstallHooks failed!");
        return 1;
    }

    PopupWindow::Init(m_appCtx.get());
    TrayMenuWindow::Init(m_hMainWnd, m_appCtx.get());
    // Create the real HWND render target once the message loop is idle instead
    // of making the user's first tray right-click initialise the GPU device.
    PostMessageW(m_hMainWnd, AppMessages::PrewarmTrayMenu, 0, 0);

    // Install keyboard hook before arming double-Alt pause/resume. If the hook
    // is unavailable, keep the tray control usable and leave no false-active
    // keyboard trigger state behind.
    if (KeyboardHook::Install())
    {
        KeyboardHook::SetDoubleAltTarget(m_hMainWnd, 400);
    }
    else
    {
        LOG_ERROR(m_appCtx->logger, L"Application::Run: keyboard hook unavailable; double-Alt pause is disabled until hooks restart");
    }

    if (justUpdated)
    {
        ToastWindow::Show(L"更新成功！已升级至最新版本", 3000);
    }

    UpdateService::GetInstance().CheckForUpdates(m_hMainWnd, true, m_appCtx.get());
    // The main window, hooks, and popup are ready before potentially slow DLL
    // scanning/loading starts. The posted message is handled on the UI thread.
    PostMessageW(m_hMainWnd, AppMessages::InitializePlugins, 0, 0);
    StartUiWatchdog();

    int exitCode = MessageLoop();
    Shutdown();
    return exitCode;
}

bool Application::InitializeProcess()
{
    g_uShellRestart = RegisterWindowMessageW(L"TaskbarCreated");
    timeBeginPeriod(1);
    m_timerResolutionRaised = true;

    bool dpiAware = false;
    auto user32 = GetModuleHandleW(L"user32.dll");
    if (user32)
    {
        auto fn = reinterpret_cast<BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT)>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        dpiAware = fn && fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }

    if (!dpiAware)
    {
        HMODULE shcore = LoadLibraryW(L"shcore.dll");
        if (shcore)
        {
            auto fn = reinterpret_cast<HRESULT(WINAPI*)(int)>(
                GetProcAddress(shcore, "SetProcessDpiAwareness"));
            dpiAware = fn && SUCCEEDED(fn(2)); // PROCESS_PER_MONITOR_DPI_AWARE
            FreeLibrary(shcore);
        }
    }

    if (!dpiAware)
    {
        dpiAware = SetProcessDPIAware() != FALSE;
    }

    InitCommonControls();
    if (SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
        m_comInitialized = true;

    return true;
}

bool Application::HandleHelperCommandLine()
{
    return AutoStartHelper::HandleCommandLine();
}

bool Application::CreateMainWindow()
{
    const wchar_t className[] = L"WinLauncherMain";

    WNDCLASSW wc{};
    wc.lpfnWndProc   = Application::WindowProc;
    wc.hInstance     = m_hInstance;
    wc.hIcon         = LoadIconW(m_hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
    wc.lpszClassName = className;

    RegisterClassW(&wc);

    m_hMainWnd = CreateWindowExW(
        0,
        className,
        L"WinLauncher",
        WS_POPUP,
        0, 0, 0, 0,
        nullptr,
        nullptr,
        m_hInstance,
        this);

    if (!m_hMainWnd)
        return false;

    m_appCtx->hMainWnd = m_hMainWnd;
    return true;
}

bool Application::InitializeServices()
{
    auto configRepository = std::make_shared<IniConfigRepository>(
        m_appCtx->logger.get(),
        m_hMainWnd,
        AppMessages::ConfigChanged);

    m_appCtx->configService = configRepository;
    m_appCtx->iconService   = std::make_shared<SystemIconService>();
    m_appCtx->userInteraction = std::make_shared<UserInteractionService>(m_appCtx.get());
    m_appCtx->commandExecution = std::make_shared<CommandExecutionService>(m_appCtx);
    m_appCtx->windowCoordinator = std::make_shared<WindowCoordinator>(m_appCtx.get());

    LOG_INFO(m_appCtx->logger, L"WinLauncher starting...");
    if (m_appCtx->pluginManager)
    {
        m_appCtx->pluginManager->SetUserInteraction(m_appCtx->userInteraction);
    }

    // Validate autostart configuration and self-check
    AutoStartHelper::ValidateAndSelfCheck();

    return true;
}

void Application::InitializePlugins()
{
    if (!m_appCtx || !m_appCtx->pluginManager)
        return;

    LOG_INFO(m_appCtx->logger, L"Application::InitializePlugins: loading plugins after interactive startup");
    m_appCtx->pluginManager->Initialize();
}

void Application::StartUiWatchdog()
{
    if (!m_appCtx || !m_hMainWnd || !m_appCtx->backgroundTasks || m_uiHeartbeat)
        return;

    m_uiHeartbeat = std::make_shared<UiHeartbeatState>();
    m_uiHeartbeat->lastTick = GetTickCount64();
    SetTimer(m_hMainWnd, UI_HEARTBEAT_TIMER_ID, 250, nullptr);
    auto heartbeat = m_uiHeartbeat;
    auto heartbeatLogger = m_appCtx->logger;
    m_uiWatchdogTask = m_appCtx->backgroundTasks->Submit(L"ui.watchdog", BackgroundTaskService::Priority::High,
        [hWnd = m_hMainWnd, heartbeat, heartbeatLogger](const std::shared_ptr<BackgroundTaskService::CancellationToken>& cancellation) {
            ULONGLONG lastWarning = 0;
            bool shouldRecoverHooks = false;
            while (!cancellation->IsCancellationRequested() && !heartbeat->stopping)
            {
                Sleep(250);
                ULONGLONG now = GetTickCount64();
                ULONGLONG last = heartbeat->lastTick.load();
                if (last == 0)
                    continue;

                ULONGLONG elapsed = now - last;
                if (elapsed > 3500)
                {
                    if (lastWarning == 0 || now - lastWarning >= 10000)
                    {
                        LOG_WARNING_NODE(heartbeatLogger, L"ui.health", L"stall", L"elapsed_ms=%llu",
                            static_cast<unsigned long long>(elapsed));
                        CrashReporter::RecordBreadcrumb(L"ui.stall", std::to_wstring(elapsed) + L"ms");
                        lastWarning = now;
                    }
                    shouldRecoverHooks = true;
                }
                else if (shouldRecoverHooks && elapsed <= 250)
                {
                    LOG_INFO(heartbeatLogger, L"ui.health: UI thread unblocked after stall, posting hook recovery message");
                    PostMessageW(hWnd, AppMessages::RestartHook, 0, 0);
                    shouldRecoverHooks = false;
                }
            }
        });
}

bool Application::LoadRuntimeSettings()
{
    if (!m_appCtx || !m_appCtx->configService)
        return false;

    m_appCtx->configService->LoadConfig();
    UIStyle::ApplyAppearanceSettings(m_appCtx->configService->GetAppearanceSettings());
    MouseHook::SetTriggerType(m_appCtx->configService->GetTriggerType());
    MouseHook::SetTriggerBlacklist(m_appCtx->configService->GetTriggerBlacklist());
    UIStyle::SetThemeMode(static_cast<UIStyle::ThemeMode>(m_appCtx->configService->GetTheme()));
    UIStyle::SetThemeColorIndex(m_appCtx->configService->GetThemeColor());
    UIStyle::SetWindowMode(m_appCtx->configService->GetWindowMode());
    if (m_appCtx->configService->HasCustomGlobalScalePercent())
    {
        UIStyle::Scaling::SetGlobalScalePercent(m_appCtx->configService->GetGlobalScalePercent());
    }
    else
    {
        UIStyle::Scaling::SetDefaultGlobalScalePercent(DpiHelper::GetPrimaryDisplayScalePercent());
    }
    UIStyle::Animation::SetEnabled(m_appCtx->configService->GetAnimationEnabled());
    UIStyle::Animation::SetDurationMs((float)m_appCtx->configService->GetAnimationDuration());
    UIStyle::Performance::SetHardwareAccelerationEnabled(m_appCtx->configService->GetHardwareAccelerationEnabled());
    UIStyle::Performance::ApplyProcessPolicy();
    return true;
}

bool Application::InstallHooks()
{
    LOG_INFO(m_appCtx->logger, L"Application::InstallHooks: attempting to install mouse hook");
    if (MouseHook::Install(m_hMainWnd))
    {
        m_mouseHookInstalled = true;
        LOG_INFO(m_appCtx->logger, L"Application::InstallHooks: mouse hook installed successfully");
        return true;
    }

    LOG_ERROR(m_appCtx->logger, L"Application::InstallHooks: failed to install mouse hook!");
    ConfirmWindow::Show(nullptr, L"错误", L"Failed to install mouse hook", nullptr, false);
    return false;
}

int Application::MessageLoop()
{
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

void Application::Shutdown()
{
    if (!m_appCtx && !m_timerResolutionRaised && !m_comInitialized)
        return;

    if (m_appCtx)
        LOG_INFO(m_appCtx->logger, L"WinLauncher shutting down...");

    if (m_uiHeartbeat) m_uiHeartbeat->stopping = true;
    m_uiWatchdogTask.Cancel();
    if (m_hMainWnd) KillTimer(m_hMainWnd, UI_HEARTBEAT_TIMER_ID);

    // Batch workers synchronously request shortcut launches from the main window.
    // Stop them before starting UI teardown so they cannot target a closing window.
    BatchLaunchService::Cancel();
    MacroPlayer::Cancel();

    // Uninstall keyboard hook
    KeyboardHook::ClearDoubleAltTarget();
    KeyboardHook::Uninstall();

    if (m_mouseHookInstalled)
    {
        if (m_appCtx) LOG_INFO(m_appCtx->logger, L"Application::Shutdown: uninstalling mouse hook");
        MouseHook::Uninstall();
        m_mouseHookInstalled = false;
    }

    // Stop new plugin work and UI callbacks before tearing down any windows.
    // Background workers may still finish after cancellation, so the dispatcher
    // must reject their completion callbacks while all UI objects are intact.
    if (m_appCtx && m_appCtx->pluginManager)
        m_appCtx->pluginManager->RequestShutdown();

    if (m_appCtx && m_appCtx->uiDispatcher)
        m_appCtx->uiDispatcher->Shutdown();

    // Begin plugin retirement while the task service is still available. A
    // cooperative plugin such as /dns can finish its cancellation callbacks
    // without blocking the UI; the task service later owns the bounded
    // process-exit fallback for anything that remains in flight.
    if (m_appCtx && m_appCtx->pluginManager)
        m_appCtx->pluginManager->Shutdown();

    // Cancel update work before the task service can enter its bounded
    // shutdown fallback; update callbacks must not target torn-down windows.
    UpdateService::GetInstance().Shutdown();

    if (m_appCtx && m_appCtx->backgroundTasks)
        m_appCtx->backgroundTasks->Shutdown(std::chrono::milliseconds(1500));

    // FolderWatcher can post heap-owned auto-pause requests. Stop and join it
    // before destroying the recipient HWND, then release any request already
    // queued behind the shutdown message.
    if (m_appCtx && m_appCtx->configService)
        m_appCtx->configService->StopFolderWatching();
    if (m_hMainWnd)
    {
        MSG pending{};
        while (PeekMessageW(&pending, m_hMainWnd, AppMessages::FolderSyncAutoPaused,
            AppMessages::FolderSyncAutoPaused, PM_REMOVE))
        {
            delete reinterpret_cast<FolderAutoPauseRequest*>(pending.lParam);
        }
    }

    if (m_hMainWnd && IsWindow(m_hMainWnd))
    {
        LOG_INFO(m_appCtx->logger, L"Application::Shutdown: destroying main window");
        DestroyWindow(m_hMainWnd);
    }
    m_hMainWnd = nullptr;

    if (m_appCtx) LOG_INFO(m_appCtx->logger, L"Application::Shutdown: releasing window singletons");
    if (m_appCtx && m_appCtx->windowCoordinator)
    {
        m_appCtx->windowCoordinator->DestroyWindows();
    }
    else
    {
        PopupWindow::Release();
        ConfigWindow::Release(true);
    }
    TrayMenuWindow::Release();
    ToastWindow::Hide();

    if (m_appCtx)
    {
        if (m_appCtx->usageHistory)
            m_appCtx->usageHistory->Flush();
        if (m_appCtx->configService)
            m_appCtx->configService->FlushPendingConfig();
        if (m_appCtx->logger)
            m_appCtx->logger->Flush();
        m_appCtx->iconService.reset();
        m_appCtx->configService.reset();
        m_appCtx.reset();
    }

    if (m_timerResolutionRaised)
    {
        timeEndPeriod(1);
        m_timerResolutionRaised = false;
    }

    if (m_comInitialized)
    {
        CoUninitialize();
        m_comInitialized = false;
    }

    if (m_hSingleInstanceMutex)
    {
        ReleaseMutex(m_hSingleInstanceMutex);
        CloseHandle(m_hSingleInstanceMutex);
        m_hSingleInstanceMutex = nullptr;
    }
}

void Application::AddTrayIcon()
{
    if (m_appCtx && m_appCtx->configService && m_appCtx->configService->GetHideTrayIcon())
        return;

    NOTIFYICONDATAW nid{};
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = m_hMainWnd;
    nid.uID              = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);

    nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = AppMessages::TrayIcon;
    nid.hIcon            = LoadIconW(m_hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
    wcscpy_s(nid.szTip, L"WinLauncher");
    if (Shell_NotifyIconW(NIM_ADD, &nid))
        m_trayIconAdded = true;
}

void Application::RemoveTrayIcon()
{
    if (!m_trayIconAdded)
        return;

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = m_hMainWnd;
    nid.uID    = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    m_trayIconAdded = false;
}

void Application::UpdateTrayIconState()
{
    bool hide = false;
    if (m_appCtx && m_appCtx->configService)
    {
        hide = m_appCtx->configService->GetHideTrayIcon();
    }

    if (hide)
    {
        RemoveTrayIcon();
    }
    else
    {
        AddTrayIcon();
    }
}

void Application::ShowPopupAtCursor(ULONG_PTR requestGeneration)
{
    if (!MouseHook::AcknowledgePopupRequest(requestGeneration) || m_popupPaused) return;

    POINT pt;
    GetCursorPos(&pt);
    PopupWindow::Show(m_hMainWnd, pt);
}

void Application::ShowConfigWindow()
{
    ConfigWindow::ShowConfig(m_hMainWnd, m_appCtx.get());
}

void Application::ShowSettingsWindow()
{
    ConfigWindow::ShowSettings(m_hMainWnd, m_appCtx.get());
}

void Application::ShowTrayMenuAtCursor()
{
    POINT pt;
    GetCursorPos(&pt);
    TrayMenuWindow::Show(pt);
}

void Application::TogglePopupPause()
{
    m_popupPaused = !m_popupPaused;
    MouseHook::SetTriggerEnabled(!m_popupPaused);
    if (m_popupPaused)
        PopupWindow::Hide();
    TrayMenuWindow::SetPaused(m_popupPaused);

    // 在屏幕中央显示简短 Toast 提示
    const wchar_t* msg = m_popupPaused ? L"弹窗已暂停" : L"弹窗已启用";
    ToastWindow::Show(msg, 500);

    LOG_INFO(m_appCtx->logger, L"Application::TogglePopupPause: paused=%d", (int)m_popupPaused);
}

void Application::RestartHook()
{
    LOG_INFO(m_appCtx->logger, L"Application::RestartHook: restarting mouse hook...");

    // Uninstall then reinstall the mouse hook
    if (m_mouseHookInstalled)
    {
        MouseHook::Uninstall();
        m_mouseHookInstalled = false;
    }

    const bool mouseInstalled = MouseHook::Install(m_hMainWnd);
    if (mouseInstalled)
    {
        m_mouseHookInstalled = true;
        MouseHook::SetTriggerEnabled(!m_popupPaused);
        LOG_INFO(m_appCtx->logger, L"Application::RestartHook: mouse hook restarted successfully");
    }
    else
    {
        LOG_ERROR(m_appCtx->logger, L"Application::RestartHook: failed to reinstall mouse hook");
    }

    // Also restart the keyboard hook
    KeyboardHook::ClearDoubleAltTarget();
    KeyboardHook::Uninstall();
    const bool keyboardInstalled = KeyboardHook::Install();
    if (keyboardInstalled)
        KeyboardHook::SetDoubleAltTarget(m_hMainWnd, 400);

    if (mouseInstalled && keyboardInstalled)
    {
        ToastWindow::Show(L"鼠标与键盘钩子已重启", 1200);
        LOG_INFO(m_appCtx->logger, L"Application::RestartHook: mouse and keyboard hooks restarted");
    }
    else
    {
        ToastWindow::Show(L"钩子重启未完成，请重试或重启 WinLauncher", 2200);
        LOG_ERROR(m_appCtx->logger, L"Application::RestartHook: mouse=%d keyboard=%d", mouseInstalled ? 1 : 0, keyboardInstalled ? 1 : 0);
    }
}

void Application::RestartApp()
{
    LOG_INFO(m_appCtx->logger, L"Application::RestartApp: restarting application...");

    // Schedule relaunch via a timer so any pending messages drain first.
    // Destroy the main window BEFORE ShellExecuteExW so the new instance
    // won't find it via FindWindowW and kill itself as a duplicate.
    SetTimer(m_hMainWnd, AppMessages::RestartAppTimerId, 350, [](HWND hWnd, UINT, UINT_PTR id, DWORD) {
        KillTimer(hWnd, id);

        // Retrieve the Application pointer before destroying the window,
        // because once the window is destroyed, GetWindowLongPtrW will return NULL.
        Application* app = nullptr;
        if (hWnd)
        {
            app = reinterpret_cast<Application*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        }

        // Destroy the hidden main window before launching the replacement.
        // This ensures FindWindowW(L"WinLauncherMain") in the new process
        // returns NULL and the single-instance guard passes.
        DestroyWindow(hWnd);

        // Release and close the single-instance mutex now so the newly spawned
        // process can successfully acquire it on startup.
        if (app && app->m_hSingleInstanceMutex)
        {
            ReleaseMutex(app->m_hSingleInstanceMutex);
            CloseHandle(app->m_hSingleInstanceMutex);
            app->m_hSingleInstanceMutex = nullptr;
        }

        wchar_t exePath[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        SHELLEXECUTEINFOW sei{};
        sei.cbSize = sizeof(sei);
        sei.fMask  = SEE_MASK_NOASYNC;
        sei.lpVerb = L"open";
        sei.lpFile = exePath;
        sei.nShow  = SW_NORMAL;
        if (!ShellExecuteExW(&sei))
        {
            ToastWindow::Show(L"重启失败，WinLauncher 将继续运行", 2200);
            return;
        }
        PostQuitMessage(0);
    });
}

LRESULT Application::HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_TIMER && wParam == UI_HEARTBEAT_TIMER_ID)
    {
        if (m_uiHeartbeat) m_uiHeartbeat->lastTick = GetTickCount64();
        return 0;
    }
    if (msg == AppMessages::UiDispatch)
    {
        if (m_appCtx && m_appCtx->uiDispatcher)
            m_appCtx->uiDispatcher->HandleMessage(lParam);
        return 0;
    }
    if (g_uShellRestart && msg == g_uShellRestart)
    {
        m_trayIconAdded = false;
        UpdateTrayIconState();
        return 0;
    }

    switch (msg)
    {
    case AppMessages::ShowPopup:
        ShowPopupAtCursor(wParam);
        return 0;

    case AppMessages::LaunchShortcutById:
    {
        Logger* logSink = (m_appCtx ? m_appCtx->logger.get() : nullptr);
        std::wstring* pId = reinterpret_cast<std::wstring*>(lParam);
        if (pId)
        {
            if (logSink)
                logSink->Log(Logger::INFO, __FILE__, __LINE__, __FUNCTION__, L"Application::LaunchShortcutById: requested id=%s", pId->c_str());
            auto pages = m_appCtx->configService->LoadConfig();
            Model::ShortcutInfo foundSc;
            bool found = false;
            for (const auto& page : pages)
            {
                for (const auto& sc : page.shortcuts)
                {
                    if (sc.id == *pId)
                    {
                        foundSc = sc;
                        found = true;
                        break;
                    }
                }
                if (found) break;
            }
            if (found)
            {
                RendShortcutInfo renderInfo;
                renderInfo.id = foundSc.id;
                renderInfo.name = foundSc.name;
                renderInfo.targetPath = foundSc.targetPath;
                renderInfo.arguments = foundSc.arguments;
                renderInfo.iconPath = foundSc.iconPath;
                renderInfo.runAsAdmin = foundSc.runAsAdmin;
                renderInfo.type = foundSc.type;
                renderInfo.targetKind = foundSc.targetKind;
                renderInfo.iconSource = foundSc.iconSource;
                renderInfo.builtinIconId = foundSc.builtinIconId;
                renderInfo.iconInvertLight = foundSc.iconInvertLight;
                renderInfo.iconInvertDark = foundSc.iconInvertDark;

                bool ok = PopupWindow::ExecuteShortcut(renderInfo, hWnd, m_appCtx.get());
                if (logSink)
                    logSink->Log(
                        Logger::INFO,
                        __FILE__,
                        __LINE__,
                        __FUNCTION__,
                        L"Application::LaunchShortcutById: executed id=%s name=%s type=%d result=%d",
                        foundSc.id.c_str(),
                        foundSc.name.c_str(),
                        static_cast<int>(foundSc.type),
                        ok ? 1 : 0);
                return ok ? 1 : 0;
            }
            if (logSink)
                logSink->Log(Logger::WORNING, __FILE__, __LINE__, __FUNCTION__, L"Application::LaunchShortcutById: id not found: %s", pId->c_str());
        }
        else
        {
            if (logSink)
                logSink->Log(Logger::WORNING, __FILE__, __LINE__, __FUNCTION__, L"Application::LaunchShortcutById: null id pointer.");
        }
        return 0;
    }

    case AppMessages::ShowConfigWindow:
        ShowConfigWindow();
        return 0;

    case AppMessages::ShowSettingsWindow:
        ShowSettingsWindow();
        return 0;

    case AppMessages::ShowPluginsWindow:
        ShowConfigWindow(); // Opens config with focus on plugins
        return 0;

    case AppMessages::InitializePlugins:
        InitializePlugins();
        return 0;

    case AppMessages::FolderSyncAutoPaused:
    {
        std::unique_ptr<FolderAutoPauseRequest> request(
            reinterpret_cast<FolderAutoPauseRequest*>(lParam));
        if (!request || !m_appCtx || !m_appCtx->configService)
            return 0;

        if (m_appCtx->configService->PauseSyncFolder(request->folderPath, request->errorCode))
        {
            if (m_appCtx->eventBus)
                m_appCtx->eventBus->Publish(EventType::ConfigChanged);
            UpdateTrayIconState();
            ToastWindow::Show(L"同步目录不可用，已自动暂停", 2200);
            LOG_G_WARNING_NODE(L"storage.folder_watcher", L"folder_auto_pause_applied",
                L"error=%lu generation=%llu", request->errorCode,
                static_cast<unsigned long long>(request->generation));
        }
        return 0;
    }

    case AppMessages::ConfigChanged:
        if (m_appCtx && m_appCtx->eventBus)
            m_appCtx->eventBus->Publish(EventType::ConfigChanged);
        UpdateTrayIconState();
        return 0;

    case AppMessages::TogglePopupPause:
        TogglePopupPause();
        return 0;

    case AppMessages::RestartHook:
        RestartHook();
        return 0;

    case AppMessages::RestartApp:
        RestartApp();
        return 0;

    case AppMessages::DoubleAltPressed:
        TogglePopupPause();
        return 0;

    case AppMessages::UpdateCheckCompleted:
    case AppMessages::UpdateDownloadProgress:
    {
        HWND hConfig = ConfigWindow::GetHWNDStatic();
        if (hConfig)
        {
            InvalidateRect(hConfig, nullptr, FALSE);
        }
        return 0;
    }

    case AppMessages::UpdateDownloadCompleted:
    {
        HWND hConfig = ConfigWindow::GetHWNDStatic();
        auto& updater = UpdateService::GetInstance();
        std::wstring prompt = L"更新下载已完成。是否立即重启并应用更新？";
        if (ConfirmWindow::Show(hConfig, L"更新已准备就绪", prompt.c_str(), m_appCtx.get(), true))
        {
            updater.ApplyUpdate(m_appCtx.get());
        }
        if (hConfig)
        {
            InvalidateRect(hConfig, nullptr, FALSE);
        }
        return 0;
    }

    case AppMessages::TrayIcon:
        if (lParam == WM_RBUTTONUP)
        {
            PostMessageW(hWnd, AppMessages::ShowTrayMenu, 0, 0);
        }
        else if (lParam == WM_LBUTTONDBLCLK || lParam == WM_LBUTTONUP)
        {
            ShowConfigWindow();
        }
        return 0;

    case AppMessages::ShowTrayMenu:
        ShowTrayMenuAtCursor();
        return 0;

    case AppMessages::PrewarmTrayMenu:
        TrayMenuWindow::Prewarm();
        return 0;

    case WM_DESTROY:
        RemoveTrayIcon();
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

LRESULT CALLBACK Application::WindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    Application* app = nullptr;
    if (msg == WM_NCCREATE)
    {
        auto createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        app = reinterpret_cast<Application*>(createStruct->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    else
    {
        app = reinterpret_cast<Application*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    }

    if (app)
        return app->HandleMessage(hWnd, msg, wParam, lParam);

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
