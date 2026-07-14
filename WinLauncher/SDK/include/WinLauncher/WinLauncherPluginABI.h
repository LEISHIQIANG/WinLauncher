#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef _WIN32
#define WL_CALL __stdcall
#define WL_EXPORT extern "C" __declspec(dllexport)
#else
#define WL_CALL
#define WL_EXPORT extern "C"
#endif

#define WINLAUNCHER_PLUGIN_ABI_VERSION 1u

struct WLCommandDescriptorV1
{
    uint32_t size;
    const wchar_t* id;
    const wchar_t* title;
    const wchar_t* description;
};

struct WLSlashCommandDescriptorV1
{
    uint32_t size;
    const wchar_t* id;
    const wchar_t* command;
    const wchar_t* title;
    const wchar_t* description;
    const wchar_t* usage;
    const wchar_t* icon;
};

struct WLCommandContextV1
{
    uint32_t size;
    const wchar_t* commandId;
    const wchar_t* query;
};

struct WLSlashCommandContextV1
{
    uint32_t size;
    const wchar_t* commandId;
    const wchar_t* command;
    const wchar_t* args;
    const wchar_t* rawInput;
    const wchar_t* selectedFiles;
};

struct WLStringResultV1
{
    uint32_t size;
    wchar_t* buffer;
    uint32_t bufferLength;
    uint32_t requiredLength;
};

struct WLSearchRequestV1
{
    uint32_t size;
    const wchar_t* query;
    bool slashMode;
    uint32_t maxResults;
};

struct WLSearchResultV1
{
    uint32_t size;
    const wchar_t* id;
    const wchar_t* title;
    const wchar_t* description;
    const wchar_t* commandId;
    int32_t score;
};

struct WLSearchResponseV1
{
    uint32_t size;
    void* hostContext;
    bool (WL_CALL* addResult)(void* hostContext, const WLSearchResultV1* result);
};

struct WLPopupActionDescriptorV1
{
    uint32_t size;
    const wchar_t* id;          // format: "plugin_id.action_name"
    const wchar_t* title;       // display text in the dropdown
    const wchar_t* icon;        // optional icon path (relative to plugin root), can be nullptr
};

struct WLHostApiV1
{
    uint32_t size;
    void* hostContext;

    bool (WL_CALL* registerCommand)(void* hostContext, const WLCommandDescriptorV1* command);
    void (WL_CALL* log)(void* hostContext, const wchar_t* message);
    bool (WL_CALL* getDataDirectory)(void* hostContext, wchar_t* buffer, uint32_t bufferLength, uint32_t* requiredLength);
    bool (WL_CALL* getAppVersion)(void* hostContext, wchar_t* buffer, uint32_t bufferLength, uint32_t* requiredLength);
    bool (WL_CALL* readClipboardText)(void* hostContext, WLStringResultV1* outText);
    bool (WL_CALL* writeClipboardText)(void* hostContext, const wchar_t* text);
    bool (WL_CALL* openUrl)(void* hostContext, const wchar_t* url);
    bool (WL_CALL* openFile)(void* hostContext, const wchar_t* path);
    bool (WL_CALL* readTextFile)(void* hostContext, const wchar_t* relativePath, WLStringResultV1* outText);
    bool (WL_CALL* writeTextFile)(void* hostContext, const wchar_t* relativePath, const wchar_t* text);
    bool (WL_CALL* registerSlashCommand)(void* hostContext, const WLSlashCommandDescriptorV1* command);
    bool (WL_CALL* getPluginConfig)(void* hostContext, const wchar_t* key, const wchar_t* defaultValue, WLStringResultV1* outValue);
    bool (WL_CALL* setPluginConfig)(void* hostContext, const wchar_t* key, const wchar_t* value);

