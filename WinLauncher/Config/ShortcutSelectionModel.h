#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d2d1.h>
#include <vector>
#include <string>
#include <algorithm>

namespace ShortcutSelectionModel
{
    std::vector<int> NormalizeIndices(const std::vector<int>& indices, int shortcutCount);
    bool IsPendingDelete(const std::vector<int>& pendingIndices, int index);
    int CountVisible(int totalShortcuts, const std::vector<int>& pendingIndices);
    D2D1_POINT_2F ComputeSlotPosition(int slot);
    int HitTestGrid(POINT pt, float scrollY, int count);
    bool HitTestAddCard(POINT pt, float scrollY, float cardX, float cardY);
    bool IsPointOutsideWindow(HWND hWnd, POINT pt);
    bool HasDragExceededThreshold(POINT startPt, POINT currentPt, int threshold = 4);
    std::wstring BuildDeletePrompt(size_t count);

    template <typename TState>
    std::vector<int> GetSelectedIndices(const std::vector<TState>& states)
    {
        std::vector<int> indices;
        for (int i = 0; i < (int)states.size(); i++)
        {
            if (states[i].selected)
            {
                indices.push_back(i);
            }
        }
        return indices;
    }

    template <typename TState>
    void SelectRange(std::vector<TState>& states, int& anchorIndex, int targetIndex)
    {
        if (anchorIndex == -1) anchorIndex = targetIndex;
        for (auto& s : states) s.selected = false;
        int start = (std::min)(anchorIndex, targetIndex);
        int end = (std::max)(anchorIndex, targetIndex);
        int n = (int)states.size();
        for (int i = start; i <= end && i < n; i++)
        {
            if (i >= 0) states[i].selected = true;
        }
    }

    template <typename TState>
    void ToggleSelection(std::vector<TState>& states, int& anchorIndex, int targetIndex)
    {
        if (targetIndex >= 0 && targetIndex < (int)states.size())
        {
            states[targetIndex].selected = !states[targetIndex].selected;
            if (states[targetIndex].selected)
            {
                anchorIndex = targetIndex;
            }
        }
    }

    template <typename TState>
    void SelectSingle(std::vector<TState>& states, int& anchorIndex, int targetIndex)
    {
        if (targetIndex >= 0 && targetIndex < (int)states.size())
        {
            if (!states[targetIndex].selected)
            {
                for (auto& s : states) s.selected = false;
                states[targetIndex].selected = true;
            }
            anchorIndex = targetIndex;
        }
    }

    template <typename TState>
    void ClearSelection(std::vector<TState>& states, int& anchorIndex)
    {
        for (auto& s : states) s.selected = false;
        anchorIndex = -1;
    }
}
