#pragma once

#include "../ShortcutManager.h"
#include "../Services/UsageHistoryStore.h"
#include <memory>
#include <string>
#include <vector>

class PluginManager;

// UI-free search engine that collects results from local shortcuts, dock bar,
// plugin commands, slash commands and async plugin results, then optionally
// sorts by usage-weighted relevance.
//
// All functions are stateless — they take their data through parameters so
// they can be tested without a HWND, D2D target, plugin DLL or thread pool.

namespace PopupSearchService
{
    struct SearchResult
    {
        enum class Kind
        {
            LocalShortcut,
            PluginCommand,
            PluginSearchResult,
            SlashCommand
        };

        Kind kind = Kind::LocalShortcut;
        RendShortcutInfo shortcut;
        ID2D1Bitmap* bitmap = nullptr;
        int originalPageIndex = -1;
        int originalShortcutIndex = -1;
        std::wstring pluginId;
        std::wstring pluginCommandId;
        std::wstring subtitle;
        std::wstring iconPath;
    };

    // Collect local shortcut matches from all pages and the dock bar.
    // queryLower must already be lowercased by the caller.
    // If modelIndices is non-null, originalPageIndex for each result is
    // mapped through it (render-page-index -> model-page-index); otherwise
    // the render-page index is used directly.
    std::vector<SearchResult> CollectLocalShortcuts(
        const std::wstring& queryLower,
        const std::vector<RendPopupPage>& pages,
        const RendPopupPage& dockPage,
        const std::vector<int>* modelIndices = nullptr);

    // Collect slash-command matches from the plugin manager.
    std::vector<SearchResult> CollectSlashCommands(
        const std::wstring& query,
        PluginManager* pluginManager);

    // Collect plugin command matches and cached async search results.
    // pluginSearchRunning is set true when further results may arrive.
    std::vector<SearchResult> CollectPluginResults(
        const std::wstring& query,
        PluginManager* pluginManager,
        bool& pluginSearchRunning);

    // Sort results by relevance using PopupSearchModel::SortKey and
    // UsageHistoryStore for usage-weighted ordering.  Does nothing when
    // sortMode != 1 or slashMode is true.
    void SortByRelevance(
        std::vector<SearchResult>& results,
        const std::wstring& queryLower,
        int sortMode,
        bool slashMode,
        std::shared_ptr<UsageHistoryStore> usageHistory);
}
