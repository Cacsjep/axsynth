#include "PluginProcessor.h"
#include "ui/PluginEditor.h"
#include <cmath>

namespace axsynth
{
    namespace
    {
        // Parameter read that survives NaN/inf from a corrupt state or host.
        inline float readParam (const std::atomic<float>* p, float lo, float hi, float fallback) noexcept
        {
            const float v = p->load (std::memory_order_relaxed);
            return std::isfinite (v) ? juce::jlimit (lo, hi, v) : fallback;
        }

        inline float dbToGain (float db) noexcept
        {
            return db <= params::outputMinDb + 0.001f ? 0.0f : juce::Decibels::decibelsToGain (db);
        }
    }

    AxSynthProcessor::AxSynthProcessor()
        : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          apvts (*this, nullptr, "AxSynth", params::createLayout()),
          engine (camera),
          reader (engine.getPool())
    {
        pPosition    = apvts.getRawParameterValue (params::position);
        pLfoRate     = apvts.getRawParameterValue (params::lfoRate);
        pLfoSync     = apvts.getRawParameterValue (params::lfoSync);
        pLfoRateSync = apvts.getRawParameterValue (params::lfoRateSync);
        pLfoAmount   = apvts.getRawParameterValue (params::lfoAmount);
        pCutoff      = apvts.getRawParameterValue (params::cutoff);
        pResonance   = apvts.getRawParameterValue (params::resonance);
        pTone        = apvts.getRawParameterValue (params::tone);
        pContrast    = apvts.getRawParameterValue (params::contrast);
        pLines       = apvts.getRawParameterValue (params::lines);
        pSpread      = apvts.getRawParameterValue (params::spread);
        pFilterEnv   = apvts.getRawParameterValue (params::filterEnv);
        pFilterType  = apvts.getRawParameterValue (params::filterType);
        pMotion      = apvts.getRawParameterValue (params::motion);
        pFilterSlope = apvts.getRawParameterValue (params::filterSlope);
        pFreeze      = apvts.getRawParameterValue (params::freeze);
        pScanMode    = apvts.getRawParameterValue (params::scanMode);
        pDrive       = apvts.getRawParameterValue (params::drive);
        pVelFilter   = apvts.getRawParameterValue (params::velFilter);
        pMono        = apvts.getRawParameterValue (params::mono);
        pGlide       = apvts.getRawParameterValue (params::glide);
        pChorusMix   = apvts.getRawParameterValue (params::chorusMix);
        pDelayMix    = apvts.getRawParameterValue (params::delayMix);
        pDelayTime   = apvts.getRawParameterValue (params::delayTime);
        pDelayFeedback = apvts.getRawParameterValue (params::delayFeedback);
        pReverbMix   = apvts.getRawParameterValue (params::reverbMix);
        pReverbSize  = apvts.getRawParameterValue (params::reverbSize);
        pUnison      = apvts.getRawParameterValue (params::unison);
        pDetune      = apvts.getRawParameterValue (params::detune);
        pSub         = apvts.getRawParameterValue (params::sub);
        pAttack      = apvts.getRawParameterValue (params::attack);
        pDecay       = apvts.getRawParameterValue (params::decay);
        pSustain     = apvts.getRawParameterValue (params::sustain);
        pRelease     = apvts.getRawParameterValue (params::release);
        pOutput      = apvts.getRawParameterValue (params::output);

        engine.start();
    }

    AxSynthProcessor::~AxSynthProcessor()
    {
        engine.stop();
        camera.disconnect();
    }

