#pragma once
#include <cmath>

// A directed transition plus at most one queued step. Coordinates are not
// wrapped until rendering, so a burst cannot cancel itself after a full lap.
class PopupWheelState
{
public:
    void Reset(int page) { target = page; pending = 0; remainder = 0; active = false; }
    bool Wheel(int delta, float position)
    {
        if ((delta > 0 && remainder < 0) || (delta < 0 && remainder > 0)) remainder = 0;
        remainder += delta;
        const int steps = remainder / 120;
        remainder %= 120;
        if (!steps) return false;
        Request(steps > 0 ? -1 : 1, position);
        return true;
    }
    void Request(int direction, float position)
    {
        if (!active) { target += direction; active = true; }
        else if ((target - position) * direction < 0)
        {
            pending = 0;
            target = direction > 0 ? static_cast<int>(std::floor(position)) + 1
                                   : static_cast<int>(std::ceil(position)) - 1;
        }
        else pending = direction;
    }
    bool AdvanceQueued(float position)
    {
        if (!pending || std::abs(target - position) > 0.25f) return false;
        target += pending;
        pending = 0;
        return true;
    }
    bool Arrive()
    {
        if (pending) { target += pending; pending = 0; return true; }
        active = false;
        return false;
    }
    static int Page(int coordinate, int count) { return count > 0 ? (coordinate % count + count) % count : 0; }
    int target = 0;
    int pending = 0;
    int remainder = 0;
    bool active = false;
};
