#include "PluginEditor.h"

namespace axsynth
{
    namespace layout
    {
        // All in base coordinates (theme::baseWidth x theme::baseHeight).
        const juce::Rectangle<int> camera { 16, 16, 568, 588 };
        const juce::Rectangle<int> stack  { 608, 46, 376, 178 };
        const juce::Rectangle<int> gear   { 962, 14, 20, 20 };
        constexpr int columns[] { 672, 796, 920 };
        constexpr int rows[]    { 298, 416, 534 };
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
          lfoRate   (p.getState(), params::lfoRate,   "LFO Rate"),
          lfoAmount (p.getState(), params::lfoAmount, "LFO Amount"),
          cutoff    (p.getState(), params::cutoff,    "Cutoff"),
          resonance (p.getState(), params::resonance, "Resonance"),
          attack    (p.getState(), params::attack,    "Attack"),
          release   (p.getState(), params::release,   "Release"),
          output    (p.getState(), params::output,    "Output")
    {
        setLookAndFeel (&lookAndFeel);
        setOpaque (true);

        addAndMakeVisible (content);
        content.addAndMakeVisible (cameraView);
        content.addChildComponent (setupPanel);
        content.addAndMakeVisible (stack);
        content.addAndMakeVisible (gear);
        content.addAndMakeVisible (meter);
        for (auto* k : { &position, &lfoRate, &lfoAmount, &cutoff, &resonance, &attack, &release, &output })
            content.addAndMakeVisible (k);

        lfoRate.getSlider().onRightClick = [this] { toggleLfoSync(); };

        gear.onClick = [this] { setSetupPanelVisible (! setupPanel.isVisible()); };

        setupPanel.onConnect = [this]
        {
            closeSetupOnImage = true;
            imageCountWhenArmed = cameraView.getImageCount();
        };

        layoutContent();

        // No credentials yet: open the setup right away; it closes on the first image.
        if (! processor.getCamera().getSettings().hasCredentials())
        {
            setSetupPanelVisible (true);
            closeSetupOnImage = true;
        }

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

        Knob* grid[3][3] { { &position, &lfoRate, &lfoAmount },
                           { &cutoff, &resonance, &attack },
                           { &release, &output, nullptr } };

        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                if (auto* k = grid[r][c])
                    k->setBounds (layout::columns[c] - Knob::width / 2, layout::rows[r] - Knob::knobSize / 2,
                                  Knob::width, Knob::height);

        meter.setBounds (juce::Rectangle<int> (layout::meterW, layout::meterH)
                             .withCentre ({ layout::columns[2], layout::rows[2] }));
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
            imageCountWhenArmed = cameraView.getImageCount();
        }

        setupPanel.setVisible (shouldShow);
        gear.setToggleState (shouldShow, juce::dontSendNotification);
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
            if (closeSetupOnImage && cameraView.getImageCount() != imageCountWhenArmed)
                setSetupPanelVisible (false);
        }
    }
}
