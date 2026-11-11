#pragma once

#include "WavetableEngine.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace axsynth
{
    // Per-sample global control values for one sub-block, shared by all voices.
    struct BlockControls
    {
        static constexpr int maxSamples = 64;
        static constexpr int maxUnison = 7;
        static constexpr int maxLines = 4;
        float position[maxSamples];   // effective wavetable position 0..1
        float filterG[maxSamples];    // TPT SVF g = tan(pi fc / fs)
        float filterK[maxSamples];    // 1 / Q
        float subLevel[maxSamples];   // sub oscillator (sine, one octave down) 0..1
        float cutoffHz[maxSamples];   // base cutoff, used when the filter envelope is on
        int unison = 1;               // oscillators per voice, 1..maxUnison
        float detuneCents = 0.0f;     // spread of the outermost unison oscillators (+-)
        int lines = 1;                // wavetable lines (frames) played at once, 1..maxLines
        float lineSpread = 0.0f;      // distance between the outermost lines, 0..1 of the table
        float filterEnvAmount = 0.0f; // 0..1: the envelope opens the filter by up to 5 octaves
    };

    // ADSR settings. Decay and release times are the time to cover 60 dB of the distance.
    struct Envelope
    {
        float attackSec = 0.010f;
        float decaySec = 0.300f;
        float sustain = 1.0f;      // 0..1
        float releaseSec = 0.300f;
    };

    // TPT state-variable filter, 12 dB/oct low-pass.
    struct SvfLowpass
    {
        float ic1 = 0.0f, ic2 = 0.0f;
        void reset() noexcept { ic1 = ic2 = 0.0f; }

        float process (float x, float g, float k) noexcept
        {
            const float a1 = 1.0f / (1.0f + g * (g + k));
            const float a2 = g * a1;
            const float a3 = g * a2;
            const float v3 = x - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            return v2;
        }
    };

    class Voice
    {
    public:
        void prepare (double sr) noexcept { sampleRate = sr; filterL.reset(); filterR.reset(); }

        void noteOn (int note, float velocity, juce::uint64 order, const Envelope&) noexcept;
        void noteOff() noexcept;
        void kill() noexcept { active = false; env = 0.0f; stage = Stage::idle; filterL.reset(); filterR.reset(); }

        bool isActive() const noexcept { return active; }
        bool isHeld() const noexcept { return active && stage != Stage::release; }
        int getNote() const noexcept { return note; }
        juce::uint64 getOrder() const noexcept { return order; }

        void setEnvelope (const Envelope&) noexcept;

        // Adds into outL/outR[0..n). pitchBendSemis applies to all voices.
        void render (float* outL, float* outR, int n, const BlockControls& ctl,
                     const Wavetable* cur, const Wavetable* prev, float fade, float fadeStep,
                     float pitchBendSemis) noexcept;

    private:
        enum class Stage { idle, attack, decay, release }; // decay settles into sustain

        double sampleRate = 44100.0;
        double phases[BlockControls::maxUnison] {};
        double subPhase = 0.0;
        int note = -1;
        float velocityGain = 1.0f;
        juce::uint64 order = 0;
        bool active = false;

        Stage stage = Stage::idle;
        float env = 0.0f, attackStep = 0.01f, decayCoeff = 0.999f, sustainLevel = 1.0f, releaseCoeff = 0.999f;
        SvfLowpass filterL, filterR;
    };

    // 8-voice polyphonic engine, oldest-voice stealing.
    class SynthEngine
    {
    public:
        static constexpr int numVoices = 8;
        static constexpr float voiceGain = 0.25f;

        void prepare (double sr) noexcept;
        void reset() noexcept;

        void noteOn (int note, float velocity, const Envelope&) noexcept;
        void noteOff (int note) noexcept;
        void allNotesOff() noexcept;
        void setEnvelope (const Envelope&) noexcept;

        void render (float* outL, float* outR, int n, const BlockControls& ctl, WavetableReader& reader, float pitchBendSemis) noexcept;

        int getNumActiveVoices() const noexcept;

    private:
        std::array<Voice, numVoices> voices;
        juce::uint64 orderCounter = 0;
    };
}
