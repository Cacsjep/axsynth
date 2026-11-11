#include "Visuals.h"
#include <cmath>

namespace axsynth
{
    namespace
    {
        const juce::Colour panelColour { 0xff1a1d21 };
        const juce::Colour plotColour  { 0xff131518 };

        float toDbProportion (float amp, float floorDb) noexcept
        {
            const float db = juce::Decibels::gainToDecibels (amp, floorDb);
            return juce::jlimit (0.0f, 1.0f, (db - floorDb) / -floorDb);
        }
    }

    void SectionPanel::paint (juce::Graphics& g)
    {
        const auto b = getLocalBounds().toFloat();
        g.setColour (panelColour);
        g.fillRoundedRectangle (b, 6.0f);

        g.setFont (theme::font (10.0f, true).withExtraKerningFactor (0.12f));
        g.setColour (theme::textDim);
        g.drawText (name.toUpperCase(), b.reduced (10.0f, 0.0f).withHeight (22.0f), juce::Justification::centredLeft, false);
    }

    void Display::paintBackground (juce::Graphics& g) const
    {
        g.setColour (plotColour);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
    }

    // ---------------------------------------------------------------------------------- harmonics

    void HarmonicsView::setHarmonics (const std::array<float, numHarmonics>& amps)
    {
        bool changed = false;
        for (size_t k = 0; k < values.size(); ++k)
            if (std::abs (amps[k] - values[k]) > 1.0e-4f) { changed = true; break; }
        if (! changed)
            return;
        values = amps;
        repaint();
    }

    void HarmonicsView::paint (juce::Graphics& g)
    {
        paintBackground (g);
        const auto b = plotArea().reduced (4.0f, 3.0f);
        float peak = 1.0e-6f;
        for (auto v : values) peak = juce::jmax (peak, v);

        // Log frequency axis (like an EQ): harmonic k sits at log(k); the first harmonics
        // get wide bars, the dense top end thin ones.
        const float logMax = std::log ((float) numHarmonics + 1.0f);
        auto xOf = [&] (float k) { return b.getX() + b.getWidth() * std::log (k) / logMax; };
        for (int k = 0; k < numHarmonics; ++k)
        {
            const float h = b.getHeight() * toDbProportion (values[(size_t) k] / peak, -48.0f);
            if (h < 0.5f)
                continue;
            const float x0 = xOf ((float) k + 1.0f), x1 = xOf ((float) k + 2.0f);
            g.setColour (k == 0 ? theme::accent : theme::accent.withAlpha (0.35f + 0.5f * (h / b.getHeight())));
            g.fillRect (juce::Rectangle<float> (x0, b.getBottom() - h, juce::jmax (1.0f, x1 - x0 - (x1 - x0 > 3.0f ? 1.0f : 0.0f)), h));
        }
    }

    // ---------------------------------------------------------------------------------- unison

    void UnisonView::setVoice (int unison, float detuneCents, float sub)
    {
        if (unison == count && std::abs (detuneCents - detune) < 0.05f && std::abs (sub - subLevel) < 0.002f)
            return;
        count = unison; detune = detuneCents; subLevel = sub;
        repaint();
    }

