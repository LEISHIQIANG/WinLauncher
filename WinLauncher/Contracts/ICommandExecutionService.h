#pragma once
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <Windows.h>
#include "../App/BackgroundTaskService.h"

struct CommandExecutionRequest
{
    std::wstring type; // cmd, powershell, python, gitbash
    std::wstring commandText;
    std::wstring arguments;
    std::vector<std::wstring> selectedFiles;
    bool showWindow = false;
    bool captureOutput = false;
    int timeoutSeconds = 300;
    int maxChars = 50000;
    std::shared_ptr<BackgroundTaskService::CancellationToken> cancellationToken;
};

struct CommandExecutionResult
{
    std::wstring output;
    DWORD exitCode = 0;
    bool timedOut = false;
    bool truncated = false;
};

using CommandOutputCallback = std::function<void(const std::wstring&)>;

class ICommandExecutionService
{
public:
    virtual ~ICommandExecutionService() = default;

    virtual bool Execute(const CommandExecutionRequest& request, CommandExecutionResult& result) = 0;
    virtual bool ExecuteStreaming(const CommandExecutionRequest& request, const CommandOutputCallback& onOutput) = 0;
};
