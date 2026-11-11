// DSP unit tests (spec 3.x, 5): scanline luminance, harmonic weighting,
// wavetable rendering/interpolation, mip selection, smoothing, crossfade.
#include "dsp/Synth.h"
#include <cmath>

using namespace axsynth;

namespace
{
    // Right channel sink for voice tests that only look at the left channel.
    float scratchR[BlockControls::maxSamples];

    juce::Image solidImage (int w, int h, juce::Colour c)
    {
        juce::Image img (juce::Image::RGB, w, h, false);
        img.clear (img.getBounds(), c);
        return img;
    }

    // Goertzel magnitude of harmonic k in one table period.
    double harmonicMagnitude (const float* data, int k, int size = tableSize)
    {
        double re = 0.0, im = 0.0;
        for (int n = 0; n < size; ++n)
        {
            const double a = juce::MathConstants<double>::twoPi * k * n / size;
            re += data[n] * std::cos (a);
            im += data[n] * std::sin (a);
        }
        return std::sqrt (re * re + im * im) * 2.0 / size;
    }
}

class ScanlineTests : public juce::UnitTest
{
public:
    ScanlineTests() : juce::UnitTest ("Scanline luminance", "dsp") {}

    void runTest() override
    {
        beginTest ("Rec. 601 luma");
        expectWithinAbsoluteError (builder::luma601 (juce::Colours::white.getPixelARGB()), 1.0f, 1.0e-4f);
        expectWithinAbsoluteError (builder::luma601 (juce::Colours::black.getPixelARGB()), 0.0f, 1.0e-4f);
        expectWithinAbsoluteError (builder::luma601 (juce::Colour (255, 0, 0).getPixelARGB()), 0.299f, 1.0e-3f);
        expectWithinAbsoluteError (builder::luma601 (juce::Colour (0, 255, 0).getPixelARGB()), 0.587f, 1.0e-3f);

        beginTest ("Scanline positions: x = x0 + (i + 0.5) * w / N");
        expectWithinAbsoluteError (builder::scanlineX (0, 10.0f, 320.0f), 15.0f, 1.0e-4f);
        expectWithinAbsoluteError (builder::scanlineX (31, 10.0f, 320.0f), 325.0f, 1.0e-4f);

        beginTest ("Uniform image gives uniform luminance");
        {
            HarmonicMatrix lum {};
            builder::extractLuminance (solidImage (320, 180, juce::Colour::greyLevel (0.5f)), { 0.2f, 0.2f, 0.6f, 0.6f }, lum);
            for (auto& f : lum)
                for (auto v : f)
                    expectWithinAbsoluteError (v, 128.0f / 255.0f, 0.01f);
        }

        beginTest ("Section 0 is the top, left/right halves map to scanlines");
        {
            juce::Image img (juce::Image::RGB, 320, 180, true);
            {
                juce::Graphics g (img);
                g.setColour (juce::Colours::white);
                g.fillRect (0, 0, 160, 90);   // top-left quadrant white
            }
            HarmonicMatrix lum {};
            builder::extractLuminance (img, { 0.0f, 0.0f, 1.0f, 1.0f }, lum);
            expectGreaterThan (lum[0][0], 0.95f);            // top of left scanline
            expectLessThan (lum[0][numHarmonics - 1], 0.05f); // bottom of left scanline
            expectLessThan (lum[31][0], 0.05f);              // top of right scanline
            expectLessThan (lum[31][numHarmonics - 1], 0.05f);
        }

        beginTest ("Rect restricts sampling");
        {
            juce::Image img (juce::Image::RGB, 320, 180, true);
            {
                juce::Graphics g (img);
                g.setColour (juce::Colours::white);
                g.fillRect (160, 0, 160, 180);   // right half white
            }
            HarmonicMatrix lum {};
            builder::extractLuminance (img, { 0.6f, 0.1f, 0.3f, 0.8f }, lum);
            for (auto& f : lum)
                for (auto v : f)
                    expectGreaterThan (v, 0.95f);
        }

        beginTest ("Bands are log spaced: top seventh = fundamental, top half = the first harmonics");
        {
            expectWithinAbsoluteError (builder::bandEdge (0), 0.0f, 1.0e-6f);
            expectWithinAbsoluteError (builder::bandEdge (1), std::log (2.0f) / std::log ((float) numHarmonics + 1.0f), 1.0e-6f);
            expectWithinAbsoluteError (builder::bandEdge (numHarmonics), 1.0f, 1.0e-6f);
            expectWithinAbsoluteError (builder::bandEdge (8), std::log (9.0f) / std::log ((float) numHarmonics + 1.0f), 1.0e-6f);

            // White stripe over the top 15 % of the image: only the fundamental band is lit.
            juce::Image img (juce::Image::RGB, 320, 400, true);
            {
                juce::Graphics g (img);
                g.setColour (juce::Colours::white);
                g.fillRect (0, 0, 320, 60);
            }
            HarmonicMatrix lum {};
            builder::extractLuminance (img, { 0.0f, 0.0f, 1.0f, 1.0f }, lum);
            expectGreaterThan (lum[10][0], 0.85f);
            expectLessThan (lum[10][2], 0.05f);
            expectLessThan (lum[10][40], 0.05f);
        }

        beginTest ("Contrast stretch: 0 = raw, 1 = darkest to 0 and brightest to 1, limited gain");
        {
            HarmonicMatrix lum {}, out {};
            for (auto& f : lum) for (size_t k = 0; k < f.size(); ++k) f[k] = 0.4f + 0.2f * (float) (k % 2);
            builder::stretchContrast (lum, out, 0.0f);
            expectWithinAbsoluteError (out[3][1], 0.6f, 1.0e-6f);
            builder::stretchContrast (lum, out, 1.0f);
            expectWithinAbsoluteError (out[3][0], 0.0f, 1.0e-6f);
            expectWithinAbsoluteError (out[3][1], 1.0f, 1.0e-6f);

            // Nearly flat (range 0.02 < 0.15): gain is capped, no blow-up to 0/1.
            for (auto& f : lum) for (size_t k = 0; k < f.size(); ++k) f[k] = 0.5f + 0.02f * (float) (k % 2);
            builder::stretchContrast (lum, out, 1.0f);
            expectWithinAbsoluteError (out[3][1], 0.02f / 0.15f, 1.0e-4f);
        }

        beginTest ("Horizontal scan: lines are rows, bands run left to right");
        {
            juce::Image img (juce::Image::RGB, 400, 320, true);
            {
                juce::Graphics g (img);
                g.setColour (juce::Colours::white);
                g.fillRect (0, 0, 60, 320);              // left 15 % white
            }
            HarmonicMatrix h {}, v {};
            builder::extractLuminance (img, { 0.0f, 0.0f, 1.0f, 1.0f }, h, true);
            builder::extractLuminance (img, { 0.0f, 0.0f, 1.0f, 1.0f }, v, false);
            for (int row : { 0, 15, 31 })
            {
                expectGreaterThan (h[(size_t) row][0], 0.85f);  // every row: fundamental band lit
                expectLessThan (h[(size_t) row][40], 0.05f);
            }
            expectGreaterThan (v[1][30], 0.95f);   // vertical: left columns fully white
            expectLessThan (v[20][0], 0.05f);      // right columns black
        }

        beginTest ("Invalid image gives zeros");
        {
            HarmonicMatrix lum;
            for (auto& f : lum) f.fill (1.0f);
            builder::extractLuminance ({}, { 0.0f, 0.0f, 1.0f, 1.0f }, lum);
            expectEquals (lum[5][5], 0.0f);
        }
    }
};

