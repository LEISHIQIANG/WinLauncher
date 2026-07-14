#include "PopupSearchModel.h"
#include <algorithm>
#include <cwctype>

namespace
{
    std::wstring LowerCopy(std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });
        return value;
    }
}

std::tuple<int, int, int64_t, int64_t> PopupSearchModel::SortKey(const std::wstring& title, const std::wstring& query, const Usage& usage)
{
    const std::wstring name = LowerCopy(title);
    const std::wstring needle = LowerCopy(query);
    const size_t position = name.find(needle);
    const int positionScore = position == std::wstring::npos ? 10000 : static_cast<int>(position);
    const int prefixScore = name.rfind(needle, 0) == 0 ? 0 : 1;
    return { prefixScore, positionScore, -static_cast<int64_t>(usage.launchCount), -static_cast<int64_t>(usage.lastUsedUtc) };
}
