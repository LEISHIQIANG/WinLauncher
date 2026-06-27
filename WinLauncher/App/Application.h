#pragma once
#include "AppContext.h"
#include <Windows.h>
#include <memory>

class Application
{
public:
    explicit Application(HINSTANCE hInstance);
    ~Application();

    int Run();

private:
    bool InitializeProcess();
    bool HandleHelperCommandLine();
    bool CreateMainWindow();
    bool InitializeServices();
    bool LoadRuntimeSettings();
    bool InstallHooks();
    int MessageLoop();
    void Shutdown();

    void AddTrayIcon();
    void RemoveTrayIcon();
    void ShowPopupAtCursor();
    void ShowConfigWindow();
    void ShowSettingsWindow();
    void ShowTrayMenuAtCursor();
    LRESULT HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    static LRESULT CALLBACK WindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HINSTANCE m_hInstance = nullptr;
    HWND m_hMainWnd = nullptr;
    std::shared_ptr<AppContext> m_appCtx;
    bool m_comInitialized = false;
    bool m_timerResolutionRaised = false;
    bool m_trayIconAdded = false;
    bool m_mouseHookInstalled = false;
};
