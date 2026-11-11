#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace axsynth::params
{
    inline constexpr const char* position    = "position";
    inline constexpr const char* lfoRate     = "lfoRate";
    inline constexpr const char* lfoSync     = "lfoSync";
    inline constexpr const char* lfoRateSync = "lfoRateSync";
    inline constexpr const char* lfoAmount   = "lfoAmount";
    inline constexpr const char* cutoff      = "cutoff";
    inline constexpr const char* resonance   = "resonance";
    inline constexpr const char* attack      = "attack";
    inline constexpr const char* release     = "release";
    inline constexpr const char* output      = "output";

    // Host-sync LFO divisions, length in quarter notes.
    inline const juce::StringArray syncNames { "1/16", "1/8", "1/4", "1/2", "1 Takt", "2 Takte", "4 Takte" };
    inline constexpr double syncQuarters[] { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0 };
    inline constexpr int syncDefaultIndex = 2; // 1/4

    // Output gain: slider minimum maps to silence (-inf).
    inline constexpr float outputMinDb = -60.0f;
    inline constexpr float outputMaxDb = 6.0f;

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // Display helpers shared with the UI (knob centre text).
    juce::String formatHz (float hz);
    juce::String formatTime (float seconds);
    juce::String formatDb (float db);
}
