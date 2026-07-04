#pragma once

#include "WinLauncherPluginABI.h"
#include <cstddef>
#include <string>

namespace WinLauncher::Plugin
{
    class Host
    {
    public:
        explicit Host(const WLHostApiV1* api) : m_api(api) {}

        bool RegisterCommand(const std::wstring& id, const std::wstring& title, const std::wstring& description = L"") const
        {
            if (!m_api || !m_api->registerCommand)
                return false;

            WLCommandDescriptorV1 command{};
            command.size = sizeof(command);
            command.id = id.c_str();
            command.title = title.c_str();
            command.description = description.c_str();
            return m_api->registerCommand(m_api->hostContext, &command);
        }

        bool RegisterSlashCommand(const std::wstring& id, const std::wstring& commandName, const std::wstring& title, const std::wstring& description = L"", const std::wstring& usage = L"", const std::wstring& icon = L"") const
        {
            if (!m_api || !m_api->registerSlashCommand)
                return false;

            WLSlashCommandDescriptorV1 command{};
            command.size = sizeof(command);
            command.id = id.c_str();
            command.command = commandName.c_str();
            command.title = title.c_str();
            command.description = description.c_str();
            command.usage = usage.c_str();
            command.icon = icon.c_str();
            return m_api->registerSlashCommand(m_api->hostContext, &command);
        }

        void Log(const std::wstring& message) const
        {
            if (m_api && m_api->log)
                m_api->log(m_api->hostContext, message.c_str());
        }

        std::wstring DataDirectory() const
        {
            if (!m_api || !m_api->getDataDirectory)
                return L"";

            uint32_t required = 0;
            m_api->getDataDirectory(m_api->hostContext, nullptr, 0, &required);
            if (required == 0)
                return L"";

            std::wstring result(required, L'\0');
            if (!m_api->getDataDirectory(m_api->hostContext, &result[0], required, &required))
                return L"";
            if (!result.empty() && result.back() == L'\0')
                result.pop_back();
            return result;
        }

        std::wstring AppVersion() const
        {
            return ReadHostString([&](wchar_t* buffer, uint32_t length, uint32_t* required) {
                return m_api && m_api->getAppVersion
                    ? m_api->getAppVersion(m_api->hostContext, buffer, length, required)
                    : false;
            });
        }

        std::wstring ReadClipboardText() const
        {
            return ReadHostResult([&](WLStringResultV1* result) {
                return m_api && m_api->readClipboardText
                    ? m_api->readClipboardText(m_api->hostContext, result)
                    : false;
            });
        }

        bool WriteClipboardText(const std::wstring& text) const
        {
            return m_api && m_api->writeClipboardText
                ? m_api->writeClipboardText(m_api->hostContext, text.c_str())
                : false;
        }

        bool OpenUrl(const std::wstring& url) const
        {
            return m_api && m_api->openUrl
                ? m_api->openUrl(m_api->hostContext, url.c_str())
                : false;
        }

        bool OpenFile(const std::wstring& path) const
        {
            return m_api && m_api->openFile
                ? m_api->openFile(m_api->hostContext, path.c_str())
                : false;
        }

        std::wstring ReadTextFile(const std::wstring& relativePath) const
        {
            return ReadHostResult([&](WLStringResultV1* result) {
                return m_api && m_api->readTextFile
                    ? m_api->readTextFile(m_api->hostContext, relativePath.c_str(), result)
                    : false;
            });
        }

        bool WriteTextFile(const std::wstring& relativePath, const std::wstring& text) const
        {
            return m_api && m_api->writeTextFile
                ? m_api->writeTextFile(m_api->hostContext, relativePath.c_str(), text.c_str())
                : false;
        }

        std::wstring GetConfig(const std::wstring& key, const std::wstring& defaultValue = L"") const
        {
            return ReadHostResult([&](WLStringResultV1* result) {
                return m_api && m_api->getPluginConfig
                    ? m_api->getPluginConfig(m_api->hostContext, key.c_str(), defaultValue.c_str(), result)
                    : false;
            });
        }

        bool SetConfig(const std::wstring& key, const std::wstring& value) const
        {
            return m_api && m_api->setPluginConfig
                ? m_api->setPluginConfig(m_api->hostContext, key.c_str(), value.c_str())
                : false;
        }

        std::wstring ShowInputDialog(const std::wstring& title, const std::wstring& prompt, const std::wstring& defaultText = L"") const
        {
            return ReadInteractiveResult([&](WLStringResultV1* result) {
                return HasApiField(offsetof(WLHostApiV1, showInputDialog) + sizeof(m_api->showInputDialog)) && m_api->showInputDialog
                    ? m_api->showInputDialog(m_api->hostContext, title.c_str(), prompt.c_str(), defaultText.c_str(), result)
                    : false;
            });
        }

        std::wstring ShowPasswordDialog(const std::wstring& title, const std::wstring& prompt) const
        {
            return ReadInteractiveResult([&](WLStringResultV1* result) {
                return HasApiField(offsetof(WLHostApiV1, showPasswordDialog) + sizeof(m_api->showPasswordDialog)) && m_api->showPasswordDialog
                    ? m_api->showPasswordDialog(m_api->hostContext, title.c_str(), prompt.c_str(), result)
                    : false;
            });
        }

        std::wstring ShowChooseDialog(const std::wstring& title, const std::wstring& prompt, const std::wstring& options) const
        {
            return ReadInteractiveResult([&](WLStringResultV1* result) {
                return HasApiField(offsetof(WLHostApiV1, showChooseDialog) + sizeof(m_api->showChooseDialog)) && m_api->showChooseDialog
                    ? m_api->showChooseDialog(m_api->hostContext, title.c_str(), prompt.c_str(), options.c_str(), result)
                    : false;
            });
        }

