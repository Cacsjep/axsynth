#pragma once

#include "Theme.h"
#include "../dsp/WavetableEngine.h"

namespace axsynth
{
    // Camera image (letterboxed, aspect-correct) with the editable wavetable rectangle.
    // The rectangle is stored in normalised image coordinates in the engine, so the
    // overlay maps exactly onto the image pixels the engine samples.
    class CameraView : public juce::Component
    {
    public:
        explicit CameraView (WavetableEngine&);

        // Message thread, ~30 Hz. Repaints only when something visible changed.
        void update (float effectivePosition);

        void paint (juce::Graphics&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

        bool hasImage() const noexcept { return image.isValid(); }
        // Number of distinct images seen so far.
        juce::uint64 getImageCount() const noexcept { return imageCount; }

    private:
        enum class Hit { none, move, topLeft, topRight, bottomLeft, bottomRight };

        juce::Rectangle<float> imageArea() const;
        juce::Rectangle<float> rectToView (juce::Rectangle<float> norm) const;
        Hit findHit (juce::Point<float>) const;
        void updateCursor (Hit);

        WavetableEngine& engine;
        juce::Image image;
        float imageAspect = 16.0f / 9.0f;
        juce::Rectangle<float> rect;     // normalised
        int activeLine = -1;
        juce::uint64 imageCount = 0;

        Hit dragHit = Hit::none;
        juce::Rectangle<float> rectOnDown;
        juce::Point<float> downPos;
    };
}
