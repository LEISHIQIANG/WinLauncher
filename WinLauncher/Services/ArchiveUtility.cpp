#include "ArchiveUtility.h"

#include <Windows.h>
#include <filesystem>
#include <vector>

namespace
{
    std::wstring PowerShellSingleQuoted(const std::wstring& value)
    {
        std::wstring quoted = L"'";
        for (wchar_t ch : value)
        {
            if (ch == L'\'') quoted += L"''";
            else quoted += ch;
        }
        quoted += L"'";
        return quoted;
    }

    bool RunPowerShell(const std::wstring& script, unsigned long timeoutMs, std::wstring& outError)
    {
        std::wstring command = L"powershell.exe -NoProfile -NonInteractive -Command \"$ErrorActionPreference='Stop'; " + script + L"\"";
        std::vector<wchar_t> mutableCommand(command.begin(), command.end());
        mutableCommand.push_back(L'\0');

        STARTUPINFOW startup{ sizeof(startup) };
        startup.dwFlags = STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
            nullptr, nullptr, &startup, &process))
        {
            outError = L"无法启动本地压缩工具";
            return false;
        }

        const DWORD wait = WaitForSingleObject(process.hProcess, timeoutMs);
        if (wait == WAIT_TIMEOUT)
        {
            TerminateProcess(process.hProcess, ERROR_TIMEOUT);
            WaitForSingleObject(process.hProcess, 5000);
            outError = L"本地压缩操作超时";
        }
        else if (wait != WAIT_OBJECT_0)
        {
            outError = L"无法等待本地压缩操作完成";
        }
        else
        {
            DWORD exitCode = 1;
            if (!GetExitCodeProcess(process.hProcess, &exitCode) || exitCode != 0)
                outError = L"本地压缩操作失败";
        }

        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return outError.empty();
    }
}

bool ArchiveUtility::CompressDirectoryContents(const std::wstring& sourceDirectory, const std::wstring& destinationZip,
    unsigned long timeoutMs, std::wstring& outError)
{
    outError.clear();
    std::error_code ec;
    std::filesystem::remove(destinationZip, ec);
    const std::wstring wildcard = (std::filesystem::path(sourceDirectory) / L"*").wstring();
    if (!RunPowerShell(L"Compress-Archive -Path " + PowerShellSingleQuoted(wildcard) +
        L" -DestinationPath " + PowerShellSingleQuoted(destinationZip) + L" -Force", timeoutMs, outError))
        return false;

    if (!std::filesystem::is_regular_file(destinationZip, ec) || std::filesystem::file_size(destinationZip, ec) == 0)
    {
        outError = L"本地压缩工具没有生成有效 ZIP 文件";
        return false;
    }
    return true;
}

bool ArchiveUtility::ExpandArchive(const std::wstring& sourceZip, const std::wstring& destinationDirectory,
    unsigned long timeoutMs, std::wstring& outError)
{
    outError.clear();
    return RunPowerShell(L"Expand-Archive -LiteralPath " + PowerShellSingleQuoted(sourceZip) +
        L" -DestinationPath " + PowerShellSingleQuoted(destinationDirectory) + L" -Force", timeoutMs, outError);
}
