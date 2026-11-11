#include "PluginEditor.h"

namespace axsynth
{
    AxSynthEditor::AxSynthEditor (AxSynthProcessor& p) : AudioProcessorEditor (p), processor (p)
    {
        setSize (1000, 620);
    }

    void AxSynthEditor::paint (juce::Graphics& g)
    {
        g.fillAll (juce::Colour (0xff15171a));
    }
}
