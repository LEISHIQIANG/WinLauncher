#define NOMINMAX
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "PopupShortcutLauncher.h"

#include "../App/AppContext.h"
#include "../App/AppMessages.h"
#include "../Config/CommandPanelWindow.h"
#include "../Config/UIStyle.h"
#include "../Services/BatchLaunchService.h"
#include "../Services/CommandVariableService.h"
#include "../Services/MacroService.h"
#include "../Services/PrivilegeLaunchService.h"
#include "PopupTimeZoneAction.h"
#include <map>
#include <string>
#include <vector>
#include <windows.h>
#include <shellapi.h>

bool PopupShortcutLauncher::HasLaunchAction(const RendShortcutInfo& shortcut)
{
    switch (shortcut.type)
    {
    case Model::ShortcutType::Macro:
    case Model::ShortcutType::Batch:
        return !shortcut.arguments.empty();
    default:
        return !shortcut.targetPath.empty();
    }
}

// ShellExecute and some file associations may synchronously contact Explorer,
// a network location, or an already-running application. They must never run
// on the popup's UI thread: a slow target should delay only its own launch,
// not freeze the popup, tray, or UI heartbeat.
bool PopupShortcutLauncher::IsBackgroundExternalLaunch(const RendShortcutInfo& shortcut)
{
    if (shortcut.type == Model::ShortcutType::File ||
        shortcut.type == Model::ShortcutType::BuiltinIcon)
    {
        return true;
    }

    return shortcut.type == Model::ShortcutType::System &&
        !shortcut.targetPath.empty() &&
        shortcut.targetPath.front() != L':';
}

static bool LaunchExternalShortcutImmediately(
    const RendShortcutInfo& shortcut,
    const std::vector<std::wstring>& selectedFiles)
{
    std::wstring arguments = shortcut.arguments;
    for (const auto& file : selectedFiles)
    {
        if (!arguments.empty()) arguments += L" ";
        arguments += L"\"" + file + L"\"";
    }

    const ULONGLONG started = GetTickCount64();
    const bool launched = PrivilegeLaunchService::Launch(shortcut.targetPath, arguments, shortcut.runAsAdmin);
    const ULONGLONG elapsed = GetTickCount64() - started;
    if (!launched)
    {
        LOG_G_ERRA(L"PopupWindow::LaunchExternalShortcutImmediately: failed to launch shortcut %s (Target=%s)",
            shortcut.name.c_str(), shortcut.targetPath.c_str());
    }
    else if (elapsed >= 250)
    {
        LOG_G_WORNING(L"PopupWindow::LaunchExternalShortcutImmediately: dispatch was slower than expected shortcut=%s elapsed_ms=%llu",
            shortcut.name.c_str(), static_cast<unsigned long long>(elapsed));
    }
    return launched;
}

static WORD ParseVirtualKey(const std::wstring& name)
{
    if (name.length() == 1)
    {
        wchar_t ch = name[0];
        if (ch >= L'A' && ch <= L'Z') return ch;
        if (ch >= L'0' && ch <= L'9') return ch;
    }
    if (name.rfind(L"F", 0) == 0 && name.length() > 1)
    {
        try {
            int num = std::stoi(name.substr(1));
            if (num >= 1 && num <= 12) return VK_F1 + (num - 1);
        } catch (...) {}
    }
    if (name == L"Space") return VK_SPACE;
    if (name == L"Enter") return VK_RETURN;
    if (name == L"Tab") return VK_TAB;
    if (name == L"Esc") return VK_ESCAPE;
    if (name == L"Backspace") return VK_BACK;
    if (name == L"Insert") return VK_INSERT;
    if (name == L"Delete") return VK_DELETE;
    if (name == L"Home") return VK_HOME;
    if (name == L"End") return VK_END;
    if (name == L"PageUp") return VK_PRIOR;
    if (name == L"PageDown") return VK_NEXT;
    if (name == L"Up") return VK_UP;
    if (name == L"Down") return VK_DOWN;
    if (name == L"Left") return VK_LEFT;
    if (name == L"Right") return VK_RIGHT;
    if (name == L"Ctrl") return VK_CONTROL;
    if (name == L"Shift") return VK_SHIFT;
    if (name == L"Alt") return VK_MENU;
    if (name == L"Win") return VK_LWIN;
    return 0;
}

