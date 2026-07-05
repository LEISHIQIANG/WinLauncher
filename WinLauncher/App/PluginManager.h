#pragma once

#include "EventBus.h"
#include "Logger.h"
#include "PluginManifest.h"
#include "PluginStateStore.h"
#include "../Model/ShortcutInfo.h"
#include "../SDK/include/WinLauncher/WinLauncherPluginABI.h"
#include <Windows.h>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct PluginCommandInfo
{
    std::wstring pluginId;
    std::wstring commandId;
    std::wstring commandName;
    std::wstring title;
    std::wstring description;
    std::wstring usage;
    std::wstring icon;
    std::vector<std::wstring> keywords;
    std::vector<std::wstring> aliases;
    bool slashCommand = false;
    int score = 0;
};

struct PluginInfo
{
    std::wstring id;
    std::wstring name;
    std::wstring version;
    std::wstring description;
    std::wstring statusText;
    std::wstring lastError;
    std::wstring permissionSummary;
    bool valid = false;
    bool enabled = false;
    bool loaded = false;
    bool quarantined = false;
    int failureCount = 0;
    size_t commandCount = 0;
    size_t permissionCount = 0;
    size_t settingCount = 0;
};

struct PopupActionInfo
{
    std::wstring pluginId;
    std::wstring actionId;
    std::wstring title;
    std::wstring icon;
};

struct PluginSettingInfo
{
    std::wstring key;
    std::wstring type;
    std::wstring title;
    std::wstring defaultValue;
    std::wstring currentValue;
    int minValue = 0;
    int maxValue = 0;
    bool hasMin = false;
    bool hasMax = false;
};

class PluginManager
{
public:
    PluginManager(std::shared_ptr<EventBus> eventBus, std::shared_ptr<Logger> logger);
    ~PluginManager();

    void Initialize();
    void Shutdown();
    void Rescan();

    std::vector<PluginInfo> GetPlugins() const;
    std::wstring GetInstalledDirectory() const;
    bool InstallPackage(const std::wstring& packagePath, std::wstring& message);
    bool UninstallPlugin(const std::wstring& pluginId, std::wstring& message);
    bool SetPluginEnabled(const std::wstring& pluginId, bool enabled);
    std::vector<PluginCommandInfo> SearchCommands(const std::wstring& query) const;
    std::vector<PluginCommandInfo> SearchSlashCommands(const std::wstring& query) const;
    void RequestSearch(const std::wstring& query);
    std::vector<PluginCommandInfo> GetCachedSearchResults(const std::wstring& query) const;
    bool IsSearchRunning(const std::wstring& query) const;
    bool ExecuteCommand(const std::wstring& pluginId, const std::wstring& commandId, const std::wstring& query, std::wstring& message, HWND outputPanelHwnd = nullptr);
    bool ExecuteSlashCommand(const std::wstring& pluginId, const std::wstring& commandId, const std::wstring& rawInput, const std::vector<std::wstring>& selectedFiles, std::wstring& message, HWND outputPanelHwnd = nullptr);
    std::vector<PluginSettingInfo> GetPluginSettings(const std::wstring& pluginId) const;
    bool SetPluginSettingValue(const std::wstring& pluginId, const std::wstring& key, const std::wstring& value);

    std::vector<PopupActionInfo> GetPopupActions() const;

    void NotifyShortcutLaunched(const Model::ShortcutInfo& shortcut);
    void NotifyPopupShown();
    void NotifyPopupHidden();

private:
    struct LoadedPlugin;
    struct PluginRecord
    {
        PluginManifest manifest;
        PluginState state;
        bool valid = false;
        bool loaded = false;
        std::wstring statusText;
        std::wstring scanError;
        std::vector<PluginCommandInfo> commands;
        std::vector<PluginCommandInfo> slashCommands;
        std::vector<PopupActionInfo> popupActions;
    };

    struct HostContext
    {
        PluginManager* manager = nullptr;
        std::wstring pluginId;
    };

    struct LoadedPlugin
    {
        ~LoadedPlugin()
        {
            Destroy();
        }

        void Destroy() noexcept
        {
            if (instance)
            {
                if (onLoadSucceeded && instance->onUnload)
                {
                    try
                    {
                        instance->onUnload(instance->userData);
                    }
                    catch (...)
                    {
                    }
                }
                if (destroy)
                {
                    try
                    {
                        destroy(instance);
                    }
                    catch (...)
                    {
                    }
                }
                instance = nullptr;
            }
            if (module)
            {
                FreeLibrary(module);
                module = nullptr;
            }
        }

