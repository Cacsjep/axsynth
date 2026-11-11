#include "Params.h"
#include <cmath>

namespace axsynth::params
{
    namespace
    {
        juce::NormalisableRange<float> logRange (float lo, float hi)
        {
            return { lo, hi,
                     [] (float s, float e, float t) { return s * std::pow (e / s, t); },
                     [] (float s, float e, float v) { return std::log (v / s) / std::log (e / s); } };
        }
    }

    juce::String formatHz (float hz)
    {
        if (hz >= 1000.0f)
            return juce::String (hz / 1000.0f, 1) + " kHz";
        if (hz >= 10.0f)
            return juce::String (juce::roundToInt (hz)) + " Hz";
        return juce::String (hz, 2) + " Hz";
    }

    juce::String formatTime (float seconds)
    {
        if (seconds >= 1.0f)
            return juce::String (seconds, 2) + " s";
        return juce::String (juce::roundToInt (seconds * 1000.0f)) + " ms";
    }

    juce::String formatDb (float db)
    {
        if (db <= outputMinDb + 0.001f)
            return juce::String (juce::CharPointer_UTF8 ("-\xe2\x88\x9e dB"));
        return (db > 0.0f ? "+" : "") + juce::String (db, 1) + " dB";
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using P = juce::AudioParameterFloat;
        using A = juce::AudioParameterFloatAttributes;
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        auto pct = A().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; })
                      .withValueFromStringFunction ([] (const juce::String& s) { return s.getFloatValue() / 100.0f; });

        layout.add (std::make_unique<P> (juce::ParameterID { position, 1 }, "Position",
            juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f,
            A().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 31.0f) + 1) + " / 32"; })
               .withValueFromStringFunction ([] (const juce::String& s) { return (s.getFloatValue() - 1.0f) / 31.0f; })));

        // Tone = harmonic tilt: 0.5 bright/buzzy ... 2.5 dark/round. 1.0 is sawtooth-like.
        layout.add (std::make_unique<P> (juce::ParameterID { tone, 1 }, "Tone",
            juce::NormalisableRange<float> (0.5f, 2.5f), 1.2f,
            A().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2); })
               .withValueFromStringFunction ([] (const juce::String& s) { return s.getFloatValue(); })));

        layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { unison, 1 }, "Unison", 1, 7, 3,
            juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return juce::String (v) + "x"; })
                                               .withValueFromStringFunction ([] (const juce::String& s) { return s.getIntValue(); })));

        layout.add (std::make_unique<P> (juce::ParameterID { detune, 1 }, "Detune",
            juce::NormalisableRange<float> (0.0f, 50.0f), 12.0f,
            A().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " ct"; })
               .withValueFromStringFunction ([] (const juce::String& s) { return s.getFloatValue(); })));

        layout.add (std::make_unique<P> (juce::ParameterID { sub, 1 }, "Sub",
            juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct));

        auto rateRange = logRange (0.05f, 20.0f);
        layout.add (std::make_unique<P> (juce::ParameterID { lfoRate, 1 }, "LFO Rate", rateRange, 0.5f,
            A().withStringFromValueFunction ([] (float v, int) { return formatHz (v); })
               .withValueFromStringFunction ([] (const juce::String& s) { return s.getFloatValue(); })));

        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { lfoSync, 1 }, "LFO Sync", false));

        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { lfoRateSync, 1 }, "LFO Rate (Sync)",
            syncNames, syncDefaultIndex));

        layout.add (std::make_unique<P> (juce::ParameterID { lfoAmount, 1 }, "LFO Amount",
            juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct));

        layout.add (std::make_unique<P> (juce::ParameterID { cutoff, 1 }, "Cutoff", logRange (20.0f, 20000.0f), 8000.0f,
            A().withStringFromValueFunction ([] (float v, int) { return formatHz (v); })
               .withValueFromStringFunction ([] (const juce::String& s)
               {
                   const auto v = s.getFloatValue();
                   return s.containsIgnoreCase ("k") ? v * 1000.0f : v;
               })));

        layout.add (std::make_unique<P> (juce::ParameterID { resonance, 1 }, "Resonance",
            juce::NormalisableRange<float> (0.0f, 1.0f), 0.1f, pct));

        auto timeAttrs = A().withStringFromValueFunction ([] (float v, int) { return formatTime (v); })
                            .withValueFromStringFunction ([] (const juce::String& s)
                            {
                                const auto v = s.getFloatValue();
                                return s.containsIgnoreCase ("ms") ? v / 1000.0f : v;
                            });

        layout.add (std::make_unique<P> (juce::ParameterID { attack, 1 }, "Attack", logRange (0.001f, 5.0f), 0.010f, timeAttrs));
        layout.add (std::make_unique<P> (juce::ParameterID { decay, 1 }, "Decay", logRange (0.005f, 10.0f), 0.300f, timeAttrs));
        layout.add (std::make_unique<P> (juce::ParameterID { sustain, 1 }, "Sustain",
            juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f, pct));
        layout.add (std::make_unique<P> (juce::ParameterID { release, 1 }, "Release", logRange (0.005f, 10.0f), 0.300f, timeAttrs));

        // dB range, skewed so 0 dB sits at a useful knob angle; minimum = -inf.
        juce::NormalisableRange<float> outRange (outputMinDb, outputMaxDb);
        outRange.setSkewForCentre (-12.0f);
        layout.add (std::make_unique<P> (juce::ParameterID { output, 1 }, "Output", outRange, 0.0f,
            A().withStringFromValueFunction ([] (float v, int) { return formatDb (v); })
               .withValueFromStringFunction ([] (const juce::String& s)
               {
                   return s.contains (juce::CharPointer_UTF8 ("\xe2\x88\x9e")) || s.containsIgnoreCase ("inf")
                       ? outputMinDb : s.getFloatValue();
               })));

        return layout;
    }
}