class HarmonicTests : public juce::UnitTest
{
public:
    HarmonicTests() : juce::UnitTest ("Harmonic weighting and rendering", "dsp") {}

    void runTest() override
    {
        beginTest ("Weighting amp_k = lum * (1/k)^tilt");
        {
            HarmonicMatrix lum, amps;
            for (auto& f : lum) f.fill (1.0f);
            builder::weightHarmonics (lum, amps, 0.7f);
            expectWithinAbsoluteError (amps[0][0], 1.0f, 1.0e-5f);
            expectWithinAbsoluteError (amps[0][1], std::pow (0.5f, 0.7f), 1.0e-5f);
            expectWithinAbsoluteError (amps[3][63], std::pow (1.0f / 64.0f, 0.7f), 1.0e-6f);
        }

        beginTest ("Black image keeps a minimum fundamental");
        {
            HarmonicMatrix lum {}, amps;
            builder::weightHarmonics (lum, amps);
            expectWithinAbsoluteError (amps[7][0], builder::minFundamental, 1.0e-6f);
            expectEquals (amps[7][1], 0.0f);
        }

        builder::Renderer renderer;
        auto table = std::make_unique<Wavetable>();

        beginTest ("Sine table: normalised, no DC, single harmonic");
        {
            HarmonicMatrix amps;
            builder::sineHarmonics (amps);
            renderer.render (amps, *table);
            const float* f = table->frame (0, 0);
            float peak = 0.0f; double dc = 0.0;
            for (int i = 0; i < tableSize; ++i) { peak = juce::jmax (peak, std::abs (f[i])); dc += f[i]; }
            expectWithinAbsoluteError (peak, 1.0f, 1.0e-3f);
            expectWithinAbsoluteError (dc / tableSize, 0.0, 1.0e-4);
            expectWithinAbsoluteError (harmonicMagnitude (f, 1), 1.0, 0.01);
            expectLessThan (harmonicMagnitude (f, 2), 1.0e-3);
            expectWithinAbsoluteError (f[tableSize], f[0], 1.0e-6f); // guard sample
        }

        beginTest ("Harmonic amplitudes are reproduced; mips are band-limited");
        {
            HarmonicMatrix amps {};
            for (auto& fr : amps) { fr[0] = 1.0f; fr[2] = 0.5f; fr[40] = 0.25f; }
            renderer.render (amps, *table);
            const float* f0 = table->frame (0, 3);
            const double h1 = harmonicMagnitude (f0, 1);
            expectWithinAbsoluteError (harmonicMagnitude (f0, 3) / h1, 0.5, 0.01);
            expectWithinAbsoluteError (harmonicMagnitude (f0, 41) / h1, 0.25, 0.01);

            const float* f1 = table->frame (6, 3);   // mip 6: up to 32 harmonics
            expectEquals (harmonicsInMip (6), 32);
            expectLessThan (harmonicMagnitude (f1, 41, mipSize (6)), 1.0e-3);
            expectGreaterThan (harmonicMagnitude (f1, 3, mipSize (6)), 0.1);

            const float* f6 = table->frame (numMips - 1, 3);   // last mip: fundamental only
            expectLessThan (harmonicMagnitude (f6, 3, mipSize (numMips - 1)), 1.0e-3);
        }

        beginTest ("Renderer cost (background thread budget)");
        {
            HarmonicMatrix amps {};
            for (auto& fr : amps) for (size_t k = 0; k < fr.size(); ++k) fr[k] = 1.0f / (float) (k + 1);
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            for (int i = 0; i < 5; ++i) renderer.render (amps, *table);
            const double ms = (juce::Time::getMillisecondCounterHiRes() - t0) / 5.0;
            std::printf ("  renderer: %.2f ms per wavetable (%.1f %% of one core at 25 updates/s)\n", ms, ms * 25.0 / 10.0);
            expectLessThan (ms, 30.0);
        }

        beginTest ("Lower mips are not clipped when removing harmonics raises the peak");
        {
            // h1 + 0.3 h3 has a lower peak than h1 alone: the fundamental-only mip overshoots.
            HarmonicMatrix amps {};
            for (auto& fr : amps) { fr[0] = 1.0f; fr[2] = 0.3f; }
            renderer.render (amps, *table);
            float peak = 0.0f;
            for (int m = 0; m < numMips; ++m)
                for (int i = 0; i <= mipSize (m); ++i)
                    peak = juce::jmax (peak, std::abs (table->frame (m, 0)[i]));
            expectWithinAbsoluteError (peak, 1.0f, 1.0e-3f);
            const float* f6 = table->frame (numMips - 1, 0);
            expectLessThan (harmonicMagnitude (f6, 3, mipSize (numMips - 1)), 1.0e-4);
            expectLessThan (harmonicMagnitude (f6, 5, mipSize (numMips - 1)), 1.0e-4);
        }

        beginTest ("Mip selection keeps harmonics below Nyquist");
        {
            for (double fHz : { 27.5, 110.0, 440.0, 1000.0, 3520.0, 8000.0 })
            {
                const int m = builder::mipForFrequency (fHz, 44100.0);
                expect (harmonicsInMip (m) * fHz < 22050.0 || m == numMips - 1);
                if (m > 0)
                    expect (harmonicsInMip (m - 1) * fHz >= 22050.0, "mip not minimal");
            }
            // Highest note plus full bend at common rates.
            const double top = 440.0 * std::pow (2.0, (127 + 2 - 69) / 12.0);
            for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
                for (double fHz : { 30.0, 261.6, 1000.0, top })
                {
                    const int m = builder::mipForFrequency (fHz, sr);
                    expect (harmonicsInMip (m) * fHz < 0.5 * sr, "harmonics above Nyquist");
                }
            expectEquals (builder::mipForFrequency (50.0, 48000.0), 0);   // 256 x 50 Hz = 12.8 kHz
            expectEquals (builder::mipForFrequency (30000.0, 48000.0), numMips - 1);
        }
    }
};