        HMODULE module = nullptr;
        WLPluginInstanceV1* instance = nullptr;
        WLDestroyPluginFn destroy = nullptr;
        WLHostApiV1 hostApi{};
        HostContext hostContext{};
        bool onLoadSucceeded = false;
    };

    struct SearchCollectContext
    {
        std::wstring pluginId;
        std::vector<PluginCommandInfo>* results = nullptr;
        size_t maxResults = 0;
        bool slashMode = false;
    };

    struct PluginDialogState
    {
        std::wstring pluginId;
        std::wstring message;
        bool cancelable = false;
        bool cancelled = false;
        uint64_t total = 0;
        uint64_t current = 0;
    };

    void ScanInstalled();
    bool LoadPlugin(const std::wstring& pluginId);
    void UnloadPlugin(const std::wstring& pluginId);
    void RunSearchWorker(std::wstring query, unsigned long long generation);
    bool RegisterRuntimeCommand(const std::wstring& pluginId, const WLCommandDescriptorV1* command);
    bool RegisterRuntimeSlashCommand(const std::wstring& pluginId, const WLSlashCommandDescriptorV1* command);
    bool RegisterPopupAction(const std::wstring& pluginId, const WLPopupActionDescriptorV1* action);
    void ClearPopupActionsForPlugin(const std::wstring& pluginId);
    void RegisterBuiltinSlashCommands();
    void RecordError(const std::wstring& pluginId, const std::wstring& stage, const std::wstring& error);
    void ClearCommandsForPlugin(const std::wstring& pluginId, bool keepManifestCommands);
    void ClearSlashCommandsForPlugin(const std::wstring& pluginId, bool keepManifestCommands);
    std::wstring PluginDataDirectory(const std::wstring& pluginId) const;
    bool HasPermission(const std::wstring& pluginId, const std::wstring& permission) const;
    bool IsSafePluginRelativePath(const std::wstring& path) const;
    bool IsSafeProcessWorkingDirectory(const std::wstring& pluginId, const std::wstring& workingDir) const;
    bool IsSafePluginConfigKey(const std::wstring& key) const;
    std::wstring PluginConfigPath(const std::wstring& pluginId) const;
    bool ReadPluginConfigValue(const std::wstring& pluginId, const std::wstring& key, const std::wstring& defaultValue, std::wstring& value) const;
    bool WritePluginConfigValue(const std::wstring& pluginId, const std::wstring& key, const std::wstring& value);
    uint64_t RegisterDialogState(const std::wstring& pluginId, const std::wstring& message, bool cancelable, uint64_t total = 0);
    bool UpdateDialogState(uint64_t handle, const std::wstring& message, uint64_t current = 0);
    bool RemoveDialogState(uint64_t handle);
    static bool CopyStringResult(const std::wstring& value, WLStringResultV1* outResult);
    static bool CopyStringBuffer(const std::wstring& value, wchar_t* buffer, uint32_t bufferLength, uint32_t* requiredLength);
    static std::wstring PermissionSummary(const std::vector<std::wstring>& permissions);

