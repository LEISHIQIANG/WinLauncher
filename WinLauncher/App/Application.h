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
    void UpdateTrayIconState();
    void ShowPopupAtCursor();
    void ShowConfigWindow();
    void ShowSettingsWindow();
    void ShowTrayMenuAtCursor();
    void TogglePopupPause();     // 暂停/启用弹窗
    void RestartHook();          // 重启鼠标+键盘钩子
    void RestartApp();           // 重启整个应用进程

    LRESULT HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    static LRESULT CALLBACK WindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HINSTANCE m_hInstance = nullptr;
    HWND m_hMainWnd = nullptr;
    std::shared_ptr<AppContext> m_appCtx;
    bool m_comInitialized = false;
    bool m_timerResolutionRaised = false;
    bool m_trayIconAdded = false;
    bool m_mouseHookInstalled = false;
    bool m_popupPaused = false;  // 当前弹窗暂停状态
};
