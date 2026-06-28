#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <taskschd.h>
#include <comdef.h>
#include <tlhelp32.h>
#include <wtsapi32.h>
#include <string>
#include <vector>
#include "App/Logger.h"

#pragma comment(lib, "taskschd.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "wtsapi32.lib")

namespace AutoStartHelper {

const wchar_t TASK_NAME[] = L"WinLauncherAutoStart";

// Check if current process is elevated
inline bool IsCurrentProcessElevated() {
    HANDLE hToken = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        return false;
    }
    TOKEN_ELEVATION elevation;
    DWORD size = sizeof(elevation);
    bool elevated = false;
    if (GetTokenInformation(hToken, TokenElevation, &elevation, size, &size)) {
        elevated = elevation.TokenIsElevated != 0;
    }
    CloseHandle(hToken);
    return elevated;
}

// Check if user belongs to Administrators group
inline bool IsCurrentUserAdminAccount() {
    if (IsUserAnAdmin()) return true;

    HANDLE hToken = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        return false;
    }
    TOKEN_ELEVATION_TYPE type;
    DWORD size = sizeof(type);
    bool isAdmin = false;
    if (GetTokenInformation(hToken, TokenElevationType, &type, size, &size)) {
        if (type == TokenElevationTypeLimited || type == TokenElevationTypeFull) {
            isAdmin = true;
        }
    }
    CloseHandle(hToken);
    return isAdmin;
}

// Helper to check if string is empty
inline bool IsEmpty(const wchar_t* s) {
    return !s || !*s;
}

// Initialize Task Scheduler COM
inline HRESULT InitTaskService(ITaskService** ppService) {
    HRESULT hr = CoCreateInstance(
        CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER,
        IID_ITaskService, (void**)ppService);
    if (FAILED(hr)) return hr;
    hr = (*ppService)->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());
    return hr;
}

// Check if Task exists
inline bool TaskExists() {
    HRESULT hrCom = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    bool needsUninit = (hrCom == S_OK || hrCom == S_FALSE);

    ITaskService* pService = NULL;
    HRESULT hr = InitTaskService(&pService);
    if (FAILED(hr) || !pService) {
        if (needsUninit) CoUninitialize();
        return false;
    }

    ITaskFolder* pFolder = NULL;
    IRegisteredTask* pTask = NULL;
    hr = pService->GetFolder(_bstr_t(L"\\"), &pFolder);
    bool exists = false;
    if (SUCCEEDED(hr) && pFolder) {
        hr = pFolder->GetTask(_bstr_t(TASK_NAME), &pTask);
        exists = SUCCEEDED(hr) && pTask;
        if (pTask) pTask->Release();
        pFolder->Release();
    }
    pService->Release();
    if (needsUninit) CoUninitialize();
    return exists;
}

// Enable Privilege
inline bool EnablePrivilege(const wchar_t* privilegeName) {
    HANDLE hToken;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        return false;
    }
    LUID luid;
    if (!LookupPrivilegeValueW(nullptr, privilegeName, &luid)) {
        CloseHandle(hToken);
        return false;
    }
    TOKEN_PRIVILEGES tp;
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (!AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), nullptr, nullptr)) {
        CloseHandle(hToken);
        return false;
    }
    bool result = GetLastError() == ERROR_SUCCESS;
    CloseHandle(hToken);
    return result;
}

