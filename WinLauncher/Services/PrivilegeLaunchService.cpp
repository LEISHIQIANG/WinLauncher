#include "PrivilegeLaunchService.h"
#include "../App/Logger.h"
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <exdisp.h>
#include <comdef.h>

bool PrivilegeLaunchService::IsCurrentProcessElevated()
{
    HANDLE hToken = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
    {
        TOKEN_ELEVATION elevation;
        DWORD cbSize = sizeof(TOKEN_ELEVATION);
        if (GetTokenInformation(hToken, TokenElevation, &elevation, cbSize, &cbSize))
        {
            CloseHandle(hToken);
            return elevation.TokenIsElevated != 0;
        }
        CloseHandle(hToken);
    }
    return false;
}

bool PrivilegeLaunchService::Launch(const std::wstring& targetPath, const std::wstring& arguments, bool runAsAdmin, const std::wstring& workingDir)
{
    bool isElevated = IsCurrentProcessElevated();
    LPCWSTR args = arguments.empty() ? nullptr : arguments.c_str();
    LPCWSTR dir = workingDir.empty() ? nullptr : workingDir.c_str();

    LOG_G_INFO(L"PrivilegeLaunchService::Launch: target=%s args=%s runAsAdmin=%d isElevated=%d", 
               targetPath.c_str(), arguments.c_str(), runAsAdmin, isElevated);

    if (runAsAdmin)
    {
        // Target wants elevation
        if (isElevated)
        {
            // Already elevated, just launch normally
            HINSTANCE hInst = ShellExecuteW(nullptr, L"open", targetPath.c_str(), args, dir, SW_SHOWNORMAL);
            if ((INT_PTR)hInst <= 32)
            {
                LOG_G_ERRA(L"PrivilegeLaunchService::Launch (Elevated+Open) failed: %d", (int)(INT_PTR)hInst);
                return false;
            }
            return true;
        }
        else
        {
            // Current is normal, target wants elevation: use "runas" to trigger UAC
            HINSTANCE hInst = ShellExecuteW(nullptr, L"runas", targetPath.c_str(), args, dir, SW_SHOWNORMAL);
            if ((INT_PTR)hInst <= 32)
            {
                LOG_G_ERRA(L"PrivilegeLaunchService::Launch (Normal+Runas) failed: %d", (int)(INT_PTR)hInst);
                return false;
            }
            return true;
        }
    }
    else
    {
        // Target wants normal privileges (standard user)
        if (isElevated)
        {
            // Current is elevated, target wants de-elevation!
            // Try method 1: Token duplication via Explorer process
            LOG_G_INFO(L"PrivilegeLaunchService::Launch (Elevated+Normal): attempting token duplication de-elevation");
            if (LaunchDeElevatedViaToken(targetPath, arguments, workingDir))
            {
                LOG_G_INFO(L"PrivilegeLaunchService::Launch: token duplication de-elevation succeeded");
                return true;
            }

            // Try method 2: COM delegation via Explorer Desktop
            LOG_G_INFO(L"PrivilegeLaunchService::Launch (Elevated+Normal): attempting Explorer COM de-elevation");
            if (LaunchDeElevatedViaCOM(targetPath, arguments, workingDir))
            {
                LOG_G_INFO(L"PrivilegeLaunchService::Launch: Explorer COM de-elevation succeeded");
                return true;
            }

            // Both failed, fallback to open with current elevated token as safety net
            LOG_G_WORNING(L"PrivilegeLaunchService::Launch: de-elevation failed, falling back to open");
            HINSTANCE hInst = ShellExecuteW(nullptr, L"open", targetPath.c_str(), args, dir, SW_SHOWNORMAL);
            if ((INT_PTR)hInst <= 32)
            {
                LOG_G_ERRA(L"PrivilegeLaunchService::Launch (Fallback open) failed: %d", (int)(INT_PTR)hInst);
                return false;
            }
            return true;
        }
        else
        {
            // Current is normal, target wants normal: just run standard open
            HINSTANCE hInst = ShellExecuteW(nullptr, L"open", targetPath.c_str(), args, dir, SW_SHOWNORMAL);
            if ((INT_PTR)hInst <= 32)
            {
                LOG_G_ERRA(L"PrivilegeLaunchService::Launch (Normal+Open) failed: %d", (int)(INT_PTR)hInst);
                return false;
            }
            return true;
        }
    }
}

