#include "Synth.h"
#include <cmath>

namespace axsynth
{
    namespace
    {
        inline float readTable (const Wavetable& t, int mip, int f0, int f1, float ff, int i0, float fi) noexcept
        {
            const float* a = t.frame (mip, f0);
            const float* b = t.frame (mip, f1);
            const float sa = a[i0] + fi * (a[i0 + 1] - a[i0]);
            const float sb = b[i0] + fi * (b[i0 + 1] - b[i0]);
            return sa + ff * (sb - sa);
        }
    }

    void Voice::setEnvelope (const Envelope& e) noexcept
    {
        attackStep = 1.0f / juce::jmax (1.0f, e.attackSec * (float) sampleRate);
        // decay / release time = time to cover 60 dB of the distance to the target
        decayCoeff   = std::exp (std::log (0.001f) / juce::jmax (1.0f, e.decaySec * (float) sampleRate));
        releaseCoeff = std::exp (std::log (0.001f) / juce::jmax (1.0f, e.releaseSec * (float) sampleRate));
        sustainLevel = juce::jlimit (0.0f, 1.0f, e.sustain);
    }

    void Voice::noteOn (int n, float velocity, juce::uint64 o, const Envelope& e) noexcept
    {
        if (! active)
        {
            env = 0.0f;
            // Unison oscillators start spread out (golden ratio), not in phase: no
            // flanged attack. Oscillator 0 starts at 0 so a single oscillator is exact.
            for (int j = 0; j < BlockControls::maxUnison; ++j)
            {
                const double p = 0.6180339887 * j;
                phases[j] = p - std::floor (p);
            }
            subPhase = 0.0;
            filterL.reset();
            filterR.reset();
        }
        // Stolen or retriggered voices continue from their current level: no click.
        note = n;
        order = o;
        velocityGain = 0.2f + 0.8f * velocity;
        active = true;
        stage = Stage::attack;
        setEnvelope (e);
    }

    void Voice::noteOff() noexcept
    {
        if (active)
            stage = Stage::release;
    }

    void Voice::render (float* outL, float* outR, int n, const BlockControls& ctl,
                        const Wavetable* cur, const Wavetable* prev, float fade, float fadeStep,
                        float pitchBendSemis) noexcept
    {
        if (! active || cur == nullptr)
            return;

        constexpr int maxU = BlockControls::maxUnison, maxL = BlockControls::maxLines;
        const double freq = 440.0 * std::pow (2.0, ((double) note - 69.0 + (double) pitchBendSemis) / 12.0);
        const double subInc = 0.5 * freq / sampleRate;

        // Unison oscillators spread over +-detune and across the stereo field. Lines
        // (different wavetable frames at the same phase) are spread across the stereo
        // field too, so left and right hear different parts of the image.
        const int count = juce::jlimit (1, maxU, ctl.unison);
        const int lines = juce::jlimit (1, maxL, ctl.lines);
        const double detune = juce::jlimit (0.0, 100.0, (double) ctl.detuneCents);
        const float lineSpread = juce::jlimit (0.0f, 1.0f, ctl.lineSpread);

        double inc[maxU];
        float gainL[maxU][maxL], gainR[maxU][maxL], lineOffset[maxL];
        const float norm = 1.0f / (std::sqrt ((float) count) * (float) lines);

        for (int l = 0; l < lines; ++l)
            lineOffset[l] = lines == 1 ? 0.0f : lineSpread * ((float) l / (float) (lines - 1) - 0.5f);

        for (int j = 0; j < count; ++j)
        {
            const float uSpread = count == 1 ? 0.0f : 2.0f * (float) j / (float) (count - 1) - 1.0f; // -1..1
            inc[j] = freq * std::pow (2.0, (double) uSpread * detune / 1200.0) / sampleRate;

            for (int l = 0; l < lines; ++l)
            {
                const float lPan = lines == 1 ? 0.0f : 0.7f * (2.0f * (float) l / (float) (lines - 1) - 1.0f);
                const float panPos = juce::jlimit (-1.0f, 1.0f, uSpread + lPan);
                const float pan = (panPos + 1.0f) * juce::MathConstants<float>::pi * 0.25f; // 0..pi/2
                gainL[j][l] = std::cos (pan) * norm;
                gainR[j][l] = std::sin (pan) * norm;
            }
        }

        // Mip for the highest oscillator, so no unison copy aliases.
        const int mip = builder::mipForFrequency (freq * std::pow (2.0, detune / 1200.0), sampleRate);
        constexpr float subPanGain = 0.70710678f; // centred
        const float envAmount = juce::jlimit (0.0f, 1.0f, ctl.filterEnvAmount);
        const double maxCutoff = 0.45 * sampleRate;

        for (int i = 0; i < n; ++i)
        {
            // Envelope: linear attack, exponential decay to sustain, exponential release.
            // The decay stage keeps tracking the sustain level, so moving Sustain while a
            // note is held glides instead of jumping.
            switch (stage)
            {
                case Stage::attack:
                    env += attackStep;
                    if (env >= 1.0f) { env = 1.0f; stage = Stage::decay; }
                    break;
                case Stage::decay:
                    env = sustainLevel + (env - sustainLevel) * decayCoeff;
                    if (sustainLevel <= 0.0f && env < 1.0e-4f) { kill(); return; }
                    break;
                case Stage::release:
                    env *= releaseCoeff;
                    if (env < 1.0e-4f) { kill(); return; }
                    break;
                case Stage::idle: return;
            }

            int f0[maxL], f1[maxL];
            float ff[maxL];
            for (int l = 0; l < lines; ++l)
            {
                const float pos = juce::jlimit (0.0f, 1.0f, ctl.position[i] + lineOffset[l]) * (float) (numFrames - 1);
                f0[l] = juce::jlimit (0, numFrames - 1, (int) pos);
                f1[l] = juce::jmin (f0[l] + 1, numFrames - 1);
                ff[l] = pos - (float) f0[l];
            }

            const float fd = prev != nullptr ? juce::jmin (1.0f, fade + fadeStep * (float) i) : 1.0f;
            float left = 0.0f, right = 0.0f;

            for (int j = 0; j < count; ++j)
            {
                const double idx = phases[j] * tableSize;
                const int i0 = juce::jlimit (0, tableSize - 1, (int) idx);
                const float fi = (float) (idx - (double) i0);

                for (int l = 0; l < lines; ++l)
                {
                    float smp = readTable (*cur, mip, f0[l], f1[l], ff[l], i0, fi);
                    if (prev != nullptr)
                    {
                        const float sp = readTable (*prev, mip, f0[l], f1[l], ff[l], i0, fi);
                        smp = sp + fd * (smp - sp);
                    }
                    left  += smp * gainL[j][l];
                    right += smp * gainR[j][l];
                }

                phases[j] += inc[j];
                if (phases[j] >= 1.0) phases[j] -= 1.0;
            }

            // Sub oscillator: pure sine one octave down, centred, through the filter too.
            if (ctl.subLevel[i] > 0.0f)
            {
                const float sub = ctl.subLevel[i] * subPanGain
                                * (float) std::sin (juce::MathConstants<double>::twoPi * subPhase);
                left += sub;
                right += sub;
            }
            subPhase += subInc;
            if (subPhase >= 1.0) subPhase -= 1.0;

            // Filter envelope: the same ADSR opens the cutoff by up to 5 octaves.
            float g = ctl.filterG[i];
            if (envAmount > 0.0f)
            {
                const double fc = juce::jmin (maxCutoff, (double) ctl.cutoffHz[i] * std::exp2 (5.0 * envAmount * env));
                g = (float) std::tan (juce::MathConstants<double>::pi * fc / sampleRate);
            }

            const float amp = env * velocityGain;
            outL[i] += filterL.process (left, g, ctl.filterK[i]) * amp;
            outR[i] += filterR.process (right, g, ctl.filterK[i]) * amp;
        }
    }

