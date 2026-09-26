#include "ShortcutSelectionModel.h"
#include "../DpiHelper.h"
#include <cmath>

namespace ShortcutSelectionModel
{
    std::vector<int> NormalizeIndices(const std::vector<int>& indices, int shortcutCount)
    {
        std::vector<int> normalized = indices;
        std::sort(normalized.begin(), normalized.end());
        normalized.erase(std::unique(normalized.begin(), normalized.end()), normalized.end());
        normalized.erase(
            std::remove_if(normalized.begin(), normalized.end(),
                [shortcutCount](int index) { return index < 0 || index >= shortcutCount; }),
            normalized.end());
        return normalized;
    }

    bool IsPendingDelete(const std::vector<int>& pendingIndices, int index)
    {
        return std::binary_search(pendingIndices.begin(), pendingIndices.end(), index);
    }

    int CountVisible(int totalShortcuts, const std::vector<int>& pendingIndices)
    {
        int count = 0;
        for (int i = 0; i < totalShortcuts; i++)
        {
            if (!IsPendingDelete(pendingIndices, i))
            {
                count++;
            }
        }
        return count;
    }

    D2D1_POINT_2F ComputeSlotPosition(int slot)
    {
        return D2D1::Point2F(
            (float)(160 + (slot % 5) * 72),
            (float)(72 + (slot / 5) * 72)
        );
    }

    int HitTestGrid(POINT pt, float scrollY, int count)
    {
        if (pt.x < 160 || pt.x > 510 || pt.y < 72 || pt.y > 440) return -1;

        float scrolledY = pt.y - 72.0f + scrollY;
        for (int i = 0; i < count; i++)
        {
            int col = i % 5, row = i / 5;
            float X = (float)(160 + col * 72);
            float Y = (float)(row * 72);
            if (pt.x >= X && pt.x <= X + 62.0f && scrolledY >= Y && scrolledY <= Y + 62.0f)
                return i;
        }
        return -1;
    }

    bool HitTestAddCard(POINT pt, float scrollY, float cardX, float cardY)
    {
        if (pt.x < 160 || pt.x > 510 || pt.y < 72 || pt.y > 440) return false;

        float scrolledY = pt.y - 72.0f + scrollY;
        float localAddY = cardY - 72.0f;
        return (pt.x >= cardX && pt.x <= cardX + 62.0f &&
            scrolledY >= localAddY && scrolledY <= localAddY + 62.0f);
    }

    bool IsPointOutsideWindow(HWND hWnd, POINT pt)
    {
        if (!hWnd) return false;

        RECT cr{};
        if (!GetClientRect(hWnd, &cr)) return false;

        float scale = DpiHelper::GetWindowScale(hWnd);
        float w = (float)(cr.right - cr.left) / scale;
        float h = (float)(cr.bottom - cr.top) / scale;

        return pt.x < 0 || pt.y < 0 || (float)pt.x >= w || (float)pt.y >= h;
    }

    bool HasDragExceededThreshold(POINT startPt, POINT currentPt, int threshold)
    {
        int dx = currentPt.x - startPt.x;
        int dy = currentPt.y - startPt.y;
        return std::abs(dx) > threshold || std::abs(dy) > threshold;
    }

    std::wstring BuildDeletePrompt(size_t count)
    {
        return count == 1
            ? L"确定要删除该快捷方式吗？"
            : L"确定要删除选中的 " + std::to_wstring(count) + L" 个快捷方式吗？";
    }
}