std::string PrivilegeLaunchService::ToUtf8(const std::wstring& wstr)
{
    if (wstr.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string result(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &result[0], len, nullptr, nullptr);
    return result;
}

bool PrivilegeLaunchService::LaunchWithStdin(
    const std::wstring& targetPath,
    const std::wstring& stdinContent,
    const std::wstring& arguments,
    bool runAsAdmin,
    const std::wstring& workingDir)
{
    LOG_G_INFO(L"PrivilegeLaunchService::LaunchWithStdin: target=%s args=%s stdinLen=%zu runAsAdmin=%d",
               targetPath.c_str(), arguments.c_str(), stdinContent.size(), runAsAdmin);

    // Rule: stdin piping does not support elevation. ShellExecute "runas" cannot inherit pipe handles.
    // If the user set runAsAdmin=true, refuse with a clear log entry.
    if (runAsAdmin)
    {
        LOG_G_ERRA(L"PrivilegeLaunchService::LaunchWithStdin: stdin pipe is incompatible with runAsAdmin (requires UAC prompt, which cannot inherit pipe handles). "
                    L"Remove the 'run as admin' option for this shortcut, or switch to CMD type.");
        return false;
    }

    if (stdinContent.empty())
    {
        LOG_G_ERRA(L"PrivilegeLaunchService::LaunchWithStdin: empty stdin content");
        return false;
    }

    // Convert script content to UTF-8 for the pipe
    std::string utf8Content = ToUtf8(stdinContent);
    if (utf8Content.empty() && !stdinContent.empty())
    {
        LOG_G_ERRA(L"PrivilegeLaunchService::LaunchWithStdin: UTF-8 conversion failed");
        return false;
    }

    // Create anonymous pipe — read handle inheritable, write handle NOT
    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    HANDLE hStdinRead = nullptr;
    HANDLE hStdinWrite = nullptr;
    if (!CreatePipe(&hStdinRead, &hStdinWrite, &sa, 0))
    {
        DWORD err = GetLastError();
        LOG_G_ERRA(L"PrivilegeLaunchService::LaunchWithStdin: CreatePipe failed, error=%lu", err);
        return false;
    }

    // Prevent the child from inheriting the write handle (only needs read)
    if (!SetHandleInformation(hStdinWrite, HANDLE_FLAG_INHERIT, 0))
    {
        DWORD err = GetLastError();
        LOG_G_WORNING(L"PrivilegeLaunchService::LaunchWithStdin: SetHandleInformation(write) failed, error=%lu", err);
        // Non-fatal — parent still holds the handle, child just won't inherit it
    }

    // Build command line
    std::wstring cmdLine = L"\"" + targetPath + L"\"";
    if (!arguments.empty())
        cmdLine += L" " + arguments;

    LOG_G_INFO(L"PrivilegeLaunchService::LaunchWithStdin: cmdLine=%s", cmdLine.c_str());

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = hStdinRead;
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION pi = {};
    BOOL ok = FALSE;
    LPCWSTR dir = workingDir.empty() ? nullptr : workingDir.c_str();

    bool isElevated = IsCurrentProcessElevated();

    if (isElevated)
    {
        // Current process is elevated, target wants normal privileges.
        // Only token-based de-elevation supports handle inheritance.
        // COM-based (ShellExecute) cannot inherit pipe handles — we skip the COM fallback entirely.
        LOG_G_INFO(L"PrivilegeLaunchService::LaunchWithStdin: attempting token de-elevation with stdin");
        if (LaunchDeElevatedViaTokenWithStdin(targetPath, arguments, workingDir, hStdinRead))
        {
            LOG_G_INFO(L"PrivilegeLaunchService::LaunchWithStdin: token de-elevation with stdin succeeded");
            ok = TRUE;
            pi.hProcess = nullptr; // Already cleaned up inside the helper
            pi.hThread = nullptr;
        }
        else
        {
            LOG_G_ERRA(L"PrivilegeLaunchService::LaunchWithStdin: token de-elevation failed (no stdin-capable fallback available)");
            CloseHandle(hStdinRead);
            CloseHandle(hStdinWrite);
            return false;
        }
    }
    else
    {
        // Both launcher and target are normal privilege — straightforward CreateProcessW
        LOG_G_INFO(L"PrivilegeLaunchService::LaunchWithStdin: CreateProcessW (normal→normal)");
        ok = CreateProcessW(
            nullptr,
            const_cast<LPWSTR>(cmdLine.c_str()),
            nullptr, nullptr,
            TRUE,                   // bInheritHandles=TRUE — required for stdin pipe
            CREATE_UNICODE_ENVIRONMENT,
            nullptr,
            dir,
            &si,
            &pi
        );

        if (!ok)
        {
            DWORD err = GetLastError();
            LOG_G_ERRA(L"PrivilegeLaunchService::LaunchWithStdin: CreateProcessW failed, error=%lu", err);
        }
    }

    // Close our copy of the read handle — child has its own inherited copy
    CloseHandle(hStdinRead);
    hStdinRead = nullptr;

    if (!ok)
    {
        CloseHandle(hStdinWrite);
        return false;
    }

    // Close thread handle immediately (we don't need it)
    if (pi.hThread)
    {
        CloseHandle(pi.hThread);
        pi.hThread = nullptr;
    }

    // Write script content into the pipe (synchronous, blocks until written)
    DWORD written = 0;
    DWORD totalToWrite = (DWORD)utf8Content.size();
    if (!WriteFile(hStdinWrite, utf8Content.data(), totalToWrite, &written, nullptr))
    {
        DWORD err = GetLastError();
        LOG_G_ERRA(L"PrivilegeLaunchService::LaunchWithStdin: WriteFile failed, error=%lu (wrote %lu/%lu bytes)", err, written, totalToWrite);
        // Still close the write handle to unblock the child
    }
    else if (written != totalToWrite)
    {
        LOG_G_WORNING(L"PrivilegeLaunchService::LaunchWithStdin: partial write %lu/%lu bytes", written, totalToWrite);
    }
    else
    {
        LOG_G_INFO(L"PrivilegeLaunchService::LaunchWithStdin: wrote %lu UTF-8 bytes to stdin pipe", written);
    }

    // Close write handle → sends EOF to child's stdin
    CloseHandle(hStdinWrite);

    // Close process handle (fire-and-forget — we don't wait for exit)
    if (pi.hProcess)
    {
        CloseHandle(pi.hProcess);
        pi.hProcess = nullptr;
    }

    LOG_G_INFO(L"PrivilegeLaunchService::LaunchWithStdin: launched successfully");
    return true;
}

bool PrivilegeLaunchService::LaunchDeElevatedViaTokenWithStdin(
    const std::wstring& targetPath,
    const std::wstring& arguments,
    const std::wstring& workingDir,
    HANDLE hStdinRead)
{
    // Duplicate the explorer token — same logic as LaunchDeElevatedViaToken
    HWND hShell = GetShellWindow();
    if (!hShell)
    {
        LOG_G_ERRA(L"PrivilegeLaunchService::LaunchDeElevatedViaTokenWithStdin: GetShellWindow failed");
        return false;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(hShell, &pid);
    if (!pid)
    {
        LOG_G_ERRA(L"PrivilegeLaunchService::LaunchDeElevatedViaTokenWithStdin: GetWindowThreadProcessId failed");
        return false;
    }

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProcess)
    {
        LOG_G_ERRA(L"PrivilegeLaunchService::LaunchDeElevatedViaTokenWithStdin: OpenProcess(explorer) failed");
        return false;
    }

    // Verify explorer is not elevated
    HANDLE hQueryToken = nullptr;
    bool explorerOk = true;
    if (OpenProcessToken(hProcess, TOKEN_QUERY, &hQueryToken))
    {
        TOKEN_ELEVATION elevation;
        DWORD size = sizeof(elevation);
        if (GetTokenInformation(hQueryToken, TokenElevation, &elevation, size, &size))
        {
            if (elevation.TokenIsElevated)
            {
                LOG_G_ERRA(L"PrivilegeLaunchService::LaunchDeElevatedViaTokenWithStdin: explorer is also elevated, cannot de-elevate");
                explorerOk = false;
            }
        }
        CloseHandle(hQueryToken);
    }

    if (!explorerOk)
    {
        CloseHandle(hProcess);
        return false;
    }

    HANDLE hToken = nullptr;
    DWORD access = TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY;
    if (!OpenProcessToken(hProcess, access, &hToken))
    {
        access = TOKEN_QUERY | TOKEN_DUPLICATE;
        if (!OpenProcessToken(hProcess, access, &hToken))
        {
            LOG_G_ERRA(L"PrivilegeLaunchService::LaunchDeElevatedViaTokenWithStdin: OpenProcessToken failed");
            CloseHandle(hProcess);
            return false;
        }
    }

    HANDLE hDupToken = nullptr;
    if (!DuplicateTokenEx(hToken, MAXIMUM_ALLOWED, nullptr, SecurityImpersonation, TokenPrimary, &hDupToken))
    {
        LOG_G_ERRA(L"PrivilegeLaunchService::LaunchDeElevatedViaTokenWithStdin: DuplicateTokenEx failed");
        CloseHandle(hToken);
        CloseHandle(hProcess);
        return false;
    }

    CloseHandle(hToken);
    CloseHandle(hProcess);

    // Build command line
    std::wstring cmdLine = L"\"" + targetPath + L"\"";
    if (!arguments.empty())
        cmdLine += L" " + arguments;

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = hStdinRead;
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION pi = {};
    LPCWSTR dir = workingDir.empty() ? nullptr : workingDir.c_str();

    BOOL ok = CreateProcessWithTokenW(
        hDupToken,
        0,
        nullptr,
        const_cast<LPWSTR>(cmdLine.c_str()),
        CREATE_UNICODE_ENVIRONMENT,
        nullptr,
        dir,
        &si,
        &pi
    );

    if (ok)
    {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        LOG_G_INFO(L"PrivilegeLaunchService::LaunchDeElevatedViaTokenWithStdin: CreateProcessWithTokenW succeeded");
    }
    else
    {
        DWORD err = GetLastError();
        LOG_G_ERRA(L"PrivilegeLaunchService::LaunchDeElevatedViaTokenWithStdin: CreateProcessWithTokenW failed, error=%lu", err);
    }

    CloseHandle(hDupToken);
    return ok != 0;
}

bool PrivilegeLaunchService::LaunchDeElevatedViaToken(const std::wstring& targetPath, const std::wstring& arguments, const std::wstring& workingDir)
{
    HWND hShell = GetShellWindow();
    if (!hShell) return false;

    DWORD pid = 0;
    GetWindowThreadProcessId(hShell, &pid);
    if (!pid) return false;

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProcess) return false;

    // Check if explorer is elevated
    HANDLE hQueryToken = NULL;
    if (OpenProcessToken(hProcess, TOKEN_QUERY, &hQueryToken))
    {
        TOKEN_ELEVATION elevation;
        DWORD size = sizeof(elevation);
        if (GetTokenInformation(hQueryToken, TokenElevation, &elevation, size, &size))
        {
            if (elevation.TokenIsElevated)
            {
                CloseHandle(hQueryToken);
                CloseHandle(hProcess);
                return false;
            }
        }
        CloseHandle(hQueryToken);
    }

    HANDLE hToken = NULL;
    DWORD access = TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY;
    if (!OpenProcessToken(hProcess, access, &hToken))
    {
        access = TOKEN_QUERY | TOKEN_DUPLICATE;
        if (!OpenProcessToken(hProcess, access, &hToken))
        {
            CloseHandle(hProcess);
            return false;
        }
    }

    HANDLE hDupToken = NULL;
    if (!DuplicateTokenEx(hToken, MAXIMUM_ALLOWED, NULL, SecurityImpersonation, TokenPrimary, &hDupToken))
    {
        CloseHandle(hToken);
        CloseHandle(hProcess);
        return false;
    }

    CloseHandle(hToken);
    CloseHandle(hProcess);

    // Build command line
    std::wstring cmdLine = L"\"" + targetPath + L"\"";
    if (!arguments.empty()) cmdLine += L" " + arguments;

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOWNORMAL;
    PROCESS_INFORMATION pi = {};

    BOOL ok = CreateProcessWithTokenW(
        hDupToken,
        0,
        nullptr,
        const_cast<LPWSTR>(cmdLine.c_str()),
        CREATE_UNICODE_ENVIRONMENT,
        nullptr,
        workingDir.empty() ? nullptr : workingDir.c_str(),
        &si,
        &pi
    );

    if (ok)
    {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }

    CloseHandle(hDupToken);
    return ok != 0;
}

