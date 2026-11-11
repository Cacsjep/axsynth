#include "Presets.h"
#include "Params.h"

namespace axsynth::presets
{
    using namespace params;

    const std::vector<Preset>& all()
    {
        static const std::vector<Preset> list {
            { "Init", {} },

            // Wide, slow, evolving: three lines over a third of the table, slow LFO sweep.
            { "Pad", { { lines, 3.0f }, { spread, 0.35f }, { tone, 1.4f }, { contrast, 0.6f },
                       { unison, 5.0f }, { detune, 18.0f }, { sub, 0.2f },
                       { lfoRate, 0.12f }, { lfoAmount, 0.25f },
                       { cutoff, 3500.0f }, { resonance, 0.15f }, { filterEnv, 0.1f },
                       { attack, 0.6f }, { decay, 1.5f }, { sustain, 0.8f }, { release, 2.5f },
                       { chorusMix, 0.35f }, { reverbMix, 0.4f }, { reverbSize, 0.8f } } },

            // Lead: bright, a little movement, filter opens on each note.
            { "Synth", { { lines, 2.0f }, { spread, 0.1f }, { tone, 1.0f }, { contrast, 0.7f },
                         { unison, 3.0f }, { detune, 10.0f }, { sub, 0.15f },
                         { lfoRate, 5.0f }, { lfoAmount, 0.04f },
                         { cutoff, 2500.0f }, { resonance, 0.25f }, { filterEnv, 0.35f },
                         { attack, 0.005f }, { decay, 0.4f }, { sustain, 0.7f }, { release, 0.25f },
                         { drive, 0.2f }, { velFilter, 0.3f }, { glide, 0.04f },
                         { delayMix, 0.18f }, { delayTime, 2.0f }, { reverbMix, 0.15f } } },

            // Short and percussive: the filter envelope does the pluck.
            { "Pluck", { { lines, 1.0f }, { tone, 1.1f }, { contrast, 0.7f },
                         { unison, 2.0f }, { detune, 6.0f }, { sub, 0.0f },
                         { cutoff, 500.0f }, { resonance, 0.3f }, { filterEnv, 0.7f },
                         { attack, 0.001f }, { decay, 0.35f }, { sustain, 0.0f }, { release, 0.3f },
                         { velFilter, 0.5f }, { delayMix, 0.25f }, { delayTime, 2.0f }, { delayFeedback, 0.4f },
                         { reverbMix, 0.2f } } },

            // Mono-ish weight: dark tone, strong sub, short filter bite.
            { "Bass", { { position, 0.2f }, { lines, 1.0f }, { tone, 1.6f }, { contrast, 0.5f },
                        { unison, 1.0f }, { detune, 0.0f }, { sub, 0.7f },
                        { cutoff, 350.0f }, { resonance, 0.2f }, { filterEnv, 0.45f },
                        { attack, 0.002f }, { decay, 0.5f }, { sustain, 0.6f }, { release, 0.12f },
                        { mono, 1.0f }, { glide, 0.06f }, { drive, 0.35f }, { velFilter, 0.3f }, { filterSlope, 1.0f } } },

            // Soft electric-piano-like decay.
            { "Keys", { { lines, 2.0f }, { spread, 0.15f }, { tone, 1.3f }, { contrast, 0.6f },
                        { unison, 2.0f }, { detune, 5.0f }, { sub, 0.1f },
                        { cutoff, 1500.0f }, { resonance, 0.1f }, { filterEnv, 0.4f },
                        { attack, 0.003f }, { decay, 1.2f }, { sustain, 0.3f }, { release, 0.5f },
                        { velFilter, 0.45f }, { chorusMix, 0.25f }, { reverbMix, 0.2f } } },

            // Tight, resonant, made for sequenced notes.
            { "Arp", { { lines, 1.0f }, { tone, 1.0f }, { contrast, 0.8f },
                       { unison, 2.0f }, { detune, 8.0f }, { sub, 0.0f },
                       { cutoff, 1200.0f }, { resonance, 0.35f }, { filterEnv, 0.55f },
                       { attack, 0.001f }, { decay, 0.18f }, { sustain, 0.15f }, { release, 0.2f },
                       { velFilter, 0.4f }, { delayMix, 0.3f }, { delayTime, 2.0f }, { delayFeedback, 0.45f },
                       { reverbMix, 0.15f } } },

            // The image as motion: four lines, LFO scanning the whole table.
            { "Sweep", { { lines, 4.0f }, { spread, 0.5f }, { tone, 1.1f }, { contrast, 0.9f },
                         { unison, 3.0f }, { detune, 12.0f }, { sub, 0.25f },
                         { lfoRate, 0.25f }, { lfoAmount, 0.6f },
                         { cutoff, 5000.0f }, { resonance, 0.2f }, { filterEnv, 0.0f },
                         { attack, 0.2f }, { decay, 1.0f }, { sustain, 0.9f }, { release, 1.5f },
                         { chorusMix, 0.3f }, { reverbMix, 0.4f }, { reverbSize, 0.75f } } },

            // Played by movement: a still scene is quiet, a hand in the rectangle makes the sound.
            { "Motion", { { motion, 1.0f }, { contrast, 0.5f }, { lines, 2.0f }, { spread, 0.3f }, { tone, 1.1f },
                          { unison, 3.0f }, { detune, 14.0f }, { sub, 0.15f },
                          { cutoff, 6000.0f }, { resonance, 0.15f },
                          { attack, 0.05f }, { decay, 1.0f }, { sustain, 1.0f }, { release, 1.2f },
                          { delayMix, 0.2f }, { delayTime, 3.0f }, { reverbMix, 0.35f }, { reverbSize, 0.7f } } },
        };
        return list;
    }

    void apply (juce::AudioProcessorValueTreeState& state, int index)
    {
        const auto& list = all();
        if (! juce::isPositiveAndBelow (index, (int) list.size()))
            return;

        const auto& preset = list[(size_t) index];
        for (auto* p : state.processor.getParameters())
        {
            auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p);
            if (ranged == nullptr)
                continue;

            float norm = ranged->getDefaultValue();
            for (const auto& [id, value] : preset.values)
                if (ranged->getParameterID() == id)
                    norm = ranged->convertTo0to1 (value);

            ranged->beginChangeGesture();
            ranged->setValueNotifyingHost (norm);
            ranged->endChangeGesture();
        }
    }
}
