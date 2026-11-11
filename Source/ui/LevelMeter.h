#pragma once

#include "Theme.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace axsynth
{
    // 6 px vertical level bar, no scale, peak hold 1 s.
    class LevelMeter : public juce::Component
    {
    public:
        LevelMeter() { setInterceptsMouseClicks (false, false); }

        // Feed the peak (linear) collected since the last call; dt in seconds.
        void push (float peak, double dtSeconds);
        void paint (juce::Graphics&) override;

        static constexpr float minDb = -60.0f;
        static constexpr double holdSeconds = 1.0;

    private:
        static float toProportion (float db) noexcept { return juce::jlimit (0.0f, 1.0f, (db - minDb) / -minDb); }

        float levelDb = minDb, holdDb = minDb;
        double holdAge = 0.0;
    };
}
