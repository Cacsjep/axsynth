#include "PluginProcessor.h"
#include "ui/PluginEditor.h"
#include <cmath>

namespace axsynth
{
    namespace
    {
        inline float softClip (float x) noexcept
        {
            constexpr float knee = 0.9f, room = 0.1f;
            const float a = std::abs (x);
            if (a <= knee)
                return x;
            return std::copysign (knee + room * std::tanh ((a - knee) / room), x);
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
        pAttack      = apvts.getRawParameterValue (params::attack);
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
        sPosition.reset (sr, smoothSec);  sPosition.setCurrentAndTargetValue (pPosition->load());
        sLfoAmount.reset (sr, smoothSec); sLfoAmount.setCurrentAndTargetValue (pLfoAmount->load());
        sResonance.reset (sr, smoothSec); sResonance.setCurrentAndTargetValue (pResonance->load());
        sGain.reset (sr, smoothSec);      sGain.setCurrentAndTargetValue (dbToGain (pOutput->load()));
        sCutoff.reset (sr, smoothSec);    sCutoff.setCurrentAndTargetValue (pCutoff->load());

        lfoPhase = 0.0;
        pitchBend = 0.0f;
        modWheel = 0.0f;
        outputPeak = 0.0f;
    }

    void AxSynthProcessor::releaseResources()
    {
        synth.reset();
    }

    void AxSynthProcessor::handleMidi (const juce::MidiMessage& m)
    {
        if (m.isNoteOn())
            synth.noteOn (m.getNoteNumber(), m.getFloatVelocity(), pAttack->load(), pRelease->load());
        else if (m.isNoteOff())
            synth.noteOff (m.getNoteNumber());
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            synth.allNotesOff();
        else if (m.isPitchWheel())
            pitchBend = (float) (m.getPitchWheelValue() - 8192) / 8192.0f;
        else if (m.isControllerOfType (1))
            modWheel = (float) m.getControllerValue() / 127.0f;
    }

    void AxSynthProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        juce::ScopedNoDenormals noDenormals;

        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();

        sPosition.setTargetValue (pPosition->load());
        sLfoAmount.setTargetValue (pLfoAmount->load());
        sResonance.setTargetValue (pResonance->load());
        sCutoff.setTargetValue (juce::jlimit (20.0f, 20000.0f, pCutoff->load()));
        sGain.setTargetValue (dbToGain (pOutput->load()));
        synth.setEnvelopeTimes (pAttack->load(), pRelease->load());

        // LFO: free-running or host-synced.
        lfoSyncActive = pLfoSync->load() > 0.5f;
        lfoSyncQuarters = params::syncQuarters[juce::jlimit (0, params::syncNames.size() - 1, (int) pLfoRateSync->load())];
        double bpm = 120.0;
        if (auto* ph = getPlayHead())
        {
            if (auto pos = ph->getPosition())
            {
                if (auto b = pos->getBpm()) bpm = juce::jmax (1.0, *b);
                if (lfoSyncActive && pos->getIsPlaying())
                    if (auto ppq = pos->getPpqPosition())
                    {
                        const double cycles = *ppq / lfoSyncQuarters;
                        lfoPhase = cycles - std::floor (cycles);
                    }
            }
        }
        lfoSamplesPerQuarter = sampleRate * 60.0 / bpm;

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
                                            : (double) pLfoRate->load() / sampleRate;
        const float nyquistLimit = (float) (0.45 * sampleRate);
        float posEff = 0.5f;

        for (int i = 0; i < n; ++i)
        {
            const float lfo = (float) std::sin (juce::MathConstants<double>::twoPi * lfoPhase);
            lfoPhase += lfoInc;
            if (lfoPhase >= 1.0) lfoPhase -= 1.0;

            posEff = juce::jlimit (0.0f, 1.0f, sPosition.getNextValue() + sLfoAmount.getNextValue() * lfo + modWheel);
            controls.position[i] = posEff;

            const float fc = juce::jmin (sCutoff.getNextValue(), nyquistLimit);
            controls.filterG[i] = (float) std::tan (juce::MathConstants<double>::pi * fc / sampleRate);
            const float q = 0.707f * std::pow (30.0f, sResonance.getNextValue());
            controls.filterK[i] = 1.0f / q;
        }
        effectivePosition.store (posEff, std::memory_order_relaxed);

        float mono[BlockControls::maxSamples];
        synth.render (mono, n, controls, reader, pitchBend * 2.0f);
        reader.advance (n);

        float peak = outputPeak.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
        {
            const float s = softClip (mono[i] * sGain.getNextValue());
            mono[i] = s;
            peak = juce::jmax (peak, std::abs (s));
        }
        outputPeak.store (peak, std::memory_order_relaxed);

        for (int c = 0; c < numChannels; ++c)
            std::copy (mono, mono + n, channels[c] + offset);
    }

    // ---- state --------------------------------------------------------------

    void AxSynthProcessor::getStateInformation (juce::MemoryBlock& dest)
    {
        auto state = apvts.copyState();

        const auto s = camera.getSettings();
        juce::ValueTree cam ("Camera");
        cam.setProperty ("host", s.host, nullptr);
        cam.setProperty ("user", s.user, nullptr);
        cam.setProperty ("password", dpapiProtect (s.password), nullptr);
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
        state.removeChild (cam, nullptr);
        state.removeChild (rect, nullptr);
        apvts.replaceState (state);

        if (rect.isValid())
            engine.setRect ({ (float) rect["x"], (float) rect["y"], (float) rect["w"], (float) rect["h"] });

        if (cam.isValid())
        {
            CameraSettings s;
            s.host       = cam["host"].toString();
            s.user       = cam["user"].toString();
            s.password   = dpapiUnprotect (cam["password"].toString());
            s.streamPath = cam.getProperty ("streamPath", CameraSettings::defaultStreamPath()).toString();
            s.scheme     = cam["scheme"].toString();
            s.auth       = cam["auth"].toString();
            s.resolution = cam["resolution"].toString();

            const auto old = camera.getSettings();
            const bool changed = old.host != s.host || old.user != s.user || old.password != s.password
                              || old.streamPath != s.streamPath;
            camera.setSettings (s);

            if (s.hasCredentials() && (changed || camera.getStatus() == CameraConnector::Status::idle))
                camera.connect (false);
        }
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
