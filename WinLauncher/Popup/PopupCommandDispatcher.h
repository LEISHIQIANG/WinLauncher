#pragma once

#include <string>

// Execution policy that is independent from HWND/D2D drawing. PopupWindow
// supplies the actual host callbacks and routes the returned action.
class PopupCommandDispatcher
{
public:
    static std::wstring UsageKey(bool localShortcut, const std::wstring& shortcutId,
        const std::wstring& pluginId, const std::wstring& commandId);
    static bool IsBuiltin(const std::wstring& pluginId, const std::wstring& commandId, const wchar_t* builtinId);
    static std::wstring NormalizeResultMessage(bool succeeded, std::wstring message);
};
