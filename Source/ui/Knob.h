#pragma once

#include "Theme.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>

namespace axsynth
{
    // Rotary slider: vertical/horizontal drag, Shift = fine, double-click = default,
    // right-click forwarded to onRightClick. Paints the flat knob itself.
    class KnobSlider : public juce::Slider
    {
    public:
        KnobSlider();

        std::function<void()> onRightClick;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;

        static constexpr float startAngle = juce::MathConstants<float>::pi * (7.0f / 6.0f);  // 7 o'clock
        static constexpr float endAngle   = juce::MathConstants<float>::pi * (17.0f / 6.0f); // 5 o'clock

    private:
        double dragProportion = 0.0;
        juce::Point<float> lastDragPos;
        bool ignoreDrag = false;
    };

    // Knob (48 px) with caps label below, bound to an APVTS parameter.
    class Knob : public juce::Component
    {
    public:
        Knob (juce::AudioProcessorValueTreeState&, const juce::String& paramID, const juce::String& label);
        ~Knob() override;

        // Rebinds to another parameter (LFO rate <-> synced note value).
        void bindTo (const juce::String& paramID);
        const juce::String& getBoundParamID() const noexcept { return boundID; }

        KnobSlider& getSlider() noexcept { return slider; }

        void paint (juce::Graphics&) override;
        void resized() override;

        static constexpr int knobSize = 48;
        static constexpr int width = 96, height = 80;
        static constexpr float labelOffset = 40.0f; // label centre below knob centre

    private:
        juce::AudioProcessorValueTreeState& state;
        juce::String label, boundID;
        KnobSlider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
}