// Get Explorer Token
inline bool GetExplorerToken(HANDLE* phToken) {
    HWND hShell = GetShellWindow();
    DWORD pid = 0;
    if (hShell) {
        GetWindowThreadProcessId(hShell, &pid);
    }

    if (!pid) {
        DWORD activeSessionId = WTSGetActiveConsoleSessionId();
        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnapshot != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe;
            pe.dwSize = sizeof(pe);
            if (Process32FirstW(hSnapshot, &pe)) {
                do {
                    if (_wcsicmp(pe.szExeFile, L"explorer.exe") == 0) {
                        DWORD sessionId = 0;
                        if (ProcessIdToSessionId(pe.th32ProcessID, &sessionId)) {
                            if (sessionId == activeSessionId) {
                                pid = pe.th32ProcessID;
                                break;
                            }
                        }
                        if (pid == 0) {
                            pid = pe.th32ProcessID;
                        }
                    }
                } while (Process32NextW(hSnapshot, &pe));
            }
            CloseHandle(hSnapshot);
        }
    }

    if (!pid) return false;

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProcess) return false;

    HANDLE hQueryToken = NULL;
    OpenProcessToken(hProcess, TOKEN_QUERY, &hQueryToken);
    if (hQueryToken) {
        TOKEN_ELEVATION elevation;
        DWORD size = sizeof(elevation);
        if (GetTokenInformation(hQueryToken, TokenElevation, &elevation, size, &size)) {
            if (elevation.TokenIsElevated) {
                CloseHandle(hQueryToken);
                CloseHandle(hProcess);
                return false;  // Explorer is elevated, refuse
            }
        }
        CloseHandle(hQueryToken);
    }

    HANDLE hToken = NULL;
    DWORD access = TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY;
    if (!OpenProcessToken(hProcess, access, &hToken)) {
        access = TOKEN_QUERY | TOKEN_DUPLICATE;
        if (!OpenProcessToken(hProcess, access, &hToken)) {
            CloseHandle(hProcess);
            return false;
        }
    }

    HANDLE hDupToken = NULL;
    if (!DuplicateTokenEx(hToken, MAXIMUM_ALLOWED, NULL,
                          SecurityImpersonation, TokenPrimary, &hDupToken)) {
        CloseHandle(hToken);
        CloseHandle(hProcess);
        return false;
    }

    CloseHandle(hToken);
    CloseHandle(hProcess);
    *phToken = hDupToken;
    return true;
}

// Launch target with standard user token
inline bool LaunchWithToken(HANDLE hToken, const wchar_t* exePath,
                            const wchar_t* args, const wchar_t* cwd) {
    std::wstring cmdLine = L"\"" + std::wstring(exePath) + L"\"";
    if (!IsEmpty(args)) cmdLine += L" " + std::wstring(args);

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOWDEFAULT;
    PROCESS_INFORMATION pi = {};

    BOOL ok = CreateProcessWithTokenW(
        hToken, 0, NULL, (LPWSTR)cmdLine.c_str(),
        CREATE_UNICODE_ENVIRONMENT,
        NULL, IsEmpty(cwd) ? nullptr : cwd, &si, &pi);

    if (ok) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    return ok != 0;
}

// Run Launcher mode (loops until explorer token is available, then launches unelevated)
inline int RunLauncher(const wchar_t* exePath, const wchar_t* arguments, const wchar_t* workingDir) {
    if (IsEmpty(exePath)) return 3;

    // Enable impersonate privilege so we can call CreateProcessWithTokenW
    EnablePrivilege(L"SeImpersonatePrivilege");

    ULONGLONG start = GetTickCount64();
    const DWORD POLL_TIMEOUT_MS = 20000;
    const DWORD POLL_INTERVAL_MS = 250;

    while (GetTickCount64() - start < POLL_TIMEOUT_MS) {
        HANDLE hToken = NULL;
        if (GetExplorerToken(&hToken)) {
            bool ok = LaunchWithToken(hToken, exePath, arguments, workingDir);
            CloseHandle(hToken);
            if (ok) return 0;
        }
        Sleep(POLL_INTERVAL_MS);
    }
    return 1;
}

