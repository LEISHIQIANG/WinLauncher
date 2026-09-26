#pragma once

#include <cmath>

// Pure spring integrator for the popup page-scroll animation. It knows
// nothing about HWND, timers, wheel state or view models so the paging
// physics stays testable: PopupWindow owns when to step and what to sync,
// this header owns how one frame of the spring evolves.
namespace PopupScrollAnimator
{
    // Tuned for a critically damped snap: stiff enough to feel immediate,
    // soft enough not to overshoot.
    constexpr float Stiffness = 400.0f;
    constexpr float Damping = 40.0f;

    // Physics sub-stepping for stability and smoothness (2 ms steps).
    constexpr float SubStepSeconds = 0.002f;

    // Only snap after the spring has become visually stationary.  A larger
    // threshold makes the last visible pixels jump instead of settling
    // smoothly.
    constexpr float SettleDistancePx = 0.75f;
    constexpr float SettleVelocityPxPerSecond = 36.0f;

    struct SpringState
    {
        float position = 0.0f;
        float velocity = 0.0f;
    };

    // Integrates one frame toward the target. dtSeconds is clamped
    // internally exactly like the original inline code (0.1 s ceiling,
    // 1 ms floor).
    inline SpringState Integrate(SpringState state, float target, float dtSeconds)
    {
        if (dtSeconds > 0.1f) dtSeconds = 0.1f;
        if (dtSeconds <= 0.0f) dtSeconds = 0.001f;

        float remainingTime = dtSeconds;
        while (remainingTime > 0.0f)
        {
            const float currentStep = (remainingTime < SubStepSeconds) ? remainingTime : SubStepSeconds;
            if (currentStep <= 0.0f) break;

            const float error = target - state.position;
            const float force = error * Stiffness - state.velocity * Damping;
            state.velocity += force * currentStep;
            state.position += state.velocity * currentStep;

            remainingTime -= currentStep;
        }
        return state;
    }

    // True when the spring is visually stationary relative to a page width
    // measured in physical pixels.
    inline bool IsSettled(const SpringState& state, float target, float pageWidthPx)
    {
        const float remainingDistancePx = std::fabs(target - state.position) * pageWidthPx;
        const float remainingVelocityPx = std::fabs(state.velocity) * pageWidthPx;
        return remainingDistancePx <= SettleDistancePx &&
            remainingVelocityPx <= SettleVelocityPxPerSecond;
    }
}
