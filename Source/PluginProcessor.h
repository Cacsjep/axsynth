#pragma once

#include "Params.h"
#include "dsp/Synth.h"

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
        double getTailLengthSeconds() const override { return 10.0; }

        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
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
        std::atomic<float>* pAttack {};
        std::atomic<float>* pRelease {};
        std::atomic<float>* pOutput {};

        juce::SmoothedValue<float> sPosition, sLfoAmount, sResonance, sGain;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> sCutoff;

        double sampleRate = 44100.0;
        double lfoPhase = 0.0;
        double lfoSamplesPerQuarter = 22050.0; // host-sync advance
        bool lfoSyncActive = false;
        double lfoSyncQuarters = 1.0;
        float pitchBend = 0.0f, modWheel = 0.0f;

        std::atomic<float> effectivePosition { 0.5f };
        std::atomic<float> outputPeak { 0.0f };

        mutable juce::CriticalSection settingsLock;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AxSynthProcessor)
    };
}