// Direct task registration
inline int EnableTaskDirect(const wchar_t* exePath, const wchar_t* arguments, const wchar_t* workingDir) {
    HRESULT hrCom = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    bool needsUninit = (hrCom == S_OK || hrCom == S_FALSE);

    ITaskService* pService = NULL;
    HRESULT hr = InitTaskService(&pService);
    if (FAILED(hr) || !pService) {
        if (needsUninit) CoUninitialize();
        return 2;
    }

    ITaskDefinition* pTaskDef = NULL;
    hr = pService->NewTask(0, &pTaskDef);
    if (FAILED(hr)) {
        pService->Release();
        if (needsUninit) CoUninitialize();
        return 2;
    }

    // Trigger: logon
    ITriggerCollection* pTriggers = NULL;
    pTaskDef->get_Triggers(&pTriggers);
    ITrigger* pTrigger = NULL;
    pTriggers->Create(TASK_TRIGGER_LOGON, &pTrigger);
    ILogonTrigger* pLogonTrigger = NULL;
    pTrigger->QueryInterface(IID_ILogonTrigger, (void**)&pLogonTrigger);
    if (pLogonTrigger) {
        pLogonTrigger->put_Delay(_bstr_t(L"PT2S"));
        pLogonTrigger->Release();
    }
    pTrigger->Release();
    pTriggers->Release();

    // Action: execute
    IActionCollection* pActions = NULL;
    pTaskDef->get_Actions(&pActions);
    IAction* pAction = NULL;
    pActions->Create(TASK_ACTION_EXEC, &pAction);
    IExecAction* pExecAction = NULL;
    pAction->QueryInterface(IID_IExecAction, (void**)&pExecAction);
    if (pExecAction) {
        pExecAction->put_Path(_bstr_t(exePath));
        if (!IsEmpty(arguments)) pExecAction->put_Arguments(_bstr_t(arguments));
        if (!IsEmpty(workingDir)) pExecAction->put_WorkingDirectory(_bstr_t(workingDir));
        pExecAction->Release();
    }
    pAction->Release();
    pActions->Release();

    // Settings
    ITaskSettings* pSettings = NULL;
    pTaskDef->get_Settings(&pSettings);
    if (pSettings) {
        pSettings->put_StartWhenAvailable(VARIANT_TRUE);
        pSettings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
        pSettings->put_StopIfGoingOnBatteries(VARIANT_FALSE);
        pSettings->put_ExecutionTimeLimit(_bstr_t(L"PT0S"));
        pSettings->put_Priority(4);
        pSettings->Release();
    }

    // Principal
    IPrincipal* pPrincipal = NULL;
    pTaskDef->get_Principal(&pPrincipal);
    if (pPrincipal) {
        pPrincipal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN);
        pPrincipal->put_RunLevel(TASK_RUNLEVEL_LUA);
        pPrincipal->Release();
    }

    // Register with retry
    ITaskFolder* pRootFolder = NULL;
    pService->GetFolder(_bstr_t(L"\\"), &pRootFolder);
    IRegisteredTask* pRegTask = NULL;
    hr = E_FAIL;
    for (int attempt = 0; attempt < 2; attempt++) {
        hr = pRootFolder->RegisterTaskDefinition(
            _bstr_t(TASK_NAME), pTaskDef,
            TASK_CREATE_OR_UPDATE,
            _variant_t(), _variant_t(),
            TASK_LOGON_INTERACTIVE_TOKEN,
            _variant_t(),
            &pRegTask);
        if (SUCCEEDED(hr)) break;
        if (attempt == 0) { if (pRegTask) { pRegTask->Release(); pRegTask = NULL; } Sleep(500); }
    }

    if (pRegTask) pRegTask->Release();
    pRootFolder->Release();
    pTaskDef->Release();
    pService->Release();
    if (needsUninit) CoUninitialize();

    return SUCCEEDED(hr) ? 0 : 2;
}

// Disable/delete task scheduler task directly
inline int DisableTaskDirect() {
    HRESULT hrCom = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    bool needsUninit = (hrCom == S_OK || hrCom == S_FALSE);

    ITaskService* pService = NULL;
    HRESULT hr = InitTaskService(&pService);
    if (FAILED(hr)) {
        if (needsUninit) CoUninitialize();
        return 2;
    }

    ITaskFolder* pFolder = NULL;
    hr = pService->GetFolder(_bstr_t(L"\\"), &pFolder);
    if (FAILED(hr)) {
        pService->Release();
        if (needsUninit) CoUninitialize();
        return 2;
    }

    HRESULT delHr = S_OK;
    IRegisteredTask* pTask = NULL;
    hr = pFolder->GetTask(_bstr_t(TASK_NAME), &pTask);
    if (SUCCEEDED(hr) && pTask) {
        delHr = pFolder->DeleteTask(_bstr_t(TASK_NAME), 0);
        pTask->Release();
    }

    pFolder->Release();
    pService->Release();
    if (needsUninit) CoUninitialize();

    return SUCCEEDED(delHr) ? 0 : 2;
}

// Clean registry Run key if it exists
inline void CleanupRegistryRunValue() {
    HKEY hKey = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE | KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS) {
        RegDeleteValueW(hKey, L"WinLauncher");
        RegCloseKey(hKey);
    }
}

