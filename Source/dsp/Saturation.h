#pragma once

#include <cmath>

namespace axsynth::sat
{
    // First-order antiderivative anti-aliasing (ADAA): instead of f(x[n]) the output is
    // the mean of f over the segment x[n-1]..x[n], (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1]).
    // That suppresses most of the aliasing a waveshaper creates, without oversampling.

    // log(cosh(x)), the antiderivative of tanh, stable for large |x|.
    inline float logCosh (float x) noexcept
    {
        const float a = std::abs (x);
        return a + std::log1p (std::exp (-2.0f * a)) - 0.69314718f;
    }

    struct TanhAdaa
    {
        float prev = 0.0f, prevF = 0.0f;
        void reset() noexcept { prev = 0.0f; prevF = 0.0f; }

        float process (float x) noexcept
        {
            const float fx = logCosh (x);
            const float d = x - prev;
            const float y = std::abs (d) > 1.0e-4f ? (fx - prevF) / d : std::tanh (0.5f * (x + prev));
            prev = x;
            prevF = fx;
            return y;
        }
    };

    // Output soft clipper: linear up to the knee, tanh-shaped above, ceiling knee + room.
    inline constexpr float knee = 0.9f, room = 0.1f;

    inline float softClip (float x) noexcept
    {
        const float a = std::abs (x);
        if (a <= knee) return x;
        return std::copysign (knee + room * std::tanh ((a - knee) / room), x);
    }

}