    static bool WL_CALL HostRegisterCommand(void* hostContext, const WLCommandDescriptorV1* command);
    static bool WL_CALL HostRegisterSlashCommand(void* hostContext, const WLSlashCommandDescriptorV1* command);
    static bool WL_CALL HostRegisterPopupAction(void* hostContext, const WLPopupActionDescriptorV1* action);
    static void WL_CALL HostLog(void* hostContext, const wchar_t* message);
    static bool WL_CALL HostGetDataDirectory(void* hostContext, wchar_t* buffer, uint32_t bufferLength, uint32_t* requiredLength);
    static bool WL_CALL HostGetAppVersion(void* hostContext, wchar_t* buffer, uint32_t bufferLength, uint32_t* requiredLength);
    static bool WL_CALL HostReadClipboardText(void* hostContext, WLStringResultV1* outText);
    static bool WL_CALL HostWriteClipboardText(void* hostContext, const wchar_t* text);
    static bool WL_CALL HostOpenUrl(void* hostContext, const wchar_t* url);
    static bool WL_CALL HostOpenFile(void* hostContext, const wchar_t* path);
    static bool WL_CALL HostReadTextFile(void* hostContext, const wchar_t* relativePath, WLStringResultV1* outText);
    static bool WL_CALL HostWriteTextFile(void* hostContext, const wchar_t* relativePath, const wchar_t* text);
    static bool WL_CALL HostGetPluginConfig(void* hostContext, const wchar_t* key, const wchar_t* defaultValue, WLStringResultV1* outValue);
    static bool WL_CALL HostSetPluginConfig(void* hostContext, const wchar_t* key, const wchar_t* value);
    static bool WL_CALL HostShowInputDialog(void* hostContext, const wchar_t* title, const wchar_t* prompt, const wchar_t* defaultText, WLStringResultV1* outText);
    static bool WL_CALL HostShowPasswordDialog(void* hostContext, const wchar_t* title, const wchar_t* prompt, WLStringResultV1* outText);
    static bool WL_CALL HostShowChooseDialog(void* hostContext, const wchar_t* title, const wchar_t* prompt, const wchar_t* options, WLStringResultV1* outSelected);
    static bool WL_CALL HostShowConfirmDialog(void* hostContext, const wchar_t* title, const wchar_t* message);
    static bool WL_CALL HostShowFilePicker(void* hostContext, const wchar_t* title, bool multiSelect, const wchar_t* filterPattern, bool onlyFolders, WLStringResultV1* outPaths);
    static bool WL_CALL HostShowNotificationToaster(void* hostContext, const wchar_t* title, const wchar_t* message, const wchar_t* type, uint32_t durationMs);
    static bool WL_CALL HostShowMessageBox(void* hostContext, const wchar_t* title, const wchar_t* message, const wchar_t* iconType, const wchar_t* buttons, WLStringResultV1* outResult);
    static bool WL_CALL HostShowBalloonTip(void* hostContext, const wchar_t* title, const wchar_t* message, const wchar_t* iconType, uint32_t durationMs);
    static bool WL_CALL HostShowLoadingDialog(void* hostContext, const wchar_t* message, bool cancelable, uint64_t* outHandle);
    static bool WL_CALL HostUpdateLoadingMessage(void* hostContext, uint64_t handle, const wchar_t* newMessage);
    static bool WL_CALL HostHideLoadingDialog(void* hostContext, uint64_t handle);
    static bool WL_CALL HostShowProgressDialog(void* hostContext, const wchar_t* title, const wchar_t* message, uint64_t total, bool cancelable, uint64_t* outHandle);
    static bool WL_CALL HostUpdateProgress(void* hostContext, uint64_t handle, uint64_t current, const wchar_t* statusMessage);
    static bool WL_CALL HostHideProgressDialog(void* hostContext, uint64_t handle);
    static bool WL_CALL HostIsDialogCancelled(void* hostContext, uint64_t handle, bool* outCancelled);
    static bool WL_CALL HostShowResultInPanel(void* hostContext, const wchar_t* title, const wchar_t* content, const wchar_t* contentType);
    static bool WL_CALL HostHttpRequest(void* hostContext, const wchar_t* method, const wchar_t* url, const wchar_t* headers, const wchar_t* body, uint32_t timeoutMs, WLStringResultV1* outResponse);
    static bool WL_CALL HostRunProcess(void* hostContext, const wchar_t* command, const wchar_t* workingDir, bool captureOutput, uint32_t timeoutMs, WLStringResultV1* outOutput, uint32_t* outExitCode);
    static bool WL_CALL HostGetScreenInfo(void* hostContext, uint32_t* outWidth, uint32_t* outHeight, uint32_t* outDpi, WLStringResultV1* outTheme);
    static bool WL_CALL HostAppendResultToPanel(void* hostContext, const wchar_t* text);
    static bool WL_CALL HostAddSearchResult(void* hostContext, const WLSearchResultV1* result);

    std::shared_ptr<EventBus> m_eventBus;
    std::shared_ptr<Logger> m_logger;
    std::unique_ptr<PluginStateStore> m_stateStore;
    std::map<std::wstring, PluginState> m_states;
    std::map<std::wstring, PluginRecord> m_plugins;
    std::map<std::wstring, std::shared_ptr<LoadedPlugin>> m_loadedPlugins;
    std::vector<PluginCommandInfo> m_builtinSlashCommands;
    mutable std::mutex m_loadedPluginsMutex;
    mutable std::mutex m_searchMutex;
    std::thread m_searchThread;
    std::wstring m_searchQuery;
    unsigned long long m_searchGeneration = 0;
    bool m_searchRunning = false;
    bool m_searchCacheReady = false;
    std::vector<PluginCommandInfo> m_cachedSearchResults;
    mutable std::mutex m_dialogMutex;
    std::map<uint64_t, PluginDialogState> m_dialogStates;
    uint64_t m_nextDialogHandle = 1;
    bool m_initialized = false;
    std::atomic_bool m_shuttingDown = false;
};