static void SimulateHotkey(const std::wstring& hotkeyStr, bool afterClose, AppContext* ctx)
{
    auto tasks = ctx ? ctx->backgroundTasks : nullptr;
    if (!tasks) return;
    tasks->Submit(L"hotkey.simulate", BackgroundTaskService::Priority::High,
        [hotkeyStr, afterClose](const std::shared_ptr<BackgroundTaskService::CancellationToken>& cancellation) {
        if (afterClose)
        {
            if (UIStyle::Animation::IsEnabled())
            {
                Sleep((DWORD)(150 + UIStyle::Animation::GetDurationMs()));
            }
            else
            {
                Sleep(150);
            }
        }

        std::vector<WORD> keys;
        size_t pos = 0;
        std::wstring s = hotkeyStr;
        while ((pos = s.find(L"+")) != std::wstring::npos)
        {
            std::wstring part = s.substr(0, pos);
            while (!part.empty() && part.front() == L' ') part.erase(0, 1);
            while (!part.empty() && part.back() == L' ') part.pop_back();

            WORD vk = ParseVirtualKey(part);
            if (vk) keys.push_back(vk);

            s.erase(0, pos + 1);
        }
        while (!s.empty() && s.front() == L' ') s.erase(0, 1);
        while (!s.empty() && s.back() == L' ') s.pop_back();
        WORD vk = ParseVirtualKey(s);
        if (vk) keys.push_back(vk);

        if (keys.empty() || cancellation->IsCancellationRequested()) return;

        for (WORD k : keys)
        {
            keybd_event(static_cast<BYTE>(k), 0, 0, 0);
        }
        for (auto it = keys.rbegin(); it != keys.rend(); ++it)
        {
            keybd_event(static_cast<BYTE>(*it), 0, KEYEVENTF_KEYUP, 0);
        }
    });
}

static std::wstring ExpandVariables(const std::wstring& inputStr, HWND parent, AppContext* ctx, bool& cancelled,
    const std::function<std::vector<std::wstring>()>& peekLiveSelection)
{
    cancelled = false;

    std::vector<std::wstring> files = peekLiveSelection ? peekLiveSelection() : std::vector<std::wstring>{};

    std::map<std::wstring, std::wstring> inputValues;
    if (!Services::CommandVariableService::ResolveInputs(parent, inputStr, inputValues))
    {
        cancelled = true;
        return L"";
    }

    return Services::CommandVariableService::ResolveVariables(inputStr, L"cmd", files, inputValues);
}

static bool LaunchUrl(const RendShortcutInfo& sc, HWND parent, AppContext* ctx, const PopupShortcutLauncher::LaunchContext& launch)
{
    bool cancelled = false;
    std::wstring url = ExpandVariables(sc.targetPath, parent, ctx, cancelled, launch.peekLiveSelection);
    if (cancelled) return false;

    std::wstring browserPath, browserArgs;
    size_t sep = sc.arguments.find(L"|||");
    if (sep != std::wstring::npos)
    {
        browserPath = sc.arguments.substr(0, sep);
        browserArgs = sc.arguments.substr(sep + 3);
    }
    else
    {
        browserPath = sc.arguments;
    }

    if (browserPath.empty())
    {
        HINSTANCE hInst = ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return (reinterpret_cast<INT_PTR>(hInst) > 32);
    }
    else
    {
        std::wstring args = ExpandVariables(browserArgs, parent, ctx, cancelled, launch.peekLiveSelection);
        if (cancelled) return false;

        size_t urlPos = args.find(L"{{url}}");
        if (urlPos != std::wstring::npos)
        {
            args.replace(urlPos, 7, url);
        }
        else
        {
            if (!args.empty()) args += L" ";
            args += url;
        }

        return PrivilegeLaunchService::Launch(browserPath, args, false);
    }
}