// Public interface: Enable autostart
inline bool SetEnabled(bool enable) {
    LOG_G_INFO(L"AutoStartHelper::SetEnabled(%s) called", enable ? L"true" : L"false");

    // Make sure registry run key is cleaned up
    CleanupRegistryRunValue();

    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    bool isCurrentAccountAdmin = IsCurrentUserAdminAccount();
    LOG_G_INFO(L"AutoStartHelper::SetEnabled: exePath=%s, isAdminAccount=%s", exePath, isCurrentAccountAdmin ? L"true" : L"false");

    if (enable) {
        if (isCurrentAccountAdmin) {
            LOG_G_INFO(L"AutoStartHelper::SetEnabled: attempting elevated task creation");
            std::wstring cmdLine = L"/c start \"\" /b \"" +
                std::wstring(exePath) + L"\" --autostart-helper enable --target-exe \"" +
                std::wstring(exePath) + L"\"";

            SHELLEXECUTEINFOW sei = { sizeof(sei) };
            sei.lpVerb = L"runas";
            sei.lpFile = L"cmd.exe";
            sei.lpParameters = cmdLine.c_str();
            sei.nShow = SW_HIDE;
            sei.fMask = SEE_MASK_NOCLOSEPROCESS;

            if (ShellExecuteExW(&sei)) {
                if (sei.hProcess) {
                    WaitForSingleObject(sei.hProcess, 30000);
                    CloseHandle(sei.hProcess);
                }
                LOG_G_INFO(L"AutoStartHelper::SetEnabled: elevated task creation succeeded");
                return true;
            }
            LOG_G_ERRA(L"AutoStartHelper::SetEnabled: ShellExecuteExW runas cmd.exe failed (error=%d)", GetLastError());
            return false;
        } else {
            LOG_G_INFO(L"AutoStartHelper::SetEnabled: standard user path direct task creation");
            int res = EnableTaskDirect(exePath, L"", L"");
            if (res == 0) {
                LOG_G_INFO(L"AutoStartHelper::SetEnabled: Direct task creation succeeded");
                return true;
            } else {
                LOG_G_ERRA(L"AutoStartHelper::SetEnabled: Direct task creation failed (res=%d)", res);
                return false;
            }
        }
    } else {
        if (isCurrentAccountAdmin) {
            LOG_G_INFO(L"AutoStartHelper::SetEnabled: attempting elevated task deletion");
            wchar_t sysDir[MAX_PATH];
            GetSystemDirectoryW(sysDir, MAX_PATH);
            std::wstring cmdPath = std::wstring(sysDir) + L"\\cmd.exe";
            std::wstring cmdLine = L"/c schtasks /Delete /TN \"" + std::wstring(TASK_NAME) + L"\" /F";

            SHELLEXECUTEINFOW sei = { sizeof(sei) };
            sei.lpVerb = L"runas";
            sei.lpFile = cmdPath.c_str();
            sei.lpParameters = cmdLine.c_str();
            sei.nShow = SW_HIDE;
            sei.fMask = SEE_MASK_NOCLOSEPROCESS;

            if (ShellExecuteExW(&sei)) {
                if (sei.hProcess) {
                    WaitForSingleObject(sei.hProcess, 15000);
                    CloseHandle(sei.hProcess);
                }
                LOG_G_INFO(L"AutoStartHelper::SetEnabled: elevated task deletion succeeded");
                return true;
            }
            LOG_G_ERRA(L"AutoStartHelper::SetEnabled: ShellExecuteExW runas schtasks failed (error=%d)", GetLastError());
            return false;
        } else {
            LOG_G_INFO(L"AutoStartHelper::SetEnabled: standard user path direct task deletion");
            int res = DisableTaskDirect();
            if (res == 0) {
                LOG_G_INFO(L"AutoStartHelper::SetEnabled: Direct task deletion succeeded");
                return true;
            } else {
                LOG_G_ERRA(L"AutoStartHelper::SetEnabled: Direct task deletion failed (res=%d)", res);
                return false;
            }
        }
    }
}

