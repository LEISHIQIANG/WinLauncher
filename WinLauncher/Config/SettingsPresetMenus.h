#pragma once

#include <string>
#include <vector>

// Trigger / popup-align preset labels, trigger-blacklist text handling and a
// lowercase helper shared by the settings page. The preset menu and blacklist
// editor flows themselves stay SettingsPage members but are defined next to
// this module because they route through IConfigWindow callbacks.
class SettingsPresetMenus
{
public:
    static std::wstring TriggerPresetLabel(int type);
    static std::wstring PopupAlignPresetLabel(int mode);
    static std::vector<std::wstring> ParseTriggerBlacklistInput(const std::wstring& input);
    static std::wstring JoinTriggerBlacklistInput(const std::vector<std::wstring>& items);
    static std::wstring TriggerBlacklistSummary(const std::vector<std::wstring>& items);
    static std::wstring ToLowerCopy(std::wstring value);
};
