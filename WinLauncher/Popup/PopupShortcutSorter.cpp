#include "PopupShortcutSorter.h"

#include "../Services/UsageHistoryStore.h"
#include <algorithm>
#include <numeric>
#include <vector>

void PopupShortcutSorter::SortPageByUsage(RendPopupPage& page, const std::shared_ptr<UsageHistoryStore>& usageHistory)
{
    if (!usageHistory || page.shortcuts.size() < 2)
        return;

    // Icon bitmaps are indexed in parallel with shortcuts. Do not reorder a
    // partially-built cache because it would associate the wrong bitmap with a
    // shortcut while a background icon refresh is running.
    if (!page.iconBitmaps.empty() && page.iconBitmaps.size() != page.shortcuts.size())
        return;

    std::vector<size_t> order(page.shortcuts.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](size_t left, size_t right) {
        const auto usageFor = [&](size_t index) {
            const auto& id = page.shortcuts[index].id;
            return id.empty() ? uint64_t{ 0 } : usageHistory->Get(L"shortcut:" + id).launchCount;
        };
        return usageFor(left) > usageFor(right);
    });

    if (std::is_sorted(order.begin(), order.end()))
        return;

    std::vector<RendShortcutInfo> sortedShortcuts;
    sortedShortcuts.reserve(page.shortcuts.size());
    std::vector<ID2D1Bitmap*> sortedBitmaps;
    if (!page.iconBitmaps.empty())
        sortedBitmaps.reserve(page.iconBitmaps.size());

    for (size_t index : order)
    {
        sortedShortcuts.push_back(std::move(page.shortcuts[index]));
        if (!page.iconBitmaps.empty())
            sortedBitmaps.push_back(page.iconBitmaps[index]);
    }

    page.shortcuts.swap(sortedShortcuts);
    if (!page.iconBitmaps.empty())
        page.iconBitmaps.swap(sortedBitmaps);
}
