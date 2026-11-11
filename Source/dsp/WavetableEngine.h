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
        // Motion 0..1: blend from the image to what changed in it (frame difference with a
        // short trail). getMotionLevel() is how much is moving, 0..1, for the output gate.
        void setMotion (float m) noexcept { motion.store (m, std::memory_order_relaxed); }
        float getMotionLevel() const noexcept { return motionLevel.load (std::memory_order_relaxed); }
        // Freeze: ignore new camera frames and keep the current image.
        void setFreeze (bool f) noexcept { freeze.store (f, std::memory_order_relaxed); }
        // Scan direction: false = columns (vertical lines), true = rows.
        void setHorizontalScan (bool h) noexcept { horizontalScan.store (h, std::memory_order_relaxed); }

        static constexpr double motionTrailSeconds = 0.35;

        // UI accessors (message thread).
        juce::Image getDisplayImage() const;
        void getDisplayFrames (DisplayFrames& out) const;
        juce::uint64 getDisplayVersion() const noexcept { return displayVersion.load(); }

        // Test hook: inject an image as if it came from the camera.
        void injectImageForTesting (const juce::Image&);
        // Test hook: process pending work synchronously (worker must not be running).
        void stepForTesting (double dtSeconds);

        WavetablePool& getPool() noexcept { return pool; }

        // Current (smoothed) harmonic amplitudes of all frames, for the UI.
        void getDisplayHarmonics (HarmonicMatrix& out) const;
        // Frames rendered so far (diagnostics: how much work the worker does).
        juce::uint64 getFramesRendered() const noexcept { return framesRendered.load (std::memory_order_relaxed); }

        static constexpr double smoothingSeconds = 0.150;

    private:
        void run() override;
        void step (double dtSeconds);
        void publish (float threshold = 0.0f);

        CameraConnector& camera;
        WavetablePool pool;
        // The 32 x 256 matrices (~32 KB each) live on the heap, so an engine (or a plugin
        // instance) never needs a large stack.
        struct Matrices
        {
            HarmonicMatrix target {}, current {}, lum {}, stretched {}, previousLum {}, motionAcc {},
                           blended {}, rendered {}, displayHarmonics {};
        };
        std::unique_ptr<Matrices> mats { std::make_unique<Matrices>() };
        HarmonicMatrix& target = mats->target;
        HarmonicMatrix& current = mats->current;
        HarmonicMatrix& lum = mats->lum;
        HarmonicMatrix& stretched = mats->stretched;
        HarmonicMatrix& previousLum = mats->previousLum;
        HarmonicMatrix& motionAcc = mats->motionAcc;
        HarmonicMatrix& blended = mats->blended;
        HarmonicMatrix& rendered = mats->rendered;
        HarmonicMatrix& displayHarmonics = mats->displayHarmonics;

        builder::Renderer renderer;
        // Frames are rendered into 'master' only when their harmonics changed audibly
        // since the last render; the pool buffer then gets a copy of the whole master.
        Wavetable master;
        bool masterValid = false;
        std::atomic<juce::uint64> framesRendered { 0 };
        static constexpr float renderThreshold = 0.02f; // of the frame's strongest harmonic (~0.17 dB)

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
        std::atomic<float> motion { 0.0f }, motionLevel { 0.0f };
        std::atomic<bool> freeze { false }, horizontalScan { false };
        bool lastHorizontal = false, havePrevious = false;

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