class EngineTests : public juce::UnitTest
{
public:
    EngineTests() : juce::UnitTest ("Wavetable engine", "dsp") {}

    void runTest() override
    {
        CameraConnector camera;   // never connected

        beginTest ("Pool hand-off: publish, take, release");
        {
            WavetablePool pool;
            const int a = pool.acquireForWrite();
            expect (a >= 0);
            pool.publish (a);
            const int b = pool.acquireForWrite();
            pool.publish (b);                 // replaces a, a becomes free again
            expectEquals (pool.takePending(), b);
            expectEquals (pool.takePending(), -1);
            int acquired = 0;
            for (int i = 0; i < 3; ++i) acquired += pool.acquireForWrite() >= 0 ? 1 : 0;
            expectEquals (acquired, 3);       // b is in use, the other three are free
            expectEquals (pool.acquireForWrite(), -1);
            pool.release (b);
            expect (pool.acquireForWrite() == b);
        }

        beginTest ("Initial wavetable is a pure sine");
        {
            WavetableEngine engine (camera);
            WavetableReader reader (engine.getPool());
            reader.prepare (48000.0);
            reader.update();
            expect (reader.getCurrent() != nullptr);
            expectLessThan (harmonicMagnitude (reader.getCurrent()->frame (0, 17), 2), 1.0e-3);
        }

        beginTest ("Smoothing: 150 ms one-pole, converges towards target");
        {
            WavetableEngine engine (camera);
            WavetableReader reader (engine.getPool());
            reader.prepare (48000.0);
            reader.update();

            engine.injectImageForTesting (solidImage (320, 180, juce::Colours::white));
            engine.stepForTesting (0.040);
            reader.update();
            WavetableEngine::DisplayFrames d1;
            engine.getDisplayFrames (d1);

            // After one 40 ms step exactly 1 - exp(-40/150) of the way (h1 stays at 1).
            const float* f = reader.getCurrent()->frame (0, 10);
            const double ratio1 = harmonicMagnitude (f, 2) / harmonicMagnitude (f, 1);
            const double fullRatio = std::pow (0.5, (double) builder::defaultTilt);
            expectWithinAbsoluteError (ratio1 / fullRatio, 1.0 - std::exp (-0.040 / 0.150), 0.01);

            for (int i = 0; i < 100; ++i)
                engine.stepForTesting (0.040);

            // Drain: the reader takes the newest after finishing its crossfade.
            for (int i = 0; i < 10; ++i) { reader.advance (48000); reader.update(); }
            reader.advance (48000);
            const float* g = reader.getCurrent()->frame (0, 10);
            expectWithinAbsoluteError (harmonicMagnitude (g, 2) / harmonicMagnitude (g, 1), fullRatio, 0.01);
        }

        beginTest ("Motion: a still image has no motion, a change has; freeze ignores new images");
        {
            WavetableEngine engine (camera);
            engine.setMotion (1.0f);
            const auto a = solidImage (320, 180, juce::Colour::greyLevel (0.3f));
            juce::Image b (juce::Image::RGB, 320, 180, true);
            {
                juce::Graphics g (b);
                g.fillAll (juce::Colour::greyLevel (0.3f));
                g.setColour (juce::Colours::white);
                g.fillRect (100, 40, 80, 100);           // "a hand"
            }

            engine.injectImageForTesting (a); engine.stepForTesting (0.04);
            engine.injectImageForTesting (a); engine.stepForTesting (0.04);
            expectLessThan (engine.getMotionLevel(), 0.01f);

            engine.injectImageForTesting (b); engine.stepForTesting (0.04);
            expectGreaterThan (engine.getMotionLevel(), 0.05f);

            for (int i = 0; i < 50; ++i) engine.stepForTesting (0.04);  // 2 s still: trail decays
            expectLessThan (engine.getMotionLevel(), 0.01f);

            engine.setFreeze (true);
            engine.injectImageForTesting (a); engine.stepForTesting (0.04);
            expect (engine.getDisplayImage().getPixelAt (120, 60).getBrightness() > 0.9f, "frozen image must stay");
            engine.setFreeze (false);
            engine.injectImageForTesting (a); engine.stepForTesting (0.04);
            expect (engine.getDisplayImage().getPixelAt (120, 60).getBrightness() < 0.5f, "unfrozen takes new images");
        }

        beginTest ("Crossfade: 20 ms between old and new table");
        {
            WavetableEngine engine (camera);
            WavetableReader reader (engine.getPool());
            reader.prepare (48000.0);
            reader.update();
            const auto* first = reader.getCurrent();

            engine.injectImageForTesting (solidImage (320, 180, juce::Colours::grey));
            engine.stepForTesting (0.040);
            reader.update();
            expect (reader.getPrevious() == first);
            expect (reader.getCurrent() != first);
            expectWithinAbsoluteError (reader.getFade(), 0.0f, 1.0e-6f);
            expectWithinAbsoluteError (reader.getFadeStep() * 960.0f, 1.0f, 1.0e-3f); // 20 ms @ 48 kHz

            reader.advance (480);
            expect (reader.getPrevious() != nullptr);
            reader.advance (480);
            expect (reader.getPrevious() == nullptr);
        }

        beginTest ("Rect change re-evaluates the last image");
        {
            WavetableEngine engine (camera);
            juce::Image img (juce::Image::RGB, 320, 180, true);
            { juce::Graphics g (img); g.setColour (juce::Colours::white); g.fillRect (0, 0, 160, 180); }
            engine.injectImageForTesting (img);
            for (int i = 0; i < 60; ++i) engine.stepForTesting (0.040);
            WavetableEngine::DisplayFrames a; engine.getDisplayFrames (a);

            engine.setRect ({ 0.6f, 0.0f, 0.4f, 1.0f });   // black area only -> sine
            for (int i = 0; i < 60; ++i) engine.stepForTesting (0.040);
            WavetableEngine::DisplayFrames b; engine.getDisplayFrames (b);

            double diff = 0.0;
            for (int i = 0; i < WavetableEngine::displayPoints; ++i)
                diff += std::abs (a[5][(size_t) i] - b[5][(size_t) i]);
            expectGreaterThan (diff, 1.0);
            expectWithinAbsoluteError (engine.getRect().getX(), 0.6f, 1.0e-6f);
        }
    }
};

