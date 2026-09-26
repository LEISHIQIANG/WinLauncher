#include "PluginManager.h"
#include "CrashReporter.h"
#include "PluginDllLoader.h"
#include "UiDispatcher.h"
#include "PluginInstaller.h"
#include "PluginConfigStore.h"
#include "../Config/CommandPanelWindow.h"
#include "../Contracts/IUserInteractionService.h"
#include "../Services/ConfigPath.h"
#include "../Services/JsonImportHelper.h"
#include "../ToastWindow.h"
#include "../version.h"
#include <algorithm>
#include <cstddef>
#include <cwchar>
#include <cstring>
#include <commdlg.h>
#include <fstream>
#include <shlobj.h>
#include <shellapi.h>
#include <sstream>

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")

namespace
{
    thread_local HWND t_currentPluginOutputPanel = nullptr;

    struct ScopedPluginOutputPanel
    {
        explicit ScopedPluginOutputPanel(HWND hwnd)
            : previous(t_currentPluginOutputPanel)
        {
            t_currentPluginOutputPanel = hwnd;
        }

        ~ScopedPluginOutputPanel()
        {
            t_currentPluginOutputPanel = previous;
        }

        HWND previous = nullptr;
    };

    std::wstring JoinPath(const std::wstring& base, const std::wstring& name)
    {
        if (base.empty()) return name;
        if (base.back() == L'\\' || base.back() == L'/')
            return base + name;
        return base + L"\\" + name;
    }

    bool IsDirectory(const std::wstring& path)
    {
        DWORD attrs = GetFileAttributesW(path.c_str());
        return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }

    bool FileExists(const std::wstring& path)
    {
        DWORD attrs = GetFileAttributesW(path.c_str());
        return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
    }

    std::wstring ParentDirectory(const std::wstring& path)
    {
        size_t pos = path.find_last_of(L"\\/");
        if (pos == std::wstring::npos)
            return L"";
        return path.substr(0, pos);
    }

    std::wstring ResolveAssetIconPath(const std::wstring& fileName)
    {
        wchar_t exePath[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::wstring exeDir = ParentDirectory(exePath);
        std::vector<std::wstring> bases;
        if (!exeDir.empty())
        {
            bases.push_back(JoinPath(exeDir, L"assets"));
            bases.push_back(JoinPath(ParentDirectory(exeDir), L"assets"));
            bases.push_back(JoinPath(ParentDirectory(ParentDirectory(exeDir)), L"assets"));
        }

        wchar_t cwd[MAX_PATH]{};
        if (GetCurrentDirectoryW(MAX_PATH, cwd) > 0)
            bases.push_back(JoinPath(cwd, L"assets"));

        for (const auto& base : bases)
        {
            std::wstring candidate = JoinPath(base, fileName);
            if (FileExists(candidate))
                return candidate;
        }
        return JoinPath(L"assets", fileName);
    }

    std::wstring CurrentHostVersion()
    {
        return WINLAUNCHER_VERSION_WSTR;
    }

    std::wstring CopyWide(const wchar_t* text)
    {
        return text ? std::wstring(text) : std::wstring();
    }

    bool ContainsLower(const std::wstring& value, const std::wstring& queryLower)
    {
        if (queryLower.empty())
            return true;
        std::wstring lower = value;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](wchar_t c) {
            return (wchar_t)towlower(c);
        });
        return lower.find(queryLower) != std::wstring::npos;
    }

    std::wstring SlashTrimmedQuery(const std::wstring& query)
    {
        if (!query.empty() && query.front() == L'/')
            return query.substr(1);
        return query;
    }

    std::wstring SlashCommandNameFromInput(const std::wstring& query)
    {
        std::wstring normalized = SlashTrimmedQuery(query);
        size_t space = normalized.find_first_of(L" \t");
        if (space != std::wstring::npos)
            normalized = normalized.substr(0, space);
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](wchar_t c) {
            return (wchar_t)towlower(c);
        });
        return normalized;
    }

    std::wstring SlashSearchQueryFromInput(const std::wstring& query)
    {
        std::wstring normalized = SlashTrimmedQuery(query);
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](wchar_t c) {
            return (wchar_t)towlower(c);
        });
        return normalized;
    }

    std::wstring SlashArgsFromInput(const std::wstring& query)
    {
        std::wstring normalized = SlashTrimmedQuery(query);
        size_t space = normalized.find_first_of(L" \t");
        if (space == std::wstring::npos)
            return L"";
        size_t firstArg = normalized.find_first_not_of(L" \t", space);
        return firstArg == std::wstring::npos ? L"" : normalized.substr(firstArg);
    }

    std::wstring JoinStrings(const std::vector<std::wstring>& values)
    {
        std::wstring out;
        for (const auto& value : values)
        {
            if (!out.empty()) out += L", ";
            out += value;
        }
        return out;
    }

    std::wstring JoinLines(const std::vector<std::wstring>& values)
    {
        std::wstring out;
        for (const auto& value : values)
        {
            if (!out.empty()) out += L"\n";
            out += value;
        }
        return out;
    }


    bool PluginSupportsSearch(const WLPluginInstanceV1* instance)
    {
        return instance &&
            instance->size >= offsetof(WLPluginInstanceV1, search) + sizeof(instance->search) &&
            instance->search != nullptr;
    }

    bool PluginSupportsSlashExecution(const WLPluginInstanceV1* instance)
    {
        return instance &&
            instance->size >= offsetof(WLPluginInstanceV1, executeSlashCommand) + sizeof(instance->executeSlashCommand) &&
            instance->executeSlashCommand != nullptr;
    }

    bool ContainsAnyLower(const std::vector<std::wstring>& values, const std::wstring& queryLower)
    {
        for (const auto& value : values)
        {
            if (ContainsLower(value, queryLower))
                return true;
        }
        return false;
    }

    bool MatchesSlashCommand(const PluginCommandInfo& command, const std::wstring& queryLower)
    {
        return queryLower.empty() ||
            ContainsLower(command.commandName, queryLower) ||
            ContainsLower(command.title, queryLower) ||
            ContainsAnyLower(command.keywords, queryLower) ||
            ContainsAnyLower(command.aliases, queryLower);
    }

    std::vector<std::wstring> SplitLines(const std::wstring& value)
    {
        std::vector<std::wstring> lines;
        std::wstring current;
        for (wchar_t ch : value)
        {
            if (ch == L'\r')
                continue;
            if (ch == L'\n')
            {
                if (!current.empty())
                    lines.push_back(current);
                current.clear();
                continue;
            }
            current.push_back(ch);
        }
        if (!current.empty())
            lines.push_back(current);
        return lines;
    }

    std::wstring ToLowerCopy(std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) {
            return (wchar_t)towlower(c);
        });
        return value;
    }

    std::wstring Utf8ToWide(const std::string& value)
    {
        if (value.empty())
            return {};
        int len = MultiByteToWideChar(CP_UTF8, 0, value.data(), (int)value.size(), nullptr, 0);
        if (len <= 0)
            return {};
        std::wstring result(len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, value.data(), (int)value.size(), &result[0], len);
        return result;
    }

    std::wstring FormatMessageText(const wchar_t* title, const wchar_t* message)
    {
        std::wstring output;
        if (title && *title)
        {
            output += title;
            output += L"\n";
        }
        if (message)
            output += message;
        return output;
    }

    UINT MessageIconFlag(const std::wstring& iconType)
    {
        std::wstring icon = ToLowerCopy(iconType);
        if (icon == L"warning")
            return MB_ICONWARNING;
        if (icon == L"error")
            return MB_ICONERROR;
        if (icon == L"question")
            return MB_ICONQUESTION;
        if (icon == L"none")
            return 0;
        return MB_ICONINFORMATION;
    }

    UINT MessageButtonsFlag(const std::wstring& buttons)
    {
        std::wstring value = ToLowerCopy(buttons);
        if (value == L"okcancel")
            return MB_OKCANCEL;
        if (value == L"yesno")
            return MB_YESNO;
        if (value == L"yesnocancel")
            return MB_YESNOCANCEL;
        if (value == L"retrycancel")
            return MB_RETRYCANCEL;
        if (value == L"abortretryignore")
            return MB_ABORTRETRYIGNORE;
        return MB_OK;
    }

    std::wstring MessageResultText(int result)
    {
        switch (result)
        {
        case IDOK: return L"ok";
        case IDCANCEL: return L"cancel";
        case IDYES: return L"yes";
        case IDNO: return L"no";
        case IDRETRY: return L"retry";
        case IDABORT: return L"abort";
        case IDIGNORE: return L"ignore";
        default: return L"";
        }
    }

    std::wstring BuildFileDialogFilter(const wchar_t* filterPattern)
    {
        std::wstring pattern = (filterPattern && *filterPattern) ? filterPattern : L"*.*";
        std::wstring filter = L"Selected files (";
        filter += pattern;
        filter += L")";
        filter.push_back(L'\0');
        filter += pattern;
        filter.push_back(L'\0');
        filter += L"All files (*.*)";
        filter.push_back(L'\0');
        filter += L"*.*";
        filter.push_back(L'\0');
        filter.push_back(L'\0');
        return filter;
    }

    std::wstring ParseOpenFileResult(const std::vector<wchar_t>& buffer)
    {
        const wchar_t* base = buffer.data();
        if (!base || !*base)
            return L"";

        std::wstring first = base;
        const wchar_t* next = base + first.size() + 1;
        if (!*next)
            return first;

        std::wstring result;
        std::wstring directory = first;
        for (const wchar_t* item = next; *item; item += wcslen(item) + 1)
        {
            if (!result.empty())
                result += L"\n";
            result += JoinPath(directory, item);
        }
        return result;
    }

    bool PathStartsWithDirectory(const std::wstring& path, const std::wstring& directory)
    {
        std::wstring lhs = ToLowerCopy(path);
        std::wstring rhs = ToLowerCopy(directory);
        std::replace(lhs.begin(), lhs.end(), L'/', L'\\');
        std::replace(rhs.begin(), rhs.end(), L'/', L'\\');
        if (!rhs.empty() && rhs.back() != L'\\')
            rhs += L"\\";
        return lhs.rfind(rhs, 0) == 0;
    }

    DWORD WaitForProcessWithTimeout(HANDLE process, uint32_t timeoutMs)
    {
        DWORD timeout = timeoutMs == 0 ? INFINITE : timeoutMs;
        return WaitForSingleObject(process, timeout);
    }

    bool IsHttpMethodAllowed(const std::wstring& method)
    {
        std::wstring upper = method;
        std::transform(upper.begin(), upper.end(), upper.begin(), [](wchar_t c) { return (wchar_t)towupper(c); });
        static const wchar_t* kAllowed[] = { L"GET", L"POST", L"PUT", L"DELETE", L"PATCH", L"HEAD" };
        for (const wchar_t* allowed : kAllowed)
        {
            if (upper == allowed)
                return true;
        }
        return false;
    }
}

