#define NOMINMAX
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "CommandExecutionService.h"
#include "../App/AppContext.h"
#include "../App/Logger.h"
#include "EnvironmentDetector.h"
#include <Windows.h>
#include <shellapi.h>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>

static std::string ToUtf8Bytes(const std::wstring& value)
{
    if (value.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), (int)value.size(), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string result((size_t)len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), (int)value.size(), result.data(), len, nullptr, nullptr);
    return result;
}

static std::wstring DecodeCommandOutputBytes(const std::string& bytes)
{
    if (bytes.empty()) return L"";

    for (size_t trim = 0; trim < 4 && trim <= bytes.size(); ++trim)
    {
        int size = (int)(bytes.size() - trim);
        if (size <= 0) break;

        int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), size, nullptr, 0);
        if (wlen > 0)
        {
            std::wstring result(wlen, L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), size, &result[0], wlen);
            return result;
        }
    }

    int wlen = MultiByteToWideChar(CP_ACP, 0, bytes.data(), (int)bytes.size(), nullptr, 0);
    if (wlen <= 0) return L"";

    std::wstring result(wlen, L'\0');
    MultiByteToWideChar(CP_ACP, 0, bytes.data(), (int)bytes.size(), &result[0], wlen);
    return result;
}

static std::wstring FormatCapturedCommandOutput(
    const std::wstring& decodedOutput,
    DWORD exitCode,
    bool hasExitCode,
    bool timedOut,
    bool truncated,
    int timeoutSeconds)
{
    std::wstring status = timedOut ? L"超时终止" : ((hasExitCode && exitCode == 0) ? L"成功" : L"失败");
    std::wstring result = L"状态: " + status;

    if (hasExitCode)
    {
        result += L"\r\n退出码: " + std::to_wstring(exitCode);
    }
    if (timedOut)
    {
        result += L"\r\n提示: 命令超过 " + std::to_wstring(timeoutSeconds) + L" 秒超时时间，进程已终止。";
    }
    if (truncated)
    {
        result += L"\r\n提示: 输出超过最大字符数，已截断。";
    }

    result += L"\r\n\r\n输出:\r\n";
    result += decodedOutput.empty() ? L"(命令没有输出)" : decodedOutput;
    return result;
}

