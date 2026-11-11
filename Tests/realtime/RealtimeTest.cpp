// Realtime-safety gate (spec 8.4): calls processBlock 10 000 times without a camera,
// with simulated wavetable swaps, and fails if the audio thread allocates.
#include "PluginProcessor.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>

namespace
{
    thread_local bool guardActive = false;
    std::atomic<long long> violations { 0 };

    void* checkedAlloc (std::size_t size)
    {
        if (guardActive)
            violations.fetch_add (1);
        if (void* p = std::malloc (size == 0 ? 1 : size))
            return p;
        throw std::bad_alloc();
    }
}

void* operator new (std::size_t s) { return checkedAlloc (s); }
void* operator new[] (std::size_t s) { return checkedAlloc (s); }
void* operator new (std::size_t s, const std::nothrow_t&) noexcept { try { return checkedAlloc (s); } catch (...) { return nullptr; } }
void* operator new[] (std::size_t s, const std::nothrow_t&) noexcept { try { return checkedAlloc (s); } catch (...) { return nullptr; } }
void operator delete (void* p) noexcept { if (guardActive && p) violations.fetch_add (1); std::free (p); }
void operator delete[] (void* p) noexcept { if (guardActive && p) violations.fetch_add (1); std::free (p); }
void operator delete (void* p, std::size_t) noexcept { operator delete (p); }
void operator delete[] (void* p, std::size_t) noexcept { operator delete[] (p); }

static juce::Image makeTestImage (int seed)
{
    juce::Image img (juce::Image::RGB, 320, 180, true);
    juce::Graphics g (img);
    juce::Random rnd (seed);
    for (int i = 0; i < 40; ++i)
    {
        g.setColour (juce::Colour (rnd.nextInt (256) * 0x10101u | 0xff000000u));
        g.fillRect (rnd.nextInt (320), rnd.nextInt (180), 10 + rnd.nextInt (80), 10 + rnd.nextInt (60));
    }
    return img;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;

    // Self-check: the hook must see an allocation made under the guard.
    {
        guardActive = true;
        auto* probe = new juce::String ("probe that does not fit SSO, forces heap allocation");
        delete probe;
        guardActive = false;
        if (violations.exchange (0) == 0) { std::printf ("FAIL: allocation hook inactive\n"); return 1; }
    }

    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;
    constexpr int numBlocks = 10000;

    axsynth::AxSynthProcessor proc;
    proc.setPlayConfigDetails (0, 2, sampleRate, blockSize);
    proc.prepareToPlay (sampleRate, blockSize);

    // Exercise LFO and filter modulation too.
    proc.getState().getParameter (axsynth::params::lfoAmount)->setValueNotifyingHost (0.6f);
    proc.getState().getParameter (axsynth::params::lfoRate)->setValueNotifyingHost (0.9f);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    midi.ensureSize (4096);

    std::array<juce::Image, 8> images;
    for (int i = 0; i < (int) images.size(); ++i)
        images[(size_t) i] = makeTestImage (i + 1);

    int swapsInjected = 0;
    long long nonSilentBlocks = 0;

    for (int b = 0; b < numBlocks; ++b)
    {
        midi.clear();
        if (b % 50 == 0)  midi.addEvent (juce::MidiMessage::noteOn (1, 36 + (b / 50) % 48, (juce::uint8) 100), b % blockSize);
        if (b % 50 == 30) midi.addEvent (juce::MidiMessage::noteOff (1, 36 + (b / 50) % 48), 7);
        if (b % 333 == 0) midi.addEvent (juce::MidiMessage::pitchWheel (1, (b * 37) % 16384), 3);
        if (b % 211 == 0) midi.addEvent (juce::MidiMessage::controllerEvent (1, 1, b % 128), 11);

        // Simulated camera frames -> wavetable swaps happen on the worker thread.
        if (b % 10 == 0)
        {
            proc.getEngine().injectImageForTesting (images[(size_t) (swapsInjected++ % (int) images.size())]);
            juce::Thread::sleep (1);
        }

        guardActive = true;
        proc.processBlock (buffer, midi);
        guardActive = false;

        if (buffer.getMagnitude (0, 0, blockSize) > 1.0e-4f)
            ++nonSilentBlocks;
    }

    proc.releaseResources();

    const auto v = violations.load();
    std::printf ("blocks: %d, swaps injected: %d, non-silent blocks: %lld, audio-thread allocations: %lld\n",
                 numBlocks, swapsInjected, nonSilentBlocks, v);

    if (v != 0)        { std::printf ("FAIL: audio thread allocated\n"); return 1; }
    if (nonSilentBlocks < numBlocks / 4) { std::printf ("FAIL: too little audio output\n"); return 1; }
    std::printf ("PASS\n");
    return 0;
}
