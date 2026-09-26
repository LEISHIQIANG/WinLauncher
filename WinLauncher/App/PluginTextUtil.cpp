#include "PluginTextUtil.h"
#include "PluginManager.h"
#include <windows.h>
#include <algorithm>
#include <cwctype>

namespace PluginTextUtil
{
    std::wstring CopyWide(const wchar_t* text)
    {
        return text ? std::wstring(text) : std::wstring();
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

    bool ContainsAnyLower(const std::vector<std::wstring>& values, const std::wstring& queryLower)
    {
        for (const auto& value : values)
        {
            if (ContainsLower(value, queryLower))
                return true;
        }
        return false;
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

    std::wstring JoinStrings(const std::vector<std::wstring>& values, const std::wstring& delimiter)
    {
        std::wstring out;
        for (const auto& value : values)
        {
            if (!out.empty()) out += delimiter;
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

    bool MatchesSlashCommand(const PluginCommandInfo& command, const std::wstring& queryLower)
    {
        return queryLower.empty() ||
            ContainsLower(command.commandName, queryLower) ||
            ContainsLower(command.title, queryLower) ||
            ContainsAnyLower(command.keywords, queryLower) ||
            ContainsAnyLower(command.aliases, queryLower);
    }

    bool PathStartsWithDirectory(const std::wstring& path, const std::wstring& directory)
    {
        std::wstring lhs = ToLowerCopy(path);
        std::wstring rhs = ToLowerCopy(directory);
        std::replace(lhs.begin(), lhs.end(), L'/', L'\\');
        std::replace(rhs.begin(), rhs.end(), L'/', L'\\');
        if (rhs.empty())
            return true;
        if (rhs.back() != L'\\')
            rhs.push_back(L'\\');
        if (lhs.size() < rhs.size())
            return false;
        return lhs.compare(0, rhs.size(), rhs) == 0;
    }
}