static std::wstring CreateTempCommandScript(const std::wstring& script, const wchar_t* extension)
{
    wchar_t tempDir[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, tempDir))
        return L"";

    wchar_t tempFile[MAX_PATH]{};
    if (!GetTempFileNameW(tempDir, L"wlc", 0, tempFile))
        return L"";

    std::wstring path = tempFile;
    std::wstring ext = extension ? extension : L".txt";
    std::wstring scriptPath = path + ext;
    if (!MoveFileExW(path.c_str(), scriptPath.c_str(), MOVEFILE_REPLACE_EXISTING))
    {
        DeleteFileW(path.c_str());
        return L"";
    }

    std::string bytes = ToUtf8Bytes(script);
    HANDLE hFile = CreateFileW(scriptPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return L"";

    DWORD written = 0;
    BOOL ok = WriteFile(hFile, bytes.data(), (DWORD)bytes.size(), &written, nullptr);
    CloseHandle(hFile);
    if (!ok || written != static_cast<DWORD>(bytes.size()))
    {
        DeleteFileW(scriptPath.c_str());
        return L"";
    }

    MoveFileExW(scriptPath.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    return scriptPath;
}

CommandExecutionService::CommandExecutionService(const std::shared_ptr<AppContext>& ctx)
    : m_ctx(ctx)
{
}

bool CommandExecutionService::Execute(const CommandExecutionRequest& request, CommandExecutionResult& result)
{
    bool ok = false;
    std::wstring output;

    if (request.type == L"cmd")
    {
        std::wstring cmdArgs = L"/c " + request.commandText;
        ok = ExecuteProcessHelper(L"cmd.exe", cmdArgs, L"", request.showWindow, request.captureOutput, request.timeoutSeconds, request.maxChars, output, request.cancellationToken);
    }
    else if (request.type == L"powershell")
    {
        ok = ExecuteScriptViaTempFile(L"powershell.exe", L".ps1", L"-NoProfile -NonInteractive -ExecutionPolicy Bypass -File", request.commandText, request.showWindow, request.captureOutput, request.timeoutSeconds, request.maxChars, output, request.cancellationToken);
    }
    else if (request.type == L"python" || EnvironmentDetector::IsVersionedPythonCommandType(request.type))
    {
        EnvironmentDetector::PythonInterpreter python;
        if (!EnvironmentDetector::TryGetPythonInterpreter(request.type, python))
        {
            LOG_G_ERRA(L"CommandExecutionService::Execute: requested python interpreter not found: %s", request.type.c_str());
            output = GetPythonUnavailableMessage(request.type);
            ok = false;
        }
        else
        {
            ok = ExecuteScriptViaTempFile(python.executablePath, L".py", L"-u", request.commandText, request.showWindow, request.captureOutput, request.timeoutSeconds, request.maxChars, output, request.cancellationToken);
        }
    }
    else if (request.type == L"gitbash")
    {
        std::wstring bashPath = FindBashExe();
        if (bashPath.empty())
        {
            LOG_G_ERRA(L"CommandExecutionService::Execute: gitbash — bash.exe not found (git.exe not in PATH or Git not installed?)");
            output = L"未找到可用的 Git Bash。\r\n请安装 Git for Windows，或确认 git.exe / bash.exe 在 PATH 中可用。";
            ok = false;
        }
        else
        {
            ok = ExecuteScriptViaTempFile(bashPath, L".sh", L"", request.commandText, request.showWindow, request.captureOutput, request.timeoutSeconds, request.maxChars, output, request.cancellationToken);
        }
    }
    else
    {
        LOG_G_ERRA(L"CommandExecutionService::Execute: unsupported command type=%s", request.type.c_str());
        output = L"不支持的命令类型: " + request.type;
        ok = false;
    }

    result.output = output;
    // We don't have separate exit code passing for simple synchronous helpers yet, but we match format output behavior
    return ok;
}

bool CommandExecutionService::ExecuteStreaming(const CommandExecutionRequest& request, const CommandOutputCallback& onOutput)
{
    if (request.type == L"cmd")
    {
        return ExecuteProcessStreaming(L"cmd.exe", L"/c " + request.commandText, false, request.timeoutSeconds, request.maxChars, onOutput, request.cancellationToken);
    }
    else if (request.type == L"powershell")
    {
        return ExecuteScriptViaTempFileStreaming(
            L"powershell.exe",
            L".ps1",
            L"-NoProfile -NonInteractive -ExecutionPolicy Bypass -File",
            request.commandText,
            false,
            request.timeoutSeconds,
            request.maxChars,
            onOutput,
            request.cancellationToken);
    }
    else if (request.type == L"python" || EnvironmentDetector::IsVersionedPythonCommandType(request.type))
    {
        EnvironmentDetector::PythonInterpreter python;
        if (!EnvironmentDetector::TryGetPythonInterpreter(request.type, python))
        {
            if (onOutput)
            {
                onOutput(L"\r\n" + GetPythonUnavailableMessage(request.type) + L"\r\n");
                onOutput(L"\r\n状态: 失败\r\n");
            }
            return false;
        }
        else
        {
            return ExecuteScriptViaTempFileStreaming(
                python.executablePath,
                L".py",
                L"-u",
                request.commandText,
                false,
                request.timeoutSeconds,
                request.maxChars,
                onOutput,
                request.cancellationToken);
        }
    }
    else if (request.type == L"gitbash")
    {
        std::wstring bashPath = FindBashExe();
        if (bashPath.empty())
        {
            if (onOutput)
            {
                onOutput(L"\r\n未找到可用的 Git Bash。\r\n请安装 Git for Windows，或确认 git.exe / bash.exe 在 PATH 中可用。\r\n");
                onOutput(L"\r\n状态: 失败\r\n");
            }
            return false;
        }
        else
        {
            return ExecuteScriptViaTempFileStreaming(
                bashPath,
                L".sh",
                L"",
                request.commandText,
                false,
                request.timeoutSeconds,
                request.maxChars,
                onOutput,
                request.cancellationToken);
        }
    }
    else
    {
        if (onOutput)
        {
            onOutput(L"\r\n不支持的命令类型: " + request.type + L"\r\n\r\n状态: 失败\r\n");
        }
        return false;
    }
}

std::wstring CommandExecutionService::FindBashExe()
{
    wchar_t foundPath[MAX_PATH]{};
    DWORD len = SearchPathW(nullptr, L"git.exe", nullptr, MAX_PATH, foundPath, nullptr);
    if (len > 0 && len < MAX_PATH)
    {
        std::wstring gp(foundPath);
        size_t cmdPos = gp.rfind(L"\\cmd\\");
        if (cmdPos != std::wstring::npos)
        {
            std::wstring root = gp.substr(0, cmdPos);
            std::wstring candidate = root + L"\\bin\\bash.exe";
            if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES)
                return candidate;
            candidate = root + L"\\usr\\bin\\bash.exe";
            if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES)
                return candidate;
        }
    }

    static const wchar_t* commonPaths[] = {
        L"C:\\Program Files\\Git\\bin\\bash.exe",
        L"C:\\Program Files\\Git\\usr\\bin\\bash.exe",
        L"C:\\Program Files (x86)\\Git\\bin\\bash.exe",
        L"C:\\Program Files (x86)\\Git\\usr\\bin\\bash.exe",
    };
    for (auto p : commonPaths)
    {
        if (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES)
            return p;
    }

    return L"";
}

