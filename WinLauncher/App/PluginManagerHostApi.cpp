#include "PluginManager.h"
#include "BackgroundTaskService.h"
#include "../Config/CommandPanelWindow.h"
#include "../ToastWindow.h"
#include "../Services/ConfigPath.h"
#include "Logger.h"
#include "../version.h"
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <winhttp.h>
#include <commdlg.h>
#include <fstream>
#include <algorithm>
#include <vector>
#include <string>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")

namespace
{
    std::wstring CurrentHostVersion()
    {
        return WINLAUNCHER_VERSION_WSTR;
    }

    std::wstring JoinPath(const std::wstring& base, const std::wstring& relative)
    {
        if (base.empty())
            return relative;
        if (relative.empty())
            return base;
        if (base.back() == L'\\' || base.back() == L'/')
            return base + relative;
        return base + L"\\" + relative;
    }

    std::string ToUtf8(const std::wstring& value)
    {
        if (value.empty())
            return {};
        int len = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), (int)value.size(), nullptr, 0, nullptr, nullptr);
        if (len <= 0)
            return {};
        std::string result(len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, value.c_str(), (int)value.size(), &result[0], len, nullptr, nullptr);
        return result;
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

bool WL_CALL PluginManager::HostRegisterCommand(void* hostContext, const WLCommandDescriptorV1* command)
{
    auto* ctx = reinterpret_cast<HostContext*>(hostContext);
    if (!ctx || !ctx->manager)
        return false;
    return ctx->manager->RegisterRuntimeCommand(ctx->pluginId, command);
}

bool WL_CALL PluginManager::HostRegisterSlashCommand(void* hostContext, const WLSlashCommandDescriptorV1* command)
{
    auto* ctx = reinterpret_cast<HostContext*>(hostContext);
    if (!ctx || !ctx->manager)
        return false;
    return ctx->manager->RegisterRuntimeSlashCommand(ctx->pluginId, command);
}

bool WL_CALL PluginManager::HostRegisterPopupAction(void* hostContext, const WLPopupActionDescriptorV1* action)
{
    auto* ctx = reinterpret_cast<HostContext*>(hostContext);
    if (!ctx || !ctx->manager)
        return false;
    return ctx->manager->RegisterPopupAction(ctx->pluginId, action);
}

void WL_CALL PluginManager::HostLog(void* hostContext, const wchar_t* message)
{
    auto* ctx = reinterpret_cast<HostContext*>(hostContext);
    if (!ctx || !ctx->manager)
        return;
    if (!ctx->manager->HasPermission(ctx->pluginId, L"log.write"))
        return;
    LOG_INFO(ctx->manager->m_logger, L"Plugin[%s]: %s", ctx->pluginId.c_str(), message ? message : L"");
}

bool WL_CALL PluginManager::HostGetDataDirectory(void* hostContext, wchar_t* buffer, uint32_t bufferLength, uint32_t* requiredLength)
{
    auto* ctx = reinterpret_cast<HostContext*>(hostContext);
    if (!ctx || !ctx->manager)
        return false;

    std::wstring dir = ctx->manager->PluginDataDirectory(ctx->pluginId);
    uint32_t required = (uint32_t)dir.size() + 1;
    if (requiredLength)
        *requiredLength = required;
    if (!buffer || bufferLength < required)
        return false;

    wcscpy_s(buffer, bufferLength, dir.c_str());
    return true;
}

bool WL_CALL PluginManager::HostGetAppVersion(void* hostContext, wchar_t* buffer, uint32_t bufferLength, uint32_t* requiredLength)
{
    auto* ctx = reinterpret_cast<HostContext*>(hostContext);
    if (!ctx || !ctx->manager || !ctx->manager->HasPermission(ctx->pluginId, L"app.info"))
        return false;
    return CopyStringBuffer(CurrentHostVersion(), buffer, bufferLength, requiredLength);
}

bool WL_CALL PluginManager::HostReadClipboardText(void* hostContext, WLStringResultV1* outText)
{
    auto* ctx = reinterpret_cast<HostContext*>(hostContext);
    if (!ctx || !ctx->manager || !ctx->manager->HasPermission(ctx->pluginId, L"clipboard.read"))
        return false;

    if (!OpenClipboard(nullptr))
        return false;
    HANDLE data = GetClipboardData(CF_UNICODETEXT);
    if (!data)
    {
        CloseClipboard();
        return false;
    }
    const wchar_t* text = static_cast<const wchar_t*>(GlobalLock(data));
    std::wstring value = text ? text : L"";
    if (text)
        GlobalUnlock(data);
    CloseClipboard();
    return CopyStringResult(value, outText);
}