    bool AxSynthProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        const auto out = layouts.getMainOutputChannelSet();
        return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    }

    void AxSynthProcessor::prepareToPlay (double sr, int)
    {
        sampleRate = sr;
        synth.prepare (sr);
        reader.prepare (sr);

        constexpr double smoothSec = 0.010;
        sPosition.reset (sr, smoothSec);  sPosition.setCurrentAndTargetValue (readParam (pPosition, 0.0f, 1.0f, 0.5f));
        sLfoAmount.reset (sr, smoothSec); sLfoAmount.setCurrentAndTargetValue (readParam (pLfoAmount, 0.0f, 1.0f, 0.0f));
        sResonance.reset (sr, smoothSec); sResonance.setCurrentAndTargetValue (readParam (pResonance, 0.0f, 1.0f, 0.1f));
        sGain.reset (sr, smoothSec);      sGain.setCurrentAndTargetValue (dbToGain (readParam (pOutput, params::outputMinDb, params::outputMaxDb, 0.0f)));
        sCutoff.reset (sr, smoothSec);    sCutoff.setCurrentAndTargetValue (readParam (pCutoff, 20.0f, 20000.0f, 8000.0f));
        sModWheel.reset (sr, smoothSec);  sModWheel.setCurrentAndTargetValue (0.0f);
        sSub.reset (sr, smoothSec);       sSub.setCurrentAndTargetValue (readParam (pSub, 0.0f, 1.0f, 0.0f));
        sDetune.reset (sr, smoothSec);    sDetune.setCurrentAndTargetValue (readParam (pDetune, 0.0f, 50.0f, 12.0f));
        sSpread.reset (sr, smoothSec);    sSpread.setCurrentAndTargetValue (readParam (pSpread, 0.0f, 1.0f, 0.25f));
        sFilterEnv.reset (sr, smoothSec); sFilterEnv.setCurrentAndTargetValue (readParam (pFilterEnv, 0.0f, 1.0f, 0.0f));

        // Effects: all memory is allocated here, never in processBlock.
        chorus.prepare ({ sr, (juce::uint32) BlockControls::maxSamples, 2 });
        chorus.setRate (0.6f);
        chorus.setDepth (0.25f);
        chorus.setCentreDelay (8.0f);
        chorus.setFeedback (0.0f);
        chorus.setMix (0.0f);
        chorus.reset();
        reverb.prepare (sr);
        const auto delayLen = (size_t) (2.0 * sr) + 2;
        delayBufL.assign (delayLen, 0.0f);
        delayBufR.assign (delayLen, 0.0f);
        delayWrite = 0;
        delayDampL = delayDampR = 0.0f;
        sChorus.reset (sr, 0.05);    sChorus.setCurrentAndTargetValue (readParam (pChorusMix, 0.0f, 1.0f, 0.0f));
        sDelayMix.reset (sr, 0.05);  sDelayMix.setCurrentAndTargetValue (readParam (pDelayMix, 0.0f, 1.0f, 0.0f));
        sDelayFb.reset (sr, 0.05);   sDelayFb.setCurrentAndTargetValue (readParam (pDelayFeedback, 0.0f, 0.9f, 0.35f));
        sReverbMix.reset (sr, 0.05); sReverbMix.setCurrentAndTargetValue (readParam (pReverbMix, 0.0f, 1.0f, 0.0f));
        sMotionGate.reset (sr, 0.05); sMotionGate.setCurrentAndTargetValue (1.0f);
        sDelaySamples.reset (sr, 0.2); sDelaySamples.setCurrentAndTargetValue ((float) (0.25 * sr));
        effectsPrepared = true;

        lfoPhase = 0.0;
        pitchBend = 0.0f;
        outputPeak = 0.0f;
    }

    void AxSynthProcessor::releaseResources()
    {
        synth.reset();
    }

    void AxSynthProcessor::handleMidi (const juce::MidiMessage& m)
    {
        if (m.isNoteOn())
            synth.noteOn (m.getNoteNumber(), m.getFloatVelocity(), envelope);
        else if (m.isNoteOff())
            synth.noteOff (m.getNoteNumber());
        else if (m.isAllSoundOff())
            synth.reset();                       // CC120: silence at once
        else if (m.isAllNotesOff())
            synth.allNotesOff();                 // CC123: release
        else if (m.isResetAllControllers())
        {
            pitchBend = 0.0f;                    // CC121
            sModWheel.setTargetValue (0.0f);
        }
        else if (m.isPitchWheel())
            pitchBend = (float) (m.getPitchWheelValue() - 8192) / 8192.0f;
        else if (m.isControllerOfType (1))
            sModWheel.setTargetValue ((float) m.getControllerValue() / 127.0f);
    }

    void AxSynthProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        juce::ScopedNoDenormals noDenormals;

        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();

        sPosition.setTargetValue (readParam (pPosition, 0.0f, 1.0f, 0.5f));
        sLfoAmount.setTargetValue (readParam (pLfoAmount, 0.0f, 1.0f, 0.0f));
        sResonance.setTargetValue (readParam (pResonance, 0.0f, 1.0f, 0.1f));
        sCutoff.setTargetValue (readParam (pCutoff, 20.0f, 20000.0f, 8000.0f));
        sGain.setTargetValue (dbToGain (readParam (pOutput, params::outputMinDb, params::outputMaxDb, 0.0f)));
        sSub.setTargetValue (readParam (pSub, 0.0f, 1.0f, 0.0f));
        sDetune.setTargetValue (readParam (pDetune, 0.0f, 50.0f, 12.0f));
        sSpread.setTargetValue (readParam (pSpread, 0.0f, 1.0f, 0.25f));
        sFilterEnv.setTargetValue (readParam (pFilterEnv, 0.0f, 1.0f, 0.0f));
        controls.unison = juce::jlimit (1, BlockControls::maxUnison, juce::roundToInt (readParam (pUnison, 1.0f, 7.0f, 3.0f)));
        controls.steepFilter = readParam (pFilterSlope, 0.0f, 1.0f, 0.0f) > 0.5f;
        controls.drive = readParam (pDrive, 0.0f, 1.0f, 0.0f);
        controls.velocityToFilter = readParam (pVelFilter, 0.0f, 1.0f, 0.0f);
        controls.glideSec = readParam (pGlide, 0.0f, 2.0f, 0.0f);
        synth.setGlide (controls.glideSec);
        synth.setMono (readParam (pMono, 0.0f, 1.0f, 0.0f) > 0.5f);

        const float motionAmount = readParam (pMotion, 0.0f, 1.0f, 0.0f);
        engine.setMotion (motionAmount);
        engine.setFreeze (readParam (pFreeze, 0.0f, 1.0f, 0.0f) > 0.5f);
        engine.setHorizontalScan (readParam (pScanMode, 0.0f, 1.0f, 0.0f) > 0.5f);
        // Motion gate: with Motion up, a still scene gets quiet.
        sMotionGate.setTargetValue (1.0f + motionAmount * (engine.getMotionLevel() - 1.0f));

        sChorus.setTargetValue (readParam (pChorusMix, 0.0f, 1.0f, 0.0f));
        sDelayMix.setTargetValue (readParam (pDelayMix, 0.0f, 1.0f, 0.0f));
        sDelayFb.setTargetValue (readParam (pDelayFeedback, 0.0f, 0.9f, 0.35f));
        sReverbMix.setTargetValue (readParam (pReverbMix, 0.0f, 1.0f, 0.0f));
        reverbSizeTarget = readParam (pReverbSize, 0.0f, 1.0f, 0.6f);

        controls.filterType = (FilterType) juce::jlimit (0, 2, juce::roundToInt (readParam (pFilterType, 0.0f, 2.0f, 0.0f)));
        controls.lines  = juce::jlimit (1, BlockControls::maxLines, juce::roundToInt (readParam (pLines, 1.0f, 4.0f, 1.0f)));
        engine.setTilt (readParam (pTone, 0.5f, 2.5f, 1.2f));          // relaxed atomic stores
        engine.setContrast (readParam (pContrast, 0.0f, 1.0f, 0.6f));
        envelope.attackSec  = readParam (pAttack, 0.001f, 5.0f, 0.010f);
        envelope.decaySec   = readParam (pDecay, 0.005f, 10.0f, 0.300f);
        envelope.sustain    = readParam (pSustain, 0.0f, 1.0f, 1.0f);
        envelope.releaseSec = readParam (pRelease, 0.005f, 10.0f, 0.300f);
        lfoRateHz  = readParam (pLfoRate, 0.05f, 20.0f, 0.5f);
        synth.setEnvelope (envelope);

        // LFO: free-running or host-synced. Bar divisions follow the host's time signature.
        lfoSyncActive = readParam (pLfoSync, 0.0f, 1.0f, 0.0f) > 0.5f;
        const int division = juce::jlimit (0, params::syncNames.size() - 1,
                                           (int) readParam (pLfoRateSync, 0.0f, 16.0f, (float) params::syncDefaultIndex));
        double bpm = 120.0;
        double barQuarters = 4.0;
        juce::Optional<juce::AudioPlayHead::PositionInfo> transport;
        if (auto* ph = getPlayHead())
            transport = ph->getPosition();

        if (transport.hasValue())
        {
            if (auto b = transport->getBpm(); b && std::isfinite (*b))
                bpm = juce::jlimit (1.0, 1000.0, *b);
            if (auto ts = transport->getTimeSignature(); ts && ts->numerator > 0 && ts->denominator > 0)
                barQuarters = 4.0 * ts->numerator / ts->denominator;
        }

        const double bars = params::syncBars[division];
        lfoSyncQuarters = bars > 0.0 ? bars * barQuarters : params::syncQuarters[division];

        if (lfoSyncActive && transport.hasValue() && transport->getIsPlaying())
        {
            if (auto ppq = transport->getPpqPosition(); ppq && std::isfinite (*ppq))
            {
                double cycles = *ppq / lfoSyncQuarters;
                // Bar divisions count from the last bar start, so cycles line up with bars.
                if (auto barStart = transport->getPpqPositionOfLastBarStart(); bars > 0.0 && barStart && std::isfinite (*barStart))
                {
                    const double barIndex = std::round (*barStart / barQuarters);
                    cycles = (barIndex + (*ppq - *barStart) / barQuarters) / bars;
                }
                lfoPhase = cycles - std::floor (cycles);
            }
        }
        lfoSamplesPerQuarter = sampleRate * 60.0 / bpm;
        {
            const int dt = juce::jlimit (0, params::delayTimeNames.size() - 1, juce::roundToInt (readParam (pDelayTime, 0.0f, 5.0f, 1.0f)));
            const double samples = juce::jlimit (1.0, 2.0 * sampleRate, lfoSamplesPerQuarter * params::delayTimeQuarters[dt]);
            sDelaySamples.setTargetValue ((float) samples);
        }

        auto* const* channels = buffer.getArrayOfWritePointers();
        int pos = 0;

        for (const auto meta : midi)
        {
            const int evPos = juce::jlimit (0, numSamples, meta.samplePosition);
            while (pos < evPos)
            {
                const int n = juce::jmin (BlockControls::maxSamples, evPos - pos);
                renderSubBlock (channels, numChannels, pos, n);
                pos += n;
            }
            handleMidi (meta.getMessage());
        }

        while (pos < numSamples)
        {
            const int n = juce::jmin (BlockControls::maxSamples, numSamples - pos);
            renderSubBlock (channels, numChannels, pos, n);
            pos += n;
        }
    }

    void AxSynthProcessor::renderSubBlock (float* const* channels, int numChannels, int offset, int n)
    {
        reader.update();

        const double lfoInc = lfoSyncActive ? 1.0 / (lfoSamplesPerQuarter * lfoSyncQuarters)
                                            : (double) lfoRateHz / sampleRate;
        if (! std::isfinite (lfoPhase) || lfoPhase < 0.0)
            lfoPhase = 0.0;
        const float nyquistLimit = (float) (0.45 * sampleRate);
        float posEff = 0.5f;

        for (int i = 0; i < n; ++i)
        {
            const float lfo = (float) std::sin (juce::MathConstants<double>::twoPi * lfoPhase);
            lfoPhase += lfoInc;
            if (lfoPhase >= 1.0) lfoPhase -= 1.0;

            posEff = juce::jlimit (0.0f, 1.0f, sPosition.getNextValue() + sLfoAmount.getNextValue() * lfo + sModWheel.getNextValue());
            controls.position[i] = posEff;

            const float cutoffHz = sCutoff.getNextValue();
            controls.cutoffHz[i] = cutoffHz;
            const float fc = juce::jmin (cutoffHz, nyquistLimit);
            controls.filterG[i] = (float) std::tan (juce::MathConstants<double>::pi * fc / sampleRate);
            const float q = 0.707f * std::pow (30.0f, sResonance.getNextValue());
            controls.filterK[i] = 1.0f / q;
            controls.subLevel[i] = sSub.getNextValue();
        }
        sDetune.skip (n - 1);
        controls.detuneCents = sDetune.getNextValue();
        sSpread.skip (n - 1);
        controls.lineSpread = sSpread.getNextValue();
        sFilterEnv.skip (n - 1);
        controls.filterEnvAmount = sFilterEnv.getNextValue();
        effectivePosition.store (posEff, std::memory_order_relaxed);
        uiLfoPhase.store ((float) lfoPhase, std::memory_order_relaxed);

        float left[BlockControls::maxSamples], right[BlockControls::maxSamples];
        synth.render (left, right, n, controls, reader, pitchBend * 2.0f);
        reader.advance (n);

        if (const auto* v = synth.getNewestVoice())
        {
            uiEnvLevel.store (v->getEnvelopeLevel(), std::memory_order_relaxed);
            uiEnvStage.store (v->getStageIndex(), std::memory_order_relaxed);
            uiEnvReleaseFrom.store (v->getReleaseFrom(), std::memory_order_relaxed);
            uiCutoff.store (juce::jmin ((float) (0.45 * sampleRate),
                                        controls.cutoffHz[n - 1] * std::exp2 (5.0f * controls.filterEnvAmount * v->getEnvelopeLevel())),
                            std::memory_order_relaxed);
        }
        else
        {
            uiEnvLevel.store (0.0f, std::memory_order_relaxed);
            uiEnvStage.store (0, std::memory_order_relaxed);
            uiCutoff.store (controls.cutoffHz[n - 1], std::memory_order_relaxed);
        }

        for (int i = 0; i < n; ++i)
        {
            const float gate = sMotionGate.getNextValue();
            left[i] *= gate;
            right[i] *= gate;
        }

        processEffects (left, right, n);

        float peak = outputPeak.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
        {
            const float g = sGain.getNextValue();
            left[i]  = sat::softClip (left[i] * g);   // safety limiter; rarely reached with the headroom
            right[i] = sat::softClip (right[i] * g);
            peak = juce::jmax (peak, std::abs (left[i]), std::abs (right[i]));
        }
        outputPeak.store (peak, std::memory_order_relaxed);

        if (numChannels >= 2)
        {
            std::copy (left, left + n, channels[0] + offset);
            std::copy (right, right + n, channels[1] + offset);
            for (int c = 2; c < numChannels; ++c)
                std::fill (channels[c] + offset, channels[c] + offset + n, 0.0f);
        }
        else if (numChannels == 1)
        {
            for (int i = 0; i < n; ++i)
                channels[0][offset + i] = 0.5f * (left[i] + right[i]);
        }
    }

    void AxSynthProcessor::processEffects (float* left, float* right, int n) noexcept
    {
        if (! effectsPrepared)
            return;

        // Chorus (JUCE dsp::Chorus, its own dry/wet). Skipped while fully dry.
        sChorus.skip (n - 1);
        const float chorusMix = sChorus.getNextValue();
        if (chorusMix > 1.0e-4f)
        {
            chorus.setMix (chorusMix * 0.5f);
            float* chans[] { left, right };
            juce::dsp::AudioBlock<float> block (chans, 2, (size_t) n);
            chorus.process (juce::dsp::ProcessContextReplacing<float> (block));
        }

        // Delay: tempo-synced, ping-pong feedback with a gentle low-pass, added to the dry.
        const int len = (int) delayBufL.size();
        for (int i = 0; i < n; ++i)
        {
            const float mix = sDelayMix.getNextValue();
            const float fb = sDelayFb.getNextValue();
            const float d = sDelaySamples.getNextValue();

            float readPos = (float) delayWrite - d;
            while (readPos < 0.0f) readPos += (float) len;
            const int r0 = (int) readPos;
            const int r1 = (r0 + 1) % len;
            const float frac = readPos - (float) r0;
            const float wl = delayBufL[(size_t) r0] + frac * (delayBufL[(size_t) r1] - delayBufL[(size_t) r0]);
            const float wr = delayBufR[(size_t) r0] + frac * (delayBufR[(size_t) r1] - delayBufR[(size_t) r0]);

            delayDampL += 0.35f * (wl - delayDampL);
            delayDampR += 0.35f * (wr - delayDampR);

            // Ping-pong: each side feeds the other.
            delayBufL[(size_t) delayWrite] = left[i]  * mix + delayDampR * fb;
            delayBufR[(size_t) delayWrite] = right[i] * mix + delayDampL * fb;
            delayWrite = (delayWrite + 1) % len;

            left[i]  += wl;
            right[i] += wr;
        }

        // Reverb (FDN). Runs while the mix is up or was up last block (tail fades out).
        sReverbMix.skip (n - 1);
        const float revMix = sReverbMix.getNextValue();
        if (revMix > 1.0e-4f || reverbMixTarget > 1.0e-4f)
        {
            reverb.setParameters (reverbSizeTarget, 0.4f);
            reverb.process (left, right, n, revMix);
        }
        reverbMixTarget = revMix;
    }

    // ---- state --------------------------------------------------------------

    void AxSynthProcessor::getStateInformation (juce::MemoryBlock& dest)
    {
        auto state = apvts.copyState();

        const auto s = camera.getSettings();
        juce::ValueTree cam ("Camera");
        cam.setProperty ("host", s.host, nullptr);
        cam.setProperty ("user", s.user, nullptr);
        cam.setProperty ("password", passwordForState (s), nullptr);
        cam.setProperty ("streamPath", s.streamPath, nullptr);
        cam.setProperty ("scheme", s.scheme, nullptr);
        cam.setProperty ("auth", s.auth, nullptr);
        cam.setProperty ("resolution", s.resolution, nullptr);
        state.appendChild (cam, nullptr);

        const auto r = engine.getRect();
        juce::ValueTree rect ("Rect");
        rect.setProperty ("x", r.getX(), nullptr);
        rect.setProperty ("y", r.getY(), nullptr);
        rect.setProperty ("w", r.getWidth(), nullptr);
        rect.setProperty ("h", r.getHeight(), nullptr);
        state.appendChild (rect, nullptr);

        if (auto xml = state.createXml())
            copyXmlToBinary (*xml, dest);
    }

    void AxSynthProcessor::setStateInformation (const void* data, int size)
    {
        auto xml = getXmlFromBinary (data, size);
        if (xml == nullptr)
            return;

        auto state = juce::ValueTree::fromXml (*xml);
        if (! state.hasType (apvts.state.getType()))
            return;

        const auto cam = state.getChildWithName ("Camera");
        const auto rect = state.getChildWithName ("Rect");

        // Keep only parameter children; replace values a corrupt state made non-finite.
        for (int i = state.getNumChildren(); --i >= 0;)
        {
            auto child = state.getChild (i);
            if (child.hasType ("Camera") || child.hasType ("Rect"))
            {
                state.removeChild (i, nullptr);
                continue;
            }

            if (auto* p = apvts.getParameter (child["id"].toString()))
                if (! std::isfinite ((double) child["value"]))
                    child.setProperty ("value", p->convertFrom0to1 (p->getDefaultValue()), nullptr);
        }
        apvts.replaceState (state);

        // replaceState skips parameters whose denormalised value already matches (a bool
        // at raw 0.46 reads as "off" = saved "off"), leaving the raw value behind. Set
        // every restored parameter explicitly so getValue() matches the saved state.
        for (const auto& child : state)
            if (auto* p = apvts.getParameter (child["id"].toString()))
            {
                const float norm = p->convertTo0to1 ((float) child["value"]);
                if (std::isfinite (norm) && std::abs (p->getValue() - norm) > 1.0e-6f)
                    p->setValueNotifyingHost (norm);
            }

        auto r = WavetableEngine::defaultRect();
        if (rect.isValid())
            r = { (float) rect["x"], (float) rect["y"], (float) rect["w"], (float) rect["h"] };
        engine.setRect (r);

        if (cam.isValid())
        {
            CameraSettings s;
            s.host       = cam["host"].toString();
            s.user       = cam["user"].toString();
            s.streamPath = cam.getProperty ("streamPath", CameraSettings::defaultStreamPath()).toString();
            s.scheme     = cam["scheme"].toString();
            s.auth       = cam["auth"].toString();
            s.resolution = cam["resolution"].toString();
            passwordFromState (s, cam["password"].toString()); // keeps the blob if not decryptable here

            const auto old = camera.getSettings();
            const bool changed = old.host != s.host || old.user != s.user || old.password != s.password
                              || old.streamPath != s.streamPath;
            camera.setSettings (s);

            if (! s.hasCredentials())
                camera.disconnect();             // restored "no camera": stop any old stream
            else if (changed || camera.getStatus() == CameraConnector::Status::idle
                             || camera.getStatus() == CameraConnector::Status::failed)
                camera.connect (false);
        }
    }

    void AxSynthProcessor::setCurrentProgram (int index)
    {
        if (! juce::isPositiveAndBelow (index, getNumPrograms()))
            return;
        currentProgram = index;
        presets::apply (apvts, index);
    }

    const juce::String AxSynthProcessor::getProgramName (int index)
    {
        const auto& list = presets::all();
        return juce::isPositiveAndBelow (index, (int) list.size()) ? juce::String (list[(size_t) index].name) : juce::String();
    }

    void AxSynthProcessor::connectCamera (const CameraSettings& s)
    {
        camera.setSettings (s);
        camera.connect (true);
    }

    juce::AudioProcessorEditor* AxSynthProcessor::createEditor()
    {
        return new AxSynthEditor (*this);
    }
}
