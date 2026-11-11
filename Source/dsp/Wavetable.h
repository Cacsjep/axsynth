#pragma once

#include <array>
#include <atomic>
#include <vector>
#include <juce_core/juce_core.h>

namespace axsynth
{
    inline constexpr int numFrames    = 32;   // scanlines (N)
    inline constexpr int numHarmonics = 256;  // bands per scanline: harmonics 1..256 (A1 reaches 14 kHz)
    inline constexpr int tableSize    = 4096; // samples per frame of mip 0: 16 per period of the top harmonic
    inline constexpr int numMips      = 16;   // half-octave steps, see harmonicsInMip

    using HarmonicMatrix = std::array<std::array<float, numHarmonics>, numFrames>;

    // Band-limited versions in half-octave steps: the brightness drop when a note crosses
    // a step is half as large as with octave steps.
    inline constexpr int mipHarmonics[numMips] { 256, 181, 128, 90, 64, 45, 32, 22, 16, 11, 8, 6, 4, 3, 2, 1 };
    inline constexpr int harmonicsInMip (int mip) noexcept { return mipHarmonics[mip]; }

    // Each mip is only as long as it needs: at least 16 samples per period of its highest
    // harmonic (clean linear interpolation), at least 64. Power of two (inverse FFT size).
    constexpr int mipSizeFor (int harmonics) noexcept
    {
        int n = 64;
        while (n < 16 * harmonics) n *= 2;
        return n;
    }
    inline constexpr int mipSize (int mip) noexcept { return mipSizeFor (harmonicsInMip (mip)); }
    static_assert (mipSizeFor (numHarmonics) == tableSize);

    // Start of mip m in the flat buffer; each frame has one guard sample for interpolation.
    constexpr size_t mipOffset (int mip) noexcept
    {
        size_t o = 0;
        for (int m = 0; m < mip; ++m) o += (size_t) numFrames * (size_t) (mipSize (m) + 1);
        return o;
    }

    // 32 frames x 16 band-limited mips of varying length (~2.2 MB). Allocated once.
    struct Wavetable
    {
        Wavetable() : data (mipOffset (numMips), 0.0f) {}

        float* frame (int mip, int f) noexcept { return data.data() + mipOffset (mip) + (size_t) f * (size_t) (mipSize (mip) + 1); }
        const float* frame (int mip, int f) const noexcept { return data.data() + mipOffset (mip) + (size_t) f * (size_t) (mipSize (mip) + 1); }

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