// Command-line handling
inline bool HandleCommandLine() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return false;

    bool handled = false;
    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];
        if (arg == L"--autostart-helper" && i + 1 < argc) {
            std::wstring action = argv[i + 1];
            std::wstring targetExe;
            std::wstring targetArgs;
            std::wstring targetCwd;

            for (int j = i + 2; j < argc; ++j) {
                if (std::wstring(argv[j]) == L"--target-exe" && j + 1 < argc) {
                    targetExe = argv[j + 1];
                    j++;
                } else if (std::wstring(argv[j]) == L"--target-args" && j + 1 < argc) {
                    targetArgs = argv[j + 1];
                    j++;
                } else if (std::wstring(argv[j]) == L"--target-cwd" && j + 1 < argc) {
                    targetCwd = argv[j + 1];
                    j++;
                }
            }

            if (action == L"enable") {
                // Determine what action to create
                // Since the helper is executed elevated under an Admin account, the task it creates:
                // If it registers targetExe directly, it might run elevated under UAC.
                // Instead, we register task action to launch ourselves with --autostart-launch !
                wchar_t currentExe[MAX_PATH];
                GetModuleFileNameW(nullptr, currentExe, MAX_PATH);

                std::wstring taskArgs = L"--autostart-launch --target-exe \"" + targetExe + L"\"";
                if (!targetArgs.empty()) {
                    taskArgs += L" --target-args \"" + targetArgs + L"\"";
                }
                if (!targetCwd.empty()) {
                    taskArgs += L" --target-cwd \"" + targetCwd + L"\"";
                }

                // Register task scheduler to run currentExe with taskArgs
                EnableTaskDirect(currentExe, taskArgs.c_str(), L"");
            } else if (action == L"disable") {
                DisableTaskDirect();
            }
            handled = true;
            break;
        } else if (arg == L"--autostart-launch") {
            std::wstring targetExe;
            std::wstring targetArgs;
            std::wstring targetCwd;

            for (int j = i + 1; j < argc; ++j) {
                if (std::wstring(argv[j]) == L"--target-exe" && j + 1 < argc) {
                    targetExe = argv[j + 1];
                    j++;
                } else if (std::wstring(argv[j]) == L"--target-args" && j + 1 < argc) {
                    targetArgs = argv[j + 1];
                    j++;
                } else if (std::wstring(argv[j]) == L"--target-cwd" && j + 1 < argc) {
                    targetCwd = argv[j + 1];
                    j++;
                }
            }

            if (!targetExe.empty()) {
                bool launched = false;
                if (!IsCurrentProcessElevated()) {
                    // Standard user: launch target directly using ShellExecuteW
                    HINSTANCE hInst = ShellExecuteW(nullptr, L"open", targetExe.c_str(), 
                                 targetArgs.empty() ? nullptr : targetArgs.c_str(), 
                                 targetCwd.empty() ? nullptr : targetCwd.c_str(), 
                                 SW_SHOWNORMAL);
                    launched = ((INT_PTR)hInst > 32);
                } else {
                    // Elevated process: run launcher to find explorer token and launch unelevated
                    int rc = RunLauncher(targetExe.c_str(), targetArgs.c_str(), targetCwd.c_str());
                    if (rc == 0) {
                        launched = true;
                    } else {
                        // Fallback: if de-elevation fails (e.g. UAC is completely disabled), launch directly
                        HINSTANCE hInst = ShellExecuteW(nullptr, L"open", targetExe.c_str(), 
                                     targetArgs.empty() ? nullptr : targetArgs.c_str(), 
                                     targetCwd.empty() ? nullptr : targetCwd.c_str(), 
                                     SW_SHOWNORMAL);
                        launched = ((INT_PTR)hInst > 32);
                    }
                }
            }
            handled = true;
            break;
        }
    }

    LocalFree(argv);
    return handled;
}

// Helper to compare paths (case-insensitive and ignores surrounding quotes)
inline bool ComparePaths(const std::wstring& path1, const std::wstring& path2) {
    std::wstring p1 = path1;
    std::wstring p2 = path2;
    // Trim quotes
    if (!p1.empty() && p1.front() == L'"') p1.erase(0, 1);
    if (!p1.empty() && p1.back() == L'"') p1.pop_back();
    if (!p2.empty() && p2.front() == L'"') p2.erase(0, 1);
    if (!p2.empty() && p2.back() == L'"') p2.pop_back();
    // Trim trailing backslashes/spaces
    while(!p1.empty() && (p1.back() == L' ' || p1.back() == L'\\')) p1.pop_back();
    // Trim leading spaces
    while(!p1.empty() && p1.front() == L' ') p1.erase(0, 1);
    while(!p2.empty() && (p2.back() == L' ' || p2.back() == L'\\')) p2.pop_back();
    while(!p2.empty() && p2.front() == L' ') p2.erase(0, 1);
    return (_wcsicmp(p1.c_str(), p2.c_str()) == 0);
}

