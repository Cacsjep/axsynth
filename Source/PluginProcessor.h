#pragma once

#include "Params.h"
#include "dsp/Synth.h"
#include "Presets.h"
#include "dsp/Reverb.h"

namespace axsynth
{
    class AxSynthProcessor : public juce::AudioProcessor
    {
    public:
        AxSynthProcessor();
        ~AxSynthProcessor() override;

        void prepareToPlay (double sampleRate, int samplesPerBlock) override;
        void releaseResources() override;
        bool isBusesLayoutSupported (const BusesLayout&) const override;
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        using AudioProcessor::processBlock;

        juce::AudioProcessorEditor* createEditor() override;
        bool hasEditor() const override { return true; }

        const juce::String getName() const override { return "AxSynth"; }
        bool acceptsMidi() const override { return true; }
        bool producesMidi() const override { return false; }
        bool isMidiEffect() const override { return false; }
        double getTailLengthSeconds() const override { return 20.0; } // 10 s release + delay and reverb tails

        int getNumPrograms() override { return (int) presets::all().size(); }
        int getCurrentProgram() override { return currentProgram.load(); }
        void setCurrentProgram (int index) override;
        const juce::String getProgramName (int index) override;
        void changeProgramName (int, const juce::String&) override {}

        void getStateInformation (juce::MemoryBlock&) override;
        void setStateInformation (const void*, int) override;

        // ---- for the editor (message thread) ----
        juce::AudioProcessorValueTreeState& getState() noexcept { return apvts; }
        CameraConnector& getCamera() noexcept { return camera; }
        WavetableEngine& getEngine() noexcept { return engine; }

        // Applies new settings, stores them, and starts a user-initiated connect.
        void connectCamera (const CameraSettings&);

        float getEffectivePosition() const noexcept { return effectivePosition.load(); }
        // Output peak (linear), read-and-decay by the UI.
        float getOutputPeak() const noexcept { return outputPeak.load(); }
        float takeOutputPeak() noexcept { return outputPeak.exchange (0.0f); }

        // Live values for the displays (message thread reads, audio thread writes).
        struct UiState
        {
            float lfoPhase = 0.0f;       // 0..1
            float envLevel = 0.0f;       // newest voice
            int envStage = 0;            // 0 idle, 1 attack, 2 decay/sustain, 3 release
            float envReleaseFrom = 0.0f;
            float cutoffHz = 8000.0f;    // newest voice, including the filter envelope
        };
        UiState getUiState() const noexcept
        {
            return { uiLfoPhase.load (std::memory_order_relaxed), uiEnvLevel.load (std::memory_order_relaxed),
                     uiEnvStage.load (std::memory_order_relaxed), uiEnvReleaseFrom.load (std::memory_order_relaxed),
                     uiCutoff.load (std::memory_order_relaxed) };
        }
        juce::uint32 getNumTableSwaps() const noexcept { return reader.getNumSwaps(); }

    private:
        void renderSubBlock (float* const* channels, int numChannels, int offset, int n);
        void handleMidi (const juce::MidiMessage&);

        juce::AudioProcessorValueTreeState apvts;
        CameraConnector camera;
        WavetableEngine engine;
        WavetableReader reader;
        SynthEngine synth;
        BlockControls controls {};

        std::atomic<float>* pPosition {};
        std::atomic<float>* pLfoRate {};
        std::atomic<float>* pLfoSync {};
        std::atomic<float>* pLfoRateSync {};
        std::atomic<float>* pLfoAmount {};
        std::atomic<float>* pCutoff {};
        std::atomic<float>* pResonance {};
        std::atomic<float>* pTone {};
        std::atomic<float>* pContrast {};
        std::atomic<float>* pLines {};
        std::atomic<float>* pSpread {};
        std::atomic<float>* pFilterEnv {};
        std::atomic<float>* pUnison {};
        std::atomic<float>* pDetune {};
        std::atomic<float>* pSub {};
        std::atomic<float>* pAttack {};
        std::atomic<float>* pDecay {};
        std::atomic<float>* pSustain {};
        std::atomic<float>* pRelease {};
        std::atomic<float>* pOutput {};

        juce::SmoothedValue<float> sPosition, sLfoAmount, sResonance, sGain, sModWheel, sSub, sDetune, sSpread, sFilterEnv;
        std::atomic<int> currentProgram { 0 };
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> sCutoff;

        double sampleRate = 44100.0;
        double lfoPhase = 0.0;
        double lfoSamplesPerQuarter = 22050.0; // host-sync advance
        bool lfoSyncActive = false;
        double lfoSyncQuarters = 1.0;
        float pitchBend = 0.0f;
        Envelope envelope;            // sanitised once per block
        float lfoRateHz = 0.5f;

        std::atomic<float> effectivePosition { 0.5f };
        std::atomic<float> outputPeak { 0.0f };
        std::atomic<float> uiLfoPhase { 0.0f }, uiEnvLevel { 0.0f }, uiEnvReleaseFrom { 0.0f }, uiCutoff { 8000.0f };
        std::atomic<int> uiEnvStage { 0 };
        std::atomic<float>* pFilterType {};
        std::atomic<float>* pMotion {};
        std::atomic<float>* pFilterSlope {};
        std::atomic<float>* pFreeze {};
        std::atomic<float>* pScanMode {};
        std::atomic<float>* pDrive {};
        std::atomic<float>* pVelFilter {};
        std::atomic<float>* pMono {};
        std::atomic<float>* pGlide {};
        std::atomic<float>* pChorusMix {};
        std::atomic<float>* pDelayMix {};
        std::atomic<float>* pDelayTime {};
        std::atomic<float>* pDelayFeedback {};
        std::atomic<float>* pReverbMix {};
        std::atomic<float>* pReverbSize {};

        // Effects (stereo, after the voices): chorus -> delay -> reverb.
        void processEffects (float* left, float* right, int n) noexcept;
        juce::dsp::Chorus<float> chorus;
        FdnReverb reverb;
        std::vector<float> delayBufL, delayBufR;   // allocated in prepareToPlay
        int delayWrite = 0;
        float delayDampL = 0.0f, delayDampR = 0.0f;
        juce::SmoothedValue<float> sChorus, sDelayMix, sDelayFb, sReverbMix, sMotionGate;
        juce::SmoothedValue<float> sDelaySamples;
        float reverbMixTarget = 0.0f, reverbSizeTarget = 0.6f;
        bool effectsPrepared = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AxSynthProcessor)
    };
}
