#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace axsynth::theme
{
    // Base layout size; the editor scales its content uniformly from here.
    inline constexpr int baseWidth  = 1000;
    inline constexpr int baseHeight = 620;

    inline const juce::Colour background { 0xff15171a };
    inline const juce::Colour cameraBg   { 0xff101215 };
    inline const juce::Colour accent     { 0xffffd233 };
    inline const juce::Colour text       { 0xffd4d7dc };
    inline const juce::Colour textDim    { 0xff7c828b };
    inline const juce::Colour knobBody   { 0xff1f2226 };
    inline const juce::Colour track      { 0xff2b2f35 };
    inline const juce::Colour field      { 0xff1b1e22 };

    inline juce::Font font (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
    }

    // Caps label text with a little tracking, as used under knobs and in the setup panel.
    inline void drawCapsLabel (juce::Graphics& g, const juce::String& s, juce::Rectangle<float> area,
                               juce::Justification just = juce::Justification::centred)
    {
        auto f = font (11.0f).withExtraKerningFactor (0.08f);
        g.setFont (f);
        g.setColour (textDim);
        g.drawText (s.toUpperCase(), area, just, false);
    }

    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel();

        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                                   bool highlighted, bool down) override;
        void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

        void fillTextEditorBackground (juce::Graphics&, int w, int h, juce::TextEditor&) override;
        void drawTextEditorOutline (juce::Graphics&, int w, int h, juce::TextEditor&) override;
        void drawCornerResizer (juce::Graphics&, int w, int h, bool mouseOver, bool dragging) override;
    };
}
