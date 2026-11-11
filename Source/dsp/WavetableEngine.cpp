#include "WavetableEngine.h"
#include <cmath>

namespace axsynth
{
    WavetableEngine::WavetableEngine (CameraConnector& c)
        : juce::Thread ("AxSynth Wavetable"), camera (c)
    {
        // First start without image: pure sine, published immediately.
        builder::sineHarmonics (current);
        target = current;
        publish();
    }

    WavetableEngine::~WavetableEngine() { stop(); }

    void WavetableEngine::start()
    {
        if (! isThreadRunning())
            startThread (juce::Thread::Priority::normal);
    }

    void WavetableEngine::stop() { stopThread (4000); }

    void WavetableEngine::setRect (juce::Rectangle<float> r)
    {
        r = r.getIntersection ({ 0.0f, 0.0f, 1.0f, 1.0f });
        if (r.getWidth() < 0.01f || r.getHeight() < 0.01f)
            return;
        rx = r.getX(); ry = r.getY(); rw = r.getWidth(); rh = r.getHeight();
        ++rectVersion;
    }

    juce::Rectangle<float> WavetableEngine::getRect() const
    {
        return { rx.load(), ry.load(), rw.load(), rh.load() };
    }

    juce::Image WavetableEngine::getDisplayImage() const
    {
        const juce::SpinLock::ScopedLockType sl (displayLock);
        return displayImage;
    }

    void WavetableEngine::getDisplayFrames (DisplayFrames& out) const
    {
        const juce::SpinLock::ScopedLockType sl (displayLock);
        out = displayFrames;
    }

    void WavetableEngine::injectImageForTesting (const juce::Image& img)
    {
        const juce::SpinLock::ScopedLockType sl (injectLock);
        injectedImage = img;
    }

    void WavetableEngine::stepForTesting (double dt) { step (dt); }

    void WavetableEngine::run()
    {
        auto last = juce::Time::getMillisecondCounterHiRes();

        while (! threadShouldExit())
        {
            camera.waitForFrame (40); // also paces the loop at <= 25 updates/s

            const auto now = juce::Time::getMillisecondCounterHiRes();
            const double dt = juce::jlimit (0.001, 0.5, (now - last) * 0.001);
            last = now;

            step (dt);

            if (converged)
                wait (10);
        }
    }

    void WavetableEngine::step (double dt)
    {
        bool newInput = false;

        juce::Image img;
        {
            const juce::SpinLock::ScopedLockType sl (injectLock);
            if (injectedImage.isValid())
            {
                img = injectedImage;
                injectedImage = {};
            }
        }

        if (img.isValid() || camera.popLatestFrame (img))
        {
            lastImage = img;
            newInput = true;
            const juce::SpinLock::ScopedLockType sl (displayLock);
            displayImage = img;
        }

        const auto rv = rectVersion.load();
        if (rv != lastRectVersion && lastImage.isValid())
            newInput = true;
        lastRectVersion = rv;

        if (newInput && lastImage.isValid())
        {
            builder::extractLuminance (lastImage, getRect(), lum);
            builder::weightHarmonics (lum, target);
            hasTarget = true;
            converged = false;
        }

        if (! hasTarget || converged)
            return;

        // One-pole smoothing per harmonic, time constant 150 ms.
        const float a = (float) (1.0 - std::exp (-dt / smoothingSeconds));
        float maxDiff = 0.0f;
        for (size_t f = 0; f < (size_t) numFrames; ++f)
            for (size_t k = 0; k < (size_t) numHarmonics; ++k)
            {
                auto& c = current[f][k];
                c += a * (target[f][k] - c);
                maxDiff = juce::jmax (maxDiff, std::abs (target[f][k] - c));
            }

        if (maxDiff < 1.0e-4f)
        {
            current = target;
            converged = true;
        }

        publish();
    }

    void WavetableEngine::publish()
    {
        const int idx = pool.acquireForWrite();
        if (idx < 0)
            return;

        auto& table = pool.get (idx);
        renderer.render (current, table);
        pool.publish (idx);

        DisplayFrames frames;
        for (int f = 0; f < numFrames; ++f)
        {
            const float* src = table.frame (0, f);
            for (int i = 0; i < displayPoints; ++i)
                frames[(size_t) f][(size_t) i] = src[i * tableSize / displayPoints];
        }

        {
            const juce::SpinLock::ScopedLockType sl (displayLock);
            displayFrames = frames;
        }
        ++displayVersion;
    }
}
