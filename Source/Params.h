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
    inline constexpr const char* tone        = "tone";
    inline constexpr const char* contrast    = "contrast";
    inline constexpr const char* lines       = "lines";
    inline constexpr const char* spread      = "spread";
    inline constexpr const char* filterEnv   = "filterEnv";
    inline constexpr const char* filterType  = "filterType";
    inline const juce::StringArray filterTypeNames { "LP", "HP", "BP" };
    inline constexpr const char* filterSlope = "filterSlope";
    inline const juce::StringArray filterSlopeNames { "12", "24" };

    inline constexpr const char* motion      = "motion";
    inline constexpr const char* freeze      = "freeze";
    inline constexpr const char* scanMode    = "scanMode";
    inline const juce::StringArray scanModeNames { "Vertical", "Horizontal" };
    inline constexpr const char* drive       = "drive";
    inline constexpr const char* velFilter   = "velFilter";
    inline constexpr const char* mono        = "mono";
    inline constexpr const char* glide       = "glide";
    inline constexpr const char* chorusMix   = "chorusMix";
    inline constexpr const char* delayMix    = "delayMix";
    inline constexpr const char* delayTime   = "delayTime";
    inline constexpr const char* delayFeedback = "delayFeedback";
    inline constexpr const char* reverbMix   = "reverbMix";
    inline constexpr const char* reverbSize  = "reverbSize";

    // Delay time: tempo-synced note values, length in quarter notes.
    inline const juce::StringArray delayTimeNames { "1/16", "1/8", "1/8.", "1/4", "1/4.", "1/2" };
    inline constexpr double delayTimeQuarters[] { 0.25, 0.5, 0.75, 1.0, 1.5, 2.0 };
    inline constexpr const char* unison      = "unison";
    inline constexpr const char* detune      = "detune";
    inline constexpr const char* sub         = "sub";
    inline constexpr const char* attack      = "attack";
    inline constexpr const char* decay       = "decay";
    inline constexpr const char* sustain     = "sustain";
    inline constexpr const char* release     = "release";
    inline constexpr const char* output      = "output";

    // Host-sync LFO divisions, length in quarter notes.
    inline const juce::StringArray syncNames { "1/16", "1/8", "1/4", "1/2", "1 Takt", "2 Takte", "4 Takte" };
    // Note divisions in quarter notes; bar divisions (syncBars > 0) follow the host time signature.
    inline constexpr double syncQuarters[] { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0 };
    inline constexpr double syncBars[]     { 0.0,  0.0, 0.0, 0.0, 1.0, 2.0, 4.0 };
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
