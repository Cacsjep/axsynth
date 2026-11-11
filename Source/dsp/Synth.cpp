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
            phase = 0.0;
            filter.reset();
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

    void Voice::render (float* out, int n, const BlockControls& ctl,
                        const Wavetable* cur, const Wavetable* prev, float fade, float fadeStep,
                        float pitchBendSemis) noexcept
    {
        if (! active || cur == nullptr)
            return;

        const double freq = 440.0 * std::pow (2.0, ((double) note - 69.0 + (double) pitchBendSemis) / 12.0);
        const double inc = freq / sampleRate;
        const int mip = builder::mipForFrequency (freq, sampleRate);

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

            const float pos = ctl.position[i] * (float) (numFrames - 1);
            const int f0 = juce::jlimit (0, numFrames - 1, (int) pos);
            const int f1 = juce::jmin (f0 + 1, numFrames - 1);
            const float ff = pos - (float) f0;

            const double idx = phase * tableSize;
            const int i0 = juce::jlimit (0, tableSize - 1, (int) idx);
            const float fi = (float) (idx - (double) i0);

            float s = readTable (*cur, mip, f0, f1, ff, i0, fi);
            if (prev != nullptr)
            {
                const float fd = juce::jmin (1.0f, fade + fadeStep * (float) i);
                const float sp = readTable (*prev, mip, f0, f1, ff, i0, fi);
                s = sp + fd * (s - sp);
            }

            phase += inc;
            if (phase >= 1.0) phase -= 1.0;

            out[i] += filter.process (s, ctl.filterG[i], ctl.filterK[i]) * env * velocityGain;
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

    void SynthEngine::render (float* out, int n, const BlockControls& ctl, WavetableReader& reader, float pitchBendSemis) noexcept
    {
        float tmp[BlockControls::maxSamples] = {};
        const auto* cur = reader.getCurrent();
        const auto* prev = reader.getPrevious();

        for (auto& v : voices)
            v.render (tmp, n, ctl, cur, prev, reader.getFade(), reader.getFadeStep(), pitchBendSemis);

        for (int i = 0; i < n; ++i)
            out[i] = tmp[i] * voiceGain;
    }

    int SynthEngine::getNumActiveVoices() const noexcept
    {
        int c = 0;
        for (auto& v : voices) c += v.isActive() ? 1 : 0;
        return c;
    }
}