class VoiceTests : public juce::UnitTest
{
public:
    VoiceTests() : juce::UnitTest ("Voices and interpolation", "dsp") {}

    void runTest() override
    {
        CameraConnector camera;
        WavetableEngine engine (camera);

        // Frame f = harmonic (f + 1) only, to check frame interpolation.
        auto table = std::make_unique<Wavetable>();
        {
            HarmonicMatrix amps {};
            for (int f = 0; f < numFrames; ++f) amps[(size_t) f][(size_t) juce::jmin (f, 3)] = 1.0f;
            builder::Renderer r;
            r.render (amps, *table);
        }

        BlockControls ctl {};
        for (int i = 0; i < BlockControls::maxSamples; ++i)
        {
            ctl.filterG[i] = (float) std::tan (juce::MathConstants<double>::pi * 20000.0 / 96000.0);
            ctl.filterK[i] = 1.0f / 0.707f;
        }

        beginTest ("Position interpolates linearly between neighbouring frames");
        {
            // Position halfway between frame 0 and 1 -> 50/50 mix of harmonic 1 and 2.
            const float pos = 0.5f / 31.0f;
            for (auto& p : ctl.position) p = pos;

            Voice v;
            const double sr = 96000.0;
            v.prepare (sr);
            v.noteOn (45, 1.0f, 1, Envelope { 0.001f, 0.3f, 1.0f, 1.0f });   // 110 Hz -> 872.7 samples / period

            std::vector<float> out (96000, 0.0f);
            for (int i = 0; i < (int) out.size(); i += 64)
                v.render (out.data() + i, scratchR, juce::jmin (64, (int) out.size() - i), ctl, table.get(), nullptr, 1.0f, 0.0f, 0.0f);

            auto mag = [&] (double hz)
            {
                double re = 0, im = 0;
                for (int n = 48000; n < 96000; ++n)
                {
                    const double a = juce::MathConstants<double>::twoPi * hz * n / sr;
                    re += out[(size_t) n] * std::cos (a); im += out[(size_t) n] * std::sin (a);
                }
                return std::sqrt (re * re + im * im);
            };
            const double r = mag (220.0) / mag (110.0);
            expectWithinAbsoluteError (r, 1.0, 0.05);
            expectLessThan (mag (330.0) / mag (110.0), 0.01);
        }

        beginTest ("ADSR: attack, decay to sustain, release");
        {
            // Unfiltered, flat table: the output envelope is the ADSR (times velocity gain 1).
            const double sr = 48000.0;
            BlockControls flat {};
            for (int i = 0; i < BlockControls::maxSamples; ++i)
            {
                flat.position[i] = 0.0f;
                flat.filterG[i] = (float) std::tan (juce::MathConstants<double>::pi * 20000.0 / 96000.0);
                flat.filterK[i] = 1.0f / 0.707f;
            }

            Voice v;
            v.prepare (sr);
            const Envelope e { 0.010f, 0.100f, 0.25f, 0.200f };
            v.noteOn (69, 1.0f, 1, e);

            auto peakOver = [&] (int samples)
            {
                float pk = 0.0f;
                std::vector<float> buf ((size_t) samples, 0.0f);
                for (int i = 0; i < samples; i += 64)
                    v.render (buf.data() + i, scratchR, juce::jmin (64, samples - i), flat, table.get(), nullptr, 1.0f, 0.0f, 0.0f);
                for (auto x : buf) pk = juce::jmax (pk, std::abs (x));
                return pk;
            };

            const float attackPeak = peakOver (960);              // 20 ms: attack done, decay started
            expectGreaterThan (attackPeak, 0.65f); // centred oscillator: 1/sqrt(2) per channel
            peakOver (48000 - 960);                                // ~1 s: decay settled
            const float sustained = peakOver (4800);
            expectWithinAbsoluteError (sustained / attackPeak, 0.25f, 0.03f);

            v.noteOff();
            peakOver ((int) (0.2 * sr));                           // release time = -60 dB
            const float released = peakOver (480);
            expectLessThan (released, sustained * 0.0015f);

            // Sustain 0: the voice ends by itself after the decay.
            Voice w;
            w.prepare (sr);
            w.noteOn (69, 1.0f, 1, Envelope { 0.001f, 0.05f, 0.0f, 1.0f });
            std::vector<float> buf (48000, 0.0f);
            for (int i = 0; i < 48000; i += 64)
                w.render (buf.data() + i, scratchR, 64, flat, table.get(), nullptr, 1.0f, 0.0f, 0.0f);
            expect (! w.isActive(), "sustain 0 voice should end after the decay");
        }

        beginTest ("Unison: detuned copies spread across stereo; sub adds the octave below");
        {
            const double sr = 48000.0;
            BlockControls c {};
            for (int i = 0; i < BlockControls::maxSamples; ++i)
            {
                c.position[i] = 0.0f;   // frame 0 = pure sine
                c.filterG[i] = (float) std::tan (juce::MathConstants<double>::pi * 20000.0 / 96000.0);
                c.filterK[i] = 1.0f / 0.707f;
            }

            auto run = [&] (int unison, float detune, float sub, std::vector<float>& l, std::vector<float>& r)
            {
                c.unison = unison;
                c.detuneCents = detune;
                for (auto& x : c.subLevel) x = sub;
                Voice v;
                v.prepare (sr);
                v.noteOn (69, 1.0f, 1, Envelope { 0.001f, 0.3f, 1.0f, 1.0f });
                l.assign (48000, 0.0f);
                r.assign (48000, 0.0f);
                for (int i = 0; i < 48000; i += 64)
                    v.render (l.data() + i, r.data() + i, 64, c, table.get(), nullptr, 1.0f, 0.0f, 0.0f);
            };
            auto toneMag = [&] (const std::vector<float>& x, double hz)
            {
                double re = 0, im = 0;
                for (int n = 24000; n < 48000; ++n)
                {
                    const double a = juce::MathConstants<double>::twoPi * hz * n / sr;
                    re += x[(size_t) n] * std::cos (a); im += x[(size_t) n] * std::sin (a);
                }
                return std::sqrt (re * re + im * im) / 12000.0;
            };

            std::vector<float> l, r;
            run (1, 0.0f, 0.0f, l, r);
            double diff = 0.0;
            for (size_t i = 0; i < l.size(); ++i) diff += std::abs (l[i] - r[i]);
            expectLessThan (diff, 1.0e-3, "single oscillator is centred");
            const double single = toneMag (l, 440.0);

            run (3, 20.0f, 0.0f, l, r);
            diff = 0.0;
            for (size_t i = 0; i < l.size(); ++i) diff += std::abs (l[i] - r[i]);
            expectGreaterThan (diff, 100.0, "unison spreads across stereo");
            const double up = 440.0 * std::pow (2.0, 20.0 / 1200.0), down = 440.0 * std::pow (2.0, -20.0 / 1200.0);
            expectGreaterThan (toneMag (r, up), toneMag (l, up) * 2.0);     // upper copy panned right
            expectGreaterThan (toneMag (l, down), toneMag (r, down) * 2.0); // lower copy panned left

            run (1, 0.0f, 1.0f, l, r);
            expectWithinAbsoluteError (toneMag (l, 220.0) / single, 1.0, 0.05); // sub at full level
        }

        beginTest ("Lines: several frames at once, spread across stereo");
        {
            const double sr = 48000.0;
            BlockControls c {};
            for (int i = 0; i < BlockControls::maxSamples; ++i)
            {
                c.position[i] = 0.5f / 31.0f * 2.0f;  // between frames 0 and 2 when spread
                c.filterG[i] = (float) std::tan (juce::MathConstants<double>::pi * 20000.0 / 96000.0);
                c.filterK[i] = 1.0f / 0.707f;
            }
            c.lines = 2;
            c.lineSpread = 2.0f / 31.0f;               // lines at frame 0 and frame 2

            Voice v;
            v.prepare (sr);
            v.noteOn (45, 1.0f, 1, Envelope { 0.001f, 0.3f, 1.0f, 1.0f });  // 110 Hz
            std::vector<float> l (48000, 0.0f), r (48000, 0.0f);
            for (int i = 0; i < 48000; i += 64)
                v.render (l.data() + i, r.data() + i, 64, c, table.get(), nullptr, 1.0f, 0.0f, 0.0f);

            auto mag = [&] (const std::vector<float>& x, double hz)
            {
                double re = 0, im = 0;
                for (int n = 24000; n < 48000; ++n)
                {
                    const double a = juce::MathConstants<double>::twoPi * hz * n / sr;
                    re += x[(size_t) n] * std::cos (a); im += x[(size_t) n] * std::sin (a);
                }
                return std::sqrt (re * re + im * im);
            };
            // Frame 0 = harmonic 1 (panned left), frame 2 = harmonic 3 (panned right).
            expectGreaterThan (mag (l, 110.0), mag (r, 110.0) * 2.0);
            expectGreaterThan (mag (r, 330.0), mag (l, 330.0) * 2.0);
            expectLessThan (mag (l, 220.0), mag (l, 110.0) * 0.02);
        }

        beginTest ("Filter envelope opens the cutoff while the envelope is high");
        {
            const double sr = 48000.0;
            BlockControls c {};
            for (int i = 0; i < BlockControls::maxSamples; ++i)
            {
                c.position[i] = 3.0f / 31.0f;  // frame 3 = harmonic 4 (440 Hz at 110 Hz)
                c.cutoffHz[i] = 120.0f;
                c.filterG[i] = (float) std::tan (juce::MathConstants<double>::pi * 120.0 / sr);
                c.filterK[i] = 1.0f / 0.707f;
            }

            auto level = [&] (float envAmount)
            {
                c.filterEnvAmount = envAmount;
                Voice v;
                v.prepare (sr);
                v.noteOn (45, 1.0f, 1, Envelope { 0.001f, 5.0f, 1.0f, 1.0f });
                std::vector<float> l (9600, 0.0f), r (9600, 0.0f);
                for (int i = 0; i < 9600; i += 64)
                    v.render (l.data() + i, r.data() + i, 64, c, table.get(), nullptr, 1.0f, 0.0f, 0.0f);
                float pk = 0.0f;
                for (int i = 4800; i < 9600; ++i) pk = juce::jmax (pk, std::abs (l[(size_t) i]));
                return pk;
            };
            expectGreaterThan (level (1.0f), level (0.0f) * 4.0f); // 120 Hz -> ~3.8 kHz lets 440 Hz through
        }

        beginTest ("Filter types: LP passes lows, HP passes highs, BP peaks at the cutoff");
        {
            const double sr = 48000.0;
            const float g = (float) std::tan (juce::MathConstants<double>::pi * 1000.0 / sr);
            const float k = 1.0f / 0.707f;
            auto gain = [&] (FilterType type, double hz)
            {
                Svf f;
                float pk = 0.0f;
                for (int n = 0; n < 48000; ++n)
                {
                    const float y = f.process ((float) std::sin (juce::MathConstants<double>::twoPi * hz * n / sr), g, k, type);
                    if (n > 24000) pk = juce::jmax (pk, std::abs (y));
                }
                return pk;
            };
            expectGreaterThan (gain (FilterType::lowPass, 100.0), 0.95f);
            expectLessThan (gain (FilterType::lowPass, 10000.0), 0.02f);
            expectLessThan (gain (FilterType::highPass, 100.0), 0.02f);
            expectGreaterThan (gain (FilterType::highPass, 10000.0), 0.95f);
            expectWithinAbsoluteError (gain (FilterType::bandPass, 1000.0), 1.0f, 0.03f);
            expectLessThan (gain (FilterType::bandPass, 100.0), 0.15f);
            expectLessThan (gain (FilterType::bandPass, 10000.0), 0.15f);

            // 24 dB: two stages, a second neutral one. Two octaves above the cutoff the
            // 12 dB filter is near -24 dB, the 24 dB one near -48 dB.
            auto gain24 = [&] (double hz)
            {
                Svf a, b;
                float pk = 0.0f;
                for (int n = 0; n < 48000; ++n)
                {
                    const float x = (float) std::sin (juce::MathConstants<double>::twoPi * hz * n / sr);
                    const float y = b.process (a.process (x, g, k, FilterType::lowPass), g, 1.41421356f, FilterType::lowPass);
                    if (n > 24000) pk = juce::jmax (pk, std::abs (y));
                }
                return pk;
            };
            const float db12 = juce::Decibels::gainToDecibels (gain (FilterType::lowPass, 4000.0));
            const float db24 = juce::Decibels::gainToDecibels (gain24 (4000.0));
            expectLessThan (db24, db12 - 18.0f);
            expectGreaterThan (gain24 (100.0), 0.95f);
        }

        beginTest ("Anti-aliasing: driven tanh folds back far less than plain tanh");
        {
            // 4.3 kHz sine at 48 kHz, driven hard: odd harmonics above Nyquist fold back
            // onto frequencies that are not multiples of 4.3 kHz. Measure that energy.
            const double sr = 48000.0, f0 = 4300.0;
            constexpr int len = 1 << 15;
            auto aliasDb = [&] (auto&& shaper)
            {
                std::vector<float> y ((size_t) len);
                for (int n = 0; n < len; ++n)
                    y[(size_t) n] = shaper ((float) std::sin (juce::MathConstants<double>::twoPi * f0 * n / sr));
                juce::dsp::FFT fft (15);
                std::vector<float> buf ((size_t) len * 2, 0.0f);
                for (int n = 0; n < len; ++n)   // Hann window
                    buf[(size_t) n] = y[(size_t) n] * (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * n / len));
                fft.performFrequencyOnlyForwardTransform (buf.data());
                double harm = 0.0, alias = 0.0;
                for (int b = 1; b < len / 2; ++b)
                {
                    const double hz = b * sr / len;
                    const double h = hz / f0;
                    const double e = (double) buf[(size_t) b] * buf[(size_t) b];
                    if (std::abs (h - std::round (h)) * f0 < 60.0) harm += e; else alias += e;
                }
                return 10.0 * std::log10 (alias / harm);
            };

            const float g = 10.0f;
            const double naive = aliasDb ([g] (float x) { return std::tanh (g * x); });
            sat::TanhAdaa adaa;
            const double antiAliased = aliasDb ([g, &adaa] (float x) { return adaa.process (g * x); });
            std::printf ("  drive aliasing: plain tanh %.1f dB, ADAA %.1f dB (relative to the harmonics)\n", naive, antiAliased);
            expectLessThan (antiAliased, naive - 6.0);

        }

