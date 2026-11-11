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
        // lines / spread: every line that plays is highlighted (same offsets as the voices).
        void update (float effectivePosition, int lines = 1, float spread = 0.0f);

        void paint (juce::Graphics&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

        bool hasImage() const noexcept { return image.isValid(); }

        // Hides the rectangle and scanlines (while the setup panel covers the view).
        void setRectVisible (bool shouldShow);
        // Scanlines as rows (horizontal scan) instead of columns.
        void setHorizontal (bool h) { if (h != horizontal) { horizontal = h; repaint(); } }
        bool isRectVisible() const noexcept { return rectVisible; }
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
        juce::uint32 activeMask = 0; // bit i set = line i plays
        bool rectVisible = true;
        bool horizontal = false;
        juce::uint64 imageCount = 0;

        Hit dragHit = Hit::none;
        juce::Rectangle<float> rectOnDown;
        juce::Point<float> downPos;
    };
}
