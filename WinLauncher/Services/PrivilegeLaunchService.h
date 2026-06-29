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

    // Launch a target program with script content piped through stdin.
    // Avoids all quoting issues (script content never appears on the command line).
    // Limitations:
    //   - Cannot elevate (runAsAdmin must be false). Returns false with log if requested.
    //   - De-elevation (elevated launcher → normal target) is supported via token duplication.
    static bool LaunchWithStdin(
        const std::wstring& targetPath,
        const std::wstring& stdinContent,   // UTF-16 script text, will be converted to UTF-8 pipe
        const std::wstring& arguments,       // e.g. L"-" for python, L"-s" for bash
        bool runAsAdmin,
        const std::wstring& workingDir = L""
    );

private:
    // Duplicates Explorer's standard user token and launches via CreateProcessWithTokenW.
    static bool LaunchDeElevatedViaToken(const std::wstring& targetPath, const std::wstring& arguments, const std::wstring& workingDir);

    // Bypasses elevation by requesting Explorer's desktop COM object to run ShellExecute.
    static bool LaunchDeElevatedViaCOM(const std::wstring& targetPath, const std::wstring& arguments, const std::wstring& workingDir);

    // Token de-elevation variant that sets up stdin handle inheritance for piping.
    static bool LaunchDeElevatedViaTokenWithStdin(
        const std::wstring& targetPath,
        const std::wstring& arguments,
        const std::wstring& workingDir,
        HANDLE hStdinRead
    );

    // Helper: convert wstring to UTF-8 byte string.
    static std::string ToUtf8(const std::wstring& wstr);
};