std::wstring CommandExecutionService::GetPythonUnavailableMessage(const std::wstring& commandType)
{
    if (!EnvironmentDetector::IsDetectionComplete())
        return L"Python 运行环境仍在检测中，请稍候再试。";

    if (EnvironmentDetector::IsVersionedPythonCommandType(commandType))
    {
        return L"未找到指定的 " + EnvironmentDetector::GetPythonDisplayName(commandType) +
            L" 解释器。该命令不会改用其他 Python 版本，请在命令编辑器中重新选择可用版本。";
    }

    return L"未找到可用的 Python 解释器。请安装 Python，或确认 python.exe / py.exe 在 PATH 中可用。";
}

bool CommandExecutionService::ExecuteProcessHelper(
    const std::wstring& targetPath,
    const std::wstring& arguments,
    const std::wstring& stdinContent,
    bool showWindow,
    bool captureOutput,
    int timeoutSeconds,
    int maxChars,
    std::wstring& outOutput,
    const std::shared_ptr<BackgroundTaskService::CancellationToken>& cancellationToken,
    bool waitForExit)
{
    HANDLE hStdinRead = nullptr;
    HANDLE hStdinWrite = nullptr;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };

    if (!stdinContent.empty())
    {
        if (!CreatePipe(&hStdinRead, &hStdinWrite, &sa, 0))
        {
            LOG_G_ERRA(L"ExecuteProcessHelper: CreatePipe for stdin failed, error=%lu", GetLastError());
            return false;
        }
        SetHandleInformation(hStdinWrite, HANDLE_FLAG_INHERIT, 0);
    }

    HANDLE hStdoutRead = nullptr;
    HANDLE hStdoutWrite = nullptr;
    if (captureOutput)
    {
        if (!CreatePipe(&hStdoutRead, &hStdoutWrite, &sa, 0))
        {
            LOG_G_ERRA(L"ExecuteProcessHelper: CreatePipe for stdout failed, error=%lu", GetLastError());
            if (hStdinRead) { CloseHandle(hStdinRead); CloseHandle(hStdinWrite); }
            return false;
        }
        SetHandleInformation(hStdoutRead, HANDLE_FLAG_INHERIT, 0);
    }

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = hStdinRead ? hStdinRead : GetStdHandle(STD_INPUT_HANDLE);

    if (captureOutput)
    {
        si.hStdOutput = hStdoutWrite;
        si.hStdError = hStdoutWrite;
    }
    else
    {
        si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    }

    std::wstring cmdLine = L"\"" + targetPath + L"\"";
    if (!arguments.empty())
        cmdLine += L" " + arguments;

    DWORD flags = CREATE_UNICODE_ENVIRONMENT;
    if (!showWindow)
    {
        flags |= CREATE_NO_WINDOW;
    }

    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessW(
        nullptr,
        const_cast<LPWSTR>(cmdLine.c_str()),
        nullptr, nullptr,
        TRUE,
        flags,
        nullptr,
        nullptr,
        &si,
        &pi
    );

    if (hStdinRead) CloseHandle(hStdinRead);
    if (hStdoutWrite) CloseHandle(hStdoutWrite);

    if (!ok)
    {
        DWORD err = GetLastError();
        LOG_G_ERRA(L"ExecuteProcessHelper: CreateProcessW failed, target=%s args=%s error=%lu",
                   targetPath.c_str(), arguments.c_str(), err);
        if (captureOutput)
        {
            outOutput = L"命令启动失败。\r\n目标: " + targetPath +
                        L"\r\n参数: " + arguments +
                        L"\r\nWindows 错误码: " + std::to_wstring(err);
        }
        if (hStdinWrite) CloseHandle(hStdinWrite);
        if (hStdoutRead) CloseHandle(hStdoutRead);
        return false;
    }

    if (pi.hThread) CloseHandle(pi.hThread);

    if (hStdinWrite)
    {
        if (!stdinContent.empty())
        {
            int len = WideCharToMultiByte(CP_UTF8, 0, stdinContent.c_str(), (int)stdinContent.size(), nullptr, 0, nullptr, nullptr);
            if (len > 0)
            {
                std::string utf8Content(len, '\0');
                WideCharToMultiByte(CP_UTF8, 0, stdinContent.c_str(), (int)stdinContent.size(), &utf8Content[0], len, nullptr, nullptr);

                DWORD written = 0;
                WriteFile(hStdinWrite, utf8Content.data(), (DWORD)utf8Content.size(), &written, nullptr);
            }
        }
        CloseHandle(hStdinWrite);
    }

    if (captureOutput)
    {
        std::string capturedBytes;
        DWORD startTime = GetTickCount();
        DWORD exitCode = 0;
        bool hasExitCode = false;
        bool timedOut = false;
        bool truncated = false;

        while (true)
        {
            if ((cancellationToken && cancellationToken->IsCancellationRequested()) ||
                BackgroundTaskService::IsCurrentTaskCancellationRequested())
            {
                TerminateProcess(pi.hProcess, 1);
                break;
            }

            if (timeoutSeconds > 0 && (GetTickCount() - startTime) > (DWORD)(timeoutSeconds * 1000))
            {
                timedOut = true;
                LOG_G_WORNING(L"ExecuteProcessHelper: process timed out, target=%s args=%s",
                              targetPath.c_str(), arguments.c_str());
                TerminateProcess(pi.hProcess, 1);
                WaitForSingleObject(pi.hProcess, 2000);
                if (GetExitCodeProcess(pi.hProcess, &exitCode))
                {
                    hasExitCode = true;
                }
                break;
            }

            DWORD avail = 0;
            if (PeekNamedPipe(hStdoutRead, nullptr, 0, nullptr, &avail, nullptr) && avail > 0)
            {
                DWORD toRead = avail;
                bool appendThisChunk = !truncated;
                if (maxChars > 0 && !truncated)
                {
                    size_t limitBytes = (size_t)maxChars * 3;
                    size_t remaining = capturedBytes.size() < limitBytes ? (limitBytes - capturedBytes.size()) : 0;
                    if (remaining == 0)
                    {
                        truncated = true;
                        appendThisChunk = false;
                    }
                    else if ((size_t)toRead > remaining)
                    {
                        toRead = (DWORD)remaining;
                        truncated = true;
                    }
                }

                std::vector<char> buffer(toRead);
                DWORD read = 0;
                if (ReadFile(hStdoutRead, buffer.data(), toRead, &read, nullptr) && read > 0)
                {
                    if (appendThisChunk)
                    {
                        capturedBytes.append(buffer.data(), read);
                    }
                }
            }
            else
            {
                DWORD wait = WaitForSingleObject(pi.hProcess, 50);
                if (wait == WAIT_OBJECT_0)
                {
                    // Read any final remaining data
                    if (PeekNamedPipe(hStdoutRead, nullptr, 0, nullptr, &avail, nullptr) && avail > 0)
                    {
                        std::vector<char> buffer(avail);
                        DWORD read = 0;
                        if (ReadFile(hStdoutRead, buffer.data(), avail, &read, nullptr) && read > 0)
                        {
                            if (!truncated)
                            {
                                capturedBytes.append(buffer.data(), read);
                            }
                        }
                    }
                    if (GetExitCodeProcess(pi.hProcess, &exitCode))
                    {
                        hasExitCode = true;
                    }
                    break;
                }
            }
        }

        CloseHandle(hStdoutRead);
        CloseHandle(pi.hProcess);

        std::wstring decodedOutput = DecodeCommandOutputBytes(capturedBytes);
        outOutput = FormatCapturedCommandOutput(decodedOutput, exitCode, hasExitCode, timedOut, truncated, timeoutSeconds);
        return hasExitCode && (exitCode == 0);
    }
    else
    {
        if (waitForExit)
        {
            while (true)
            {
                if ((cancellationToken && cancellationToken->IsCancellationRequested()) ||
                    BackgroundTaskService::IsCurrentTaskCancellationRequested())
                {
                    TerminateProcess(pi.hProcess, 1);
                    break;
                }
                DWORD wait = WaitForSingleObject(pi.hProcess, 100);
                if (wait == WAIT_OBJECT_0) break;
            }
        }
        CloseHandle(pi.hProcess);
        return true;
    }
}