bool WL_CALL PluginManager::HostWriteClipboardText(void* hostContext, const wchar_t* text)
{
    auto* ctx = reinterpret_cast<HostContext*>(hostContext);
    if (!ctx || !ctx->manager || !ctx->manager->HasPermission(ctx->pluginId, L"clipboard.write") || !text)
        return false;

    size_t bytes = (wcslen(text) + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory)
        return false;
    void* target = GlobalLock(memory);
    if (!target)
    {
        GlobalFree(memory);
        return false;
    }
    memcpy(target, text, bytes);
    GlobalUnlock(memory);

    if (!OpenClipboard(nullptr))
    {
        GlobalFree(memory);
        return false;
    }
    EmptyClipboard();
    bool ok = SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
    CloseClipboard();
    if (!ok)
        GlobalFree(memory);
    return ok;
}

bool WL_CALL PluginManager::HostOpenUrl(void* hostContext, const wchar_t* url)
{
    auto* ctx = reinterpret_cast<HostContext*>(hostContext);
    if (!ctx || !ctx->manager || !ctx->manager->HasPermission(ctx->pluginId, L"open.url") || !url)
        return false;

    std::wstring value = url;
    std::wstring lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
    if (lower.rfind(L"https://", 0) != 0 && lower.rfind(L"http://", 0) != 0)
        return false;

    HINSTANCE result = ShellExecuteW(nullptr, L"open", value.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return (INT_PTR)result > 32;
}

bool WL_CALL PluginManager::HostOpenFile(void* hostContext, const wchar_t* path)
{
    auto* ctx = reinterpret_cast<HostContext*>(hostContext);
    if (!ctx || !ctx->manager || !ctx->manager->HasPermission(ctx->pluginId, L"open.file") || !path)
        return false;
    HINSTANCE result = ShellExecuteW(nullptr, L"open", path, nullptr, nullptr, SW_SHOWNORMAL);
    return (INT_PTR)result > 32;
}

bool WL_CALL PluginManager::HostReadTextFile(void* hostContext, const wchar_t* relativePath, WLStringResultV1* outText)
{
    auto* ctx = reinterpret_cast<HostContext*>(hostContext);
    if (!ctx || !ctx->manager || !ctx->manager->HasPermission(ctx->pluginId, L"file.read") || !relativePath)
        return false;
    if (!ctx->manager->IsSafePluginRelativePath(relativePath))
        return false;

    std::wstring fullPath = JoinPath(ctx->manager->PluginDataDirectory(ctx->pluginId), relativePath);
    std::wifstream fs(fullPath);
    if (!fs)
        return false;
    std::wstring content((std::istreambuf_iterator<wchar_t>(fs)), std::istreambuf_iterator<wchar_t>());
    return CopyStringResult(content, outText);
}

bool WL_CALL PluginManager::HostWriteTextFile(void* hostContext, const wchar_t* relativePath, const wchar_t* text)
{
    auto* ctx = reinterpret_cast<HostContext*>(hostContext);
    if (!ctx || !ctx->manager || !ctx->manager->HasPermission(ctx->pluginId, L"file.write") || !relativePath || !text)
        return false;
    if (!ctx->manager->IsSafePluginRelativePath(relativePath))
        return false;

    std::wstring fullPath = JoinPath(ctx->manager->PluginDataDirectory(ctx->pluginId), relativePath);
    std::wstring parent = fullPath.substr(0, fullPath.find_last_of(L"\\/"));
    ConfigPath::EnsureDirectoryExists(parent);

    std::wofstream fs(fullPath, std::ios::trunc);
    if (!fs)
        return false;
    fs << text;
    return true;
}

bool WL_CALL PluginManager::HostGetPluginConfig(void* hostContext, const wchar_t* key, const wchar_t* defaultValue, WLStringResultV1* outValue)
{
    auto* ctx = reinterpret_cast<HostContext*>(hostContext);
    if (!ctx || !ctx->manager || !ctx->manager->HasPermission(ctx->pluginId, L"plugin.config.read") || !key)
        return false;

    std::wstring value;
    if (!ctx->manager->ReadPluginConfigValue(ctx->pluginId, key, defaultValue ? defaultValue : L"", value))
        return false;
    return CopyStringResult(value, outValue);
}

bool WL_CALL PluginManager::HostSetPluginConfig(void* hostContext, const wchar_t* key, const wchar_t* value)
{
    auto* ctx = reinterpret_cast<HostContext*>(hostContext);
    if (!ctx || !ctx->manager || !ctx->manager->HasPermission(ctx->pluginId, L"plugin.config.write") || !key || !value)
        return false;
    return ctx->manager->WritePluginConfigValue(ctx->pluginId, key, value);
}

bool WL_CALL PluginManager::HostShowInputDialog(void* hostContext, const wchar_t* title, const wchar_t* prompt, const wchar_t* defaultText, WLStringResultV1* outText)
{
    auto* ctx = reinterpret_cast<HostContext*>(hostContext);
    if (!ctx || !ctx->manager || !ctx->manager->HasPermission(ctx->pluginId, L"ui.input") || !outText)
        return false;
    if (!outText->buffer || outText->bufferLength == 0)
    {
        outText->requiredLength = 4096;
        return false;
    }

    std::wstring value;
    bool accepted = false;
    if (!ctx->manager->m_uiDispatcher || !ctx->manager->m_uiDispatcher->InvokeSync(L"plugin.ui.input", [&]() {
        if (ctx->manager->m_userInteraction)
        {
            accepted = ctx->manager->m_userInteraction->ShowPrompt(nullptr, title ? title : L"WinLauncher", prompt ? prompt : L"", value, defaultText ? defaultText : L"");
        }
    }) || !accepted)
        return false;
    return CopyStringResult(value, outText);
}

bool WL_CALL PluginManager::HostShowPasswordDialog(void* hostContext, const wchar_t* title, const wchar_t* prompt, WLStringResultV1* outText)
{
    auto* ctx = reinterpret_cast<HostContext*>(hostContext);
    if (!ctx || !ctx->manager || !ctx->manager->HasPermission(ctx->pluginId, L"ui.input") || !outText)
        return false;
    if (!outText->buffer || outText->bufferLength == 0)
    {
        outText->requiredLength = 4096;
        return false;
    }

    std::wstring value;
    bool accepted = false;
    if (!ctx->manager->m_uiDispatcher || !ctx->manager->m_uiDispatcher->InvokeSync(L"plugin.ui.password", [&]() {
        if (ctx->manager->m_userInteraction)
        {
            accepted = ctx->manager->m_userInteraction->ShowPasswordPrompt(nullptr, title ? title : L"WinLauncher", prompt ? prompt : L"", value);
        }
    }) || !accepted)
        return false;
    return CopyStringResult(value, outText);
}

bool WL_CALL PluginManager::HostShowChooseDialog(void* hostContext, const wchar_t* title, const wchar_t* prompt, const wchar_t* options, WLStringResultV1* outSelected)
{
    auto* ctx = reinterpret_cast<HostContext*>(hostContext);
    if (!ctx || !ctx->manager || !ctx->manager->HasPermission(ctx->pluginId, L"ui.input") || !outSelected)
        return false;
    if (!outSelected->buffer || outSelected->bufferLength == 0)
    {
        outSelected->requiredLength = 4096;
        return false;
    }

    std::vector<std::wstring> items = SplitLines(options ? options : L"");
    if (items.empty())
        return false;
    std::wstring value;
    bool accepted = false;
    if (!ctx->manager->m_uiDispatcher || !ctx->manager->m_uiDispatcher->InvokeSync(L"plugin.ui.choose", [&]() {
        if (ctx->manager->m_userInteraction)
        {
            accepted = ctx->manager->m_userInteraction->ShowChoosePrompt(nullptr, title ? title : L"WinLauncher", prompt ? prompt : L"", items, value);
        }
    }) || !accepted)
        return false;
    return CopyStringResult(value, outSelected);
}

bool WL_CALL PluginManager::HostShowConfirmDialog(void* hostContext, const wchar_t* title, const wchar_t* message)
{
    auto* ctx = reinterpret_cast<HostContext*>(hostContext);
    if (!ctx || !ctx->manager || !ctx->manager->HasPermission(ctx->pluginId, L"ui.input"))
        return false;
    bool accepted = false;
    if (!ctx->manager->m_uiDispatcher) return false;
    return ctx->manager->m_uiDispatcher->InvokeSync(L"plugin.ui.confirm", [&]() {
        if (ctx->manager->m_userInteraction)
        {
            accepted = ctx->manager->m_userInteraction->ShowConfirm(nullptr, title ? title : L"WinLauncher", message ? message : L"");
        }
    }) && accepted;
}

bool WL_CALL PluginManager::HostShowFilePicker(void* hostContext, const wchar_t* title, bool multiSelect, const wchar_t* filterPattern, bool onlyFolders, WLStringResultV1* outPaths)
{
    auto* ctx = reinterpret_cast<HostContext*>(hostContext);
    if (!ctx || !ctx->manager || !ctx->manager->HasPermission(ctx->pluginId, L"ui.filepick") || !outPaths)
        return false;
    if (!outPaths->buffer || outPaths->bufferLength == 0)
    {
        outPaths->requiredLength = 32768;
        return false;
    }

    if (!ctx->manager->m_uiDispatcher) return false;
    std::wstring selectedPaths;
    bool selected = false;
    bool dispatched = ctx->manager->m_uiDispatcher->InvokeSync(L"plugin.ui.file_picker", [&]() {
        if (onlyFolders)
        {
            BROWSEINFOW bi{};
            bi.hwndOwner = nullptr;
            bi.lpszTitle = title ? title : L"Select folder";
            bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
            PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
            if (!pidl) return;
            wchar_t path[MAX_PATH]{};
            selected = SHGetPathFromIDListW(pidl, path) != FALSE;
            if (selected) selectedPaths = path;
            CoTaskMemFree(pidl);
            return;
        }

        std::vector<wchar_t> fileBuffer(multiSelect ? 32768 : MAX_PATH, L'\0');
        std::wstring filter = BuildFileDialogFilter(filterPattern);
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = nullptr;
        ofn.lpstrTitle = title ? title : L"Select file";
        ofn.lpstrFile = fileBuffer.data();
        ofn.nMaxFile = (DWORD)fileBuffer.size();
        ofn.lpstrFilter = filter.c_str();
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
        if (multiSelect) ofn.Flags |= OFN_ALLOWMULTISELECT;
        if (GetOpenFileNameW(&ofn))
        {
            selectedPaths = ParseOpenFileResult(fileBuffer);
            selected = true;
        }
    });
    return dispatched && selected && CopyStringResult(selectedPaths, outPaths);
}

bool WL_CALL PluginManager::HostShowNotificationToaster(void* hostContext, const wchar_t* title, const wchar_t* message, const wchar_t*, uint32_t durationMs)
{
    auto* ctx = reinterpret_cast<HostContext*>(hostContext);
    if (!ctx || !ctx->manager || !ctx->manager->HasPermission(ctx->pluginId, L"ui.notify"))
        return false;
    if (!ctx->manager->m_uiDispatcher) return false;
    std::wstring text = FormatMessageText(title, message);
    return ctx->manager->m_uiDispatcher->Post(L"plugin.ui.toast", [text, durationMs]() {
        ToastWindow::Show(text, durationMs == 0 ? 3000 : durationMs);
    });
}

bool WL_CALL PluginManager::HostShowMessageBox(void* hostContext, const wchar_t* title, const wchar_t* message, const wchar_t* iconType, const wchar_t* buttons, WLStringResultV1* outResult)
{
    auto* ctx = reinterpret_cast<HostContext*>(hostContext);
    if (!ctx || !ctx->manager || !ctx->manager->HasPermission(ctx->pluginId, L"ui.notify") || !outResult)
        return false;
    if (!outResult->buffer || outResult->bufferLength == 0)
    {
        outResult->requiredLength = 16;
        return false;
    }

    if (!ctx->manager->m_uiDispatcher) return false;
    UINT flags = MessageIconFlag(iconType ? iconType : L"info") | MessageButtonsFlag(buttons ? buttons : L"ok");
    int clicked = 0;
    if (!ctx->manager->m_uiDispatcher->InvokeSync(L"plugin.ui.message_box", [&]() {
        clicked = MessageBoxW(nullptr, message ? message : L"", title ? title : L"WinLauncher", flags);
    })) return false;
    return CopyStringResult(MessageResultText(clicked), outResult);
}

bool WL_CALL PluginManager::HostShowBalloonTip(void* hostContext, const wchar_t* title, const wchar_t* message, const wchar_t* type, uint32_t durationMs)
{
    return HostShowNotificationToaster(hostContext, title, message, type, durationMs == 0 ? 5000 : durationMs);
}

bool WL_CALL PluginManager::HostShowLoadingDialog(void* hostContext, const wchar_t* message, bool cancelable, uint64_t* outHandle)
{
    auto* ctx = reinterpret_cast<HostContext*>(hostContext);
    if (!ctx || !ctx->manager || !ctx->manager->HasPermission(ctx->pluginId, L"ui.notify") || !outHandle)
        return false;
    *outHandle = ctx->manager->RegisterDialogState(ctx->pluginId, message ? message : L"", cancelable);
    if (ctx->manager->m_uiDispatcher)
    {
        std::wstring text = message ? message : L"Working...";
        ctx->manager->m_uiDispatcher->Post(L"plugin.ui.loading", [text]() { ToastWindow::Show(text, 1500); });
    }
    return true;
}

bool WL_CALL PluginManager::HostUpdateLoadingMessage(void* hostContext, uint64_t handle, const wchar_t* newMessage)
{
    auto* ctx = reinterpret_cast<HostContext*>(hostContext);
    if (!ctx || !ctx->manager || !ctx->manager->HasPermission(ctx->pluginId, L"ui.notify"))
        return false;
    if (!ctx->manager->UpdateDialogState(handle, newMessage ? newMessage : L""))
        return false;
    if (ctx->manager->m_uiDispatcher)
    {
        std::wstring text = newMessage ? newMessage : L"Working...";
        ctx->manager->m_uiDispatcher->Post(L"plugin.ui.loading_update", [text]() { ToastWindow::Show(text, 1500); });
    }
    return true;
}

bool WL_CALL PluginManager::HostHideLoadingDialog(void* hostContext, uint64_t handle)
{
    auto* ctx = reinterpret_cast<HostContext*>(hostContext);
    if (!ctx || !ctx->manager || !ctx->manager->HasPermission(ctx->pluginId, L"ui.notify"))
        return false;
    return ctx->manager->RemoveDialogState(handle);
}

bool WL_CALL PluginManager::HostShowProgressDialog(void* hostContext, const wchar_t*, const wchar_t* message, uint64_t total, bool cancelable, uint64_t* outHandle)
{
    auto* ctx = reinterpret_cast<HostContext*>(hostContext);
    if (!ctx || !ctx->manager || !ctx->manager->HasPermission(ctx->pluginId, L"ui.notify") || !outHandle || total == 0)
        return false;
    *outHandle = ctx->manager->RegisterDialogState(ctx->pluginId, message ? message : L"", cancelable, total);
    if (ctx->manager->m_uiDispatcher)
    {
        std::wstring text = message ? message : L"Starting...";
        ctx->manager->m_uiDispatcher->Post(L"plugin.ui.progress", [text]() { ToastWindow::Show(text, 1500); });
    }
    return true;
}

bool WL_CALL PluginManager::HostUpdateProgress(void* hostContext, uint64_t handle, uint64_t current, const wchar_t* statusMessage)
{
    auto* ctx = reinterpret_cast<HostContext*>(hostContext);
    if (!ctx || !ctx->manager || !ctx->manager->HasPermission(ctx->pluginId, L"ui.notify"))
        return false;
    if (!ctx->manager->UpdateDialogState(handle, statusMessage ? statusMessage : L"", current))
        return false;
    if (statusMessage && *statusMessage && ctx->manager->m_uiDispatcher)
    {
        std::wstring text = statusMessage;
        ctx->manager->m_uiDispatcher->Post(L"plugin.ui.progress_update", [text]() { ToastWindow::Show(text, 1000); });
    }
    return true;
}

bool WL_CALL PluginManager::HostHideProgressDialog(void* hostContext, uint64_t handle)
{
    return HostHideLoadingDialog(hostContext, handle);
}

bool WL_CALL PluginManager::HostIsDialogCancelled(void* hostContext, uint64_t handle, bool* outCancelled)
{
    auto* ctx = reinterpret_cast<HostContext*>(hostContext);
    if (!ctx || !ctx->manager || !ctx->manager->HasPermission(ctx->pluginId, L"ui.notify") || !outCancelled)
        return false;
    return ctx->manager->IsDialogCancelled(handle, outCancelled);
}

bool WL_CALL PluginManager::HostShowResultInPanel(void* hostContext, const wchar_t* title, const wchar_t* content, const wchar_t*)
{
    auto* ctx = reinterpret_cast<HostContext*>(hostContext);
    if (!ctx || !ctx->manager || !ctx->manager->HasPermission(ctx->pluginId, L"ui.notify"))
        return false;
    if (!ctx->manager->m_uiDispatcher) return false;
    std::wstring titleText = title ? title : L"WinLauncher";
    std::wstring contentText = content ? content : L"";
    return ctx->manager->m_uiDispatcher->InvokeSync(L"plugin.ui.result", [titleText, contentText]() {
        MessageBoxW(nullptr, contentText.c_str(), titleText.c_str(), MB_OK | MB_ICONINFORMATION);
    });
}

bool WL_CALL PluginManager::HostHttpRequest(void* hostContext, const wchar_t* method, const wchar_t* url, const wchar_t* headers, const wchar_t* body, uint32_t timeoutMs, WLStringResultV1* outResponse)
{
    auto* ctx = reinterpret_cast<HostContext*>(hostContext);
    if (!ctx || !ctx->manager || !ctx->manager->HasPermission(ctx->pluginId, L"network.request") || !method || !url || !outResponse)
        return false;

    std::wstring verb = method;
    if (!IsHttpMethodAllowed(verb))
        return false;

    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256]{};
    wchar_t path[2048]{};
    wchar_t extra[2048]{};
    uc.lpszHostName = host;
    uc.dwHostNameLength = (DWORD)_countof(host);
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = (DWORD)_countof(path);
    uc.lpszExtraInfo = extra;
    uc.dwExtraInfoLength = (DWORD)_countof(extra);
    if (!WinHttpCrackUrl(url, 0, 0, &uc))
        return false;
    if (uc.nScheme != INTERNET_SCHEME_HTTP && uc.nScheme != INTERNET_SCHEME_HTTPS)
        return false;

    std::wstring objectName = path;
    objectName += extra;
    if (objectName.empty())
        objectName = L"/";

    HINTERNET session = WinHttpOpen(L"WinLauncherPlugin/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
        return false;
    uint32_t timeout = timeoutMs == 0 ? 30000 : timeoutMs;
    WinHttpSetTimeouts(session, timeout, timeout, timeout, timeout);

    HINTERNET connect = WinHttpConnect(session, host, uc.nPort, 0);
    if (!connect)
    {
        WinHttpCloseHandle(session);
        return false;
    }

    DWORD flags = uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(connect, verb.c_str(), objectName.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!request)
    {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    std::string bodyBytes = ToUtf8(body ? body : L"");
    if (BackgroundTaskService::IsCurrentTaskCancellationRequested())
    {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }
    BOOL sent = WinHttpSendRequest(
        request,
        headers && *headers ? headers : WINHTTP_NO_ADDITIONAL_HEADERS,
        headers && *headers ? (DWORD)-1L : 0,
        bodyBytes.empty() ? WINHTTP_NO_REQUEST_DATA : bodyBytes.data(),
        (DWORD)bodyBytes.size(),
        (DWORD)bodyBytes.size(),
        0);
    BOOL received = sent && !BackgroundTaskService::IsCurrentTaskCancellationRequested() && WinHttpReceiveResponse(request, nullptr);

    std::string responseBytes;
    if (received)
    {
        DWORD available = 0;
        while (WinHttpQueryDataAvailable(request, &available) && available > 0)
        {
            if (BackgroundTaskService::IsCurrentTaskCancellationRequested()) break;
            std::string chunk(available, '\0');
            DWORD read = 0;
            if (!WinHttpReadData(request, &chunk[0], available, &read))
                break;
            chunk.resize(read);
            responseBytes += chunk;
            if (responseBytes.size() > 4 * 1024 * 1024)
                break;
        }
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    if (!received || BackgroundTaskService::IsCurrentTaskCancellationRequested())
        return false;
    return CopyStringResult(Utf8ToWide(responseBytes), outResponse);
}

bool WL_CALL PluginManager::HostRunProcess(void* hostContext, const wchar_t* command, const wchar_t* workingDir, bool captureOutput, uint32_t timeoutMs, WLStringResultV1* outOutput, uint32_t* outExitCode)
{
    auto* ctx = reinterpret_cast<HostContext*>(hostContext);
    if (!ctx || !ctx->manager || !ctx->manager->HasPermission(ctx->pluginId, L"process.run") || !command || !outExitCode)
        return false;
    std::wstring workDir = workingDir && *workingDir ? workingDir : ctx->manager->PluginDataDirectory(ctx->pluginId);
    if (!ctx->manager->IsSafeProcessWorkingDirectory(ctx->pluginId, workDir))
        return false;

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (captureOutput)
    {
        if (!outOutput || !CreatePipe(&readPipe, &writePipe, &sa, 0))
            return false;
        SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
    }

    std::wstring cmdLine = command;
    STARTUPINFOW si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);
    if (captureOutput)
    {
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdOutput = writePipe;
        si.hStdError = writePipe;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    }

    BOOL ok = CreateProcessW(nullptr, &cmdLine[0], nullptr, nullptr, captureOutput ? TRUE : FALSE, CREATE_NO_WINDOW, nullptr, workDir.c_str(), &si, &pi);
    if (writePipe)
        CloseHandle(writePipe);
    if (!ok)
    {
        if (readPipe)
            CloseHandle(readPipe);
        return false;
    }

    std::string output;
    DWORD waitResult = WAIT_TIMEOUT;
    DWORD start = GetTickCount();
    while (true)
    {
        waitResult = WaitForSingleObject(pi.hProcess, 25);
        if (BackgroundTaskService::IsCurrentTaskCancellationRequested())
        {
            TerminateProcess(pi.hProcess, ERROR_CANCELLED);
            waitResult = WaitForProcessWithTimeout(pi.hProcess, 1000);
            break;
        }
        if (captureOutput && readPipe)
        {
            DWORD available = 0;
            while (PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr) && available > 0)
            {
                char buffer[4096];
                DWORD read = 0;
                if (!ReadFile(readPipe, buffer, (DWORD)(std::min<size_t>)(sizeof(buffer), available), &read, nullptr) || read == 0)
                    break;
                output.append(buffer, buffer + read);
                if (output.size() > 1024 * 1024)
                    break;
            }
        }
        if (waitResult != WAIT_TIMEOUT)
            break;
        if (timeoutMs != 0 && GetTickCount() - start > timeoutMs)
        {
            TerminateProcess(pi.hProcess, 1);
            waitResult = WaitForProcessWithTimeout(pi.hProcess, 1000);
            break;
        }
    }

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    *outExitCode = exitCode;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (readPipe)
        CloseHandle(readPipe);
    if (waitResult == WAIT_TIMEOUT)
        return false;
    if (captureOutput)
        return CopyStringResult(Utf8ToWide(output), outOutput);
    return true;
}

bool WL_CALL PluginManager::HostGetScreenInfo(void* hostContext, uint32_t* outWidth, uint32_t* outHeight, uint32_t* outDpi, WLStringResultV1* outTheme)
{
    auto* ctx = reinterpret_cast<HostContext*>(hostContext);
    if (!ctx || !ctx->manager || !ctx->manager->HasPermission(ctx->pluginId, L"app.info") || !outWidth || !outHeight || !outDpi || !outTheme)
        return false;
    *outWidth = (uint32_t)GetSystemMetrics(SM_CXSCREEN);
    *outHeight = (uint32_t)GetSystemMetrics(SM_CYSCREEN);
    *outDpi = 96;
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32)
    {
        using GetDpiForSystemFn = UINT(WINAPI*)();
        auto getDpiForSystem = reinterpret_cast<GetDpiForSystemFn>(GetProcAddress(user32, "GetDpiForSystem"));
        if (getDpiForSystem)
            *outDpi = getDpiForSystem();
    }

    DWORD lightTheme = 1;
    DWORD size = sizeof(lightTheme);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &lightTheme, &size);
    return CopyStringResult(lightTheme ? L"light" : L"dark", outTheme);
}

bool WL_CALL PluginManager::HostAppendResultToPanel(void* hostContext, const wchar_t* text)
{
    auto* ctx = reinterpret_cast<HostContext*>(hostContext);
    HWND panel = PluginManager::GetCurrentPluginOutputPanel();
    if (!ctx || !ctx->manager || !text || !*text || !panel)
        return false;
    return CommandPanelWindow::PostAppend(panel, text);
}
