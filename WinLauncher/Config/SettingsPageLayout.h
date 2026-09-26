#pragma once

#include <d2d1.h>
#include <Windows.h>

// Pure layout geometry for the settings page: content bounds, card rects and
// point tests shared by painting, hover and click routing. Everything is a
// pure function of the category layout constants so hit testing and drawing
// cannot drift apart. Label tables and text utilities stay in SettingsPage.cpp.
namespace SettingsPageLayout
{
    constexpr float CONTENT_LEFT = 160.0f;
    constexpr float CONTENT_RIGHT = 510.0f;
    constexpr float TWO_COLUMN_GAP = 10.0f;
    constexpr float TWO_COLUMN_WIDTH = (CONTENT_RIGHT - CONTENT_LEFT - TWO_COLUMN_GAP) / 2.0f;
    constexpr float TWO_COLUMN_STEP = TWO_COLUMN_WIDTH + TWO_COLUMN_GAP;
    constexpr float CARD_HEIGHT = 32.0f;

    constexpr float GLOBAL_SCALE_CARD_LEFT = 160.0f;
    constexpr float ANIMATION_DURATION_CARD_TOP = 112.0f;
    constexpr float ANIMATION_DURATION_TRACK_Y = 130.0f;
    constexpr float ANIMATION_DURATION_APPLY_TOP = 118.0f;
    constexpr float ANIMATION_DURATION_APPLY_BOTTOM = 142.0f;
    constexpr int ANIMATION_DURATION_MIN_MS = 50;
    constexpr int ANIMATION_DURATION_MAX_MS = 1000;
    constexpr int ANIMATION_DURATION_STEP_MS = 50;

    constexpr float GLOBAL_SCALE_CARD_TOP = 154.0f;
    constexpr float GLOBAL_SCALE_CARD_RIGHT = CONTENT_RIGHT;
    constexpr float GLOBAL_SCALE_CARD_BOTTOM = 190.0f;
    constexpr float GLOBAL_SCALE_TRACK_LEFT = 250.0f;
    constexpr float GLOBAL_SCALE_TRACK_RIGHT = 402.0f;
    constexpr float GLOBAL_SCALE_TRACK_Y = 172.0f;
    constexpr float GLOBAL_SCALE_APPLY_LEFT = 448.0f;
    constexpr float GLOBAL_SCALE_APPLY_TOP = 160.0f;
    constexpr float GLOBAL_SCALE_APPLY_RIGHT = 506.0f;
    constexpr float GLOBAL_SCALE_APPLY_BOTTOM = 184.0f;
    constexpr float SYSTEM_SETTINGS_CONTENT_OFFSET = 42.0f;

    constexpr float TRIGGER_TOP = 108.0f;
    constexpr float TRIGGER_BOTTOM = 136.0f;
    constexpr int TRIGGER_PRESET_BUTTON = 3;
    constexpr int POPUP_ALIGN_PRESET_BUTTON = 3;
    constexpr int POPUP_ALIGN_PRIMARY_COUNT = 3;
    constexpr int POPUP_ALIGN_PRESET_LAST = 10;
    constexpr int FOUR_SEGMENT_COUNT = 4;
    constexpr float FOUR_SEGMENT_GAP = 10.0f;
    constexpr float FOUR_SEGMENT_WIDTH = (CONTENT_RIGHT - CONTENT_LEFT - FOUR_SEGMENT_GAP * (FOUR_SEGMENT_COUNT - 1)) / FOUR_SEGMENT_COUNT;

    inline D2D1_RECT_F TwoColumnRect(int col, float top, float height = CARD_HEIGHT)
    {
        const float left = CONTENT_LEFT + col * TWO_COLUMN_STEP;
        return D2D1::RectF(left, top, left + TWO_COLUMN_WIDTH, top + height);
    }

    inline D2D1_RECT_F FourSegmentRect(int index, float top, float bottom)
    {
        const float left = CONTENT_LEFT + index * (FOUR_SEGMENT_WIDTH + FOUR_SEGMENT_GAP);
        return D2D1::RectF(left, top, left + FOUR_SEGMENT_WIDTH, bottom);
    }

    inline D2D1_RECT_F PopupAlignRect(int index)
    {
        return FourSegmentRect(index, 182.0f, 210.0f);
    }

    inline D2D1_RECT_F PopupBehaviorRect(int index, float top)
    {
        return TwoColumnRect(index, top, 28.0f);
    }

    inline D2D1_RECT_F TriggerBlacklistRect()
    {
        return D2D1::RectF(CONTENT_LEFT, 430.0f, CONTENT_RIGHT, 462.0f);
    }

    inline D2D1_RECT_F TriggerBlacklistEditRect()
    {
        return D2D1::RectF(438.0f, 436.0f, 502.0f, 456.0f);
    }

    inline D2D1_RECT_F AboutOpenSourceLinkRect()
    {
        return D2D1::RectF(CONTENT_LEFT, 412.0f, CONTENT_RIGHT, 442.0f);
    }

    inline bool PointInRect(const D2D1_RECT_F& rect, POINT pt)
    {
        return pt.x >= rect.left && pt.x <= rect.right && pt.y >= rect.top && pt.y <= rect.bottom;
    }

    inline D2D1_RECT_F TriggerButtonRect(int index)
    {
        return FourSegmentRect(index, TRIGGER_TOP, TRIGGER_BOTTOM);
    }
}