void PluginManager::SetCurrentPluginOutputPanel(HWND hwnd)
{
    t_currentPluginOutputPanel = hwnd;
}

HWND PluginManager::GetCurrentPluginOutputPanel()
{
    return t_currentPluginOutputPanel;
}

PluginManager::PluginManager(std::shared_ptr<EventBus> eventBus, std::shared_ptr<Logger> logger,
    std::shared_ptr<UiDispatcher> uiDispatcher,
    std::shared_ptr<BackgroundTaskService> backgroundTasks)
    : m_eventBus(std::move(eventBus))
    , m_logger(std::move(logger))
    , m_uiDispatcher(std::move(uiDispatcher))
    , m_backgroundTasks(std::move(backgroundTasks))
{
}

PluginManager::~PluginManager()
{
    Shutdown();
    AbandonRetiringPluginsForProcessExit();
}

void PluginManager::Initialize()
{
    if (m_initialized)
        return;

    RegisterBuiltinSlashCommands();
    ConfigPath::PrepareUserPluginInstalledDirectory();
    std::wstring stateDir = ConfigPath::PrepareUserPluginStateDirectory();
    m_stateStore = std::make_unique<PluginStateStore>(stateDir);
    m_stateStore->Load(m_states);

    ScanInstalled();

    for (const auto& [pluginId, record] : m_plugins)
    {
        if (record.valid && record.state.enabled && !record.state.quarantined)
            LoadPlugin(pluginId);
    }

    if (m_stateStore)
        m_stateStore->Save(m_states);

    m_initialized = true;
    LOG_INFO(m_logger, L"PluginManager initialized. plugins=%d", (int)m_plugins.size());
}

void PluginManager::Shutdown()
{
    if (m_shuttingDown)
        return;
    m_shuttingDown = true;

    {
        std::lock_guard<std::mutex> lock(m_searchMutex);
        m_searchGeneration++;
        m_searchQuery.clear();
        m_searchCacheReady = false;
        m_cachedSearchResults.clear();
    }
    m_searchTask.Cancel();

    std::vector<std::wstring> ids;
    {
        std::lock_guard<std::mutex> lock(m_loadedPluginsMutex);
        ids.reserve(m_loadedPlugins.size());
        for (const auto& [pluginId, loaded] : m_loadedPlugins)
            ids.push_back(pluginId);
    }
    for (const auto& pluginId : ids)
        UnloadPlugin(pluginId);

    if (m_stateStore)
        m_stateStore->Save(m_states);

    m_initialized = false;
    m_shuttingDown = false;
}

void PluginManager::RequestShutdown()
{
    m_shutdownRequested = true;
    {
        std::lock_guard<std::mutex> lock(m_searchMutex);
        ++m_searchGeneration;
        m_searchQuery.clear();
        m_searchCacheReady = false;
    }
    m_searchTask.Cancel();

    std::vector<std::shared_ptr<LoadedPlugin>> loaded;
    {
        std::lock_guard<std::mutex> lock(m_loadedPluginsMutex);
        for (const auto& [pluginId, item] : m_loadedPlugins)
            loaded.push_back(item);
    }
    for (const auto& item : loaded)
        item->RequestCooperativeShutdown();
}

bool PluginManager::Rescan(std::wstring* message)
{
    if (m_shutdownRequested)
    {
        if (message) *message = L"程序正在退出，无法重新加载插件";
        return false;
    }
    if (m_activeExecutions.load() != 0)
    {
        if (message) *message = L"仍有插件命令正在运行，请结束后重试";
        LOG_WORNING(m_logger, L"PluginManager::Rescan rejected: active executions=%u", m_activeExecutions.load());
        return false;
    }
    if (HasRetiringPlugins())
    {
        if (message) *message = L"插件仍在停止，请稍后重试";
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(m_searchMutex);
        if (m_searchRunning)
        {
            if (message) *message = L"插件搜索仍在运行，请稍后重试";
            return false;
        }
    }
    Shutdown();
    m_plugins.clear();
    m_initialized = false;
    Initialize();
    if (message) *message = L"已重新加载插件";
    return true;
}

