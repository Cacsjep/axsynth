#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace axsynth::presets
{
    // Factory sound presets. Each sets every parameter: listed values as given,
    // everything else to its default. Camera settings and rectangle are not touched.
    struct Preset
    {
        const char* name;
        std::vector<std::pair<const char*, float>> values; // real (not normalised) values
    };

    const std::vector<Preset>& all();

    // Applies preset i to the parameters, notifying the host. Message thread.
    void apply (juce::AudioProcessorValueTreeState&, int index);
}
