#include "Reverb.h"
#include <cmath>

namespace axsynth
{
    namespace
    {
        // Mutually prime-ish delay times (ms) so the echoes do not line up.
        constexpr float lineMs[FdnReverb::numLines] { 29.7f, 37.1f, 41.1f, 43.7f, 53.3f, 59.9f, 67.7f, 73.1f };
        constexpr float diffuserMs[4] { 4.77f, 3.59f, 12.73f, 9.31f };
    }

    void FdnReverb::prepare (double sampleRate)
    {
        sr = sampleRate;
        modDepth = (float) (0.0006 * sr);   // +-0.6 ms
        for (int i = 0; i < numLines; ++i)
        {
            lengths[(size_t) i] = (float) (lineMs[i] * 0.001 * sr);
            lines[(size_t) i].resize ((int) lengths[(size_t) i] + (int) modDepth + 8);
        }
        for (int i = 0; i < 4; ++i)
        {
            diffusers[(size_t) i].length = juce::jmax (1, (int) (diffuserMs[i] * 0.001 * sr));
            diffusers[(size_t) i].d.resize (diffusers[(size_t) i].length + 2);
        }
        prepared = true;
        setParameters (0.6f, 0.4f);
        reset();
    }

    void FdnReverb::reset() noexcept
    {
        for (auto& l : lines) l.clear();
        for (auto& a : diffusers) a.d.clear();
        damp.fill (0.0f);
        modPhase = 0.0;
    }

    void FdnReverb::setParameters (float size, float damping) noexcept
    {
        const float t60 = 0.4f + 7.6f * size * size;                 // seconds
        for (int i = 0; i < numLines; ++i)
            gains[(size_t) i] = std::pow (10.0f, -3.0f * lengths[(size_t) i] / (t60 * (float) sr));
        // Damping: one-pole low-pass in the loop, ~12 kHz (0) down to ~2.5 kHz (1).
        const float fc = 12000.0f * std::pow (0.2f, juce::jlimit (0.0f, 1.0f, damping));
        dampCoeff = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * fc / (float) sr);
    }

    void FdnReverb::process (float* left, float* right, int n, float mix) noexcept
    {
        if (! prepared)
            return;

        const double modInc = 0.37 / sr;   // slow modulation
        for (int s = 0; s < n; ++s)
        {
            // Input: mono sum through the diffusers.
            float in = 0.5f * (left[s] + right[s]);
            for (auto& a : diffusers)
                in = a.process (in);

            // Read the lines (two of them modulated in opposite directions).
            std::array<float, numLines> x {};
            const float mod = (float) std::sin (juce::MathConstants<double>::twoPi * modPhase) * modDepth;
            modPhase += modInc;
            if (modPhase >= 1.0) modPhase -= 1.0;
            for (int i = 0; i < numLines; ++i)
            {
                float d = lengths[(size_t) i];
                if (i == 2) d += mod;
                if (i == 5) d -= mod;
                const float v = lines[(size_t) i].readInterp (d);
                damp[(size_t) i] += dampCoeff * (v - damp[(size_t) i]);
                x[(size_t) i] = damp[(size_t) i] * gains[(size_t) i];
            }

            // Householder feedback: y = x - 2/N * sum(x). Lossless mixing between the lines.
            float sum = 0.0f;
            for (auto v : x) sum += v;
            const float h = sum * (2.0f / (float) numLines);
            for (int i = 0; i < numLines; ++i)
            {
                const float sign = (i & 1) ? -1.0f : 1.0f;
                lines[(size_t) i].push (x[(size_t) i] - h + sign * in);
            }

            // Stereo out: alternate lines to each side.
            const float wetL = x[0] - x[2] + x[4] - x[6];
            const float wetR = x[1] - x[3] + x[5] - x[7];
            left[s]  += mix * 0.6f * wetL;
            right[s] += mix * 0.6f * wetR;
        }
    }
}
