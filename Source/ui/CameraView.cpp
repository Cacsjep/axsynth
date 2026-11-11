#include "CameraView.h"

namespace axsynth
{
    namespace
    {
        constexpr float handleSize = 7.0f;
        constexpr float handleGrab = 9.0f;  // hit radius around a corner
        constexpr float minSizePx = 24.0f;
    }

    CameraView::CameraView (WavetableEngine& e) : engine (e), rect (e.getRect())
    {
        setOpaque (true);
    }

    void CameraView::update (float effectivePosition)
    {
        bool changed = false;

        auto img = engine.getDisplayImage();
        if (img.getPixelData() != image.getPixelData())
        {
            image = img;
            if (image.isValid())
                ++imageCount;
            if (image.isValid() && image.getHeight() > 0)
                imageAspect = (float) image.getWidth() / (float) image.getHeight();
            changed = true;
        }

        if (dragHit == Hit::none)
        {
            const auto r = engine.getRect();
            if (r != rect)
            {
                rect = r;
                changed = true;
            }
        }

        const int line = juce::jlimit (0, numFrames - 1, juce::roundToInt (effectivePosition * (float) (numFrames - 1)));
        if (line != activeLine)
        {
            activeLine = line;
            changed = true;
        }

        if (changed)
            repaint();
    }

    juce::Rectangle<float> CameraView::imageArea() const
    {
        const auto b = getLocalBounds().toFloat();
        float w = b.getWidth(), h = w / imageAspect;
        if (h > b.getHeight())
        {
            h = b.getHeight();
            w = h * imageAspect;
        }
        return juce::Rectangle<float> (w, h).withCentre (b.getCentre());
    }

    juce::Rectangle<float> CameraView::rectToView (juce::Rectangle<float> n) const
    {
        const auto a = imageArea();
        return { a.getX() + n.getX() * a.getWidth(), a.getY() + n.getY() * a.getHeight(),
                 n.getWidth() * a.getWidth(), n.getHeight() * a.getHeight() };
    }

    void CameraView::paint (juce::Graphics& g)
    {
        g.fillAll (theme::cameraBg);

        const auto area = imageArea();
        const auto r = rectToView (rect);

        if (image.isValid())
        {
            g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
            g.drawImage (image, area);
            g.setColour (juce::Colours::black.withAlpha (0.30f)); // 70 % brightness
            g.fillRect (area);

            // Outside the rectangle: darker still.
            juce::RectangleList<float> outside (area);
            outside.subtract (r);
            g.setColour (juce::Colours::black.withAlpha (0.40f));
            g.fillRectList (outside);
        }

        // Scanlines.
        for (int i = 0; i < numFrames; ++i)
        {
            const float x = builder::scanlineX (i, r.getX(), r.getWidth());
            if (i == activeLine)
                continue;
            g.setColour (juce::Colours::white.withAlpha (0.15f));
            g.fillRect (juce::Rectangle<float> (x - 0.5f, r.getY(), 1.0f, r.getHeight()));
        }
        if (activeLine >= 0)
        {
            const float x = builder::scanlineX (activeLine, r.getX(), r.getWidth());
            g.setColour (theme::accent);
            g.fillRect (juce::Rectangle<float> (x - 0.75f, r.getY(), 1.5f, r.getHeight()));
        }

        // Border + corner handles.
        g.setColour (theme::accent);
        g.drawRect (r, 1.5f);
        for (auto p : { r.getTopLeft(), r.getTopRight(), r.getBottomLeft(), r.getBottomRight() })
            g.fillRect (juce::Rectangle<float> (handleSize, handleSize).withCentre (p));
    }

    CameraView::Hit CameraView::findHit (juce::Point<float> p) const
    {
        const auto r = rectToView (rect);
        auto near = [p] (juce::Point<float> c) { return std::abs (p.x - c.x) <= handleGrab && std::abs (p.y - c.y) <= handleGrab; };
        if (near (r.getTopLeft()))     return Hit::topLeft;
        if (near (r.getTopRight()))    return Hit::topRight;
        if (near (r.getBottomLeft()))  return Hit::bottomLeft;
        if (near (r.getBottomRight())) return Hit::bottomRight;
        if (r.contains (p))            return Hit::move;
        return Hit::none;
    }

    void CameraView::updateCursor (Hit h)
    {
        using MC = juce::MouseCursor;
        switch (h)
        {
            case Hit::topLeft:     setMouseCursor (MC::TopLeftCornerResizeCursor); break;
            case Hit::topRight:    setMouseCursor (MC::TopRightCornerResizeCursor); break;
            case Hit::bottomLeft:  setMouseCursor (MC::BottomLeftCornerResizeCursor); break;
            case Hit::bottomRight: setMouseCursor (MC::BottomRightCornerResizeCursor); break;
            case Hit::move:        setMouseCursor (MC::DraggingHandCursor); break;
            case Hit::none:        setMouseCursor (MC::NormalCursor); break;
        }
    }

    void CameraView::mouseMove (const juce::MouseEvent& e) { updateCursor (findHit (e.position)); }
    void CameraView::mouseExit (const juce::MouseEvent&)   { if (dragHit == Hit::none) updateCursor (Hit::none); }

    void CameraView::mouseDown (const juce::MouseEvent& e)
    {
        rect = engine.getRect();
        dragHit = e.mods.isLeftButtonDown() ? findHit (e.position) : Hit::none;
        rectOnDown = rect;
        downPos = e.position;
        updateCursor (dragHit);
    }

    void CameraView::mouseDrag (const juce::MouseEvent& e)
    {
        if (dragHit == Hit::none)
            return;

        const auto area = imageArea();
        if (area.isEmpty())
            return;

        const float dx = (e.position.x - downPos.x) / area.getWidth();
        const float dy = (e.position.y - downPos.y) / area.getHeight();
        // Minimum size, never larger than the rect already is (a restored rect may be smaller).
        const float minW = juce::jmin (minSizePx / area.getWidth(), rectOnDown.getWidth());
        const float minH = juce::jmin (minSizePx / area.getHeight(), rectOnDown.getHeight());

        float x0 = rectOnDown.getX(), y0 = rectOnDown.getY();
        float x1 = rectOnDown.getRight(), y1 = rectOnDown.getBottom();

        if (dragHit == Hit::move)
        {
            const float w = x1 - x0, h = y1 - y0;
            x0 = juce::jlimit (0.0f, 1.0f - w, x0 + dx);
            y0 = juce::jlimit (0.0f, 1.0f - h, y0 + dy);
            x1 = x0 + w;
            y1 = y0 + h;
        }
        else
        {
            const bool left = dragHit == Hit::topLeft || dragHit == Hit::bottomLeft;
            const bool top  = dragHit == Hit::topLeft || dragHit == Hit::topRight;
            if (left) x0 = juce::jlimit (0.0f, x1 - minW, x0 + dx);
            else      x1 = juce::jlimit (x0 + minW, 1.0f, x1 + dx);
            if (top)  y0 = juce::jlimit (0.0f, y1 - minH, y0 + dy);
            else      y1 = juce::jlimit (y0 + minH, 1.0f, y1 + dy);
        }

        rect = juce::Rectangle<float>::leftTopRightBottom (x0, y0, x1, y1);
        engine.setRect (rect);
        repaint();
    }

    void CameraView::mouseUp (const juce::MouseEvent& e)
    {
        dragHit = Hit::none;
        updateCursor (findHit (e.position));
    }
}