        beginTest ("Glide, mono legato, velocity to filter, drive");
        {
            const double sr = 48000.0;
            BlockControls c {};
            for (int i = 0; i < BlockControls::maxSamples; ++i)
            {
                c.position[i] = 0.0f;
                c.cutoffHz[i] = 20000.0f;
                c.filterG[i] = (float) std::tan (juce::MathConstants<double>::pi * 20000.0 / 96000.0);
                c.filterK[i] = 1.0f / 0.707f;
            }
            float l[64], r[64];

            // Glide: starts at the previous note, reaches the target after the glide time.
            c.glideSec = 0.2f;
            Voice v;
            v.prepare (sr);
            v.noteOn (69, 1.0f, 1, Envelope {}, 57.0);
            v.render (l, r, 64, c, table.get(), nullptr, 1.0f, 0.0f, 0.0f);
            expectLessThan (v.getCurrentPitch(), 58.0);
            for (int i = 0; i < 300; ++i) v.render (l, r, 64, c, table.get(), nullptr, 1.0f, 0.0f, 0.0f); // 0.4 s
            expectWithinAbsoluteError (v.getCurrentPitch(), 69.0, 0.05);
            c.glideSec = 0.0f;

            // Mono: one voice, last note priority, falls back to the held note.
            SynthEngine s;
            s.prepare (sr);
            s.setMono (true);
            s.noteOn (60, 1.0f, Envelope {});
            s.noteOn (64, 1.0f, Envelope {});
            expectEquals (s.getNumActiveVoices(), 1);
            expectEquals (s.getNewestVoice()->getNote(), 64);
            s.noteOff (64);
            expectEquals (s.getNewestVoice()->getNote(), 60);
            expectEquals (s.getNewestVoice()->getStageIndex(), 1);   // still in attack: legato, held
            s.noteOff (60);
            expectEquals (s.getNewestVoice()->getStageIndex(), 3);   // release

            // Velocity to filter: with a low cutoff, a hard note is brighter (louder 4th harmonic frame).
            auto peakWithVelocity = [&] (float vel)
            {
                BlockControls d = c;
                for (int i = 0; i < 64; ++i) { d.position[i] = 3.0f / 31.0f; d.cutoffHz[i] = 200.0f; }
                d.velocityToFilter = 1.0f;
                Voice w;
                w.prepare (sr);
                w.noteOn (45, vel, 1, Envelope { 0.001f, 1.0f, 1.0f, 1.0f });
                float pk = 0.0f;
                for (int b = 0; b < 300; ++b)
                {
                    float ll[64] = {}, rr[64] = {};
                    w.render (ll, rr, 64, d, table.get(), nullptr, 1.0f, 0.0f, 0.0f);
                    if (b > 150) for (auto x : ll) pk = juce::jmax (pk, std::abs (x));
                }
                return pk / (0.2f + 0.8f * vel);   // remove the velocity amplitude scaling
            };
            expectGreaterThan (peakWithVelocity (1.0f), peakWithVelocity (0.1f) * 3.0f);

            // Drive: a pure sine gets a 3rd harmonic.
            auto thirdHarmonic = [&] (float drv)
            {
                BlockControls d = c;
                d.drive = drv;
                Voice w;
                w.prepare (sr);
                w.noteOn (45, 1.0f, 1, Envelope { 0.001f, 1.0f, 1.0f, 1.0f });
                std::vector<float> ll (48000, 0.0f), rr (48000, 0.0f);
                for (int i = 0; i < 48000; i += 64)
                    w.render (ll.data() + i, rr.data() + i, 64, d, table.get(), nullptr, 1.0f, 0.0f, 0.0f);
                double re3 = 0, im3 = 0, re1 = 0, im1 = 0;
                for (int n = 24000; n < 48000; ++n)
                {
                    const double a1 = juce::MathConstants<double>::twoPi * 110.0 * n / sr, a3 = 3.0 * a1;
                    re1 += ll[(size_t) n] * std::cos (a1); im1 += ll[(size_t) n] * std::sin (a1);
                    re3 += ll[(size_t) n] * std::cos (a3); im3 += ll[(size_t) n] * std::sin (a3);
                }
                return std::sqrt (re3 * re3 + im3 * im3) / std::sqrt (re1 * re1 + im1 * im1);
            };
            expectLessThan (thirdHarmonic (0.0f), 0.01);
            expectGreaterThan (thirdHarmonic (1.0f), 0.05);
        }

