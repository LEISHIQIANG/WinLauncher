#include "PopupSearchModel.h"
#include "PinyinHelper.h"
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

    int prefixScore = 4;
    int positionScore = 10000;

    const size_t pos = name.find(needle);
    if (pos == 0)
    {
        prefixScore = 0;
        positionScore = 0;
    }
    else if (pos != std::wstring::npos)
    {
        prefixScore = 3;
        positionScore = static_cast<int>(pos);
    }
    else
    {
        bool isInitials = false, isFullPinyin = false;
        if (PinyinHelper::Match(title, needle, isInitials, isFullPinyin))
        {
            if (isInitials)
            {
                std::wstring initials = PinyinHelper::GetInitials(title);
                size_t ipos = initials.find(needle);
                prefixScore = (ipos == 0) ? 1 : 2;
                positionScore = static_cast<int>(ipos == std::wstring::npos ? 100 : ipos);
            }
            else if (isFullPinyin)
            {
                std::wstring full = PinyinHelper::GetFullPinyin(title);
                size_t fpos = full.find(needle);
                prefixScore = (fpos == 0) ? 2 : 3;
                positionScore = static_cast<int>(fpos == std::wstring::npos ? 100 : fpos);
            }
        }
    }

    return { prefixScore, positionScore, -static_cast<int64_t>(usage.launchCount), -static_cast<int64_t>(usage.lastUsedUtc) };
}
