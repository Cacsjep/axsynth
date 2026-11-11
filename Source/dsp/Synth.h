#pragma once

#include "WavetableEngine.h"
#include "Saturation.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace axsynth
{
    enum class FilterType { lowPass = 0, highPass = 1, bandPass = 2 };

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
        FilterType filterType = FilterType::lowPass;
        bool steepFilter = false;     // 24 dB/oct: a second, neutral (Butterworth) stage
        float drive = 0.0f;           // 0..1 saturation before the filter
        float velocityToFilter = 0.0f;// 0..1: velocity moves the cutoff by up to +-3 octaves
        float glideSec = 0.0f;        // pitch glide time
    };

    // ADSR settings. Decay and release times are the time to cover 60 dB of the distance.
    struct Envelope
    {
        float attackSec = 0.010f;
        float decaySec = 0.300f;
        float sustain = 1.0f;      // 0..1
        float releaseSec = 0.300f;
    };

    // TPT state-variable filter, 12 dB/oct. Band-pass is normalised to unity peak gain.
    struct Svf
    {
        float ic1 = 0.0f, ic2 = 0.0f;
        void reset() noexcept { ic1 = ic2 = 0.0f; }

        float process (float x, float g, float k, FilterType type) noexcept
        {
            const float a1 = 1.0f / (1.0f + g * (g + k));
            const float a2 = g * a1;
            const float a3 = g * a2;
            const float v3 = x - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            switch (type)
            {
                case FilterType::highPass: return x - k * v1 - v2;
                case FilterType::bandPass: return k * v1;
                case FilterType::lowPass:  break;
            }
            return v2;
        }
    };

    class Voice
    {
    public:
        void prepare (double sr) noexcept { sampleRate = sr; filterL.reset(); filterR.reset(); filter2L.reset(); filter2R.reset(); }

        // startNote: pitch to glide from (the previous note), or < 0 for none.
        void noteOn (int note, float velocity, juce::uint64 order, const Envelope&, double startNote = -1.0) noexcept;
        // Legato (mono): new target pitch, envelope keeps running.
        void glideTo (int note, juce::uint64 order) noexcept;
        double getCurrentPitch() const noexcept { return currentPitch; }
        void noteOff() noexcept;
        void kill() noexcept { active = false; env = 0.0f; stage = Stage::idle; filterL.reset(); filterR.reset(); filter2L.reset(); filter2R.reset(); }

        bool isActive() const noexcept { return active; }
        bool isHeld() const noexcept { return active && stage != Stage::release; }
        int getNote() const noexcept { return note; }
        juce::uint64 getOrder() const noexcept { return order; }

        // Display: envelope level, stage (0 idle, 1 attack, 2 decay/sustain, 3 release)
        // and the level the release started from.
        float getEnvelopeLevel() const noexcept { return env; }
        int getStageIndex() const noexcept { return (int) stage; }
        float getReleaseFrom() const noexcept { return releaseFrom; }

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
        float velocityGain = 1.0f, velocity = 1.0f;
        double currentPitch = 60.0, targetPitch = 60.0; // in semitones (MIDI note numbers)
        juce::uint64 order = 0;
        bool active = false;

        Stage stage = Stage::idle;
        float env = 0.0f, attackStep = 0.01f, decayCoeff = 0.999f, sustainLevel = 1.0f, releaseCoeff = 0.999f;
        Svf filterL, filterR, filter2L, filter2R;
        sat::TanhAdaa driveL, driveR;
        float releaseFrom = 0.0f;   // envelope level when the release started (display)
    };

    // 8-voice polyphonic engine, oldest-voice stealing.
    class SynthEngine
    {
    public:
        static constexpr int numVoices = 16;
        static constexpr float voiceGain = 0.2f; // headroom for chords with unison before the soft clipper

        void prepare (double sr) noexcept;
        void reset() noexcept;

        void noteOn (int note, float velocity, const Envelope&) noexcept;
        void noteOff (int note) noexcept;
        // Mono: one voice, last-note priority, legato glide. Glide also applies in poly
        // (each new note slides in from the previous one).
        void setMono (bool m) noexcept;
        void setGlide (float seconds) noexcept { glideSec = seconds; }
        void allNotesOff() noexcept;
        void setEnvelope (const Envelope&) noexcept;

        void render (float* outL, float* outR, int n, const BlockControls& ctl, WavetableReader& reader, float pitchBendSemis) noexcept;

        int getNumActiveVoices() const noexcept;

        // The most recently triggered sounding voice, for the envelope display. Null if none.
        const Voice* getNewestVoice() const noexcept;

    private:
        std::array<Voice, numVoices> voices;
        juce::uint64 orderCounter = 0;
        bool mono = false;
        float glideSec = 0.0f;
        double lastPitch = -1.0;
        std::array<int, 16> held {};   // mono note stack, newest last
        int numHeld = 0;
    };
}