        beginTest ("16 voices, oldest is stolen");
        {
            SynthEngine s;
            s.prepare (48000.0);
            for (int n = 0; n < SynthEngine::numVoices; ++n) s.noteOn (60 + n, 1.0f, Envelope {});
            expectEquals (s.getNumActiveVoices(), SynthEngine::numVoices);
            s.noteOn (100, 1.0f, Envelope {});
            expectEquals (s.getNumActiveVoices(), SynthEngine::numVoices);
            // note 60 (oldest) was stolen: releasing it must not affect anything
            s.noteOff (60);
            WavetableReader reader (engine.getPool());
            reader.prepare (48000.0);
            reader.update();
            for (auto& p : ctl.position) p = 0.5f;
            float buf[64];
            for (int i = 0; i < 2000; ++i) s.render (buf, scratchR, 64, ctl, reader, 0.0f);
            expectEquals (s.getNumActiveVoices(), SynthEngine::numVoices);
            s.allNotesOff();
            for (int i = 0; i < 2000; ++i) s.render (buf, scratchR, 64, ctl, reader, 0.0f);
            expectEquals (s.getNumActiveVoices(), 0);
        }
    }
};

static ScanlineTests scanlineTests;
static HarmonicTests harmonicTests;
static EngineTests engineTests;
static VoiceTests voiceTests;
