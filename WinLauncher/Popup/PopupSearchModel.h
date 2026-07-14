#pragma once

#include <cstdint>
#include <string>
#include <tuple>

namespace PopupSearchModel
{
    struct Usage
    {
        uint64_t launchCount = 0;
        uint64_t lastUsedUtc = 0;
    };

    // Lower keys sort first. This is deliberately UI-free so search ordering
    // can be covered without a popup HWND, D2D target or plugin DLL.
    std::tuple<int, int, int64_t, int64_t> SortKey(const std::wstring& title, const std::wstring& query, const Usage& usage);
}
