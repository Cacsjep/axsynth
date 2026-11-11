#pragma once

#include "../PluginProcessor.h"

namespace axsynth
{
    // STUB - replaced by the real UI (spec section 6).
    class AxSynthEditor : public juce::AudioProcessorEditor
    {
    public:
        explicit AxSynthEditor (AxSynthProcessor&);
        void paint (juce::Graphics&) override;

    private:
        AxSynthProcessor& processor;
    };
}
