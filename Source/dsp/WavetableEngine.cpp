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
        if (! (std::isfinite (r.getX()) && std::isfinite (r.getY()) && std::isfinite (r.getWidth()) && std::isfinite (r.getHeight())))
            return;
        r = r.getIntersection ({ 0.0f, 0.0f, 1.0f, 1.0f });
        if (! (r.getWidth() >= 0.01f && r.getHeight() >= 0.01f))
            return;
        {
            const juce::SpinLock::ScopedLockType sl (rectLock);
            rect = r;
        }
        ++rectVersion;
    }

    juce::Rectangle<float> WavetableEngine::getRect() const
    {
        const juce::SpinLock::ScopedLockType sl (rectLock);
        return rect;
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

    void WavetableEngine::getDisplayHarmonics (HarmonicMatrix& out) const
    {
        const juce::SpinLock::ScopedLockType sl (displayLock);
        out = displayHarmonics;
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
        bool newInput = false, newImage = false;
        const bool frozen = freeze.load (std::memory_order_relaxed);

        juce::Image img;
        {
            const juce::SpinLock::ScopedLockType sl (injectLock);
            if (injectedImage.isValid())
            {
                img = injectedImage;
                injectedImage = {};
            }
        }

        if (! frozen && (img.isValid() || camera.popLatestFrame (img)))
        {
            lastImage = img;
            newInput = newImage = true;
            const juce::SpinLock::ScopedLockType sl (displayLock);
            displayImage = img;
        }

        // Rect or scan direction changed: a new view, not motion.
        bool newView = false;
        const auto rv = rectVersion.load();
        if (rv != lastRectVersion) newView = true;
        lastRectVersion = rv;

        const bool horizontal = horizontalScan.load (std::memory_order_relaxed);
        if (horizontal != lastHorizontal) newView = true;
        lastHorizontal = horizontal;

        if (newView && lastImage.isValid())
            newInput = true;

        const float t = tilt.load (std::memory_order_relaxed);
        if (std::abs (t - lastTilt) > 1.0e-4f && lastImage.isValid())
            newInput = true;
        lastTilt = t;

        const float con = contrast.load (std::memory_order_relaxed);
        if (std::abs (con - lastContrast) > 1.0e-4f && lastImage.isValid())
            newInput = true;
        lastContrast = con;

        const float mot = juce::jlimit (0.0f, 1.0f, motion.load (std::memory_order_relaxed));

        if ((newImage || newView) && lastImage.isValid())
        {
            builder::extractLuminance (lastImage, getRect(), lum, horizontal);

            // Motion: per band frame difference with a noise gate, peak-held into a trail.
            if (havePrevious && newImage && ! newView)
            {
                constexpr float noiseGate = 0.02f, gain = 6.0f;
                for (size_t f = 0; f < (size_t) numFrames; ++f)
                    for (size_t k = 0; k < (size_t) numHarmonics; ++k)
                    {
                        const float d = juce::jmax (0.0f, std::abs (lum[f][k] - previousLum[f][k]) - noiseGate) * gain;
                        motionAcc[f][k] = juce::jmax (motionAcc[f][k], juce::jmin (1.0f, d));
                    }
            }
            previousLum = lum;
            havePrevious = true;
        }

        // Motion trail decays continuously; the level is the mean over the rect.
        {
            const float decay = (float) std::exp (-dt / motionTrailSeconds);
            double sum = 0.0;
            for (auto& f : motionAcc)
                for (auto& v : f) { v *= decay; sum += v; }
            const float level = juce::jlimit (0.0f, 1.0f, (float) (sum / (numFrames * numHarmonics)) * 8.0f);
            motionLevel.store (level, std::memory_order_relaxed);
        }

        if (mot > 0.0f && lastImage.isValid())
            newInput = true; // the trail changes every step

        if (newInput && lastImage.isValid())
        {
            for (size_t f = 0; f < (size_t) numFrames; ++f)
                for (size_t k = 0; k < (size_t) numHarmonics; ++k)
                    blended[f][k] = lum[f][k] + mot * (motionAcc[f][k] - lum[f][k]);

            builder::stretchContrast (blended, stretched, con);
            builder::weightHarmonics (stretched, target, t);
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
            publish (0.0f);   // settled: render exactly
            return;
        }

        publish (renderThreshold);
    }

    void WavetableEngine::publish (float threshold)
    {
        bool anyRendered = false;
        for (int f = 0; f < numFrames; ++f)
        {
            const auto& now = current[(size_t) f];
            bool changed = ! masterValid;
            if (! changed)
            {
                float peak = 1.0e-9f;
                for (auto v : now) peak = juce::jmax (peak, v);
                const auto& was = rendered[(size_t) f];
                for (size_t k = 0; k < now.size() && ! changed; ++k)
                    changed = std::abs (now[k] - was[k]) > threshold * peak;
            }
            if (changed)
            {
                renderer.renderFrame (current, master, f);
                rendered[(size_t) f] = now;
                anyRendered = true;
                framesRendered.fetch_add (1, std::memory_order_relaxed);
            }
        }
        masterValid = true;

        if (! anyRendered)
            return; // nothing audible changed

        const int idx = pool.acquireForWrite();
        if (idx < 0)
            return;

        auto& table = pool.get (idx);
        std::copy (master.data.begin(), master.data.end(), table.data.begin());
        pool.publish (idx);

        DisplayFrames frames;
        for (int f = 0; f < numFrames; ++f)
        {
            const float* src = master.frame (0, f);
            for (int i = 0; i < displayPoints; ++i)
                frames[(size_t) f][(size_t) i] = src[i * mipSize (0) / displayPoints];
        }

        {
            const juce::SpinLock::ScopedLockType sl (displayLock);
            displayFrames = frames;
            displayHarmonics = current;
        }
        ++displayVersion;
    }
}