// Perform validation and self-check of the autostart task
inline bool ValidateAndSelfCheck() {
    LOG_G_INFO(L"AutoStartHelper::ValidateAndSelfCheck called");
    // Ensure registry run key is cleaned up
    CleanupRegistryRunValue();

    HRESULT hrCom = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    bool needsUninit = (hrCom == S_OK || hrCom == S_FALSE);

    ITaskService* pService = NULL;
    HRESULT hr = InitTaskService(&pService);
    if (FAILED(hr) || !pService) {
        LOG_G_ERRA(L"AutoStartHelper::ValidateAndSelfCheck: InitTaskService failed (HRESULT=0x%08X)", hr);
        if (needsUninit) CoUninitialize();
        return false;
    }

    ITaskFolder* pFolder = NULL;
    IRegisteredTask* pTask = NULL;
    hr = pService->GetFolder(_bstr_t(L"\\"), &pFolder);
    bool isValid = true;
    bool exists = false;
    if (SUCCEEDED(hr) && pFolder) {
        hr = pFolder->GetTask(_bstr_t(TASK_NAME), &pTask);
        if (SUCCEEDED(hr) && pTask) {
            exists = true;
            ITaskDefinition* pDefinition = NULL;
            hr = pTask->get_Definition(&pDefinition);
            if (SUCCEEDED(hr) && pDefinition) {
                IActionCollection* pActions = NULL;
                hr = pDefinition->get_Actions(&pActions);
                if (SUCCEEDED(hr) && pActions) {
                    long count = 0;
                    pActions->get_Count(&count);
                    if (count > 0) {
                        IAction* pAction = NULL;
                        hr = pActions->get_Item(1, &pAction);
                        if (SUCCEEDED(hr) && pAction) {
                            IExecAction* pExecAction = NULL;
                            hr = pAction->QueryInterface(IID_IExecAction, (void**)&pExecAction);
                            if (SUCCEEDED(hr) && pExecAction) {
                                BSTR bstrPath = NULL;
                                BSTR bstrArgs = NULL;
                                pExecAction->get_Path(&bstrPath);
                                pExecAction->get_Arguments(&bstrArgs);

                                std::wstring taskPath = bstrPath ? bstrPath : L"";
                                std::wstring taskArgs = bstrArgs ? bstrArgs : L"";

                                if (bstrPath) SysFreeString(bstrPath);
                                if (bstrArgs) SysFreeString(bstrArgs);

                                wchar_t currentExe[MAX_PATH];
                                GetModuleFileNameW(nullptr, currentExe, MAX_PATH);

                                // Check path match
                                bool pathMatch = ComparePaths(taskPath, currentExe);
                                bool argsMatch = true;

                                if (!taskArgs.empty()) {
                                    size_t pos = taskArgs.find(L"--target-exe");
                                    if (pos != std::wstring::npos) {
                                        std::wstring targetPart = taskArgs.substr(pos + 12);
                                        while (!targetPart.empty() && targetPart.front() == L' ') targetPart.erase(0, 1);
                                        if (!targetPart.empty() && targetPart.front() == L'"') {
                                            size_t endQuote = targetPart.find(L'"', 1);
                                            if (endQuote != std::wstring::npos) {
                                                targetPart = targetPart.substr(1, endQuote - 1);
                                            }
                                        }
                                        if (!ComparePaths(targetPart, currentExe)) {
                                            argsMatch = false;
                                        }
                                    } else {
                                        argsMatch = false;
                                    }
                                }

                                if (!pathMatch || !argsMatch) {
                                    LOG_G_WORNING(L"AutoStartHelper::ValidateAndSelfCheck: task target mismatch! PathMatch=%d, ArgsMatch=%d", pathMatch, argsMatch);
                                    isValid = false;
                                }

                                pExecAction->Release();
                            } else {
                                isValid = false;
                            }
                            pAction->Release();
                        } else {
                            isValid = false;
                        }
                    } else {
                        isValid = false;
                    }
                    pActions->Release();
                } else {
                    isValid = false;
                }
                pDefinition->Release();
            } else {
                isValid = false;
            }
            pTask->Release();
        }
        pFolder->Release();
    }
    pService->Release();
    if (needsUninit) CoUninitialize();

    if (exists && !isValid) {
        LOG_G_WORNING(L"AutoStartHelper::ValidateAndSelfCheck: autostart task exists but is invalid, recreating...");
        // Mismatch detected! Try to delete directly first.
        if (DisableTaskDirect() != 0) {
            // Fallback to UAC-enabled delete if direct delete fails.
            SetEnabled(false);
        }
        return false;
    }

    if (exists) {
        LOG_G_INFO(L"AutoStartHelper::ValidateAndSelfCheck: task is valid");
    } else {
        LOG_G_INFO(L"AutoStartHelper::ValidateAndSelfCheck: task does not exist");
    }
    return true;
}

} // namespace AutoStartHelper

