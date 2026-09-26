#pragma once

#include "../ShortcutManager.h"
#include <memory>

class UsageHistoryStore;

// UI-free usage-frequency sorter for rendered popup pages. Reorders shortcuts
// (and their parallel icon bitmaps) by launch count while preserving the
// partially-built icon cache invariant documented in the implementation.
namespace PopupShortcutSorter
{
    void SortPageByUsage(RendPopupPage& page, const std::shared_ptr<UsageHistoryStore>& usageHistory);
}