std::vector<PluginInfo> PluginManager::GetPlugins() const
{
    std::vector<PluginInfo> result;
    result.reserve(m_plugins.size());
    for (const auto& [id, record] : m_plugins)
    {
        PluginInfo info;
        info.id = id;
        info.name = record.manifest.name.empty() ? id : record.manifest.name;
        info.version = record.manifest.version;
        info.description = record.manifest.description;
        info.statusText = record.statusText;
        info.lastError = record.state.lastError.empty() ? record.scanError : record.state.lastError;
        info.permissionSummary = PermissionSummary(record.manifest.permissions);
        info.valid = record.valid;
        info.enabled = record.state.enabled;
        info.loaded = record.loaded;
        info.quarantined = record.state.quarantined;
        info.failureCount = record.state.failureCount;
        info.commandCount = record.commands.size();
        info.permissionCount = record.manifest.permissions.size();
        info.settingCount = record.manifest.settings.size();
        result.push_back(std::move(info));
    }
    return result;
}

std::wstring PluginManager::GetInstalledDirectory() const
{
    return ConfigPath::GetUserPluginInstalledDirectory();
}

bool PluginManager::InstallPackage(const std::wstring& packagePath, std::wstring& message)
{
    if (m_shutdownRequested || m_activeExecutions.load() != 0)
    {
        message = m_shutdownRequested ? L"程序正在退出，无法安装插件" : L"仍有插件命令正在运行，请结束后再安装";
        return false;
    }
    if (HasRetiringPlugins())
    {
        message = L"插件仍在停止，请稍后再安装";
        return false;
    }
    PluginManifest packageManifest;
    if (!PluginInstaller::InspectPackage(packagePath, packageManifest, message))
        return false;

    if (m_plugins.find(packageManifest.id) != m_plugins.end())
    {
        UnloadPlugin(packageManifest.id);
        if (HasRetiringPlugins())
        {
            message = L"现有插件仍在停止，请稍后再安装";
            return false;
        }
    }

    std::wstring pluginId;
    if (!PluginInstaller::InstallPackage(packagePath, pluginId, message))
        return false;

    Rescan();
    message = L"插件已安装: " + pluginId;
    return true;
}

bool PluginManager::UninstallPlugin(const std::wstring& pluginId, std::wstring& message)
{
    if (m_shutdownRequested || m_activeExecutions.load() != 0)
    {
        message = m_shutdownRequested ? L"程序正在退出，无法卸载插件" : L"仍有插件命令正在运行，请结束后再卸载";
        return false;
    }
    if (HasRetiringPlugins())
    {
        message = L"插件仍在停止，请稍后再卸载";
        return false;
    }
    auto it = m_plugins.find(pluginId);
    if (it == m_plugins.end())
    {
        message = L"插件不存在";
        return false;
    }

    UnloadPlugin(pluginId);
    if (HasRetiringPlugins())
    {
        message = L"插件仍在停止，请稍后再卸载";
        return false;
    }
    if (!PluginInstaller::UninstallPlugin(it->second.manifest, message))
        return false;

    m_plugins.erase(pluginId);
    m_states.erase(pluginId);
    if (m_stateStore)
        m_stateStore->Save(m_states);

    message = L"插件已卸载: " + pluginId;
    return true;
}

bool PluginManager::SetPluginEnabled(const std::wstring& pluginId, bool enabled)
{
    if (m_shutdownRequested || m_activeExecutions.load() != 0)
        return false;
    if (HasRetiringPlugins())
        return false;
    auto it = m_plugins.find(pluginId);
    if (it == m_plugins.end())
        return false;

    PluginRecord& record = it->second;
    record.state.enabled = enabled;
    record.state.lastError.clear();
    record.state.quarantined = false;
    m_states[pluginId] = record.state;

    bool ok = true;
    if (enabled)
    {
        ok = LoadPlugin(pluginId);
    }
    else
    {
        UnloadPlugin(pluginId);
        if (!HasRetiringPlugins())
            record.statusText = record.valid ? L"已禁用" : L"不可用";
    }

    if (m_stateStore)
        m_stateStore->Save(m_states);
    return ok;
}

std::vector<PluginCommandInfo> PluginManager::SearchCommands(const std::wstring& query) const
{
    if (!query.empty() && query.front() == L'/')
        return {};

    std::wstring normalized = SlashTrimmedQuery(query);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](wchar_t c) {
        return (wchar_t)towlower(c);
    });

    std::vector<PluginCommandInfo> result;
    for (const auto& [id, record] : m_plugins)
    {
        if (!record.valid || !record.loaded || !record.state.enabled || record.state.quarantined)
            continue;

        for (const auto& command : record.commands)
        {
            if (ContainsLower(command.title, normalized) ||
                ContainsLower(command.commandId, normalized) ||
                ContainsLower(command.description, normalized))
            {
                result.push_back(command);
            }
        }
    }
    return result;
}

std::vector<PluginCommandInfo> PluginManager::SearchSlashCommands(const std::wstring& query) const
{
    if (query.empty() || query.front() != L'/')
        return {};

    std::wstring normalized = SlashCommandNameFromInput(query);
    std::vector<PluginCommandInfo> result;
    for (const auto& command : m_builtinSlashCommands)
    {
        if (MatchesSlashCommand(command, normalized))
        {
            result.push_back(command);
        }
    }

    for (const auto& [id, record] : m_plugins)
    {
        if (!record.valid || !record.loaded || !record.state.enabled || record.state.quarantined)
            continue;

        for (const auto& command : record.slashCommands)
        {
            if (MatchesSlashCommand(command, normalized))
            {
                result.push_back(command);
            }
        }
    }
    return result;
}

void PluginManager::RequestSearch(const std::wstring& query)
{
    if (m_shuttingDown || m_shutdownRequested)
        return;

    if (query.empty() || query.front() == L'/')
    {
        std::lock_guard<std::mutex> lock(m_searchMutex);
        m_searchGeneration++;
        m_searchQuery.clear();
        m_searchCacheReady = false;
        m_cachedSearchResults.clear();
        return;
    }

    unsigned long long generation = 0;
    {
        std::lock_guard<std::mutex> lock(m_searchMutex);
        if (m_searchRunning)
        {
            if (m_searchQuery != query)
            {
                m_searchQuery = query;
                m_searchGeneration++;
                m_searchCacheReady = false;
                m_cachedSearchResults.clear();
            }
            return;
        }

        if (m_searchQuery == query && m_searchCacheReady)
            return;

        m_searchQuery = query;
        m_searchCacheReady = false;
        m_cachedSearchResults.clear();
        generation = ++m_searchGeneration;
        m_searchRunning = true;
    }

    auto self = shared_from_this();
    m_searchTask = m_backgroundTasks ? m_backgroundTasks->Submit(L"plugin.search", BackgroundTaskService::Priority::Normal,
        [self, query, generation](const std::shared_ptr<BackgroundTaskService::CancellationToken>& cancellation) {
            if (!cancellation->IsCancellationRequested()) self->RunSearchWorker(query, generation);
        }) : BackgroundTaskService::TaskHandle{};
    if (!m_searchTask)
    {
        std::lock_guard<std::mutex> lock(m_searchMutex);
        m_searchRunning = false;
    }
}

std::vector<PluginCommandInfo> PluginManager::GetCachedSearchResults(const std::wstring& query) const
{
    std::lock_guard<std::mutex> lock(m_searchMutex);
    if (query.empty() || query != m_searchQuery || !m_searchCacheReady)
        return {};
    return m_cachedSearchResults;
}

bool PluginManager::IsSearchRunning(const std::wstring& query) const
{
    std::lock_guard<std::mutex> lock(m_searchMutex);
    return !query.empty() && query == m_searchQuery && m_searchRunning;
}