    void UnisonView::paint (juce::Graphics& g)
    {
        paintBackground (g);
        const auto b = plotArea().reduced (10.0f, 6.0f);
        const float midY = b.getY() + b.getHeight() * 0.42f;

        g.setColour (theme::track);
        g.drawHorizontalLine ((int) midY, b.getX(), b.getRight());

        // Detune drawn as pitch offset up/down (50 ct = full half-height).
        const float maxDy = b.getHeight() * 0.36f;
        for (int j = 0; j < count; ++j)
        {
            const float spread = count == 1 ? 0.0f : 2.0f * (float) j / (float) (count - 1) - 1.0f;
            const float x = b.getCentreX() + spread * b.getWidth() * 0.45f;
            const float y = midY - spread * maxDy * juce::jlimit (0.0f, 1.0f, detune / 50.0f);
            g.setColour (theme::accent.withAlpha (0.35f));
            g.drawLine (x, midY, x, y, 1.0f);
            g.setColour (theme::accent);
            g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ x, y }));
        }

        // Sub: bar under the centre, width by level.
        if (subLevel > 0.001f)
        {
            const float w = b.getWidth() * 0.5f * subLevel;
            g.setColour (theme::text.withAlpha (0.6f));
            g.fillRoundedRectangle (juce::Rectangle<float> (w, 4.0f).withCentre ({ b.getCentreX(), b.getBottom() - 3.0f }), 2.0f);
        }
    }

    // ---------------------------------------------------------------------------------- lfo

    void LfoView::setLfo (float p, float amt, bool synced, const juce::String& syncText)
    {
        if (std::abs (p - phase) < 1.0e-3f && std::abs (amt - amount) < 1.0e-3f && synced == sync && syncText == syncLabel)
            return;
        phase = p; amount = amt; sync = synced; syncLabel = syncText;
        repaint();
    }

    void LfoView::paint (juce::Graphics& g)
    {
        paintBackground (g);
        const auto b = plotArea().reduced (8.0f, 6.0f);
        const float amp = b.getHeight() * 0.5f * (0.12f + 0.88f * amount);
        const float midY = b.getCentreY();

        juce::Path wave;
        constexpr int n = 96;
        for (int i = 0; i <= n; ++i)
        {
            const float t = (float) i / (float) n;
            const float y = midY - amp * std::sin (juce::MathConstants<float>::twoPi * t);
            const float x = b.getX() + t * b.getWidth();
            if (i == 0) wave.startNewSubPath (x, y); else wave.lineTo (x, y);
        }
        g.setColour (theme::text.withAlpha (0.45f));
        g.strokePath (wave, juce::PathStrokeType (1.2f));

        const float px = b.getX() + phase * b.getWidth();
        const float py = midY - amp * std::sin (juce::MathConstants<float>::twoPi * phase);
        g.setColour (theme::accent);
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ px, py }));

        if (sync)
        {
            g.setFont (theme::font (10.0f, true));
            g.drawText ("SYNC " + syncLabel, b.withHeight (12.0f), juce::Justification::topRight, false);
        }
    }

    // ---------------------------------------------------------------------------------- filter

    void FilterView::setFilter (int type, float cutoffHz, float liveCutoffHz, float resonance, bool steep)
    {
        if (type == filterType && steep == steepSlope && std::abs (cutoffHz - cutoff) < cutoff * 1.0e-3f
            && std::abs (liveCutoffHz - liveCutoff) < liveCutoff * 1.0e-3f && std::abs (resonance - res) < 1.0e-3f)
            return;
        filterType = type; cutoff = cutoffHz; liveCutoff = liveCutoffHz; res = resonance; steepSlope = steep;
        repaint();
    }

    juce::Path FilterView::responsePath (float fc) const
    {
        // Analog prototype of the SVF: same k = 1/Q as the DSP (Q = 0.707 * 30^res).
        const auto b = plotArea().reduced (4.0f, 4.0f);
        const float k = 1.0f / (0.707f * std::pow (30.0f, res));
        constexpr float minDb = -36.0f, maxDb = 24.0f;

        juce::Path p;
        constexpr int n = 120;
        for (int i = 0; i <= n; ++i)
        {
            const float t = (float) i / (float) n;
            const float f = 20.0f * std::pow (1000.0f, t);           // 20 Hz .. 20 kHz
            const float w = f / juce::jmax (1.0f, fc);
            auto stage = [&] (float kk)
            {
                const float den = std::sqrt ((1.0f - w * w) * (1.0f - w * w) + (kk * w) * (kk * w));
                if (filterType == 1) return (w * w) / den;              // high-pass
                if (filterType == 2) return (kk * w) / den;             // band-pass, unity peak
                return 1.0f / den;                                      // low-pass
            };
            float mag = stage (k);
            if (steepSlope) mag *= stage (1.41421356f);                // 24 dB: second neutral stage
            const float db = juce::jlimit (minDb, maxDb, juce::Decibels::gainToDecibels (mag, minDb));
            const float x = b.getX() + t * b.getWidth();
            const float y = b.getY() + b.getHeight() * (maxDb - db) / (maxDb - minDb);
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        return p;
    }

    void FilterView::paint (juce::Graphics& g)
    {
        paintBackground (g);
        const auto b = plotArea().reduced (4.0f, 4.0f);

        // 0 dB line.
        g.setColour (theme::track);
        g.drawHorizontalLine ((int) (b.getY() + b.getHeight() * 24.0f / 60.0f), b.getX(), b.getRight());

        auto base = responsePath (cutoff);
        juce::Path fill (base);
        fill.lineTo (b.getRight(), b.getBottom());
        fill.lineTo (b.getX(), b.getBottom());
        fill.closeSubPath();
        g.setColour (theme::accent.withAlpha (0.10f));
        g.fillPath (fill);
        g.setColour (theme::text.withAlpha (0.55f));
        g.strokePath (base, juce::PathStrokeType (1.2f));

        if (std::abs (liveCutoff - cutoff) > cutoff * 0.01f)
        {
            g.setColour (theme::accent);
            g.strokePath (responsePath (liveCutoff), juce::PathStrokeType (1.5f));
        }
    }

    // ---------------------------------------------------------------------------------- envelope

    void EnvelopeView::setEnvelope (float a, float d, float s, float r, float lvl, int stg, float relFrom)
    {
        if (a == attack && d == decay && s == sustain && r == release
            && std::abs (lvl - level) < 1.0e-3f && stg == stage && relFrom == releaseStart)
            return;
        attack = a; decay = d; sustain = s; release = r; level = lvl; stage = stg; releaseStart = relFrom;
        repaint();
    }

    void EnvelopeView::paint (juce::Graphics& g)
    {
        paintBackground (g);
        const auto b = plotArea().reduced (8.0f, 6.0f);

        // Segment widths follow the times on a log-ish scale; sustain has a fixed width.
        auto w = [] (float sec) { return 0.15f + std::log10 (1.0f + sec * 100.0f); };
        const float wa = w (attack), wd = w (decay), ws = 1.0f, wr = w (release);
        const float scale = b.getWidth() / (wa + wd + ws + wr);
        const float xA = b.getX(), xD = xA + wa * scale, xS = xD + wd * scale, xR = xS + ws * scale, xE = xR + wr * scale;
        auto yOf = [&] (float v) { return b.getBottom() - v * b.getHeight(); };

        juce::Path p;
        p.startNewSubPath (xA, yOf (0.0f));
        p.lineTo (xD, yOf (1.0f));
        constexpr int n = 40;
        for (int i = 1; i <= n; ++i)   // exponential decay: covers 60 dB of the distance
        {
            const float t = (float) i / (float) n;
            p.lineTo (xD + t * (xS - xD), yOf (sustain + (1.0f - sustain) * std::pow (0.001f, t)));
        }
        p.lineTo (xR, yOf (sustain));
        for (int i = 1; i <= n; ++i)
        {
            const float t = (float) i / (float) n;
            p.lineTo (xR + t * (xE - xR), yOf (sustain * std::pow (0.001f, t)));
        }

        juce::Path fill (p);
        fill.lineTo (xE, b.getBottom());
        fill.closeSubPath();
        g.setColour (theme::accent.withAlpha (0.10f));
        g.fillPath (fill);
        g.setColour (theme::text.withAlpha (0.55f));
        g.strokePath (p, juce::PathStrokeType (1.2f));

        if (stage == 0)
            return;

        // Live position of the newest voice.
        float x = xA;
        if (stage == 1)
            x = xA + level * (xD - xA);
        else if (stage == 2)
        {
            const float t = sustain < 0.999f && level > sustain + 1.0e-4f
                          ? std::log ((level - sustain) / (1.0f - sustain)) / std::log (0.001f) : 1.0f;
            x = t >= 0.999f ? (xS + xR) * 0.5f : xD + juce::jlimit (0.0f, 1.0f, t) * (xS - xD);
        }
        else if (stage == 3)
        {
            const float t = releaseStart > 1.0e-5f && level > 0.0f
                          ? std::log (level / releaseStart) / std::log (0.001f) : 1.0f;
            x = xR + juce::jlimit (0.0f, 1.0f, t) * (xE - xR);
        }
        g.setColour (theme::accent);
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ x, yOf (level) }));
    }

    // ---------------------------------------------------------------------------------- effects

    void FxView::setFx (float c, float dm, float bars, float f, float rm, float rs)
    {
        if (c == chorusMix && dm == delay && bars == delayLen && f == fb && rm == rev && rs == size)
            return;
        chorusMix = c; delay = dm; delayLen = bars; fb = f; rev = rm; size = rs;
        repaint();
    }

    void FxView::paint (juce::Graphics& g)
    {
        paintBackground (g);
        const auto b = plotArea().reduced (8.0f, 6.0f);
        // Time axis: one bar (4 quarters) across the display.
        auto xAt = [&] (float bars) { return b.getX() + juce::jmin (1.0f, bars) * b.getWidth(); };

        // Reverb tail: decaying fill, longer with size.
        if (rev > 0.001f)
        {
            juce::Path tail;
            tail.startNewSubPath (b.getX(), b.getBottom());
            const float len = 0.25f + 0.75f * size;
            constexpr int n = 60;
            for (int i = 0; i <= n; ++i)
            {
                const float t = (float) i / (float) n;
                tail.lineTo (b.getX() + t * b.getWidth(), b.getBottom() - b.getHeight() * rev * 0.8f * std::pow (0.001f, t / len));
            }
            tail.lineTo (b.getRight(), b.getBottom());
            tail.closeSubPath();
            g.setColour (theme::text.withAlpha (0.12f));
            g.fillPath (tail);
        }

        // Dry hit, doubled when the chorus is on.
        g.setColour (theme::text.withAlpha (0.7f));
        g.fillRect (juce::Rectangle<float> (b.getX(), b.getY(), 2.0f, b.getHeight()));
        if (chorusMix > 0.001f)
        {
            g.setColour (theme::text.withAlpha (0.25f + 0.5f * chorusMix));
            g.fillRect (juce::Rectangle<float> (b.getX() + 4.0f, b.getY() + b.getHeight() * 0.2f, 2.0f, b.getHeight() * 0.8f));
        }

        // Echoes.
        if (delay > 0.001f)
        {
            float h = delay;
            for (int k = 1; k <= 16 && h > 0.01f; ++k)
            {
                const float x = xAt ((float) k * delayLen);
                if (x >= b.getRight() - 1.0f)
                    break;
                g.setColour (theme::accent.withAlpha (0.4f + 0.6f * h));
                g.fillRect (juce::Rectangle<float> (x, b.getBottom() - b.getHeight() * h, 2.0f, b.getHeight() * h));
                h *= fb;
            }
        }
    }

    // ---------------------------------------------------------------------------------- buttons

    ChoiceButtons::ChoiceButtons (juce::RangedAudioParameter& p, juce::StringArray labels)
        : attachment (p, [this] (float v) { selected = juce::roundToInt (v); repaint(); }),
          names (std::move (labels)),
          isToggle (names.size() == 1)
    {
        setMouseClickGrabsKeyboardFocus (false);
        attachment.sendInitialUpdate();
    }

    void ChoiceButtons::paint (juce::Graphics& g)
    {
        const auto b = getLocalBounds().toFloat();
        const int n = names.size();
        const float w = b.getWidth() / (float) n;
        g.setFont (theme::font (10.0f, true).withExtraKerningFactor (0.08f));
        for (int i = 0; i < n; ++i)
        {
            const auto cell = juce::Rectangle<float> (b.getX() + (float) i * w, b.getY(), w, b.getHeight()).reduced (1.5f, 1.0f);
            const bool on = isToggle ? selected != 0 : i == selected;
            if (on)
            {
                g.setColour (theme::accent);
                g.fillRoundedRectangle (cell, 3.0f);
                g.setColour (theme::background);
            }
            else
            {
                g.setColour (theme::track);
                g.drawRoundedRectangle (cell.reduced (0.5f), 3.0f, 1.0f);
                g.setColour (theme::textDim);
            }
            g.drawText (names[i], cell, juce::Justification::centred, false);
        }
    }

    void ChoiceButtons::mouseDown (const juce::MouseEvent& e)
    {
        if (isToggle)
        {
            attachment.setValueAsCompleteGesture (selected != 0 ? 0.0f : 1.0f);
            return;
        }
        const int i = juce::jlimit (0, names.size() - 1, (int) (e.position.x / ((float) getWidth() / (float) names.size())));
        attachment.setValueAsCompleteGesture ((float) i);
    }
}
