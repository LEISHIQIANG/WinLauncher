#include "PopupSearchService.h"
#include "PopupSearchModel.h"
#include "PopupCommandDispatcher.h"
#include "../App/PluginManager.h"
#include <algorithm>
#include <cwctype>

namespace PopupSearchService
{
    namespace
    {
        std::wstring LowerCopy(const std::wstring& value)
        {
            std::wstring result = value;
            std::transform(result.begin(), result.end(), result.begin(),
                [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });
            return result;
        }
    }

    std::vector<SearchResult> CollectLocalShortcuts(
        const std::wstring& queryLower,
        const std::vector<RendPopupPage>& pages,
        const RendPopupPage& dockPage,
        const std::vector<int>* modelIndices)
    {
        std::vector<SearchResult> results;

        for (size_t pIndex = 0; pIndex < pages.size(); pIndex++)
        {
            const auto& page = pages[pIndex];
            for (size_t sIndex = 0; sIndex < page.shortcuts.size(); sIndex++)
            {
                const auto& sc = page.shortcuts[sIndex];
                if (LowerCopy(sc.name).find(queryLower) != std::wstring::npos)
                {
                    SearchResult item;
                    item.shortcut = sc;
                    if (sIndex < page.iconBitmaps.size())
                        item.bitmap = page.iconBitmaps[sIndex];
                    item.originalPageIndex = (modelIndices && static_cast<int>(pIndex) < static_cast<int>(modelIndices->size()))
                        ? (*modelIndices)[static_cast<int>(pIndex)]
                        : static_cast<int>(pIndex);
                    item.originalShortcutIndex = static_cast<int>(sIndex);
                    results.push_back(std::move(item));
                }
            }
        }

        for (size_t sIndex = 0; sIndex < dockPage.shortcuts.size(); sIndex++)
        {
            const auto& sc = dockPage.shortcuts[sIndex];
            if (LowerCopy(sc.name).find(queryLower) != std::wstring::npos)
            {
                SearchResult item;
                item.shortcut = sc;
                if (sIndex < dockPage.iconBitmaps.size())
                    item.bitmap = dockPage.iconBitmaps[sIndex];
                item.originalPageIndex = -2;
                item.originalShortcutIndex = static_cast<int>(sIndex);
                results.push_back(std::move(item));
            }
        }

        return results;
    }

    std::vector<SearchResult> CollectSlashCommands(
        const std::wstring& query,
        PluginManager* pluginManager)
    {
        std::vector<SearchResult> results;
        if (!pluginManager)
            return results;

        auto slashCommands = pluginManager->SearchSlashCommands(query);
        for (const auto& command : slashCommands)
        {
            SearchResult item;
            item.kind = SearchResult::Kind::SlashCommand;
            item.shortcut.name = L"/" + command.commandName;
            item.originalPageIndex = -1;
            item.originalShortcutIndex = -1;
            item.pluginId = command.pluginId;
            item.pluginCommandId = command.commandId;
            item.subtitle = command.usage.empty() ? command.description : command.usage;
            item.iconPath = command.icon;
            results.push_back(std::move(item));
        }

        return results;
    }

    std::vector<SearchResult> CollectPluginResults(
        const std::wstring& query,
        PluginManager* pluginManager,
        bool& pluginSearchRunning)
    {
        std::vector<SearchResult> results;
        pluginSearchRunning = false;
        if (!pluginManager)
            return results;

        auto pluginCommands = pluginManager->SearchCommands(query);
        for (const auto& command : pluginCommands)
        {
            SearchResult item;
            item.kind = SearchResult::Kind::PluginCommand;
            item.shortcut.name = command.title;
            item.originalPageIndex = -1;
            item.originalShortcutIndex = -1;
            item.pluginId = command.pluginId;
            item.pluginCommandId = command.commandId;
            item.subtitle = command.description;
            item.iconPath = command.icon;
            results.push_back(std::move(item));
        }

        auto pluginResults = pluginManager->GetCachedSearchResults(query);
        for (const auto& result : pluginResults)
        {
            SearchResult item;
            item.kind = SearchResult::Kind::PluginSearchResult;
            item.shortcut.name = result.title;
            item.originalPageIndex = -1;
            item.originalShortcutIndex = -1;
            item.pluginId = result.pluginId;
            item.pluginCommandId = result.commandId;
            item.subtitle = result.description;
            item.iconPath = result.icon;
            results.push_back(std::move(item));
        }

        pluginSearchRunning = pluginManager->IsSearchRunning(query);
        return results;
    }

    void SortByRelevance(
        std::vector<SearchResult>& results,
        const std::wstring& queryLower,
        int sortMode,
        bool slashMode,
        std::shared_ptr<UsageHistoryStore> usageHistory)
    {
        if (sortMode != 1 || slashMode || results.empty())
            return;

        std::stable_sort(results.begin(), results.end(),
            [&](const SearchResult& a, const SearchResult& b)
            {
                auto score = [&](const SearchResult& item)
                {
                    UsageHistoryEntry usage{};
                    if (usageHistory)
                    {
                        const std::wstring key = PopupCommandDispatcher::UsageKey(
                            item.kind == SearchResult::Kind::LocalShortcut,
                            item.shortcut.id, item.pluginId, item.pluginCommandId);
                        usage = usageHistory->Get(key);
                    }
                    const auto sortKey = PopupSearchModel::SortKey(
                        item.shortcut.name, queryLower,
                        { usage.launchCount, usage.lastUsedUtc });
                    return std::tuple_cat(sortKey,
                        std::make_tuple(item.originalPageIndex, item.originalShortcutIndex));
                };
                return score(a) < score(b);
            });
    }
}
