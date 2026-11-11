#include "Knob.h"

namespace axsynth
{
    KnobSlider::KnobSlider()
    {
        setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        setRotaryParameters (startAngle, endAngle, true);
        setRepaintsOnMouseActivity (true);
        setScrollWheelEnabled (true);
        setPopupMenuEnabled (false);

        // Clicking a knob must not take keyboard focus away from the host.
        setWantsKeyboardFocus (false);
        setMouseClickGrabsKeyboardFocus (false);
    }

    void KnobSlider::mouseDown (const juce::MouseEvent& e)
    {
        ignoreDrag = e.mods.isPopupMenu();
        if (ignoreDrag)
        {
            if (onRightClick)
                onRightClick();
            return;
        }

        dragProportion = valueToProportionOfLength (getValue());
        lastDragPos = e.position;
        juce::Slider::mouseDown (e); // starts the gesture
    }

    void KnobSlider::mouseDoubleClick (const juce::MouseEvent& e)
    {
        // A fast double right-click (sync toggle) must not reset the value.
        if (e.mods.isPopupMenu())
            return;
        juce::Slider::mouseDoubleClick (e);
    }

    void KnobSlider::mouseEnter (const juce::MouseEvent& e) { juce::Slider::mouseEnter (e); if (auto* p = getParentComponent()) p->repaint(); }
    void KnobSlider::mouseExit (const juce::MouseEvent& e)  { juce::Slider::mouseExit (e);  if (auto* p = getParentComponent()) p->repaint(); }
    void KnobSlider::mouseUp (const juce::MouseEvent& e)    { juce::Slider::mouseUp (e);    if (auto* p = getParentComponent()) p->repaint(); }

    void KnobSlider::mouseDrag (const juce::MouseEvent& e)
    {
        // Only drag inside a gesture started by Slider::mouseDown (not after Alt+click reset).
        if (ignoreDrag || ! dragActive || ! isEnabled())
            return;

        // Incremental, so toggling Shift mid-drag never jumps.
        const auto d = e.position - lastDragPos;
        lastDragPos = e.position;
        const double pixelsForFullRange = e.mods.isShiftDown() ? 1200.0 : 200.0;
        dragProportion = juce::jlimit (0.0, 1.0, dragProportion + (double) (d.x - d.y) / pixelsForFullRange);
        setValue (proportionOfLengthToValue (dragProportion), juce::sendNotificationSync);
    }

    void KnobSlider::paint (juce::Graphics& g)
    {
        const auto b = getLocalBounds().toFloat();
        const auto c = b.getCentre();
        const float outer = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
        const float stroke = outer >= 20.0f ? 3.0f : 2.5f;
        const float arcR = outer - stroke * 0.5f;
        const float bodyR = outer - stroke * 1.8f;
        const auto prop = (float) valueToProportionOfLength (getValue());
        const float angle = startAngle + prop * (endAngle - startAngle);

        g.setColour (theme::knobBody);
        g.fillEllipse (juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (c));

        const juce::PathStrokeType arcStroke (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        juce::Path bgArc;
        bgArc.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (theme::track);
        g.strokePath (bgArc, arcStroke);

        if (prop > 0.001f)
        {
            juce::Path valArc;
            valArc.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, angle, true);
            g.setColour (isEnabled() ? theme::accent : theme::textDim);
            g.strokePath (valArc, arcStroke);
        }

        // Thin pointer.
        const auto dir = juce::Point<float> (std::sin (angle), -std::cos (angle));
        g.setColour (theme::text);
        g.drawLine ({ c + dir * (bodyR * 0.3f), c + dir * (bodyR - 1.5f) }, outer >= 20.0f ? 1.5f : 1.2f);
    }

    // -------------------------------------------------------------------------

    Knob::Knob (juce::AudioProcessorValueTreeState& s, const juce::String& paramID, const juce::String& l, Size size)
        : state (s), label (l), diameter (size == Size::big ? 48 : size == Size::normal ? 36 : 28)
    {
        slider.onValueChange = [this] { if (slider.isMouseOverOrDragging()) repaint(); };
        setInterceptsMouseClicks (false, true); // neighbouring knobs may overlap; only the dial takes clicks
        setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (slider);
        bindTo (paramID);
    }

    Knob::~Knob()
    {
        attachment.reset();
    }

    void Knob::bindTo (const juce::String& paramID)
    {
        // Never swap attachments in the middle of a drag: it would leave the
        // host's change gesture open. The caller retries on its next refresh.
        if (paramID == boundID || slider.isMouseButtonDown())
            return;
        attachment.reset();
        boundID = paramID;
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramID, slider);
        slider.repaint();
    }

    void Knob::resized()
    {
        slider.setBounds ((getWidth() - diameter) / 2, 0, diameter, diameter);
    }

    void Knob::paint (juce::Graphics& g)
    {
        const auto area = juce::Rectangle<float> (0.0f, (float) (diameter + labelGap), (float) getWidth(), (float) labelHeight);
        if (slider.isMouseOverOrDragging() && slider.isEnabled())
        {
            g.setColour (theme::text);
            g.setFont (theme::font (11.0f, true));
            g.drawText (slider.getTextFromValue (slider.getValue()), area, juce::Justification::centred, false);
        }
        else
        {
            theme::drawCapsLabel (g, label, area);
        }
    }
}
