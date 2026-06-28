#pragma once
#include <string>
#include <windows.h>

class PrivilegeLaunchService
{
public:
    // Determine if the current process has administrator (elevated) privileges.
    static bool IsCurrentProcessElevated();

    // Launch a target program with specific runAsAdmin and working directory parameters.
    // Handles de-elevation if launcher is elevated and target is normal.
    static bool Launch(const std::wstring& targetPath, const std::wstring& arguments, bool runAsAdmin, const std::wstring& workingDir = L"");

private:
    // Duplicates Explorer's standard user token and launches via CreateProcessWithTokenW.
    static bool LaunchDeElevatedViaToken(const std::wstring& targetPath, const std::wstring& arguments, const std::wstring& workingDir);

    // Bypasses elevation by requesting Explorer's desktop COM object to run ShellExecute.
    static bool LaunchDeElevatedViaCOM(const std::wstring& targetPath, const std::wstring& arguments, const std::wstring& workingDir);
};