static bool LaunchCommand(const RendShortcutInfo& sc, HWND parent, AppContext* ctx, const std::vector<std::wstring>& selectedFiles)
{
    std::vector<std::wstring> segments;
    std::wstring s = sc.arguments;
    size_t pos = 0;
    while ((pos = s.find(L"|||")) != std::wstring::npos)
    {
        segments.push_back(s.substr(0, pos));
        s.erase(0, pos + 3);
    }
    segments.push_back(s);

    std::wstring type = L"cmd";
    bool showWindow = false;
    bool captureOutput = false;
    int timeoutSeconds = 300;
    int maxChars = 50000;

    if (segments.size() > 0) type = segments[0];
    if (segments.size() > 2) showWindow = (segments[2] == L"1");
    if (segments.size() > 3) captureOutput = (segments[3] == L"1");
    if (captureOutput) showWindow = false;
    if (segments.size() > 4)
    {
        try
        {
            const int configuredTimeout = std::stoi(segments[4]);
            if (configuredTimeout >= 1 && configuredTimeout <= 3600)
                timeoutSeconds = configuredTimeout;
            else
                LOG_G_WORNING(L"ExecuteCommand: invalid timeout=%d; using default 300 seconds", configuredTimeout);
        }
        catch (...)
        {
            LOG_G_WORNING(L"ExecuteCommand: invalid timeout text; using default 300 seconds");
        }
    }
    if (segments.size() > 5) { try { int v = std::stoi(segments[5]); if (v > 2000) maxChars = v; } catch(...) {} }

    std::map<std::wstring, std::wstring> inputValues;
    if (!Services::CommandVariableService::ResolveInputs(parent, sc.targetPath, inputValues, ctx ? ctx->userInteraction.get() : nullptr))
    {
        return false;
    }

    std::wstring resolvedCmd = Services::CommandVariableService::ResolveVariables(sc.targetPath, type, selectedFiles, inputValues);

    if (ctx && ctx->userInteraction)
    {
        if (!ctx->userInteraction->ConfirmHighRiskCommand(parent, resolvedCmd, sc.name))
        {
            return false;
        }
    }

    auto commandExec = ctx ? ctx->commandExecution : nullptr;

    if (captureOutput)
    {
        maxChars = 0;
        std::wstring panelTitle = L"命令输出 - " + sc.name;
        std::wstring initialText;
        CommandPanelWindow::ShowLive(parent, panelTitle.c_str(), initialText.c_str(),
            [commandExec, type, resolvedCmd, timeoutSeconds, maxChars](HWND panelHwnd) {
                auto append = [panelHwnd](const std::wstring& text) {
                    CommandPanelWindow::PostAppend(panelHwnd, text);
                };

                if (commandExec)
                {
                    CommandExecutionRequest request;
                    request.type = type;
                    request.commandText = resolvedCmd;
                    request.timeoutSeconds = timeoutSeconds;
                    request.maxChars = maxChars;
                    commandExec->ExecuteStreaming(request, append);
                }
                else
                {
                    append(L"\r\n未找到命令执行服务。\r\n状态: 失败\r\n");
                }
            },
            ctx);
        return true;
    }

    std::wstring output;
    bool ok = false;

    if (commandExec)
    {
        CommandExecutionRequest request;
        request.type = type;
        request.commandText = resolvedCmd;
        request.showWindow = showWindow;
        request.captureOutput = captureOutput;
        request.timeoutSeconds = timeoutSeconds;
        request.maxChars = maxChars;

        CommandExecutionResult result;
        ok = commandExec->Execute(request, result);
        output = result.output;
    }
    else
    {
        output = L"未找到命令执行服务。";
        ok = false;
    }

    if (captureOutput && (ok || !output.empty()))
    {
        std::wstring panelTitle = L"命令输出 - " + sc.name;
        CommandPanelWindow::Show(parent, panelTitle.c_str(), output.c_str(), ctx);
    }

    return ok;
}

