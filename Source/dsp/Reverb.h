#pragma once

#include <array>
#include <vector>
#include <juce_core/juce_core.h>

namespace axsynth
{
    // Stereo feedback delay network reverb: input diffusion (4 allpasses), 8 delay lines
    // mixed by a Householder matrix, per-line damping and decay, two lines slowly
    // modulated against metallic ringing. All memory is allocated in prepare().
    class FdnReverb
    {
    public:
        void prepare (double sampleRate);
        void reset() noexcept;

        // size 0..1 sets the decay time (0.4 .. 8 s), damping 0..1 darkens the tail.
        void setParameters (float size, float damping) noexcept;

        // Adds mix * wet to left/right in place. Real-time safe.
        void process (float* left, float* right, int n, float mix) noexcept;

        static constexpr int numLines = 8;

    private:
        struct Delay
        {
            std::vector<float> buf;
            int write = 0;
            void resize (int len) { buf.assign ((size_t) juce::jmax (4, len), 0.0f); write = 0; }
            void clear() noexcept { std::fill (buf.begin(), buf.end(), 0.0f); write = 0; }
            float readInterp (float delay) const noexcept
            {
                const int len = (int) buf.size();
                float pos = (float) write - delay;
                while (pos < 0.0f) pos += (float) len;
                const int i0 = (int) pos % len;
                const int i1 = (i0 + 1) % len;
                const float fr = pos - std::floor (pos);
                return buf[(size_t) i0] + fr * (buf[(size_t) i1] - buf[(size_t) i0]);
            }
            float read (int delay) const noexcept
            {
                const int len = (int) buf.size();
                return buf[(size_t) ((write - delay + len) % len)];
            }
            void push (float x) noexcept { buf[(size_t) write] = x; write = (write + 1) % (int) buf.size(); }
        };

        struct Allpass
        {
            Delay d;
            int length = 1;
            float process (float x) noexcept
            {
                constexpr float g = 0.65f;
                const float delayed = d.read (length);
                const float v = x + g * delayed;
                d.push (v);
                return delayed - g * v;
            }
        };

        double sr = 48000.0;
        std::array<Delay, numLines> lines;
        std::array<float, numLines> lengths {};   // samples
        std::array<float, numLines> gains {};     // per-line decay
        std::array<float, numLines> damp {};      // one-pole state
        std::array<Allpass, 4> diffusers;
        float dampCoeff = 0.3f;
        double modPhase = 0.0;
        float modDepth = 8.0f;                    // samples
        bool prepared = false;
    };
}
