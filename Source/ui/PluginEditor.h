#pragma once

#include "../PluginProcessor.h"
#include "Theme.h"
#include "Knob.h"
#include "CameraView.h"
#include "WavetableStack.h"
#include "LevelMeter.h"
#include "SetupPanel.h"
#include "Visuals.h"

namespace axsynth
{
    // Small flat gear icon button.
    class GearButton : public juce::Button
    {
    public:
        GearButton() : juce::Button ("setup") {}
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    };

    // Preset name as flat caps text (no box), opens the preset menu.
    class PresetButton : public juce::Button
    {
    public:
        PresetButton() : juce::Button ("preset") {}
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    };

    // Plugin UI. Everything is laid out at theme::baseWidth x baseHeight inside 'content'
    // and scaled uniformly with the window (fixed aspect ratio). Controls are grouped in
    // sections, each with a live display.
    class AxSynthEditor : public juce::AudioProcessorEditor, private juce::Timer
    {
    public:
        explicit AxSynthEditor (AxSynthProcessor&);
        ~AxSynthEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        // Polls processor/engine/camera state; the 30 Hz timer calls this. Public for tests.
        void refresh();

        bool isSetupPanelVisible() const { return setupPanel.isVisible(); }
        void setSetupPanelVisible (bool shouldShow);
        bool isCameraRectVisible() const { return cameraView.isRectVisible(); }

    private:
        void timerCallback() override { refresh(); }
        void layoutContent();
        void toggleLfoSync();
        void armCloseOnImage();
        void showPresetMenu();

        AxSynthProcessor& processor;
        theme::LookAndFeel lookAndFeel;

        juce::Component content;
        CameraView cameraView;
        SetupPanel setupPanel;
        WavetableStack stack;
        GearButton gear;
        LevelMeter meter;

        SectionPanel secWavetable { "Wavetable" }, secVoice { "Voice" }, secFilter { "Filter" },
                     secLfo { "LFO" }, secEnvelope { "Envelope" }, secFx { "Effects" }, secOutput { "Output" };
        HarmonicsView harmonicsView;
        UnisonView unisonView;
        FilterView filterView;
        LfoView lfoView;
        EnvelopeView envelopeView;
        FxView fxView;

        Knob position, lines, spread, tone, contrast, motion,
             unison, detune, sub, glide,
             cutoff, resonance, filterEnv, drive, velFilter,
             lfoRate, lfoAmount,
             attack, decay, sustain, release,
             chorus, delay, delayTime, delayFeedback, reverb, reverbSize,
             output;
        ChoiceButtons filterType, filterSlope, scanMode, freeze, mono, lfoSync;
        PresetButton presetButton;
        HarmonicMatrix harmonicsScratch {};

        double lastRefreshMs = 0.0;
        bool closeSetupOnImage = false;
        juce::uint64 frameCountWhenArmed = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AxSynthEditor)
    };
}
