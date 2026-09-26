#pragma once

#include <string>
#include <vector>

struct PluginCommandInfo;

namespace PluginTextUtil
{
    std::wstring CopyWide(const wchar_t* text);
    std::wstring ToLowerCopy(std::wstring value);
    std::wstring Utf8ToWide(const std::string& value);
    bool ContainsLower(const std::wstring& value, const std::wstring& queryLower);
    bool ContainsAnyLower(const std::vector<std::wstring>& values, const std::wstring& queryLower);
    std::wstring SlashTrimmedQuery(const std::wstring& query);
    std::wstring SlashCommandNameFromInput(const std::wstring& query);
    std::wstring SlashSearchQueryFromInput(const std::wstring& query);
    std::wstring SlashArgsFromInput(const std::wstring& query);
    std::wstring JoinStrings(const std::vector<std::wstring>& values, const std::wstring& delimiter = L", ");
    std::wstring JoinLines(const std::vector<std::wstring>& values);
    std::vector<std::wstring> SplitLines(const std::wstring& value);
    bool MatchesSlashCommand(const PluginCommandInfo& command, const std::wstring& queryLower);
    bool PathStartsWithDirectory(const std::wstring& path, const std::wstring& directory);
}
