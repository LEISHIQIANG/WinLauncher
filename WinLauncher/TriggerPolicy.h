#pragma once

#include <Windows.h>

// Stateless matching rules for every popup trigger preset. Keeping this
// separate from the low-level hook makes the full preset matrix testable
// without installing a global Windows hook.
namespace TriggerPolicy
{
    enum class Button
    {
        None,
        Middle,
        XButton1,
        XButton2,
    };

    struct MatchResult
    {
        bool activated = false;
        Button button = Button::None;
    };

    constexpr int kDefaultTriggerType = 0;
    constexpr int kLastTriggerType = 7;

    inline int NormalizeTriggerType(int type)
    {
        return type >= kDefaultTriggerType && type <= kLastTriggerType
            ? type
            : kDefaultTriggerType;
    }

    inline MatchResult Match(int type, WPARAM message, DWORD mouseData,
                             bool ctrlDown, bool shiftDown, bool altDown)
    {
        const int normalizedType = NormalizeTriggerType(type);
        const WORD xButton = HIWORD(mouseData);

        switch (normalizedType)
        {
        case 0:
            return message == WM_MBUTTONDOWN ? MatchResult{ true, Button::Middle } : MatchResult{};
        case 1:
            return message == WM_XBUTTONDOWN && xButton == XBUTTON1 ? MatchResult{ true, Button::XButton1 } : MatchResult{};
        case 2:
            return message == WM_XBUTTONDOWN && xButton == XBUTTON2 ? MatchResult{ true, Button::XButton2 } : MatchResult{};
        case 3:
            return message == WM_MBUTTONDOWN && ctrlDown ? MatchResult{ true, Button::Middle } : MatchResult{};
        case 4:
            return message == WM_MBUTTONDOWN && shiftDown ? MatchResult{ true, Button::Middle } : MatchResult{};
        case 5:
            return message == WM_MBUTTONDOWN && altDown ? MatchResult{ true, Button::Middle } : MatchResult{};
        case 6:
            return message == WM_XBUTTONDOWN && xButton == XBUTTON1 && ctrlDown ? MatchResult{ true, Button::XButton1 } : MatchResult{};
        case 7:
            return message == WM_XBUTTONDOWN && xButton == XBUTTON2 && ctrlDown ? MatchResult{ true, Button::XButton2 } : MatchResult{};
        default: return {};
        }
    }
}
