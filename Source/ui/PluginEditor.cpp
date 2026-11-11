#include "PluginEditor.h"

namespace axsynth
{
    namespace layout
    {
        // All in base coordinates (theme::baseWidth x theme::baseHeight).
        const juce::Rectangle<int> camera { 16, 16, 568, 588 };
        const juce::Rectangle<int> stack  { 608, 46, 376, 178 };
        const juce::Rectangle<int> gear   { 962, 14, 20, 20 };
        constexpr int columns[] { 655, 749, 843, 937 };
        constexpr int rows[]    { 280, 374, 468, 562 };
        constexpr int meterW = 6, meterH = 58;
    }

    void GearButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
    {
        const auto b = getLocalBounds().toFloat().reduced (1.0f);
        const auto c = b.getCentre();
        const float rOuter = b.getWidth() * 0.5f, rInner = rOuter * 0.74f, rHole = rOuter * 0.32f;
        constexpr int teeth = 8;

        juce::Path p;
        for (int i = 0; i < teeth * 4; ++i)
        {
            // Each tooth: two points on the outer radius, two on the inner.
            const int phase = i % 4;
            const float a = juce::MathConstants<float>::twoPi * ((float) (i / 4) + (float) phase * 0.25f) / (float) teeth;
            const float r = (phase == 0 || phase == 1) ? rOuter : rInner;
            const auto pt = c + juce::Point<float> (std::sin (a), -std::cos (a)) * r;
            if (i == 0) p.startNewSubPath (pt);
            else        p.lineTo (pt);
        }
        p.closeSubPath();
        p.addEllipse (juce::Rectangle<float> (rHole * 2.0f, rHole * 2.0f).withCentre (c));
        p.setUsingNonZeroWinding (false);

        g.setColour (down ? theme::accent : (highlighted || getToggleState() ? theme::text : theme::textDim));
        g.fillPath (p);
    }

    // -------------------------------------------------------------------------

    AxSynthEditor::AxSynthEditor (AxSynthProcessor& p)
        : AudioProcessorEditor (p), processor (p),
          cameraView (p.getEngine()),
          setupPanel (p),
          stack (p.getEngine()),
          position  (p.getState(), params::position,  "Position"),
          tone      (p.getState(), params::tone,      "Tone"),
          lfoRate   (p.getState(), params::lfoRate,   "LFO Rate"),
          lfoAmount (p.getState(), params::lfoAmount, "LFO Amount"),
          unison    (p.getState(), params::unison,    "Unison"),
          detune    (p.getState(), params::detune,    "Detune"),
          sub       (p.getState(), params::sub,       "Sub"),
          cutoff    (p.getState(), params::cutoff,    "Cutoff"),
          resonance (p.getState(), params::resonance, "Resonance"),
          output    (p.getState(), params::output,    "Output"),
          attack    (p.getState(), params::attack,    "Attack"),
          decay     (p.getState(), params::decay,     "Decay"),
          sustain   (p.getState(), params::sustain,   "Sustain"),
          release   (p.getState(), params::release,   "Release")
    {
        setLookAndFeel (&lookAndFeel);
        setOpaque (true);

        addAndMakeVisible (content);
        content.addAndMakeVisible (cameraView);
        content.addChildComponent (setupPanel);
        content.addAndMakeVisible (stack);
        content.addAndMakeVisible (gear);
        content.addAndMakeVisible (meter);
        for (auto* k : { &position, &tone, &lfoRate, &lfoAmount, &unison, &detune, &sub,
                         &cutoff, &resonance, &output, &attack, &decay, &sustain, &release })
            content.addAndMakeVisible (k);

        lfoRate.getSlider().onRightClick = [this] { toggleLfoSync(); };

        // Clicks on the plugin must not move keyboard focus (keys belong to the host).
        setMouseClickGrabsKeyboardFocus (false);
        content.setMouseClickGrabsKeyboardFocus (false);
        cameraView.setMouseClickGrabsKeyboardFocus (false);
        stack.setMouseClickGrabsKeyboardFocus (false);
        gear.setWantsKeyboardFocus (false);

        gear.onClick = [this] { setSetupPanelVisible (! setupPanel.isVisible()); };

        // Close on the first frame of the new connection, counted at the camera after
        // connect() restarted it, so a frame from the previous stream cannot close it.
        setupPanel.onConnect = [this] { armCloseOnImage(); };

        layoutContent();

        // No credentials yet: open the setup right away; it closes on the first image.
        if (! processor.getCamera().getSettings().hasCredentials())
        {
            setSetupPanelVisible (true);
            armCloseOnImage();
        }

        processor.takeOutputPeak(); // drop the peak collected while the editor was closed

        auto* c = getConstrainer();
        c->setFixedAspectRatio ((double) theme::baseWidth / (double) theme::baseHeight);
        setResizable (true, true);
        setResizeLimits (theme::baseWidth * 6 / 10, theme::baseHeight * 6 / 10,
                         theme::baseWidth * 5 / 2,  theme::baseHeight * 5 / 2);
        setSize (theme::baseWidth, theme::baseHeight);

        refresh();
        startTimerHz (30);
    }

