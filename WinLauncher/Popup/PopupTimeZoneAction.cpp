#include "PopupTimeZoneAction.h"

#include "../App/AppContext.h"
#include <atomic>
#include <string>
#include <vector>

static bool TimeZoneKeyEquals(const wchar_t* left, const wchar_t* right)
{
    return left && right && _wcsicmp(left, right) == 0;
}

static std::wstring GetCurrentTimeZoneKey()
{
    DYNAMIC_TIME_ZONE_INFORMATION info{};
    if (GetDynamicTimeZoneInformation(&info) == TIME_ZONE_ID_INVALID)
        return L"";
    return info.TimeZoneKeyName;
}

static std::wstring GetSystemToolPath(const wchar_t* fileName)
{
    wchar_t systemDir[MAX_PATH]{};
    if (GetSystemDirectoryW(systemDir, MAX_PATH) == 0)
        return fileName ? fileName : L"";

    std::wstring path = systemDir;
    path += L"\\";
    path += fileName;
    return path;
}

static bool RunHiddenProcessAndWait(const std::wstring& exePath, const std::wstring& arguments, DWORD timeoutMs)
{
    std::wstring commandLine = L"\"" + exePath + L"\"";
    if (!arguments.empty())
        commandLine += L" " + arguments;

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');

    if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return false;

    DWORD waitResult = WaitForSingleObject(pi.hProcess, timeoutMs);
    DWORD exitCode = 1;
    bool ok = false;
    if (waitResult == WAIT_OBJECT_0 && GetExitCodeProcess(pi.hProcess, &exitCode))
    {
        ok = (exitCode == 0);
    }
    else if (waitResult == WAIT_TIMEOUT)
    {
        TerminateProcess(pi.hProcess, 1);
        SetLastError(WAIT_TIMEOUT);
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (!ok && waitResult == WAIT_OBJECT_0)
        SetLastError(exitCode);
    return ok;
}

static bool SetTimeZoneByKey(const wchar_t* keyName)
{
    if (!keyName || !*keyName)
        return false;

    std::wstring tzutilPath = GetSystemToolPath(L"tzutil.exe");
    std::wstring arguments = L"/s \"";
    arguments += keyName;
    arguments += L"\"";

    if (!RunHiddenProcessAndWait(tzutilPath, arguments, 5000))
        return false;

    SendMessageTimeoutW(HWND_BROADCAST, WM_TIMECHANGE, 0, 0, SMTO_ABORTIFHUNG, 200, nullptr);
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"TimeZoneInformation"), SMTO_ABORTIFHUNG, 200, nullptr);
    return true;
}

static bool ToggleChinaLosAngelesTimeZone(HWND parent)
{
    static const wchar_t* kChinaTimeZone = L"China Standard Time";
    static const wchar_t* kLosAngelesTimeZone = L"Pacific Standard Time";

    std::wstring currentKey = GetCurrentTimeZoneKey();
    const wchar_t* targetKey = TimeZoneKeyEquals(currentKey.c_str(), kChinaTimeZone)
        ? kLosAngelesTimeZone
        : kChinaTimeZone;

    if (SetTimeZoneByKey(targetKey))
    {
        LOG_G_INFO(L"ToggleChinaLosAngelesTimeZone: switched from %s to %s", currentKey.c_str(), targetKey);
        return true;
    }

    DWORD err = GetLastError();
    LOG_G_ERRA(L"ToggleChinaLosAngelesTimeZone: failed, current=%s target=%s error=%lu",
               currentKey.c_str(), targetKey, err);
    MessageBoxW(parent,
                L"切换时区失败。请确认当前用户具有更改时区权限。",
                L"WinLauncher",
                MB_OK | MB_ICONWARNING);
    return false;
}

bool PopupTimeZoneAction::ToggleChinaLosAngelesAsync(AppContext* ctx)
{
    static std::atomic_bool s_running{ false };
    if (s_running.exchange(true))
    {
        LOG_G_WORNING(L"LaunchChinaLosAngelesTimeZoneToggleAsync: toggle already running");
        return true;
    }

    auto tasks = ctx ? ctx->backgroundTasks : nullptr;
    auto handle = tasks ? tasks->Submit(L"timezone.toggle", BackgroundTaskService::Priority::High,
        [](const std::shared_ptr<BackgroundTaskService::CancellationToken>& cancellation) {
            if (!cancellation->IsCancellationRequested()) ToggleChinaLosAngelesTimeZone(nullptr);
            s_running.store(false);
        }) : BackgroundTaskService::TaskHandle{};
    if (!handle) s_running.store(false);
    return static_cast<bool>(handle);
}
