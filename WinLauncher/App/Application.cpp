#include "Application.h"
#include "AppMessages.h"
#include "PluginHost.h"
#include "../AutoStartHelper.h"
#include "../Config/ConfigWindow.h"
#include "../Config/ConfirmWindow.h"
#include "../Config/UIStyle.h"
#include "../DpiHelper.h"
#include "../KeyboardHook.h"
#include "../MouseHook.h"
#include "../PopupWindow.h"
#include "../ToastWindow.h"
#include "../resource.h"
#include "../Services/EnvironmentDetector.h"
#include "../Services/IniConfigRepository.h"
#include "../Services/SystemIconService.h"
#include "../TrayMenuWindow.h"
#include "../Services/BatchLaunchService.h"
#include <CommCtrl.h>
#include <ole2.h>
#include <shellapi.h>
#include <timeapi.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "winmm.lib")

static UINT g_uShellRestart = 0;

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

    if (HandleHelperCommandLine())
        return 0;

    // Check if an instance is already running
    HWND hExisting = FindWindowW(L"WinLauncherMain", nullptr);
    if (hExisting)
    {
        PostMessageW(hExisting, AppMessages::ShowConfigWindow, 0, 0);
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

    if (!InitializeServices())
    {
        LOG_ERROR(m_appCtx->logger, L"Application::Run: InitializeServices failed!");
        return 1;
    }

    // Start background environment detection (executors like python, git bash)
    EnvironmentDetector::StartDetection();

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

    // Install keyboard hook and register double-Alt shortcut for TogglePopupPause
    KeyboardHook::Install();
    KeyboardHook::SetDoubleAltTarget(m_hMainWnd, 400);

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
    auto configRepository = std::make_unique<IniConfigRepository>(
        m_appCtx->logger.get(),
        m_hMainWnd,
        AppMessages::ConfigChanged);

    m_appCtx->configService = std::move(configRepository);
    m_appCtx->iconService   = std::make_unique<SystemIconService>();

    LOG_INFO(m_appCtx->logger, L"WinLauncher starting...");

    // Validate autostart configuration and self-check
    AutoStartHelper::ValidateAndSelfCheck();

    return true;
}

bool Application::LoadRuntimeSettings()
{
    if (!m_appCtx || !m_appCtx->configService)
        return false;

    m_appCtx->configService->LoadConfig();
    UIStyle::ApplyAppearanceSettings(m_appCtx->configService->GetAppearanceSettings());
    MouseHook::SetTriggerType(m_appCtx->configService->GetTriggerType());
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

    // Uninstall keyboard hook
    KeyboardHook::ClearDoubleAltTarget();
    KeyboardHook::Uninstall();

    if (m_mouseHookInstalled)
    {
        LOG_INFO(m_appCtx->logger, L"Application::Shutdown: uninstalling mouse hook");
        MouseHook::Uninstall();
        m_mouseHookInstalled = false;
    }

    if (m_hMainWnd && IsWindow(m_hMainWnd))
    {
        LOG_INFO(m_appCtx->logger, L"Application::Shutdown: destroying main window");
        DestroyWindow(m_hMainWnd);
    }
    m_hMainWnd = nullptr;

    LOG_INFO(m_appCtx->logger, L"Application::Shutdown: releasing window singletons");
    PopupWindow::Release();
    ConfigWindow::Release();
    TrayMenuWindow::Release();
    ToastWindow::Hide();

    if (m_appCtx)
    {
        if (m_appCtx->pluginHost)
            m_appCtx->pluginHost->UnloadAll();

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

void Application::ShowPopupAtCursor()
{
    if (m_popupPaused) return;   // 暂停状态下忽略弹窗请求

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

    if (MouseHook::Install(m_hMainWnd))
    {
        m_mouseHookInstalled = true;
        LOG_INFO(m_appCtx->logger, L"Application::RestartHook: mouse hook restarted successfully");
        ToastWindow::Show(L"钩子已重启", 500);
    }
    else
    {
        LOG_ERROR(m_appCtx->logger, L"Application::RestartHook: failed to reinstall mouse hook");
    }

    // Also restart the keyboard hook
    KeyboardHook::ClearDoubleAltTarget();
    KeyboardHook::Uninstall();
    KeyboardHook::Install();
    KeyboardHook::SetDoubleAltTarget(m_hMainWnd, 400);
    LOG_INFO(m_appCtx->logger, L"Application::RestartHook: keyboard hook restarted");
}

void Application::RestartApp()
{
    LOG_INFO(m_appCtx->logger, L"Application::RestartApp: restarting application...");

    // Get current executable path
    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    // Use ShellExecute to relaunch; delay slightly so Toast is visible
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask  = SEE_MASK_NOASYNC;
    sei.lpVerb = L"open";
    sei.lpFile = exePath;
    sei.nShow  = SW_NORMAL;

    // Schedule relaunch via a timer (300ms) so the toast can show briefly
    // We post WM_QUIT after the timer fires
    SetTimer(m_hMainWnd, 0xDEAD, 350, [](HWND hWnd, UINT, UINT_PTR id, DWORD) {
        KillTimer(hWnd, id);
        wchar_t exePath2[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exePath2, MAX_PATH);
        SHELLEXECUTEINFOW sei2{};
        sei2.cbSize = sizeof(sei2);
        sei2.fMask  = SEE_MASK_NOASYNC;
        sei2.lpVerb = L"open";
        sei2.lpFile = exePath2;
        sei2.nShow  = SW_NORMAL;
        ShellExecuteExW(&sei2);
        PostQuitMessage(0);
    });
}

LRESULT Application::HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (g_uShellRestart && msg == g_uShellRestart)
    {
        m_trayIconAdded = false;
        UpdateTrayIconState();
        return 0;
    }

    switch (msg)
    {
    case AppMessages::ShowPopup:
        ShowPopupAtCursor();
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

    case AppMessages::TrayIcon:
        if (lParam == WM_RBUTTONUP)
        {
            ShowTrayMenuAtCursor();
        }
        else if (lParam == WM_LBUTTONDBLCLK || lParam == WM_LBUTTONUP)
        {
            ShowConfigWindow();
        }
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
