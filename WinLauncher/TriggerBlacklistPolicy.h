#pragma once

#include <algorithm>
#include <cwctype>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace TriggerBlacklistPolicy
{
    inline void NormalizeProcessNameInPlace(std::wstring& value)
    {
        while (!value.empty() && iswspace(value.back()))
            value.pop_back();

        size_t start = 0;
        while (start < value.size() && iswspace(value[start]))
            ++start;

        const size_t slash = value.find_last_of(L"\\/");
        if (slash != std::wstring::npos && slash + 1 > start)
            start = slash + 1;
        while (start < value.size() && iswspace(value[start]))
            ++start;
        if (start > 0)
            value.erase(0, start);

        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
            return static_cast<wchar_t>(towlower(ch));
        });
    }

    inline std::wstring StemOf(std::wstring_view normalizedName)
    {
        constexpr std::wstring_view exeSuffix = L".exe";
        if (normalizedName.size() >= exeSuffix.size() &&
            normalizedName.substr(normalizedName.size() - exeSuffix.size()) == exeSuffix)
        {
            normalizedName.remove_suffix(exeSuffix.size());
        }
        return std::wstring(normalizedName);
    }

    struct Rule
    {
        std::wstring name;
        std::wstring stem;
    };

    class Matcher
    {
    public:
        static Matcher Compile(const std::vector<std::wstring>& processNames)
        {
            Matcher matcher;
            matcher.m_rules.reserve(processNames.size());

            for (auto item : processNames)
            {
                NormalizeProcessNameInPlace(item);
                if (item.empty())
                    continue;

                std::wstring stem = StemOf(item);
                if (stem.empty())
                    continue;

                const bool duplicate = std::any_of(
                    matcher.m_rules.begin(),
                    matcher.m_rules.end(),
                    [&](const Rule& rule) {
                        return rule.name == item || rule.stem == stem;
                    });
                if (!duplicate)
                    matcher.m_rules.push_back({ std::move(item), std::move(stem) });
            }
            return matcher;
        }

        bool empty() const noexcept
        {
            return m_rules.empty();
        }

        size_t size() const noexcept
        {
            return m_rules.size();
        }

        bool MatchesNormalized(
            std::wstring_view normalizedProcessName,
            std::wstring_view normalizedProcessStem) const noexcept
        {
            for (const auto& rule : m_rules)
            {
                if (rule.name == normalizedProcessName ||
                    rule.stem == normalizedProcessStem ||
                    normalizedProcessName.find(rule.name) != std::wstring_view::npos ||
                    normalizedProcessStem.find(rule.stem) != std::wstring_view::npos)
                {
                    return true;
                }
            }
            return false;
        }

    private:
        std::vector<Rule> m_rules;
    };
}