    AxSynthEditor::~AxSynthEditor()
    {
        stopTimer();
        setLookAndFeel (nullptr);
    }

    void AxSynthEditor::layoutContent()
    {
        content.setBounds (0, 0, theme::baseWidth, theme::baseHeight);
        cameraView.setBounds (layout::camera);
        setupPanel.setBounds (layout::camera);
        stack.setBounds (layout::stack);
        gear.setBounds (layout::gear);

        // Wavetable / LFO, voice, filter / output (+ level meter), envelope.
        Knob* grid[4][4] { { &position, &tone, &lfoRate, &lfoAmount },
                           { &unison, &detune, &sub, nullptr },
                           { &cutoff, &resonance, &output, nullptr },
                           { &attack, &decay, &sustain, &release } };

        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                if (auto* k = grid[r][c])
                    k->setBounds (layout::columns[c] - Knob::width / 2, layout::rows[r] - Knob::knobSize / 2,
                                  Knob::width, Knob::height);

        meter.setBounds (juce::Rectangle<int> (layout::meterW, layout::meterH)
                             .withCentre ({ layout::columns[3], layout::rows[2] }));
    }

    void AxSynthEditor::resized()
    {
        const float scale = (float) getWidth() / (float) theme::baseWidth;
        content.setTransform (juce::AffineTransform::scale (scale));
    }

    void AxSynthEditor::paint (juce::Graphics& g)
    {
        g.fillAll (theme::background);
    }

    void AxSynthEditor::setSetupPanelVisible (bool shouldShow)
    {
        if (shouldShow == setupPanel.isVisible())
            return;

        if (shouldShow)
        {
            setupPanel.loadSettings();
            setupPanel.updateStatus();
            closeSetupOnImage = false;
        }
        else if (setupPanel.hasKeyboardFocus (true))
        {
            juce::Component::unfocusAllComponents();
        }

        setupPanel.setVisible (shouldShow);
        cameraView.setRectVisible (! shouldShow); // no rectangle behind the login form
        gear.setToggleState (shouldShow, juce::dontSendNotification);
    }

    void AxSynthEditor::armCloseOnImage()
    {
        closeSetupOnImage = true;
        frameCountWhenArmed = processor.getCamera().getFrameCount();
    }

    void AxSynthEditor::toggleLfoSync()
    {
        if (auto* p = processor.getState().getParameter (params::lfoSync))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
            p->endChangeGesture();
        }
        refresh();
    }

    void AxSynthEditor::refresh()
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        const double dt = lastRefreshMs > 0.0 ? juce::jlimit (0.0, 0.5, (now - lastRefreshMs) * 0.001) : 1.0 / 30.0;
        lastRefreshMs = now;

        const float pos = processor.getEffectivePosition();
        cameraView.update (pos);
        stack.update (pos);
        meter.push (processor.takeOutputPeak(), dt);

        const bool sync = *processor.getState().getRawParameterValue (params::lfoSync) > 0.5f;
        lfoRate.bindTo (sync ? params::lfoRateSync : params::lfoRate);

        if (setupPanel.isVisible())
        {
            setupPanel.updateStatus();
            const auto& cam = processor.getCamera();
            if (closeSetupOnImage && cam.getStatus() == CameraConnector::Status::streaming
                && cam.getFrameCount() != frameCountWhenArmed)
                setSetupPanelVisible (false);
        }
    }
}
