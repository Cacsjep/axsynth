#pragma once

#include "../PluginProcessor.h"
#include "Theme.h"
#include "Knob.h"
#include "CameraView.h"
#include "WavetableStack.h"
#include "LevelMeter.h"
#include "SetupPanel.h"

namespace axsynth
{
    // Small flat gear icon button.
    class GearButton : public juce::Button
    {
    public:
        GearButton() : juce::Button ("setup") {}
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    };

    // Plugin UI (spec section 6). Everything is laid out at 1000 x 620 inside 'content'
    // and scaled uniformly with the window (fixed aspect ratio).
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

    private:
        void timerCallback() override { refresh(); }
        void layoutContent();
        void toggleLfoSync();
        void armCloseOnImage();

        AxSynthProcessor& processor;
        theme::LookAndFeel lookAndFeel;

        juce::Component content;
        CameraView cameraView;
        SetupPanel setupPanel;
        WavetableStack stack;
        GearButton gear;
        LevelMeter meter;

        Knob position, lfoRate, lfoAmount, cutoff, resonance, attack, release, output;

        double lastRefreshMs = 0.0;
        bool closeSetupOnImage = false;
        juce::uint64 frameCountWhenArmed = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AxSynthEditor)
    };
}