    bool (WL_CALL* showInputDialog)(void* hostContext, const wchar_t* title, const wchar_t* prompt, const wchar_t* defaultText, WLStringResultV1* outText);
    bool (WL_CALL* showPasswordDialog)(void* hostContext, const wchar_t* title, const wchar_t* prompt, WLStringResultV1* outText);
    bool (WL_CALL* showChooseDialog)(void* hostContext, const wchar_t* title, const wchar_t* prompt, const wchar_t* options, WLStringResultV1* outSelected);
    bool (WL_CALL* showConfirmDialog)(void* hostContext, const wchar_t* title, const wchar_t* message);
    bool (WL_CALL* showFilePicker)(void* hostContext, const wchar_t* title, bool multiSelect, const wchar_t* filterPattern, bool onlyFolders, WLStringResultV1* outPaths);
    bool (WL_CALL* showNotificationToaster)(void* hostContext, const wchar_t* title, const wchar_t* message, const wchar_t* type, uint32_t durationMs);
    bool (WL_CALL* showMessageBox)(void* hostContext, const wchar_t* title, const wchar_t* message, const wchar_t* iconType, const wchar_t* buttons, WLStringResultV1* outResult);
    bool (WL_CALL* showBalloonTip)(void* hostContext, const wchar_t* title, const wchar_t* message, const wchar_t* iconType, uint32_t durationMs);
    bool (WL_CALL* showLoadingDialog)(void* hostContext, const wchar_t* message, bool cancelable, uint64_t* outHandle);
    bool (WL_CALL* updateLoadingMessage)(void* hostContext, uint64_t handle, const wchar_t* newMessage);
    bool (WL_CALL* hideLoadingDialog)(void* hostContext, uint64_t handle);
    bool (WL_CALL* showProgressDialog)(void* hostContext, const wchar_t* title, const wchar_t* message, uint64_t total, bool cancelable, uint64_t* outHandle);
    bool (WL_CALL* updateProgress)(void* hostContext, uint64_t handle, uint64_t current, const wchar_t* statusMessage);
    bool (WL_CALL* hideProgressDialog)(void* hostContext, uint64_t handle);
    bool (WL_CALL* isDialogCancelled)(void* hostContext, uint64_t handle, bool* outCancelled);
    bool (WL_CALL* showResultInPanel)(void* hostContext, const wchar_t* title, const wchar_t* content, const wchar_t* contentType);
    bool (WL_CALL* httpRequest)(void* hostContext, const wchar_t* method, const wchar_t* url, const wchar_t* headers, const wchar_t* body, uint32_t timeoutMs, WLStringResultV1* outResponse);
    bool (WL_CALL* runProcess)(void* hostContext, const wchar_t* command, const wchar_t* workingDir, bool captureOutput, uint32_t timeoutMs, WLStringResultV1* outOutput, uint32_t* outExitCode);
    bool (WL_CALL* getScreenInfo)(void* hostContext, uint32_t* outWidth, uint32_t* outHeight, uint32_t* outDpi, WLStringResultV1* outTheme);

    // Plugin extension point: register an action that appears in the PopupWindow "More" dropdown
    bool (WL_CALL* registerPopupAction)(void* hostContext, const WLPopupActionDescriptorV1* action);

    // Append text to the currently running command output panel, when the host command is panel-backed.
    bool (WL_CALL* appendResultToPanel)(void* hostContext, const wchar_t* text);
};

struct WLPluginInstanceV1
{
    uint32_t size;
    void* userData;

    bool (WL_CALL* onLoad)(void* userData);
    void (WL_CALL* onUnload)(void* userData);
    bool (WL_CALL* executeCommand)(void* userData, const WLCommandContextV1* context, WLStringResultV1* outMessage);
    void (WL_CALL* onPopupShown)(void* userData);
    void (WL_CALL* onPopupHidden)(void* userData);
    bool (WL_CALL* search)(void* userData, const WLSearchRequestV1* request, WLSearchResponseV1* response);
    bool (WL_CALL* executeSlashCommand)(void* userData, const WLSlashCommandContextV1* context, WLStringResultV1* outMessage);

    // Optional cooperative-unload callbacks. Hosts must check size before use.
    void (WL_CALL* requestShutdown)(void* userData);
    bool (WL_CALL* isShutdownComplete)(void* userData);
};

typedef uint32_t (WL_CALL* WLGetAbiVersionFn)();
typedef bool (WL_CALL* WLCreatePluginFn)(const WLHostApiV1* host, WLPluginInstanceV1** outInstance);
typedef void (WL_CALL* WLDestroyPluginFn)(WLPluginInstanceV1* instance);

WL_EXPORT uint32_t WL_CALL WinLauncherPlugin_GetAbiVersion();
WL_EXPORT bool WL_CALL WinLauncherPlugin_Create(const WLHostApiV1* host, WLPluginInstanceV1** outInstance);
WL_EXPORT void WL_CALL WinLauncherPlugin_Destroy(WLPluginInstanceV1* instance);
