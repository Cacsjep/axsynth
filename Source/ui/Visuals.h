#pragma once

#include "Theme.h"
#include "../Params.h"
#include "../dsp/Wavetable.h"
#include <array>

namespace axsynth
{
    // Flat panel behind a group of controls, with a small caps title.
    class SectionPanel : public juce::Component
    {
    public:
        explicit SectionPanel (juce::String title) : name (std::move (title)) { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics&) override;

    private:
        juce::String name;
    };

    // Base for the small live displays: repaints only when its data changed.
    class Display : public juce::Component
    {
    public:
        Display() { setInterceptsMouseClicks (false, false); }

    protected:
        juce::Rectangle<float> plotArea() const { return getLocalBounds().toFloat().reduced (1.0f); }
        void paintBackground (juce::Graphics&) const;
    };

    // Harmonic amplitudes of the frame that is playing (64 bars, dB scaled).
    class HarmonicsView : public Display
    {
    public:
        void setHarmonics (const std::array<float, numHarmonics>& amps);
        void paint (juce::Graphics&) override;

    private:
        std::array<float, numHarmonics> values {};
    };

    // Unison oscillators as dots: x = stereo position, y = detune; sub as a bar below.
    class UnisonView : public Display
    {
    public:
        void setVoice (int unison, float detuneCents, float sub);
        void paint (juce::Graphics&) override;

    private:
        int count = 1;
        float detune = 0.0f, subLevel = 0.0f;
    };

    // One LFO cycle scaled by the amount, with the live phase as a dot.
    class LfoView : public Display
    {
    public:
        void setLfo (float phase, float amount, bool synced, const juce::String& syncText);
        void paint (juce::Graphics&) override;

    private:
        float phase = 0.0f, amount = 0.0f;
        bool sync = false;
        juce::String syncLabel;
    };

    // Filter magnitude response; a second curve shows the live cutoff with the envelope.
    class FilterView : public Display
    {
    public:
        void setFilter (int type, float cutoffHz, float liveCutoffHz, float resonance, bool steep = false);
        void paint (juce::Graphics&) override;

    private:
        juce::Path responsePath (float cutoffHz) const;
        int filterType = 0;
        bool steepSlope = false;
        float cutoff = 8000.0f, liveCutoff = 8000.0f, res = 0.1f;
    };

    // ADSR shape with the newest voice's position as a dot.
    class EnvelopeView : public Display
    {
    public:
        // Times in seconds, sustain 0..1; level/stage/releaseFrom from the newest voice.
        void setEnvelope (float a, float d, float s, float r, float level, int stage, float releaseFrom);
        void paint (juce::Graphics&) override;

    private:
        float attack = 0.01f, decay = 0.3f, sustain = 1.0f, release = 0.3f;
        float level = 0.0f, releaseStart = 0.0f;
        int stage = 0;
    };

    // Delay echoes (spacing = time, height = mix and feedback) and the reverb tail.
    class FxView : public Display
    {
    public:
        void setFx (float chorus, float delayMix, float delayBars, float feedback, float reverbMix, float reverbSize);
        void paint (juce::Graphics&) override;

    private:
        float chorusMix = 0.0f, delay = 0.0f, delayLen = 0.125f, fb = 0.35f, rev = 0.0f, size = 0.6f;
    };

    // Small segmented buttons bound to a parameter (with host gestures).
    // Choice parameter: one label per choice. Bool parameter: one label, toggles.
    class ChoiceButtons : public juce::Component
    {
    public:
        ChoiceButtons (juce::RangedAudioParameter&, juce::StringArray labels);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        int getSelected() const noexcept { return selected; }

    private:
        juce::ParameterAttachment attachment;
        juce::StringArray names;
        bool isToggle;
        int selected = 0;
    };
}