        bool ShowConfirmDialog(const std::wstring& title, const std::wstring& message) const
        {
            return HasApiField(offsetof(WLHostApiV1, showConfirmDialog) + sizeof(m_api->showConfirmDialog)) && m_api->showConfirmDialog
                ? m_api->showConfirmDialog(m_api->hostContext, title.c_str(), message.c_str())
                : false;
        }

        std::wstring ShowFilePicker(const std::wstring& title, bool multiSelect = false, const std::wstring& filterPattern = L"*.*", bool onlyFolders = false) const
        {
            return ReadInteractiveResult([&](WLStringResultV1* result) {
                return HasApiField(offsetof(WLHostApiV1, showFilePicker) + sizeof(m_api->showFilePicker)) && m_api->showFilePicker
                    ? m_api->showFilePicker(m_api->hostContext, title.c_str(), multiSelect, filterPattern.c_str(), onlyFolders, result)
                    : false;
            }, 32768);
        }

        bool ShowNotification(const std::wstring& title, const std::wstring& message, const std::wstring& type = L"info", uint32_t durationMs = 3000) const
        {
            return HasApiField(offsetof(WLHostApiV1, showNotificationToaster) + sizeof(m_api->showNotificationToaster)) && m_api->showNotificationToaster
                ? m_api->showNotificationToaster(m_api->hostContext, title.c_str(), message.c_str(), type.c_str(), durationMs)
                : false;
        }

        std::wstring ShowMessageBox(const std::wstring& title, const std::wstring& message, const std::wstring& iconType = L"info", const std::wstring& buttons = L"ok") const
        {
            return ReadInteractiveResult([&](WLStringResultV1* result) {
                return HasApiField(offsetof(WLHostApiV1, showMessageBox) + sizeof(m_api->showMessageBox)) && m_api->showMessageBox
                    ? m_api->showMessageBox(m_api->hostContext, title.c_str(), message.c_str(), iconType.c_str(), buttons.c_str(), result)
                    : false;
            }, 64);
        }

        bool ShowResultInPanel(const std::wstring& title, const std::wstring& content, const std::wstring& contentType = L"text") const
        {
            return HasApiField(offsetof(WLHostApiV1, showResultInPanel) + sizeof(m_api->showResultInPanel)) && m_api->showResultInPanel
                ? m_api->showResultInPanel(m_api->hostContext, title.c_str(), content.c_str(), contentType.c_str())
                : false;
        }

        std::wstring HttpRequest(const std::wstring& method, const std::wstring& url, const std::wstring& headers = L"", const std::wstring& body = L"", uint32_t timeoutMs = 30000) const
        {
            return ReadHostResult([&](WLStringResultV1* result) {
                return HasApiField(offsetof(WLHostApiV1, httpRequest) + sizeof(m_api->httpRequest)) && m_api->httpRequest
                    ? m_api->httpRequest(m_api->hostContext, method.c_str(), url.c_str(), headers.c_str(), body.c_str(), timeoutMs, result)
                    : false;
            });
        }

        std::wstring RunProcess(const std::wstring& command, uint32_t* exitCode, const std::wstring& workingDir = L"", bool captureOutput = true, uint32_t timeoutMs = 0) const
        {
            return ReadHostResult([&](WLStringResultV1* result) {
                return HasApiField(offsetof(WLHostApiV1, runProcess) + sizeof(m_api->runProcess)) && m_api->runProcess
                    ? m_api->runProcess(m_api->hostContext, command.c_str(), workingDir.c_str(), captureOutput, timeoutMs, result, exitCode)
                    : false;
            });
        }

    private:
        bool HasApiField(size_t fieldEnd) const
        {
            return m_api && m_api->size >= fieldEnd;
        }

        template <typename Fn>
        std::wstring ReadHostString(Fn fn) const
        {
            uint32_t required = 0;
            fn(nullptr, 0, &required);
            if (required == 0)
                return L"";
            std::wstring result(required, L'\0');
            if (!fn(&result[0], required, &required))
                return L"";
            if (!result.empty() && result.back() == L'\0')
                result.pop_back();
            return result;
        }

        template <typename Fn>
        std::wstring ReadHostResult(Fn fn) const
        {
            WLStringResultV1 probe{};
            probe.size = sizeof(probe);
            fn(&probe);
            if (probe.requiredLength == 0)
                return L"";
            std::wstring result(probe.requiredLength, L'\0');
            WLStringResultV1 output{};
            output.size = sizeof(output);
            output.buffer = &result[0];
            output.bufferLength = probe.requiredLength;
            if (!fn(&output))
                return L"";
            if (!result.empty() && result.back() == L'\0')
                result.pop_back();
            return result;
        }

        template <typename Fn>
        std::wstring ReadInteractiveResult(Fn fn, uint32_t capacity = 4096) const
        {
            std::wstring result(capacity, L'\0');
            WLStringResultV1 output{};
            output.size = sizeof(output);
            output.buffer = &result[0];
            output.bufferLength = capacity;
            if (!fn(&output))
                return L"";
            if (output.requiredLength > capacity)
                result.resize(output.requiredLength, L'\0');
            if (output.requiredLength > capacity)
            {
                output.buffer = &result[0];
                output.bufferLength = output.requiredLength;
                if (!fn(&output))
                    return L"";
            }
            if (!result.empty() && result.back() == L'\0')
                result.pop_back();
            return result;
        }

        const WLHostApiV1* m_api = nullptr;
    };
}