bool PluginManager::ExecuteCommand(const std::wstring& pluginId, const std::wstring& commandId, const std::wstring& query, std::wstring& message, HWND outputPanelHwnd)
{
    CrashReporter::RecordBreadcrumb(L"plugin.exec", pluginId + L":" + commandId);
    if (m_shutdownRequested)
    {
        message = L"程序正在退出，命令已取消";
        return false;
    }
    struct ActiveExecutionGuard
    {
        std::atomic_uint32_t& count;
        explicit ActiveExecutionGuard(std::atomic_uint32_t& value) : count(value) { ++count; }
        ~ActiveExecutionGuard() { --count; }
    } executionGuard(m_activeExecutions);

    std::shared_ptr<LoadedPlugin> loaded;
    {
        std::lock_guard<std::mutex> lock(m_loadedPluginsMutex);
        auto loadedIt = m_loadedPlugins.find(pluginId);
        if (loadedIt == m_loadedPlugins.end() || !loadedIt->second || !loadedIt->second->instance)
        {
            message = L"插件未加载";
            return false;
        }
        loaded = loadedIt->second;
    }

    WLPluginInstanceV1* instance = loaded->instance;
    if (!instance || !instance->executeCommand)
    {
        message = L"插件没有提供命令执行入口";
        return false;
    }

    std::vector<wchar_t> buffer(32768, L'\0');
    WLStringResultV1 result{};
    result.size = sizeof(result);
    result.buffer = buffer.data();
    result.bufferLength = (uint32_t)buffer.size();

    WLCommandContextV1 context{};
    context.size = sizeof(context);
    context.commandId = commandId.c_str();
    context.query = query.c_str();

    bool ok = false;
    ScopedPluginOutputPanel outputPanel(outputPanelHwnd);
    try
    {
        ok = instance->executeCommand(instance->userData, &context, &result);
    }
    catch (...)
    {
        message = L"插件命令执行入口抛出异常";
        RecordError(pluginId, L"execute", message);
        return false;
    }

    buffer.back() = L'\0';
    size_t validLen = wcsnlen_s(buffer.data(), buffer.size());
    message.assign(buffer.data(), validLen);
    if (!ok)
    {
        RecordError(pluginId, L"execute", message.empty() ? L"插件命令执行失败" : message);
    }
    return ok;
}

bool PluginManager::ExecuteSlashCommand(const std::wstring& pluginId, const std::wstring& commandId, const std::wstring& rawInput, const std::vector<std::wstring>& selectedFiles, std::wstring& message, HWND outputPanelHwnd)
{
    if (pluginId.empty())
    {
        if (commandId == L"winlauncher.about")
        {
            message = L"WinLauncher " + CurrentHostVersion();
            return true;
        }
        if (commandId == L"winlauncher.reload")
        {
            return Rescan(&message);
        }

        message = L"内置 / 命令需要主窗口处理";
        return true;
    }

    if (m_shutdownRequested)
    {
        message = L"程序正在退出，命令已取消";
        return false;
    }
    struct ActiveSlashExecutionGuard
    {
        std::atomic_uint32_t& count;
        explicit ActiveSlashExecutionGuard(std::atomic_uint32_t& value) : count(value) { ++count; }
        ~ActiveSlashExecutionGuard() { --count; }
    } executionGuard(m_activeExecutions);

    std::shared_ptr<LoadedPlugin> loaded;
    std::wstring commandName;
    {
        std::lock_guard<std::mutex> lock(m_loadedPluginsMutex);
        auto loadedIt = m_loadedPlugins.find(pluginId);
        if (loadedIt == m_loadedPlugins.end() || !loadedIt->second || !loadedIt->second->instance)
        {
            message = L"插件未加载";
            return false;
        }
        loaded = loadedIt->second;
        auto recordIt = m_plugins.find(pluginId);
        if (recordIt != m_plugins.end())
        {
            for (const auto& command : recordIt->second.slashCommands)
            {
                if (command.commandId == commandId)
                {
                    commandName = command.commandName;
                    break;
                }
            }
        }
    }

    WLPluginInstanceV1* instance = loaded->instance;
    if (!instance || !PluginSupportsSlashExecution(instance))
    {
        message = L"插件没有提供 / 命令执行入口";
        return false;
    }

    std::wstring args = SlashArgsFromInput(rawInput);
    std::wstring selectedFilesText = JoinLines(selectedFiles);
    std::vector<wchar_t> buffer(32768, L'\0');
    WLStringResultV1 result{};
    result.size = sizeof(result);
    result.buffer = buffer.data();
    result.bufferLength = (uint32_t)buffer.size();

    WLSlashCommandContextV1 context{};
    context.size = sizeof(context);
    context.commandId = commandId.c_str();
    context.command = commandName.c_str();
    context.args = args.c_str();
    context.rawInput = rawInput.c_str();
    context.selectedFiles = selectedFilesText.c_str();

    bool ok = false;
    ScopedPluginOutputPanel outputPanel(outputPanelHwnd);
    try
    {
        ok = instance->executeSlashCommand(instance->userData, &context, &result);
    }
    catch (...)
    {
        message = L"/ 命令执行入口抛出异常";
        RecordError(pluginId, L"slash.execute", message);
        return false;
    }

    message = buffer.data();
    if (!ok)
    {
        RecordError(pluginId, L"slash.execute", message.empty() ? L"/ 命令执行失败" : message);
    }
    return ok;
}

std::vector<PluginSettingInfo> PluginManager::GetPluginSettings(const std::wstring& pluginId) const
{
    std::vector<PluginSettingInfo> result;
    auto it = m_plugins.find(pluginId);
    if (it == m_plugins.end() || !it->second.valid)
        return result;

    result.reserve(it->second.manifest.settings.size());
    for (const auto& manifestSetting : it->second.manifest.settings)
    {
        PluginSettingInfo info;
        info.key = manifestSetting.key;
        info.type = manifestSetting.type;
        info.title = manifestSetting.title;
        info.defaultValue = manifestSetting.defaultValue;
        info.minValue = manifestSetting.minValue;
        info.maxValue = manifestSetting.maxValue;
        info.hasMin = manifestSetting.hasMin;
        info.hasMax = manifestSetting.hasMax;
        if (!ReadPluginConfigValue(pluginId, info.key, info.defaultValue, info.currentValue))
            info.currentValue = info.defaultValue;
        result.push_back(std::move(info));
    }
    return result;
}

bool PluginManager::SetPluginSettingValue(const std::wstring& pluginId, const std::wstring& key, const std::wstring& value)
{
    auto it = m_plugins.find(pluginId);
    if (it == m_plugins.end() || !it->second.valid)
        return false;

    auto setting = std::find_if(it->second.manifest.settings.begin(), it->second.manifest.settings.end(), [&](const PluginSettingManifest& item) {
        return item.key == key;
    });
    if (setting == it->second.manifest.settings.end())
        return false;

    std::wstring normalized = value;
    if (setting->type == L"boolean")
    {
        if (normalized != L"true" && normalized != L"false")
            return false;
    }
    else if (setting->type == L"integer")
    {
        wchar_t* end = nullptr;
        long parsed = wcstol(normalized.c_str(), &end, 10);
        if (!end || *end != L'\0')
            return false;
        if (setting->hasMin && parsed < setting->minValue)
            parsed = setting->minValue;
        if (setting->hasMax && parsed > setting->maxValue)
            parsed = setting->maxValue;
        normalized = std::to_wstring(parsed);
    }

    return WritePluginConfigValue(pluginId, key, normalized);
}

void PluginManager::NotifyShortcutLaunched(const Model::ShortcutInfo&)
{
}

void PluginManager::NotifyPopupShown()
{
    std::vector<std::shared_ptr<LoadedPlugin>> snapshot;
    {
        std::lock_guard<std::mutex> lock(m_loadedPluginsMutex);
        snapshot.reserve(m_loadedPlugins.size());
        for (const auto& [pluginId, loaded] : m_loadedPlugins)
        {
            if (loaded)
                snapshot.push_back(loaded);
        }
    }

    for (const auto& loaded : snapshot)
    {
        if (loaded->instance && loaded->instance->onPopupShown)
        {
            try
            {
                loaded->instance->onPopupShown(loaded->instance->userData);
            }
            catch (...)
            {
            }
        }
    }
}