void PopupShortcutLauncher::LaunchFromPopup(const RendShortcutInfo& sc, HWND popupHwnd, AppContext* ctx,
    const std::vector<std::wstring>& consumedFiles, bool selectionConsumed, const LaunchContext& launch)
{
    const bool isVirtualSystemAction =
        sc.type == Model::ShortcutType::System &&
        !sc.targetPath.empty() &&
        sc.targetPath.front() == L':';

    const bool acceptsSelectedFiles =
        !isVirtualSystemAction &&
        (sc.type == Model::ShortcutType::File || sc.type == Model::ShortcutType::System) &&
        (sc.targetKind == Model::ShortcutTargetKind::Exe ||
         sc.targetKind == Model::ShortcutTargetKind::File ||
         sc.targetKind == Model::ShortcutTargetKind::Link ||
         sc.targetKind == Model::ShortcutTargetKind::Unknown);

    if (IsBackgroundExternalLaunch(sc))
    {
        const std::vector<std::wstring> launchFiles = (selectionConsumed && acceptsSelectedFiles)
            ? consumedFiles
            : std::vector<std::wstring>{};
        LOG_G_INFO(L"PopupWindow::LaunchShortcut: dispatching immediate external launch shortcut=%s target=%s", sc.name.c_str(), sc.targetPath.c_str());
        if (!LaunchExternalShortcutImmediately(sc, launchFiles))
        {
            LOG_G_ERRA(L"PopupWindow::LaunchShortcut: failed to dispatch shortcut %s", sc.name.c_str());
        }
    }
    else
    {
        Execute(sc, popupHwnd, ctx, launch);
    }
}

bool PopupShortcutLauncher::Execute(const RendShortcutInfo& sc, HWND parent, AppContext* ctx, const LaunchContext& launch)
{
    if (sc.type == Model::ShortcutType::Hotkey)
    {
        bool afterClose = sc.runAsAdmin;
        SimulateHotkey(sc.targetPath, afterClose, ctx);
        return true;
    }
    else if (sc.type == Model::ShortcutType::Url)
    {
        return LaunchUrl(sc, parent, ctx, launch);
    }
    else if (sc.type == Model::ShortcutType::Command)
    {
        return LaunchCommand(sc, parent, ctx, launch.selectedFiles);
    }
    else if (sc.type == Model::ShortcutType::System)
    {
        if (sc.targetPath == L":config_window")
        {
            PostMessageW(ctx->hMainWnd, AppMessages::ShowConfigWindow, 0, 0);
            return true;
        }
        else if (sc.targetPath == L":topmost_toggle")
        {
            HWND targetWnd = GetForegroundWindow();
            if (targetWnd)
            {
                LONG_PTR exStyle = GetWindowLongPtrW(targetWnd, GWL_EXSTYLE);
                if (exStyle & WS_EX_TOPMOST)
                {
                    SetWindowPos(targetWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                }
                else
                {
                    SetWindowPos(targetWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                }
                return true;
            }
            return false;
        }
        else if (sc.targetPath == L":timezone_cn_la_toggle")
        {
            return PopupTimeZoneAction::ToggleChinaLosAngelesAsync(ctx);
        }
        else
        {
            return PrivilegeLaunchService::Launch(sc.targetPath, sc.arguments, sc.runAsAdmin);
        }
    }
    else if (sc.type == Model::ShortcutType::Macro)
    {
        double speed = 1.0;
        std::wstring triggerMode = L"immediate";
        std::vector<MacroEvent> events;
        if (MacroHelper::Parse(sc.arguments, speed, triggerMode, events))
        {
            const bool waitForClose = launch.popupWillHide || triggerMode == L"after_close";
            return MacroPlayer::Play(events, speed, waitForClose ? L"after_close" : triggerMode, parent,
                waitForClose ? launch.restoreForegroundWnd : nullptr);
        }
        return false;
    }
    else if (sc.type == Model::ShortcutType::Batch)
    {
        return BatchLaunchService::Execute(sc.arguments, parent, ctx);
    }
    else
    {
        return PrivilegeLaunchService::Launch(sc.targetPath, sc.arguments, sc.runAsAdmin);
    }
}