    // ---------------------------------------------------------------------

    void SynthEngine::prepare (double sr) noexcept
    {
        for (auto& v : voices) { v.prepare (sr); v.kill(); }
    }

    void SynthEngine::reset() noexcept
    {
        for (auto& v : voices) v.kill();
    }

    void SynthEngine::noteOn (int note, float velocity, const Envelope& e) noexcept
    {
        Voice* target = nullptr;

        for (auto& v : voices)                      // same note already sounding: retrigger
            if (v.isActive() && v.getNote() == note) { target = &v; break; }

        if (target == nullptr)
            for (auto& v : voices)                  // free voice
                if (! v.isActive()) { target = &v; break; }

        if (target == nullptr)                      // steal the oldest
        {
            target = &voices[0];
            for (auto& v : voices)
                if (v.getOrder() < target->getOrder()) target = &v;
        }

        target->noteOn (note, velocity, ++orderCounter, e);
    }

    void SynthEngine::noteOff (int note) noexcept
    {
        for (auto& v : voices)
            if (v.isHeld() && v.getNote() == note)
                v.noteOff();
    }

    void SynthEngine::allNotesOff() noexcept
    {
        for (auto& v : voices) v.noteOff();
    }

    void SynthEngine::setEnvelope (const Envelope& e) noexcept
    {
        for (auto& v : voices) v.setEnvelope (e);
    }

    void SynthEngine::render (float* outL, float* outR, int n, const BlockControls& ctl, WavetableReader& reader, float pitchBendSemis) noexcept
    {
        float tmpL[BlockControls::maxSamples] = {};
        float tmpR[BlockControls::maxSamples] = {};
        const auto* cur = reader.getCurrent();
        const auto* prev = reader.getPrevious();

        for (auto& v : voices)
            v.render (tmpL, tmpR, n, ctl, cur, prev, reader.getFade(), reader.getFadeStep(), pitchBendSemis);

        // Centre pan gain is 1/sqrt(2); compensate so a single centred oscillator keeps
        // the same level per channel as before.
        constexpr float g = voiceGain * 1.41421356f;
        for (int i = 0; i < n; ++i)
        {
            outL[i] = tmpL[i] * g;
            outR[i] = tmpR[i] * g;
        }
    }

    int SynthEngine::getNumActiveVoices() const noexcept
    {
        int c = 0;
        for (auto& v : voices) c += v.isActive() ? 1 : 0;
        return c;
    }
}
