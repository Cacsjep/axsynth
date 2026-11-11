#include "WavetableStack.h"

namespace axsynth
{
    namespace
    {
        // Stack geometry relative to the component size.
        constexpr float frontWidth  = 0.66f; // width of one frame
        constexpr float depthX      = 0.32f; // diagonal offset of the last frame
        constexpr float depthY      = 0.50f;
        constexpr float amplitude   = 0.20f; // half height of a frame
    }

    WavetableStack::WavetableStack (WavetableEngine& e) : engine (e)
    {
        setInterceptsMouseClicks (false, false);
    }

    void WavetableStack::update (float effectivePosition)
    {
        bool changed = false;

        const auto v = engine.getDisplayVersion();
        if (v != version)
        {
            version = v;
            engine.getDisplayFrames (frames);
            changed = true;
        }

        if (std::abs (effectivePosition - position) > 1.0e-4f)
        {
            position = effectivePosition;
            changed = true;
        }

        if (changed)
            repaint();
    }

    juce::Path WavetableStack::framePath (const float* values, float depth) const
    {
        const auto b = getLocalBounds().toFloat().reduced (4.0f);
        const float w = b.getWidth() * frontWidth;
        const float x0 = b.getX() + depth * b.getWidth() * depthX;
        const float yMid = b.getBottom() - b.getHeight() * amplitude - depth * b.getHeight() * depthY;
        const float amp = b.getHeight() * amplitude;

        juce::Path p;
        constexpr int n = WavetableEngine::displayPoints;
        for (int i = 0; i <= n; ++i)
        {
            const float v = juce::jlimit (-1.0f, 1.0f, values[i % n]);
            const float x = x0 + w * (float) i / (float) n;
            const float y = yMid - v * amp;
            if (i == 0) p.startNewSubPath (x, y);
            else        p.lineTo (x, y);
        }
        return p;
    }

    void WavetableStack::paint (juce::Graphics& g)
    {
        // Back to front, fading with depth. Slot 0 (front) is reserved for the active frame.
        for (int f = numFrames - 1; f >= 0; --f)
        {
            const float depth = (float) (f + 1) / (float) numFrames;
            g.setColour (theme::text.withAlpha (0.45f - 0.38f * depth));
            g.strokePath (framePath (frames[(size_t) f].data(), depth), juce::PathStrokeType (1.0f));
        }

        // Active frame, interpolated between neighbours, in front (spec 6).
        const float pos = juce::jlimit (0.0f, 1.0f, position) * (float) (numFrames - 1);
        const int f0 = juce::jlimit (0, numFrames - 1, (int) pos);
        const int f1 = juce::jmin (f0 + 1, numFrames - 1);
        const float t = pos - (float) f0;

        std::array<float, WavetableEngine::displayPoints> active;
        for (size_t i = 0; i < active.size(); ++i)
            active[i] = frames[(size_t) f0][i] + t * (frames[(size_t) f1][i] - frames[(size_t) f0][i]);

        g.setColour (theme::accent);
        g.strokePath (framePath (active.data(), 0.0f),
                      juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}
