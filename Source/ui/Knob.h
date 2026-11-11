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
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseEnter (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void startedDragging() override  { dragActive = true; }
        void stoppedDragging() override  { dragActive = false; }

        static constexpr float startAngle = juce::MathConstants<float>::pi * (7.0f / 6.0f);  // 7 o'clock
        static constexpr float endAngle   = juce::MathConstants<float>::pi * (17.0f / 6.0f); // 5 o'clock

    private:
        double dragProportion = 0.0;
        juce::Point<float> lastDragPos;
        bool ignoreDrag = false;
        bool dragActive = false; // true only inside a host change gesture
    };

    // Knob with a caps label right below, bound to an APVTS parameter. While hovered or
    // dragged the label shows the value instead.
    class Knob : public juce::Component
    {
    public:
        enum class Size { small, normal, big };

        Knob (juce::AudioProcessorValueTreeState&, const juce::String& paramID, const juce::String& label,
              Size size = Size::normal);
        ~Knob() override;

        // Rebinds to another parameter (LFO rate <-> synced note value).
        void bindTo (const juce::String& paramID);
        const juce::String& getBoundParamID() const noexcept { return boundID; }

        KnobSlider& getSlider() noexcept { return slider; }

        void paint (juce::Graphics&) override;
        void resized() override;

        int getDiameter() const noexcept { return diameter; }
        static constexpr int width = 76;          // fits the longest label
        static constexpr int labelHeight = 13;
        static constexpr int labelGap = 2;        // between knob and label
        int getTotalHeight() const noexcept { return diameter + labelGap + labelHeight; }

        // Places the knob so its circle is centred at (cx, cy).
        void placeAt (int cx, int cy) { setBounds (cx - width / 2, cy - diameter / 2, width, getTotalHeight()); }

    private:
        juce::AudioProcessorValueTreeState& state;
        juce::String label, boundID;
        int diameter = 34;
        KnobSlider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
}
