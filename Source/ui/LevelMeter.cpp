#include "LevelMeter.h"

namespace axsynth
{
    void LevelMeter::push (float peak, double dt)
    {
        const float db = juce::Decibels::gainToDecibels (peak, minDb);
        const float oldLevel = levelDb, oldHold = holdDb;

        // Instant attack, ~24 dB/s fall.
        levelDb = juce::jmax (db, levelDb - (float) (24.0 * dt));

        holdAge += dt;
        if (db >= holdDb || holdAge >= holdSeconds)
        {
            holdDb = db;
            holdAge = 0.0;
        }

        if (std::abs (oldLevel - levelDb) > 0.05f || oldHold != holdDb)
            repaint();
    }

    void LevelMeter::paint (juce::Graphics& g)
    {
        const auto b = getLocalBounds().toFloat();
        const float radius = b.getWidth() * 0.5f;

        g.setColour (theme::track);
        g.fillRoundedRectangle (b, radius);

        const float h = b.getHeight() * toProportion (levelDb);
        if (h > 0.5f)
        {
            g.setColour (theme::accent);
            g.fillRoundedRectangle (b.withTop (b.getBottom() - juce::jmax (h, b.getWidth())), radius);
        }

        if (holdDb > minDb + 0.5f)
        {
            const float y = b.getBottom() - b.getHeight() * toProportion (holdDb);
            g.setColour (theme::accent);
            g.fillRect (b.getX(), juce::jlimit (b.getY(), b.getBottom() - 1.5f, y), b.getWidth(), 1.5f);
        }
    }
}
