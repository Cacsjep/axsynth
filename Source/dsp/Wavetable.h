#pragma once

#include <array>
#include <atomic>
#include <vector>
#include <juce_core/juce_core.h>

namespace axsynth
{
    inline constexpr int numFrames    = 32;   // scanlines (N)
    inline constexpr int numHarmonics = 64;   // sections per scanline (H)
    inline constexpr int tableSize    = 2048; // samples per frame
    inline constexpr int numMips      = 7;    // 64, 32, 16, 8, 4, 2, 1 harmonics
    inline constexpr int tableStride  = tableSize + 1; // +1 guard sample for interpolation

    using HarmonicMatrix = std::array<std::array<float, numHarmonics>, numFrames>;

    inline constexpr int harmonicsInMip (int mip) noexcept { return numHarmonics >> mip; }

    // 32 frames x 7 band-limited mip levels x 2048 samples. Allocated once.
    struct Wavetable
    {
        Wavetable() : data ((size_t) numMips * numFrames * tableStride, 0.0f) {}

        float* frame (int mip, int f) noexcept { return data.data() + ((size_t) mip * numFrames + (size_t) f) * tableStride; }
        const float* frame (int mip, int f) const noexcept { return data.data() + ((size_t) mip * numFrames + (size_t) f) * tableStride; }

        std::vector<float> data;
    };

    // Lock-free hand-off of finished wavetables from the worker to the audio thread.
    // Four preallocated buffers: audio holds at most two (current + fading-out previous),
    // one may sit in the pending slot, the worker writes into the fourth.
    class WavetablePool
    {
    public:
        static constexpr int size = 4;

        WavetablePool()
        {
            for (auto& s : states) s.store (stateFree);
        }

        // Worker: grab a free buffer to write into (-1 if none, which cannot happen with 4).
        int acquireForWrite() noexcept
        {
            for (int i = 0; i < size; ++i)
            {
                int expected = stateFree;
                if (states[(size_t) i].compare_exchange_strong (expected, stateWriting))
                    return i;
            }
            return -1;
        }

        // Worker: publish a written buffer. Replaces an unconsumed pending one.
        void publish (int index) noexcept
        {
            states[(size_t) index].store (statePending);
            const int old = pending.exchange (index);
            if (old >= 0)
                states[(size_t) old].store (stateFree);
        }

        // Audio thread: take the newest published table, or -1.
        int takePending() noexcept
        {
            const int idx = pending.exchange (-1);
            if (idx >= 0)
                states[(size_t) idx].store (stateInUse);
            return idx;
        }

        // Audio thread: give a table back.
        void release (int index) noexcept
        {
            if (index >= 0)
                states[(size_t) index].store (stateFree);
        }

        Wavetable& get (int index) noexcept { return tables[(size_t) index]; }

    private:
        enum { stateFree, stateWriting, statePending, stateInUse };
        std::array<Wavetable, size> tables;
        std::array<std::atomic<int>, size> states;
        std::atomic<int> pending { -1 };
    };
}