static HRESULT GetProperty(IDispatch* pDisp, const wchar_t* name, VARIANT* pVar)
{
    DISPID dispid;
    OLECHAR* szName = const_cast<OLECHAR*>(name);
    HRESULT hr = pDisp->GetIDsOfNames(IID_NULL, &szName, 1, LOCALE_USER_DEFAULT, &dispid);
    if (FAILED(hr)) return hr;

    DISPPARAMS params = { NULL, NULL, 0, 0 };
    return pDisp->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYGET, &params, pVar, NULL, NULL);
}

bool PrivilegeLaunchService::LaunchDeElevatedViaCOM(const std::wstring& targetPath, const std::wstring& arguments, const std::wstring& workingDir)
{
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    bool needsUninit = (hr == S_OK || hr == S_FALSE);

    bool success = false;
    IShellWindows* psw = nullptr;
    hr = CoCreateInstance(CLSID_ShellWindows, NULL, CLSCTX_LOCAL_SERVER, IID_IShellWindows, (void**)&psw);
    if (SUCCEEDED(hr) && psw)
    {
        HWND hwndShell = GetShellWindow();
        if (hwndShell)
        {
            long lHwnd = 0;
            IDispatch* pdisp = nullptr;
            VARIANT vEmpty = {};
            VariantInit(&vEmpty);
            
            VARIANT vLoc = {};
            vLoc.vt = VT_I4;
            vLoc.lVal = 0;

            hr = psw->FindWindowSW(&vLoc, &vEmpty, 8, &lHwnd, 1, &pdisp); // SWC_DESKTOP=8, SWFO_NEEDDISPATCH=1
            if (SUCCEEDED(hr) && pdisp)
            {
                VARIANT varDoc = {};
                hr = GetProperty(pdisp, L"Document", &varDoc);
                if (SUCCEEDED(hr) && varDoc.vt == VT_DISPATCH && varDoc.pdispVal)
                {
                    VARIANT varApp = {};
                    hr = GetProperty(varDoc.pdispVal, L"Application", &varApp);
                    if (SUCCEEDED(hr) && varApp.vt == VT_DISPATCH && varApp.pdispVal)
                    {
                        DISPID dispidExecute;
                        OLECHAR* szExecute = (OLECHAR*)L"ShellExecute";
                        hr = varApp.pdispVal->GetIDsOfNames(IID_NULL, &szExecute, 1, LOCALE_USER_DEFAULT, &dispidExecute);
                        if (SUCCEEDED(hr))
                        {
                            // Args are in reverse order
                            VARIANTARG vargs[5];
                            VariantInit(&vargs[0]);
                            vargs[0].vt = VT_I4;
                            vargs[0].lVal = SW_SHOWNORMAL; // vShow

                            VariantInit(&vargs[1]);
                            vargs[1].vt = VT_BSTR;
                            vargs[1].bstrVal = SysAllocString(L"open"); // vOperation

                            VariantInit(&vargs[2]);
                            vargs[2].vt = VT_BSTR;
                            vargs[2].bstrVal = SysAllocString(workingDir.c_str()); // vDir

                            VariantInit(&vargs[3]);
                            vargs[3].vt = VT_BSTR;
                            vargs[3].bstrVal = SysAllocString(arguments.c_str()); // vArgs

                            VariantInit(&vargs[4]);
                            vargs[4].vt = VT_BSTR;
                            vargs[4].bstrVal = SysAllocString(targetPath.c_str()); // File

                            DISPPARAMS dp = { vargs, NULL, 5, 0 };
                            VARIANT vResult = {};
                            hr = varApp.pdispVal->Invoke(dispidExecute, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &dp, &vResult, NULL, NULL);
                            if (SUCCEEDED(hr))
                            {
                                success = true;
                            }

                            SysFreeString(vargs[1].bstrVal);
                            SysFreeString(vargs[2].bstrVal);
                            SysFreeString(vargs[3].bstrVal);
                            SysFreeString(vargs[4].bstrVal);
                        }
                        varApp.pdispVal->Release();
                    }
                    varDoc.pdispVal->Release();
                }
                pdisp->Release();
            }
        }
        psw->Release();
    }

    if (needsUninit) CoUninitialize();
    return success;
}