bool CommandExecutionService::ExecuteProcessStreaming(
    const std::wstring& targetPath,
    const std::wstring& arguments,
    bool showWindow,
    int timeoutSeconds,
    int maxChars,
    const CommandOutputCallback& onOutput,
    const std::shared_ptr<BackgroundTaskService::CancellationToken>& cancellationToken)
{
    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    HANDLE hStdoutRead = nullptr;
    HANDLE hStdoutWrite = nullptr;
    HANDLE hStderrRead = nullptr;
    HANDLE hStderrWrite = nullptr;
    if (!CreatePipe(&hStdoutRead, &hStdoutWrite, &sa, 0))
    {
        DWORD err = GetLastError();
        LOG_G_ERRA(L"ExecuteProcessStreaming: CreatePipe for stdout failed, error=%lu", err);
        if (onOutput) onOutput(L"\r\n命令启动失败: 无法创建输出管道，Windows 错误码 " + std::to_wstring(err) + L"\r\n");
        return false;
    }
    SetHandleInformation(hStdoutRead, HANDLE_FLAG_INHERIT, 0);
    if (!CreatePipe(&hStderrRead, &hStderrWrite, &sa, 0))
    {
        DWORD err = GetLastError();
        CloseHandle(hStdoutRead);
        CloseHandle(hStdoutWrite);
        LOG_G_ERRA(L"ExecuteProcessStreaming: CreatePipe for stderr failed, error=%lu", err);
        if (onOutput) onOutput(L"\r\n命令启动失败: 无法创建错误输出管道，Windows 错误码 " + std::to_wstring(err) + L"\r\n");
        return false;
    }
    SetHandleInformation(hStderrRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = hStdoutWrite;
    si.hStdError = hStderrWrite;

    std::wstring cmdLine = L"\"" + targetPath + L"\"";
    if (!arguments.empty())
        cmdLine += L" " + arguments;

    DWORD flags = CREATE_UNICODE_ENVIRONMENT;
    if (!showWindow)
    {
        flags |= CREATE_NO_WINDOW;
    }

    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessW(
        nullptr,
        const_cast<LPWSTR>(cmdLine.c_str()),
        nullptr, nullptr,
        TRUE,
        flags,
        nullptr,
        nullptr,
        &si,
        &pi
    );

    CloseHandle(hStdoutWrite);
    CloseHandle(hStderrWrite);

    if (!ok)
    {
        DWORD err = GetLastError();
        CloseHandle(hStdoutRead);
        CloseHandle(hStderrRead);
        LOG_G_ERRA(L"ExecuteProcessStreaming: CreateProcessW failed, target=%s args=%s error=%lu",
                   targetPath.c_str(), arguments.c_str(), err);
        if (onOutput)
        {
            onOutput(L"\r\n命令启动失败。\r\n目标: " + targetPath +
                     L"\r\n参数: " + arguments +
                     L"\r\nWindows 错误码: " + std::to_wstring(err) + L"\r\n");
        }
        return false;
    }

    if (pi.hThread) CloseHandle(pi.hThread);

    HANDLE hJob = CreateJobObjectW(nullptr, nullptr);
    if (hJob)
    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobInfo{};
        jobInfo.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &jobInfo, sizeof(jobInfo));
        if (!AssignProcessToJobObject(hJob, pi.hProcess))
        {
            LOG_G_WORNING(L"ExecuteProcessStreaming: AssignProcessToJobObject failed, error=%lu", GetLastError());
        }
    }

    std::string capturedBytes;
    std::string capturedErrorBytes;
    DWORD startTime = GetTickCount();
    DWORD exitCode = 0;
    bool hasExitCode = false;
    bool timedOut = false;
    bool truncated = false;
    bool truncationNoticeSent = false;

    while (true)
    {
        if ((cancellationToken && cancellationToken->IsCancellationRequested()) ||
            BackgroundTaskService::IsCurrentTaskCancellationRequested())
        {
            if (hJob) TerminateJobObject(hJob, 1);
            else TerminateProcess(pi.hProcess, 1);
            break;
        }

        if (timeoutSeconds > 0 && (GetTickCount() - startTime) > (DWORD)(timeoutSeconds * 1000))
        {
            timedOut = true;
            if (hJob) TerminateJobObject(hJob, 1);
            else TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 5000);
            if (GetExitCodeProcess(pi.hProcess, &exitCode))
            {
                hasExitCode = true;
            }
            break;
        }

        DWORD avail = 0;
        bool readAny = false;
        while (PeekNamedPipe(hStdoutRead, nullptr, 0, nullptr, &avail, nullptr) && avail > 0)
        {
            DWORD toRead = avail;
            bool appendThisChunk = !truncated;
            if (maxChars > 0 && !truncated)
            {
                size_t limitBytes = (size_t)maxChars * 3;
                size_t remaining = capturedBytes.size() < limitBytes ? (limitBytes - capturedBytes.size()) : 0;
                if (remaining == 0)
                {
                    truncated = true;
                    appendThisChunk = false;
                }
                else if ((size_t)toRead > remaining)
                {
                    toRead = (DWORD)remaining;
                    truncated = true;
                }
            }

            std::vector<char> buffer(toRead);
            DWORD read = 0;
            if (ReadFile(hStdoutRead, buffer.data(), toRead, &read, nullptr) && read > 0)
            {
                readAny = true;
                std::string chunk(buffer.data(), read);
                if (appendThisChunk)
                {
                    capturedBytes.append(chunk);
                    if (onOutput)
                    {
                        onOutput(DecodeCommandOutputBytes(chunk));
                    }
                }
                if (truncated && !truncationNoticeSent && onOutput)
                {
                    onOutput(L"\r\n\r\n[输出超过最大字符数，后续输出不再显示，命令仍在继续运行...]\r\n");
                    truncationNoticeSent = true;
                }
            }
        }

        while (PeekNamedPipe(hStderrRead, nullptr, 0, nullptr, &avail, nullptr) && avail > 0)
        {
            std::vector<char> buffer(avail);
            DWORD read = 0;
            if (ReadFile(hStderrRead, buffer.data(), avail, &read, nullptr) && read > 0)
            {
                readAny = true;
                std::string chunk(buffer.data(), read);
                if (!truncated)
                {
                    capturedErrorBytes.append(chunk);
                    if (onOutput)
                    {
                        onOutput(DecodeCommandOutputBytes(chunk));
                    }
                }
            }
        }

        if (!readAny)
        {
            DWORD wait = WaitForSingleObject(pi.hProcess, 50);
            if (wait == WAIT_OBJECT_0)
            {
                // Final flush stdout
                if (PeekNamedPipe(hStdoutRead, nullptr, 0, nullptr, &avail, nullptr) && avail > 0)
                {
                    std::vector<char> buffer(avail);
                    DWORD read = 0;
                    if (ReadFile(hStdoutRead, buffer.data(), avail, &read, nullptr) && read > 0)
                    {
                        if (!truncated)
                        {
                            capturedBytes.append(buffer.data(), read);
                            if (onOutput) onOutput(DecodeCommandOutputBytes(std::string(buffer.data(), read)));
                        }
                    }
                }
                // Final flush stderr
                if (PeekNamedPipe(hStderrRead, nullptr, 0, nullptr, &avail, nullptr) && avail > 0)
                {
                    std::vector<char> buffer(avail);
                    DWORD read = 0;
                    if (ReadFile(hStderrRead, buffer.data(), avail, &read, nullptr) && read > 0)
                    {
                        if (!truncated)
                        {
                            capturedErrorBytes.append(buffer.data(), read);
                            if (onOutput) onOutput(DecodeCommandOutputBytes(std::string(buffer.data(), read)));
                        }
                    }
                }
                if (GetExitCodeProcess(pi.hProcess, &exitCode))
                {
                    hasExitCode = true;
                }
                break;
            }
        }
    }

    CloseHandle(hStdoutRead);
    CloseHandle(hStderrRead);
    CloseHandle(pi.hProcess);
    if (hJob) CloseHandle(hJob);

    if (onOutput)
    {
        onOutput(L"\r\n\r\n");
        std::wstring status = timedOut ? L"超时终止" : ((hasExitCode && exitCode == 0) ? L"成功" : L"失败");
        onOutput(L"状态: " + status + L"\r\n");
        if (hasExitCode)
        {
            onOutput(L"退出码: " + std::to_wstring(exitCode) + L"\r\n");
        }
    }

    return hasExitCode && (exitCode == 0);
}

