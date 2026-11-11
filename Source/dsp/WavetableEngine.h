#pragma once

#include "WavetableBuilder.h"
#include "../camera/CameraConnector.h"

namespace axsynth
{
    // Background worker: takes the newest camera frame, extracts scanline luminance,
    // smooths the harmonic amplitudes (one-pole, 150 ms), renders the wavetable and
    // publishes it lock-free to the audio thread. Also keeps display data for the UI.
    class WavetableEngine : private juce::Thread
    {
    public:
        static constexpr int displayPoints = 128;
        using DisplayFrames = std::array<std::array<float, displayPoints>, numFrames>;

        explicit WavetableEngine (CameraConnector&);
        ~WavetableEngine() override;

        void start();
        void stop();

        // Wavetable rectangle in normalised image coordinates (UI/state, any thread).
        void setRect (juce::Rectangle<float>);
        juce::Rectangle<float> getRect() const;
        static juce::Rectangle<float> defaultRect() { return { 0.2f, 0.2f, 0.6f, 0.6f }; }

        // Harmonic tilt: amp_k = lum * (1/k)^tilt. Any thread; re-weights the last image.
        void setTilt (float t) noexcept { tilt.store (t, std::memory_order_relaxed); }
        // Contrast stretch amount 0..1 (see builder::stretchContrast). Any thread.
        void setContrast (float c) noexcept { contrast.store (c, std::memory_order_relaxed); }

        // UI accessors (message thread).
        juce::Image getDisplayImage() const;
        void getDisplayFrames (DisplayFrames& out) const;
        juce::uint64 getDisplayVersion() const noexcept { return displayVersion.load(); }

        // Test hook: inject an image as if it came from the camera.
        void injectImageForTesting (const juce::Image&);
        // Test hook: process pending work synchronously (worker must not be running).
        void stepForTesting (double dtSeconds);

        WavetablePool& getPool() noexcept { return pool; }

        static constexpr double smoothingSeconds = 0.150;

    private:
        void run() override;
        void step (double dtSeconds);
        void publish();

        CameraConnector& camera;
        WavetablePool pool;
        builder::Renderer renderer;

        HarmonicMatrix target {}, current {}, lum {};
        bool hasTarget = false, converged = true;
        juce::Image lastImage;
        juce::uint32 lastRectVersion = 0;

        mutable juce::SpinLock rectLock;
        juce::Rectangle<float> rect { 0.2f, 0.2f, 0.6f, 0.6f };
        std::atomic<juce::uint32> rectVersion { 1 };
        std::atomic<float> tilt { builder::defaultTilt };
        float lastTilt = builder::defaultTilt;
        std::atomic<float> contrast { 0.0f };
        float lastContrast = 0.0f;
        HarmonicMatrix stretched {};

        mutable juce::SpinLock displayLock;
        juce::Image displayImage;
        DisplayFrames displayFrames {};
        std::atomic<juce::uint64> displayVersion { 0 };

        juce::SpinLock injectLock;
        juce::Image injectedImage;
    };

    // Audio-thread side: holds current and fading-out tables, 20 ms crossfade.
    class WavetableReader
    {
    public:
        explicit WavetableReader (WavetablePool& p) : pool (p) {}
        ~WavetableReader() { pool.release (current); pool.release (previous); }

        void prepare (double sampleRate) noexcept { fadeStep = 1.0f / (float) (0.020 * sampleRate); }

        // Call at the start of each sub-block.
        void update() noexcept
        {
            if (previous >= 0)
                return; // finish the running crossfade first

            const int idx = pool.takePending();
            if (idx < 0)
                return;

            numSwaps.fetch_add (1, std::memory_order_relaxed);

            if (current < 0)
            {
                current = idx;
                fade = 1.0f;
            }
            else
            {
                previous = current;
                current = idx;
                fade = 0.0f;
            }
        }

        // Advance crossfade by n samples (after rendering the sub-block).
        void advance (int n) noexcept
        {
            if (previous < 0)
                return;
            fade += fadeStep * (float) n;
            if (fade >= 1.0f)
            {
                fade = 1.0f;
                pool.release (previous);
                previous = -1;
            }
        }

        const Wavetable* getCurrent() noexcept  { return current  >= 0 ? &pool.get (current)  : nullptr; }
        const Wavetable* getPrevious() noexcept { return previous >= 0 ? &pool.get (previous) : nullptr; }
        float getFade() const noexcept { return fade; }
        float getFadeStep() const noexcept { return previous >= 0 ? fadeStep : 0.0f; }
        // Tables taken from the pool so far (tests and diagnostics, any thread).
        juce::uint32 getNumSwaps() const noexcept { return numSwaps.load (std::memory_order_relaxed); }

    private:
        WavetablePool& pool;
        int current = -1, previous = -1;
        float fade = 1.0f, fadeStep = 0.001f;
        std::atomic<juce::uint32> numSwaps { 0 };
    };
}
