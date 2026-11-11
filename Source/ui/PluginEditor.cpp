#include "PluginEditor.h"

namespace axsynth
{
    namespace layout
    {
        // All in base coordinates (theme::baseWidth x theme::baseHeight = 1280 x 720).
        const juce::Rectangle<int> camera { 16, 16, 560, 688 };
        const juce::Rectangle<int> gear   { 1242, 14, 20, 20 };
        const juce::Rectangle<int> preset { 1080, 12, 150, 24 };
        const juce::Rectangle<int> stack  { 592, 44, 552, 150 };
        const juce::Rectangle<int> output { 1156, 44, 108, 150 };

        // Sections: two columns of 330 px, three rows.
        const juce::Rectangle<int> wavetable { 592, 206, 330, 158 };
        const juce::Rectangle<int> voice     { 934, 206, 330, 158 };
        const juce::Rectangle<int> filter    { 592, 376, 330, 158 };
        const juce::Rectangle<int> lfo       { 934, 376, 330, 158 };
        const juce::Rectangle<int> envelope  { 592, 546, 330, 158 };
        const juce::Rectangle<int> fx        { 934, 546, 330, 158 };

        constexpr int titleH = 24, displayH = 46, knobBottom = 132; // knob circles end here (section y)

        juce::Rectangle<int> displayIn (juce::Rectangle<int> sec) { return { sec.getX() + 10, sec.getY() + titleH, sec.getWidth() - 20, displayH }; }

        // Buttons in the title row, right aligned; 'right' = distance of their right edge.
        juce::Rectangle<int> headerButtons (juce::Rectangle<int> sec, int width, int right = 10)
        {
            return { sec.getRight() - right - width, sec.getY() + 4, width, 17 };
        }

        // Bottom-aligned so all labels in a section share one line.
        void place (Knob& k, juce::Rectangle<int> sec, int dx)
        {
            k.placeAt (sec.getX() + dx, sec.getY() + knobBottom - k.getDiameter() / 2);
        }
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

    void PresetButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
    {
        g.setColour (down ? theme::accent : (highlighted ? theme::text : theme::textDim));
        g.setFont (theme::font (11.0f, true));
        g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centredRight, true);
    }

    // -------------------------------------------------------------------------