bool CommandExecutionService::ExecuteScriptViaTempFile(
    const std::wstring& interpreter,
    const std::wstring& extension,
    const std::wstring& extraArgsBefore,
    const std::wstring& scriptContent,
    bool showWindow,
    bool captureOutput,
    int timeoutSeconds,
    int maxChars,
    std::wstring& output,
    const std::shared_ptr<BackgroundTaskService::CancellationToken>& cancellationToken)
{
    std::wstring scriptFile = CreateTempCommandScript(scriptContent, extension.c_str());
    if (scriptFile.empty())
    {
        output = L"无法创建临时脚本文件。";
        return false;
    }

    std::wstring args = extraArgsBefore;
    if (!args.empty()) args += L" ";
    args += L"\"" + scriptFile + L"\"";

    bool ok = false;
    if (!captureOutput)
    {
        auto sharedCtx = m_ctx.lock();
        auto tasks = sharedCtx ? sharedCtx->backgroundTasks : nullptr;
        auto handle = tasks ? tasks->Submit(L"script.execute", BackgroundTaskService::Priority::Normal,
            [this, interpreter, args, scriptFile, showWindow, timeoutSeconds, maxChars](const std::shared_ptr<BackgroundTaskService::CancellationToken>& cancellation) {
            std::wstring dummyOutput;
            if (!cancellation->IsCancellationRequested())
                ExecuteProcessHelper(interpreter, args, L"", showWindow, false, timeoutSeconds, maxChars, dummyOutput, cancellation, true);
            DeleteFileW(scriptFile.c_str());
        }) : BackgroundTaskService::TaskHandle{};

        if (!handle)
        {
            DeleteFileW(scriptFile.c_str());
            ok = false;
        }
        else
        {
            ok = true;
        }
    }
    else
    {
        ok = ExecuteProcessHelper(interpreter, args, L"", showWindow, true, timeoutSeconds, maxChars, output, cancellationToken);
        DeleteFileW(scriptFile.c_str());
    }

    return ok;
}

bool CommandExecutionService::ExecuteScriptViaTempFileStreaming(
    const std::wstring& interpreter,
    const std::wstring& extension,
    const std::wstring& extraArgsBefore,
    const std::wstring& scriptContent,
    bool showWindow,
    int timeoutSeconds,
    int maxChars,
    const CommandOutputCallback& onOutput,
    const std::shared_ptr<BackgroundTaskService::CancellationToken>& cancellationToken)
{
    std::wstring scriptFile = CreateTempCommandScript(scriptContent, extension.c_str());
    if (scriptFile.empty())
    {
        if (onOutput) onOutput(L"\r\n无法创建临时脚本文件。\r\n");
        return false;
    }

    std::wstring args = extraArgsBefore;
    if (!args.empty()) args += L" ";
    args += L"\"" + scriptFile + L"\"";

    bool ok = ExecuteProcessStreaming(interpreter, args, showWindow, timeoutSeconds, maxChars, onOutput, cancellationToken);
    DeleteFileW(scriptFile.c_str());
    return ok;
}
