#pragma once

#include "Theme.h"
#include "../dsp/WavetableEngine.h"

namespace axsynth
{
    // 3D wavetable view: all frames offset diagonally backwards and fading out,
    // the interpolated active frame drawn on top in the accent colour.
    class WavetableStack : public juce::Component
    {
    public:
        explicit WavetableStack (WavetableEngine&);

        void update (float effectivePosition);
        void paint (juce::Graphics&) override;

    private:
        juce::Path framePath (const float* values, float depth) const;

        WavetableEngine& engine;
        WavetableEngine::DisplayFrames frames {};
        juce::uint64 version = ~(juce::uint64) 0;
        float position = -1.0f;
    };
}