    AxSynthEditor::AxSynthEditor (AxSynthProcessor& p)
        : AudioProcessorEditor (p), processor (p),
          cameraView (p.getEngine()),
          setupPanel (p),
          stack (p.getEngine()),
          position  (p.getState(), params::position,  "Position", Knob::Size::big),
          lines     (p.getState(), params::lines,     "Lines",    Knob::Size::small),
          spread    (p.getState(), params::spread,    "Spread",   Knob::Size::small),
          tone      (p.getState(), params::tone,      "Tone",     Knob::Size::small),
          contrast  (p.getState(), params::contrast,  "Contrast", Knob::Size::small),
          motion    (p.getState(), params::motion,    "Motion",   Knob::Size::small),
          unison    (p.getState(), params::unison,    "Unison"),
          detune    (p.getState(), params::detune,    "Detune"),
          sub       (p.getState(), params::sub,       "Sub"),
          glide     (p.getState(), params::glide,     "Glide"),
          cutoff    (p.getState(), params::cutoff,    "Cutoff",   Knob::Size::big),
          resonance (p.getState(), params::resonance, "Resonance"),
          filterEnv (p.getState(), params::filterEnv, "Env Amt"),
          drive     (p.getState(), params::drive,     "Drive"),
          velFilter (p.getState(), params::velFilter, "Vel",      Knob::Size::small),
          lfoRate   (p.getState(), params::lfoRate,   "Rate"),
          lfoAmount (p.getState(), params::lfoAmount, "Amount"),
          attack    (p.getState(), params::attack,    "Attack"),
          decay     (p.getState(), params::decay,     "Decay"),
          sustain   (p.getState(), params::sustain,   "Sustain"),
          release   (p.getState(), params::release,   "Release"),
          chorus    (p.getState(), params::chorusMix, "Chorus"),
          delay     (p.getState(), params::delayMix,  "Delay"),
          delayTime (p.getState(), params::delayTime, "Time"),
          delayFeedback (p.getState(), params::delayFeedback, "Fdbk"),
          reverb    (p.getState(), params::reverbMix, "Reverb"),
          reverbSize (p.getState(), params::reverbSize, "Size"),
          output    (p.getState(), params::output,    "Level"),
          filterType (*p.getState().getParameter (params::filterType), params::filterTypeNames),
          filterSlope (*p.getState().getParameter (params::filterSlope), params::filterSlopeNames),
          scanMode   (*p.getState().getParameter (params::scanMode), { "VERT", "HORZ" }),
          freeze     (*p.getState().getParameter (params::freeze), { "FREEZE" }),
          mono       (*p.getState().getParameter (params::mono), { "MONO" }),
          lfoSync    (*p.getState().getParameter (params::lfoSync), { "SYNC" })
    {
        setLookAndFeel (&lookAndFeel);
        setOpaque (true);

        addAndMakeVisible (content);
        content.addAndMakeVisible (cameraView);
        content.addChildComponent (setupPanel);
        content.addAndMakeVisible (stack);
        for (auto* sec : { &secWavetable, &secVoice, &secFilter, &secLfo, &secEnvelope, &secFx, &secOutput })
            content.addAndMakeVisible (sec);
        for (juce::Component* d : { (juce::Component*) &harmonicsView, (juce::Component*) &unisonView,
                                    (juce::Component*) &filterView, (juce::Component*) &lfoView,
                                    (juce::Component*) &envelopeView, (juce::Component*) &fxView,
                                    (juce::Component*) &filterType, (juce::Component*) &filterSlope, (juce::Component*) &scanMode,
                                    (juce::Component*) &freeze, (juce::Component*) &mono, (juce::Component*) &lfoSync })
            content.addAndMakeVisible (d);
        content.addAndMakeVisible (gear);
        content.addAndMakeVisible (meter);
        for (auto* k : { &position, &lines, &spread, &tone, &contrast, &motion, &unison, &detune, &sub, &glide,
                         &cutoff, &resonance, &filterEnv, &drive, &velFilter, &lfoRate, &lfoAmount,
                         &attack, &decay, &sustain, &release,
                         &chorus, &delay, &delayTime, &delayFeedback, &reverb, &reverbSize, &output })
            content.addAndMakeVisible (k);

        lfoRate.getSlider().onRightClick = [this] { toggleLfoSync(); };

        // Clicks on the plugin must not move keyboard focus (keys belong to the host).
        setMouseClickGrabsKeyboardFocus (false);
        content.setMouseClickGrabsKeyboardFocus (false);
        cameraView.setMouseClickGrabsKeyboardFocus (false);
        stack.setMouseClickGrabsKeyboardFocus (false);
        gear.setWantsKeyboardFocus (false);

        gear.onClick = [this] { setSetupPanelVisible (! setupPanel.isVisible()); };

        presetButton.setWantsKeyboardFocus (false);
        presetButton.onClick = [this] { showPresetMenu(); };
        content.addAndMakeVisible (presetButton);

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
        using namespace layout;
        content.setBounds (0, 0, theme::baseWidth, theme::baseHeight);
        cameraView.setBounds (camera);
        setupPanel.setBounds (camera);
        stack.setBounds (layout::stack);
        gear.setBounds (layout::gear);
        presetButton.setBounds (preset);

        secWavetable.setBounds (wavetable);
        secVoice.setBounds (voice);
        secFilter.setBounds (filter);
        secLfo.setBounds (lfo);
        secEnvelope.setBounds (envelope);
        secFx.setBounds (fx);
        secOutput.setBounds (layout::output);

        harmonicsView.setBounds (displayIn (wavetable));
        unisonView.setBounds (displayIn (voice));
        filterView.setBounds (displayIn (filter));
        lfoView.setBounds (displayIn (lfo));
        envelopeView.setBounds (displayIn (envelope));
        fxView.setBounds (displayIn (fx));

        freeze.setBounds (headerButtons (wavetable, 62));
        scanMode.setBounds (headerButtons (wavetable, 84, 78));
        mono.setBounds (headerButtons (voice, 50));
        filterType.setBounds (headerButtons (filter, 90));
        filterSlope.setBounds (headerButtons (filter, 56, 108));
        lfoSync.setBounds (headerButtons (lfo, 50));

        // Wavetable: Position big, the shaping controls small.
        place (position, wavetable, 40);
        place (lines,    wavetable, 98);
        place (spread,   wavetable, 143);
        place (tone,     wavetable, 188);
        place (contrast, wavetable, 238);
        place (motion,   wavetable, 294);

        place (unison, voice, 52);
        place (detune, voice, 128);
        place (sub,    voice, 204);
        place (glide,  voice, 280);

        place (cutoff,    filter, 44);
        place (resonance, filter, 118);
        place (filterEnv, filter, 186);
        place (drive,     filter, 248);
        place (velFilter, filter, 300);

        place (lfoRate,   lfo, 110);
        place (lfoAmount, lfo, 220);

        place (attack,  envelope, 52);
        place (decay,   envelope, 128);
        place (sustain, envelope, 204);
        place (release, envelope, 280);

        place (chorus,        fx, 34);
        place (delay,         fx, 86);
        place (delayTime,     fx, 138);
        place (delayFeedback, fx, 190);
        place (reverb,        fx, 242);
        place (reverbSize,    fx, 294);

        // Output: next to the wavetable stack.
        output.placeAt (layout::output.getX() + 40, layout::output.getY() + 132 - output.getDiameter() / 2);
        meter.setBounds (layout::output.getRight() - 24, layout::output.getY() + titleH + 2, 6,
                         layout::output.getHeight() - titleH - 14);
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

    void AxSynthEditor::showPresetMenu()
    {
        juce::PopupMenu menu;
        const int current = processor.getCurrentProgram();
        for (int i = 0; i < processor.getNumPrograms(); ++i)
            menu.addItem (i + 1, processor.getProgramName (i), true, i == current);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton),
                            [safe = juce::Component::SafePointer<AxSynthEditor> (this)] (int result)
                            {
                                if (safe != nullptr && result > 0)
                                {
                                    safe->processor.setCurrentProgram (result - 1);
                                    safe->refresh();
                                }
                            });
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
        cameraView.update (pos, juce::roundToInt (processor.getState().getRawParameterValue (params::lines)->load()),
                           processor.getState().getRawParameterValue (params::spread)->load());
        stack.update (pos);
        meter.push (processor.takeOutputPeak(), dt);

