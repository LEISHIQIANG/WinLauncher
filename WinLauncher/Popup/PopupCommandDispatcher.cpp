#include "PopupCommandDispatcher.h"

std::wstring PopupCommandDispatcher::UsageKey(bool localShortcut, const std::wstring& shortcutId,
    const std::wstring& pluginId, const std::wstring& commandId)
{
    return localShortcut ? L"shortcut:" + shortcutId : L"plugin:" + pluginId + L":" + commandId;
}

bool PopupCommandDispatcher::IsBuiltin(const std::wstring& pluginId, const std::wstring& commandId, const wchar_t* builtinId)
{
    return builtinId && commandId == builtinId;
}

std::wstring PopupCommandDispatcher::NormalizeResultMessage(bool succeeded, std::wstring message)
{
    if (message.empty() && !succeeded) message = L"命令执行失败，无错误详情。";
    return succeeded ? message : L"执行失败：\r\n" + message;
}
