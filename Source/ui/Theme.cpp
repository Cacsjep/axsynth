#include "Theme.h"

namespace axsynth::theme
{
    LookAndFeel::LookAndFeel()
    {
        setDefaultSansSerifTypefaceName ("Segoe UI");

        setColour (juce::ResizableWindow::backgroundColourId, background);
        setColour (juce::TextEditor::backgroundColourId, field);
        setColour (juce::TextEditor::textColourId, text);
        setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.35f));
        setColour (juce::TextEditor::highlightedTextColourId, text);
        setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        setColour (juce::TextEditor::focusedOutlineColourId, accent);
        setColour (juce::CaretComponent::caretColourId, accent);
        setColour (juce::TextButton::buttonColourId, accent);
        setColour (juce::TextButton::textColourOffId, background);
        setColour (juce::TextButton::textColourOnId, background);
    }

    void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                            bool highlighted, bool down)
    {
        auto c = b.isEnabled() ? accent : track;
        if (b.isEnabled() && (highlighted || down))
            c = c.brighter (down ? 0.05f : 0.15f);
        g.setColour (c);
        g.fillRoundedRectangle (b.getLocalBounds().toFloat(), 3.0f);
    }

    void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
    {
        g.setFont (getTextButtonFont (b, b.getHeight()));
        g.setColour (b.isEnabled() ? background : textDim);
        g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, false);
    }

    juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int)
    {
        return font (13.0f, true);
    }

    void LookAndFeel::fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor& te)
    {
        g.setColour (te.findColour (juce::TextEditor::backgroundColourId));
        g.fillRoundedRectangle (juce::Rectangle<int> (w, h).toFloat(), 3.0f);
    }

    void LookAndFeel::drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& te)
    {
        // Flat: only a thin accent underline while focused.
        if (te.hasKeyboardFocus (true) && ! te.isReadOnly())
        {
            g.setColour (accent);
            g.fillRect (2.0f, (float) h - 1.5f, (float) w - 4.0f, 1.5f);
        }
    }

    void LookAndFeel::drawCornerResizer (juce::Graphics& g, int w, int h, bool mouseOver, bool dragging)
    {
        g.setColour (mouseOver || dragging ? textDim : track);
        const float s = (float) juce::jmin (w, h);
        for (float i = 0.35f; i < 1.0f; i += 0.3f)
            g.drawLine ((float) w - s * i, (float) h, (float) w, (float) h - s * i, 1.0f);
    }
}