        const auto presetName = processor.getProgramName (processor.getCurrentProgram()).toUpperCase()
                              + juce::String (juce::CharPointer_UTF8 (" \xe2\x96\xbe"));
        if (presetButton.getButtonText() != presetName)
            presetButton.setButtonText (presetName);

        // Live displays.
        auto& st = processor.getState();
        auto val = [&st] (const char* id) { return st.getRawParameterValue (id)->load(); };
        const auto ui = processor.getUiState();

        processor.getEngine().getDisplayHarmonics (harmonicsScratch);
        {
            const float fp = juce::jlimit (0.0f, 1.0f, pos) * (float) (numFrames - 1);
            const int f0 = juce::jlimit (0, numFrames - 1, (int) fp);
            const int f1 = juce::jmin (f0 + 1, numFrames - 1);
            const float t = fp - (float) f0;
            std::array<float, numHarmonics> amps {};
            for (size_t k = 0; k < amps.size(); ++k)
                amps[k] = harmonicsScratch[(size_t) f0][k] + t * (harmonicsScratch[(size_t) f1][k] - harmonicsScratch[(size_t) f0][k]);
            harmonicsView.setHarmonics (amps);
        }
        unisonView.setVoice (juce::roundToInt (val (params::unison)), val (params::detune), val (params::sub));
        filterView.setFilter (juce::roundToInt (val (params::filterType)), val (params::cutoff), ui.cutoffHz, val (params::resonance),
                              val (params::filterSlope) > 0.5f);
        envelopeView.setEnvelope (val (params::attack), val (params::decay), val (params::sustain), val (params::release),
                                  ui.envLevel, ui.envStage, ui.envReleaseFrom);
        {
            const int div = juce::jlimit (0, params::delayTimeNames.size() - 1, juce::roundToInt (val (params::delayTime)));
            fxView.setFx (val (params::chorusMix), val (params::delayMix), (float) params::delayTimeQuarters[div] / 4.0f,
                          val (params::delayFeedback), val (params::reverbMix), val (params::reverbSize));
        }
        cameraView.setHorizontal (val (params::scanMode) > 0.5f);

        const bool sync = *processor.getState().getRawParameterValue (params::lfoSync) > 0.5f;
        lfoView.setLfo (ui.lfoPhase, val (params::lfoAmount), sync,
                        params::syncNames[juce::jlimit (0, params::syncNames.size() - 1, juce::roundToInt (val (params::lfoRateSync)))]);
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
