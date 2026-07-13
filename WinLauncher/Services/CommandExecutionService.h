#pragma once
#include "../Contracts/ICommandExecutionService.h"
#include <memory>
#include <string>

struct AppContext;

class CommandExecutionService : public ICommandExecutionService
{
public:
    explicit CommandExecutionService(const std::shared_ptr<AppContext>& ctx);
    virtual ~CommandExecutionService() override = default;

    virtual bool Execute(const CommandExecutionRequest& request, CommandExecutionResult& result) override;
    virtual bool ExecuteStreaming(const CommandExecutionRequest& request, const CommandOutputCallback& onOutput) override;

private:
    std::wstring FindBashExe();
    std::wstring GetPythonUnavailableMessage(const std::wstring& commandType);
    bool ExecuteProcessHelper(
        const std::wstring& targetPath,
        const std::wstring& arguments,
        const std::wstring& stdinContent,
        bool showWindow,
        bool captureOutput,
        int timeoutSeconds,
        int maxChars,
        std::wstring& outOutput,
        const std::shared_ptr<BackgroundTaskService::CancellationToken>& cancellationToken,
        bool waitForExit = false
    );
    bool ExecuteProcessStreaming(
        const std::wstring& targetPath,
        const std::wstring& arguments,
        bool showWindow,
        int timeoutSeconds,
        int maxChars,
        const CommandOutputCallback& onOutput,
        const std::shared_ptr<BackgroundTaskService::CancellationToken>& cancellationToken
    );
    bool ExecuteScriptViaTempFile(
        const std::wstring& interpreter,
        const std::wstring& extension,
        const std::wstring& extraArgsBefore,
        const std::wstring& scriptContent,
        bool showWindow,
        bool captureOutput,
        int timeoutSeconds,
        int maxChars,
        std::wstring& output,
        const std::shared_ptr<BackgroundTaskService::CancellationToken>& cancellationToken
    );
    bool ExecuteScriptViaTempFileStreaming(
        const std::wstring& interpreter,
        const std::wstring& extension,
        const std::wstring& extraArgsBefore,
        const std::wstring& scriptContent,
        bool showWindow,
        int timeoutSeconds,
        int maxChars,
        const CommandOutputCallback& onOutput,
        const std::shared_ptr<BackgroundTaskService::CancellationToken>& cancellationToken
    );

    std::weak_ptr<AppContext> m_ctx;
};