void PluginManager::NotifyPopupHidden()
{
    std::vector<std::shared_ptr<LoadedPlugin>> snapshot;
    {
        std::lock_guard<std::mutex> lock(m_loadedPluginsMutex);
        snapshot.reserve(m_loadedPlugins.size());
        for (const auto& [pluginId, loaded] : m_loadedPlugins)
        {
            if (loaded)
                snapshot.push_back(loaded);
        }
    }

    for (const auto& loaded : snapshot)
    {
        if (loaded->instance && loaded->instance->onPopupHidden)
        {
            try
            {
                loaded->instance->onPopupHidden(loaded->instance->userData);
            }
            catch (...)
            {
            }
        }
    }
}

void PluginManager::ScanInstalled()
{
    std::wstring installedDir = ConfigPath::PrepareUserPluginInstalledDirectory();
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW((installedDir + L"\\*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE)
        return;

    do
    {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
            continue;
        if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0)
            continue;

        std::wstring pluginDir = JoinPath(installedDir, data.cFileName);
        std::wstring manifestPath = JoinPath(pluginDir, L"plugin.json");
        if (!IsDirectory(pluginDir))
            continue;

        PluginRecord record;
        std::wstring error;
        if (PluginManifestReader::LoadFromFile(manifestPath, record.manifest, error))
        {
            record.valid = true;
            record.state = m_states[record.manifest.id];
            record.statusText = record.state.enabled ? L"待加载" : L"已禁用";
            for (const auto& manifestCommand : record.manifest.commands)
            {
                PluginCommandInfo command;
                command.pluginId = record.manifest.id;
                command.commandId = manifestCommand.id;
                command.title = manifestCommand.title;
                command.description = manifestCommand.description;
                record.commands.push_back(std::move(command));
            }
            for (const auto& manifestCommand : record.manifest.slashCommands)
            {
                PluginCommandInfo command;
                command.pluginId = record.manifest.id;
                command.commandId = manifestCommand.id;
                command.commandName = manifestCommand.command;
                command.title = manifestCommand.title;
                command.description = manifestCommand.description;
                command.usage = manifestCommand.usage;
                command.icon = manifestCommand.icon.empty() ? L"" : JoinPath(record.manifest.rootDirectory, manifestCommand.icon);
                command.keywords = manifestCommand.keywords;
                command.aliases = manifestCommand.aliases;
                command.slashCommand = true;
                record.slashCommands.push_back(std::move(command));
            }
            m_plugins[record.manifest.id] = record;
        }
        else
        {
            record.valid = false;
            record.manifest.id = data.cFileName;
            record.manifest.name = data.cFileName;
            record.manifest.rootDirectory = pluginDir;
            record.manifest.manifestPath = manifestPath;
            record.scanError = error;
            record.state = m_states[record.manifest.id];
            record.state.lastError = error;
            record.statusText = L"清单无效";
            m_states[record.manifest.id] = record.state;
            m_plugins[record.manifest.id] = record;
            RecordError(record.manifest.id, L"manifest", error);
        }
    } while (FindNextFileW(find, &data));

    FindClose(find);
}

void PluginManager::RunSearchWorker(std::wstring query, unsigned long long generation)
{
    std::vector<PluginCommandInfo> results;
    const bool slashMode = !query.empty() && query.front() == L'/';

    WLSearchRequestV1 request{};
    request.size = sizeof(request);
    request.query = query.c_str();
    request.slashMode = slashMode;
    request.maxResults = 24;

    std::vector<std::pair<std::wstring, std::shared_ptr<LoadedPlugin>>> snapshot;
    {
        std::lock_guard<std::mutex> lock(m_loadedPluginsMutex);
        for (const auto& [pluginId, loaded] : m_loadedPlugins)
        {
            if (m_shuttingDown || m_shutdownRequested)
                break;
            if (!loaded || !PluginSupportsSearch(loaded->instance))
                continue;

            auto recordIt = m_plugins.find(pluginId);
            if (recordIt == m_plugins.end() ||
                !recordIt->second.valid ||
                !recordIt->second.loaded ||
                !recordIt->second.state.enabled ||
                recordIt->second.state.quarantined)
            {
                continue;
            }

            snapshot.emplace_back(pluginId, loaded);
        }
    }

    for (const auto& [pluginId, loaded] : snapshot)
    {
        if (m_shuttingDown || m_shutdownRequested || results.size() >= request.maxResults)
            break;
        if (!loaded || !PluginSupportsSearch(loaded->instance))
            continue;

        SearchCollectContext context{};
        context.pluginId = pluginId;
        context.results = &results;
        context.maxResults = request.maxResults;
        context.slashMode = slashMode;

        WLSearchResponseV1 response{};
        response.size = sizeof(response);
        response.hostContext = &context;
        response.addResult = &PluginManager::HostAddSearchResult;

        try
        {
            loaded->instance->search(loaded->instance->userData, &request, &response);
        }
        catch (...)
        {
            RecordError(pluginId, L"search", L"插件搜索入口抛出异常");
        }
    }

    std::stable_sort(results.begin(), results.end(), [](const PluginCommandInfo& left, const PluginCommandInfo& right) {
        return left.score > right.score;
    });

    {
        std::lock_guard<std::mutex> lock(m_searchMutex);
        if (generation == m_searchGeneration && query == m_searchQuery && !m_shuttingDown && !m_shutdownRequested)
        {
            m_cachedSearchResults = std::move(results);
            m_searchCacheReady = true;
        }
        m_searchRunning = false;
    }
}

bool PluginManager::LoadPlugin(const std::wstring& pluginId)
{
    auto recordIt = m_plugins.find(pluginId);
    if (recordIt == m_plugins.end())
        return false;

    PluginRecord& record = recordIt->second;
    if (!record.valid)
    {
        record.statusText = L"清单无效";
        return false;
    }
    if (record.state.quarantined)
    {
        record.statusText = L"已隔离";
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(m_loadedPluginsMutex);
        if (m_loadedPlugins.find(pluginId) != m_loadedPlugins.end())
        {
            record.loaded = true;
            record.statusText = L"已加载";
            return true;
        }
    }

    auto loaded = std::make_shared<LoadedPlugin>();
    loaded->hostContext.manager = this;
    loaded->hostContext.pluginId = pluginId;
    loaded->hostApi.size = sizeof(WLHostApiV1);
    loaded->hostApi.hostContext = &loaded->hostContext;
    loaded->hostApi.registerCommand = &PluginManager::HostRegisterCommand;
    loaded->hostApi.registerSlashCommand = &PluginManager::HostRegisterSlashCommand;
    loaded->hostApi.log = &PluginManager::HostLog;
    loaded->hostApi.getDataDirectory = &PluginManager::HostGetDataDirectory;
    loaded->hostApi.getAppVersion = &PluginManager::HostGetAppVersion;
    loaded->hostApi.readClipboardText = &PluginManager::HostReadClipboardText;
    loaded->hostApi.writeClipboardText = &PluginManager::HostWriteClipboardText;
    loaded->hostApi.openUrl = &PluginManager::HostOpenUrl;
    loaded->hostApi.openFile = &PluginManager::HostOpenFile;
    loaded->hostApi.readTextFile = &PluginManager::HostReadTextFile;
    loaded->hostApi.writeTextFile = &PluginManager::HostWriteTextFile;
    loaded->hostApi.getPluginConfig = &PluginManager::HostGetPluginConfig;
    loaded->hostApi.setPluginConfig = &PluginManager::HostSetPluginConfig;
    loaded->hostApi.showInputDialog = &PluginManager::HostShowInputDialog;
    loaded->hostApi.showPasswordDialog = &PluginManager::HostShowPasswordDialog;
    loaded->hostApi.showChooseDialog = &PluginManager::HostShowChooseDialog;
    loaded->hostApi.showConfirmDialog = &PluginManager::HostShowConfirmDialog;
    loaded->hostApi.showFilePicker = &PluginManager::HostShowFilePicker;
    loaded->hostApi.showNotificationToaster = &PluginManager::HostShowNotificationToaster;
    loaded->hostApi.showMessageBox = &PluginManager::HostShowMessageBox;
    loaded->hostApi.showBalloonTip = &PluginManager::HostShowBalloonTip;
    loaded->hostApi.showLoadingDialog = &PluginManager::HostShowLoadingDialog;
    loaded->hostApi.updateLoadingMessage = &PluginManager::HostUpdateLoadingMessage;
    loaded->hostApi.hideLoadingDialog = &PluginManager::HostHideLoadingDialog;
    loaded->hostApi.showProgressDialog = &PluginManager::HostShowProgressDialog;
    loaded->hostApi.updateProgress = &PluginManager::HostUpdateProgress;
    loaded->hostApi.hideProgressDialog = &PluginManager::HostHideProgressDialog;
    loaded->hostApi.isDialogCancelled = &PluginManager::HostIsDialogCancelled;
    loaded->hostApi.showResultInPanel = &PluginManager::HostShowResultInPanel;
    loaded->hostApi.httpRequest = &PluginManager::HostHttpRequest;
    loaded->hostApi.runProcess = &PluginManager::HostRunProcess;
    loaded->hostApi.getScreenInfo = &PluginManager::HostGetScreenInfo;
    loaded->hostApi.registerPopupAction = &PluginManager::HostRegisterPopupAction;
    loaded->hostApi.appendResultToPanel = &PluginManager::HostAppendResultToPanel;

    HMODULE module = LoadLibraryW(record.manifest.entryPath.c_str());
    if (!module)
    {
        RecordError(pluginId, L"load", L"无法加载插件 DLL，Win32 错误码: " + std::to_wstring(GetLastError()));
        record.statusText = L"加载失败";
        return false;
    }
    loaded->module = module;

    auto getAbi = reinterpret_cast<WLGetAbiVersionFn>(GetProcAddress(module, "WinLauncherPlugin_GetAbiVersion"));
    auto create = reinterpret_cast<WLCreatePluginFn>(GetProcAddress(module, "WinLauncherPlugin_Create"));
    auto destroy = reinterpret_cast<WLDestroyPluginFn>(GetProcAddress(module, "WinLauncherPlugin_Destroy"));
    if (!getAbi || !create || !destroy)
    {
        RecordError(pluginId, L"abi", L"插件缺少必需导出函数");
        record.statusText = L"ABI 无效";
        return false;
    }
    loaded->destroy = destroy;

    uint32_t abi = getAbi();
    if (abi != WINLAUNCHER_PLUGIN_ABI_VERSION)
    {
        RecordError(pluginId, L"abi", L"插件 ABI 版本不兼容");
        record.statusText = L"ABI 不兼容";
        return false;
    }

    WLPluginInstanceV1* instance = nullptr;
    if (!create(&loaded->hostApi, &instance) || !instance)
    {
        RecordError(pluginId, L"create", L"插件实例创建失败");
        record.statusText = L"创建失败";
        return false;
    }

    loaded->instance = instance;
    if (instance->onLoad && !instance->onLoad(instance->userData))
    {
        RecordError(pluginId, L"load", L"插件 onLoad 返回失败");
        record.statusText = L"加载失败";
        return false;
    }
    loaded->onLoadSucceeded = true;

    record.loaded = true;
    record.statusText = L"已加载";
    record.state.lastError.clear();
    m_states[pluginId] = record.state;
    {
        std::lock_guard<std::mutex> lock(m_loadedPluginsMutex);
        m_loadedPlugins[pluginId] = std::move(loaded);
    }
    LOG_INFO(m_logger, L"Plugin loaded: %s", pluginId.c_str());
    return true;
}

void PluginManager::UnloadPlugin(const std::wstring& pluginId)
{
    std::shared_ptr<LoadedPlugin> loaded;
    {
        std::lock_guard<std::mutex> lock(m_loadedPluginsMutex);
        auto loadedIt = m_loadedPlugins.find(pluginId);
        if (loadedIt == m_loadedPlugins.end())
            return;
        loaded = loadedIt->second;
        m_loadedPlugins.erase(loadedIt);
    }

    auto recordIt = m_plugins.find(pluginId);
    if (recordIt != m_plugins.end())
    {
        recordIt->second.loaded = false;
        ClearCommandsForPlugin(pluginId, true);
        ClearSlashCommandsForPlugin(pluginId, true);
        ClearPopupActionsForPlugin(pluginId);
    }

    if (loaded->SupportsCooperativeShutdown())
    {
        loaded->RequestCooperativeShutdown();
        if (!loaded->IsCooperativeShutdownComplete())
        {
            if (recordIt != m_plugins.end())
                recordIt->second.statusText = L"正在停止";
            {
                std::lock_guard<std::mutex> lock(m_loadedPluginsMutex);
                m_retiringPlugins[pluginId] = loaded;
            }
            ScheduleRetirement(pluginId, loaded);
            LOG_INFO(m_logger, L"Plugin retirement scheduled: %s", pluginId.c_str());
            return;
        }
    }

    if (recordIt != m_plugins.end())
        recordIt->second.statusText = recordIt->second.state.enabled ? L"未加载" : L"已禁用";
    loaded->Destroy();
    LOG_INFO(m_logger, L"Plugin unloaded: %s", pluginId.c_str());
}

bool PluginManager::HasRetiringPlugins() const
{
    std::lock_guard<std::mutex> lock(m_loadedPluginsMutex);
    return !m_retiringPlugins.empty();
}

void PluginManager::ScheduleRetirement(const std::wstring& pluginId, const std::shared_ptr<LoadedPlugin>& loaded)
{
    // During process teardown PluginManager may already be in its destructor,
    // where shared_from_this() would throw. Keeping the module resident is
    // safer than attempting to unload code with callbacks still in flight.
    auto self = weak_from_this().lock();
    if (!self)
    {
        LOG_WORNING(m_logger, L"Plugin retirement deferred to process exit: %s", pluginId.c_str());
        return;
    }
    auto waitForShutdown = [self, pluginId, loaded](const std::shared_ptr<BackgroundTaskService::CancellationToken>& cancellation) {
        while (!loaded->IsCooperativeShutdownComplete())
        {
            if (cancellation->IsCancellationRequested())
            {
                self->AbandonRetiringPluginsForProcessExit();
                return;
            }
            Sleep(25);
        }
        self->FinalizeRetirement(pluginId, loaded);
    };

    if (m_backgroundTasks)
    {
        if (m_backgroundTasks->Submit(L"plugin.retire." + pluginId, BackgroundTaskService::Priority::Normal, waitForShutdown))
            return;
    }

    // A host without an async runtime keeps the DLL loaded. This is safer than
    // synchronously blocking the UI or unloading code with outstanding work.
    LOG_WORNING(m_logger, L"Plugin retirement requires the background task service: %s", pluginId.c_str());
}

void PluginManager::FinalizeRetirement(const std::wstring& pluginId, const std::shared_ptr<LoadedPlugin>& loaded)
{
    {
        std::lock_guard<std::mutex> lock(m_loadedPluginsMutex);
        const auto it = m_retiringPlugins.find(pluginId);
        if (it == m_retiringPlugins.end() || it->second != loaded)
            return;
        m_retiringPlugins.erase(it);
    }

    const auto recordIt = m_plugins.find(pluginId);
    if (recordIt != m_plugins.end())
        recordIt->second.statusText = recordIt->second.state.enabled ? L"未加载" : L"已禁用";
    loaded->Destroy();
    LOG_INFO(m_logger, L"Plugin retirement completed: %s", pluginId.c_str());
}

void PluginManager::AbandonRetiringPluginsForProcessExit() noexcept
{
    // If Windows is tearing the host down before a cooperative callback has
    // returned, deliberately retain the module. Calling FreeLibrary here would
    // leave the callback executing code that no longer exists.
    static auto* retained = new std::vector<std::shared_ptr<LoadedPlugin>>();
    std::lock_guard<std::mutex> lock(m_loadedPluginsMutex);
    for (auto& [pluginId, loaded] : m_retiringPlugins)
        retained->push_back(std::move(loaded));
    m_retiringPlugins.clear();
}

bool PluginManager::RegisterRuntimeCommand(const std::wstring& pluginId, const WLCommandDescriptorV1* command)
{
    if (!command || command->size < sizeof(WLCommandDescriptorV1) || !command->id || !command->title)
        return false;

    auto it = m_plugins.find(pluginId);
    if (it == m_plugins.end())
        return false;

    PluginCommandInfo info;
    info.pluginId = pluginId;
    info.commandId = CopyWide(command->id);
    info.title = CopyWide(command->title);
    info.description = CopyWide(command->description);
    if (info.commandId.empty() || info.title.empty())
        return false;
    if (info.commandId.rfind(pluginId + L".", 0) != 0)
        return false;

    auto& commands = it->second.commands;
    auto duplicate = std::find_if(commands.begin(), commands.end(), [&](const PluginCommandInfo& existing) {
        return existing.commandId == info.commandId;
    });
    if (duplicate != commands.end())
        *duplicate = std::move(info);
    else
        commands.push_back(std::move(info));
    return true;
}

bool PluginManager::RegisterRuntimeSlashCommand(const std::wstring& pluginId, const WLSlashCommandDescriptorV1* command)
{
    if (!command || command->size < sizeof(WLSlashCommandDescriptorV1) || !command->id || !command->command || !command->title)
        return false;

    auto it = m_plugins.find(pluginId);
    if (it == m_plugins.end())
        return false;

    PluginCommandInfo info;
    info.pluginId = pluginId;
    info.commandId = CopyWide(command->id);
    info.commandName = CopyWide(command->command);
    info.title = CopyWide(command->title);
    info.description = CopyWide(command->description);
    info.usage = CopyWide(command->usage);
    info.icon = CopyWide(command->icon);
    info.slashCommand = true;
    if (info.commandId.empty() || info.commandName.empty() || info.title.empty())
        return false;
    if (info.commandId.rfind(pluginId + L".", 0) != 0)
        return false;
    if (!info.icon.empty())
        info.icon = JoinPath(it->second.manifest.rootDirectory, info.icon);

    auto& commands = it->second.slashCommands;
    auto duplicate = std::find_if(commands.begin(), commands.end(), [&](const PluginCommandInfo& existing) {
        return existing.commandId == info.commandId;
    });
    if (duplicate != commands.end())
        *duplicate = std::move(info);
    else
        commands.push_back(std::move(info));
    return true;
}

bool PluginManager::RegisterPopupAction(const std::wstring& pluginId, const WLPopupActionDescriptorV1* action)
{
    if (!action || action->size < sizeof(WLPopupActionDescriptorV1) || !action->id || !action->title)
        return false;

    auto it = m_plugins.find(pluginId);
    if (it == m_plugins.end())
        return false;

    PopupActionInfo info;
    info.pluginId = pluginId;
    info.actionId = CopyWide(action->id);
    info.title = CopyWide(action->title);
    info.icon = CopyWide(action->icon);
    if (info.actionId.empty() || info.title.empty())
        return false;
    if (info.actionId.rfind(pluginId + L".", 0) != 0)
        return false;
    if (!info.icon.empty())
        info.icon = JoinPath(it->second.manifest.rootDirectory, info.icon);

    auto& actions = it->second.popupActions;
    auto duplicate2 = std::find_if(actions.begin(), actions.end(), [&](const PopupActionInfo& existing) {
        return existing.actionId == info.actionId;
    });
    if (duplicate2 != actions.end())
        *duplicate2 = std::move(info);
    else
        actions.push_back(std::move(info));
    return true;
}

std::vector<PopupActionInfo> PluginManager::GetPopupActions() const
{
    std::vector<PopupActionInfo> result;
    for (const auto& [id, record] : m_plugins)
    {
        if (!record.valid || !record.loaded || !record.state.enabled || record.state.quarantined)
            continue;
        for (const auto& action : record.popupActions)
        {
            result.push_back(action);
        }
    }
    return result;
}

void PluginManager::ClearPopupActionsForPlugin(const std::wstring& pluginId)
{
    auto it = m_plugins.find(pluginId);
    if (it == m_plugins.end())
        return;
    it->second.popupActions.clear();
}

void PluginManager::RegisterBuiltinSlashCommands()
{
    if (!m_builtinSlashCommands.empty())
        return;

    auto add = [&](const wchar_t* id, const wchar_t* command, const wchar_t* title, const wchar_t* description, const wchar_t* usage, const wchar_t* iconFile, std::vector<std::wstring> aliases = {}, std::vector<std::wstring> keywords = {}) {
        PluginCommandInfo info;
        info.commandId = id;
        info.commandName = command;
        info.title = title;
        info.description = description;
        info.usage = usage;
        info.icon = ResolveAssetIconPath(iconFile);
        info.aliases = std::move(aliases);
        info.keywords = std::move(keywords);
        info.slashCommand = true;
        m_builtinSlashCommands.push_back(std::move(info));
    };

    add(L"winlauncher.settings", L"settings", L"设置", L"打开 WinLauncher 设置窗口", L"/settings", L"slash_settings.ico", { L"config" }, { L"preferences", L"options" });
    add(L"winlauncher.reload", L"reload", L"重新加载", L"重新扫描和加载插件", L"/reload", L"slash_reload.ico", {}, { L"refresh", L"rescan" });
    add(L"winlauncher.about", L"about", L"关于", L"显示 WinLauncher 版本信息", L"/about", L"slash_about.ico", {}, { L"version" });
}

void PluginManager::RecordError(const std::wstring& pluginId, const std::wstring& stage, const std::wstring& error)
{
    auto& state = m_states[pluginId];
    state.failureCount++;
    state.lastError = error;
    if (state.failureCount >= 3)
        state.quarantined = true;

    auto recordIt = m_plugins.find(pluginId);
    if (recordIt != m_plugins.end())
    {
        recordIt->second.state = state;
        recordIt->second.statusText = state.quarantined ? L"已隔离" : L"错误";
    }

    if (m_stateStore)
    {
        m_stateStore->AppendError(pluginId, stage, error);
        m_stateStore->Save(m_states);
    }
    LOG_ERROR(m_logger, L"Plugin error: id=%s stage=%s error=%s", pluginId.c_str(), stage.c_str(), error.c_str());
}

void PluginManager::ClearCommandsForPlugin(const std::wstring& pluginId, bool keepManifestCommands)
{
    auto it = m_plugins.find(pluginId);
    if (it == m_plugins.end())
        return;

    std::vector<PluginCommandInfo> kept;
    if (keepManifestCommands)
    {
        for (const auto& manifestCommand : it->second.manifest.commands)
        {
            PluginCommandInfo command;
            command.pluginId = pluginId;
            command.commandId = manifestCommand.id;
            command.title = manifestCommand.title;
            command.description = manifestCommand.description;
            kept.push_back(std::move(command));
        }
    }
    it->second.commands = std::move(kept);
}

void PluginManager::ClearSlashCommandsForPlugin(const std::wstring& pluginId, bool keepManifestCommands)
{
    auto it = m_plugins.find(pluginId);
    if (it == m_plugins.end())
        return;

    std::vector<PluginCommandInfo> kept;
    if (keepManifestCommands)
    {
        for (const auto& manifestCommand : it->second.manifest.slashCommands)
        {
            PluginCommandInfo command;
            command.pluginId = pluginId;
            command.commandId = manifestCommand.id;
            command.commandName = manifestCommand.command;
            command.title = manifestCommand.title;
            command.description = manifestCommand.description;
            command.usage = manifestCommand.usage;
            command.icon = manifestCommand.icon.empty() ? L"" : JoinPath(it->second.manifest.rootDirectory, manifestCommand.icon);
            command.keywords = manifestCommand.keywords;
            command.aliases = manifestCommand.aliases;
            command.slashCommand = true;
            kept.push_back(std::move(command));
        }
    }
    it->second.slashCommands = std::move(kept);
}


std::wstring PluginManager::PluginDataDirectory(const std::wstring& pluginId) const
{
    auto it = m_plugins.find(pluginId);
    if (it == m_plugins.end())
        return L"";
    std::wstring dir = JoinPath(it->second.manifest.rootDirectory, L"data");
    ConfigPath::EnsureDirectoryExists(dir);
    return dir;
}

bool PluginManager::HasPermission(const std::wstring& pluginId, const std::wstring& permission) const
{
    auto it = m_plugins.find(pluginId);
    if (it == m_plugins.end())
        return false;
    if (permission == L"app.info" ||
        permission == L"log.write" ||
        permission == L"plugin.config.read" ||
        permission == L"ui.input" ||
        permission == L"ui.filepick" ||
        permission == L"ui.notify")
    {
        return true;
    }
    const auto& permissions = it->second.manifest.permissions;
    return std::find(permissions.begin(), permissions.end(), permission) != permissions.end();
}

bool PluginManager::IsSafePluginRelativePath(const std::wstring& path) const
{
    if (path.empty()) return false;
    if (path.find(L":") != std::wstring::npos) return false;
    if (path.rfind(L"\\\\", 0) == 0) return false;
    if (path.front() == L'\\' || path.front() == L'/') return false;
    if (path.find(L"..") != std::wstring::npos) return false;
    return true;
}

bool PluginManager::IsSafeProcessWorkingDirectory(const std::wstring& pluginId, const std::wstring& workingDir) const
{
    if (workingDir.empty())
        return true;
    DWORD attrs = GetFileAttributesW(workingDir.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0)
        return false;
    return PathStartsWithDirectory(workingDir, PluginDataDirectory(pluginId));
}

bool PluginManager::IsSafePluginConfigKey(const std::wstring& key) const
{
    return PluginConfigStore::IsSafeKey(key);
}

std::wstring PluginManager::PluginConfigPath(const std::wstring& pluginId) const
{
    return PluginConfigStore::GetConfigPath(PluginDataDirectory(pluginId));
}

bool PluginManager::ReadPluginConfigValue(const std::wstring& pluginId, const std::wstring& key, const std::wstring& defaultValue, std::wstring& value) const
{
    return PluginConfigStore::ReadValue(PluginDataDirectory(pluginId), key, defaultValue, value);
}

bool PluginManager::WritePluginConfigValue(const std::wstring& pluginId, const std::wstring& key, const std::wstring& value)
{
    return PluginConfigStore::WriteValue(PluginDataDirectory(pluginId), key, value);
}

uint64_t PluginManager::RegisterDialogState(const std::wstring& pluginId, const std::wstring& message, bool cancelable, uint64_t total)
{
    std::lock_guard<std::mutex> lock(m_dialogMutex);
    uint64_t handle = m_nextDialogHandle++;
    PluginDialogState state;
    state.pluginId = pluginId;
    state.message = message;
    state.cancelable = cancelable;
    state.total = total;
    m_dialogStates[handle] = std::move(state);
    return handle;
}

bool PluginManager::UpdateDialogState(uint64_t handle, const std::wstring& message, uint64_t current)
{
    std::lock_guard<std::mutex> lock(m_dialogMutex);
    auto it = m_dialogStates.find(handle);
    if (it == m_dialogStates.end())
        return false;
    if (!message.empty())
        it->second.message = message;
    it->second.current = current;
    return true;
}

bool PluginManager::RemoveDialogState(uint64_t handle)
{
    std::lock_guard<std::mutex> lock(m_dialogMutex);
    return m_dialogStates.erase(handle) > 0;
}

bool PluginManager::CopyStringResult(const std::wstring& value, WLStringResultV1* outResult)
{
    if (!outResult || outResult->size < sizeof(WLStringResultV1))
        return false;
    outResult->requiredLength = (uint32_t)value.size() + 1;
    if (!outResult->buffer || outResult->bufferLength < outResult->requiredLength)
        return false;
    wcscpy_s(outResult->buffer, outResult->bufferLength, value.c_str());
    return true;
}

bool PluginManager::CopyStringBuffer(const std::wstring& value, wchar_t* buffer, uint32_t bufferLength, uint32_t* requiredLength)
{
    uint32_t required = (uint32_t)value.size() + 1;
    if (requiredLength)
        *requiredLength = required;
    if (!buffer || bufferLength < required)
        return false;
    wcscpy_s(buffer, bufferLength, value.c_str());
    return true;
}

std::wstring PluginManager::PermissionSummary(const std::vector<std::wstring>& permissions)
{
    if (permissions.empty())
        return L"无权限";

    std::vector<std::wstring> labels;
    labels.reserve(permissions.size());
    for (const auto& permission : permissions)
    {
        if (permission == L"app.info")
            labels.push_back(L"应用信息");
        else if (permission == L"log.write")
            labels.push_back(L"写日志");
        else if (permission == L"plugin.config.read")
            labels.push_back(L"读插件配置");
        else if (permission == L"plugin.config.write")
            labels.push_back(L"写插件配置");
        else if (permission == L"clipboard.read")
            labels.push_back(L"读剪贴板");
        else if (permission == L"clipboard.write")
            labels.push_back(L"写剪贴板");
        else if (permission == L"open.url")
            labels.push_back(L"打开网址");
        else if (permission == L"open.file")
            labels.push_back(L"打开文件");
        else if (permission == L"file.read")
            labels.push_back(L"读插件文件");
        else if (permission == L"file.write")
            labels.push_back(L"写插件文件");
        else if (permission == L"ui.input")
            labels.push_back(L"输入对话框");
        else if (permission == L"ui.filepick")
            labels.push_back(L"文件选择");
        else if (permission == L"ui.notify")
            labels.push_back(L"通知提示");
        else if (permission == L"network.request")
            labels.push_back(L"网络请求");
        else if (permission == L"process.run")
            labels.push_back(L"运行进程");
        else if (permission == L"ui.settings")
            labels.push_back(L"插件设置");
        else if (permission == L"background.worker")
            labels.push_back(L"后台运行");
        else
            labels.push_back(permission);
    }

    return JoinStrings(labels);
}

bool WL_CALL PluginManager::HostAddSearchResult(void* hostContext, const WLSearchResultV1* result)
{
    auto* ctx = reinterpret_cast<SearchCollectContext*>(hostContext);
    if (!ctx || !ctx->results || !result || result->size < sizeof(WLSearchResultV1))
        return false;
    if (ctx->results->size() >= ctx->maxResults)
        return false;

    PluginCommandInfo info;
    info.pluginId = ctx->pluginId;
    info.commandId = CopyWide(result->commandId);
    info.title = CopyWide(result->title);
    info.description = CopyWide(result->description);
    info.score = result->score;

    if (info.commandId.empty() || info.title.empty())
        return false;
    if (info.commandId.rfind(ctx->pluginId + L".", 0) != 0)
        return false;
    if (ctx->slashMode)
        return false;

    ctx->results->push_back(std::move(info));
    return true;
}
