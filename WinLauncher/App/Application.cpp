#include "Application.h"
#include "AppMessages.h"
#include "PluginHost.h"
#include "../AutoStartHelper.h"
#include "../Config/ConfigWindow.h"
#include "../Config/UIStyle.h"
#include "../MouseHook.h"
#include "../PopupWindow.h"
#include "../resource.h"
#include "../Services/IniConfigRepository.h"
#include "../Services/SystemIconService.h"
#include "../TrayMenuWindow.h"
#include <CommCtrl.h>
#include <ole2.h>
#include <shellapi.h>
#include <timeapi.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(linker, "\"/manifestdependency:type='Win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

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

    m_appCtx = std::make_shared<AppContext>();
    m_appCtx->hInstance = m_hInstance;

    if (!CreateMainWindow())
        return 1;

    if (!InitializeServices())
        return 1;

    AddTrayIcon();

    if (!LoadRuntimeSettings())
        return 1;

    if (!InstallHooks())
        return 1;

    PopupWindow::Init(m_appCtx.get());
    TrayMenuWindow::Init(m_hMainWnd, m_appCtx.get());

    int exitCode = MessageLoop();
    Shutdown();
    return exitCode;
}

bool Application::InitializeProcess()
{
    timeBeginPeriod(1);
    m_timerResolutionRaised = true;

    auto user32 = GetModuleHandleW(L"user32.dll");
    if (user32)
    {
        auto fn = reinterpret_cast<BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT)>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (fn)
        {
            fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
        else
        {
            SetProcessDPIAware();
        }
    }
    else
    {
        SetProcessDPIAware();
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
    wc.lpfnWndProc = Application::WindowProc;
    wc.hInstance = m_hInstance;
    wc.hIcon = LoadIconW(m_hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
    wc.lpszClassName = className;

    RegisterClassW(&wc);

    m_hMainWnd = CreateWindowExW(
        0,
        className,
        L"WinLauncher",
        WS_POPUP,
        0,
        0,
        0,
        0,
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
    m_appCtx->iconService = std::make_unique<SystemIconService>();

    LOG_INFO(m_appCtx->logger, L"WinLauncher starting...");
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
    return true;
}

bool Application::InstallHooks()
{
    if (MouseHook::Install(m_hMainWnd))
    {
        m_mouseHookInstalled = true;
        return true;
    }

    MessageBoxW(nullptr, L"Failed to install mouse hook", L"WinLauncher", MB_ICONERROR);
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

    if (m_mouseHookInstalled)
    {
        MouseHook::Uninstall();
        m_mouseHookInstalled = false;
    }

    if (m_hMainWnd && IsWindow(m_hMainWnd))
        DestroyWindow(m_hMainWnd);
    m_hMainWnd = nullptr;

    PopupWindow::Release();
    ConfigWindow::Release();
    TrayMenuWindow::Release();

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
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = m_hMainWnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = AppMessages::TrayIcon;
    nid.hIcon = LoadIconW(m_hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
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
    nid.hWnd = m_hMainWnd;
    nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    m_trayIconAdded = false;
}

void Application::ShowPopupAtCursor()
{
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

LRESULT Application::HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case AppMessages::ShowPopup:
        ShowPopupAtCursor();
        return 0;

    case AppMessages::ShowConfigWindow:
        ShowConfigWindow();
        return 0;

    case AppMessages::ShowSettingsWindow:
        ShowSettingsWindow();
        return 0;

    case AppMessages::ConfigChanged:
        if (m_appCtx && m_appCtx->eventBus)
            m_appCtx->eventBus->Publish(EventType::ConfigChanged);
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
