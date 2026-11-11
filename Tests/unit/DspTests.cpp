// DSP unit tests (spec 3.x, 5): scanline luminance, harmonic weighting,
// wavetable rendering/interpolation, mip selection, smoothing, crossfade.
#include "dsp/Synth.h"
#include <cmath>

using namespace axsynth;

namespace
{
    juce::Image solidImage (int w, int h, juce::Colour c)
    {
        juce::Image img (juce::Image::RGB, w, h, false);
        img.clear (img.getBounds(), c);
        return img;
    }

    // Goertzel magnitude of harmonic k in one table period.
    double harmonicMagnitude (const float* data, int k)
    {
        double re = 0.0, im = 0.0;
        for (int n = 0; n < tableSize; ++n)
        {
            const double a = juce::MathConstants<double>::twoPi * k * n / tableSize;
            re += data[n] * std::cos (a);
            im += data[n] * std::sin (a);
        }
        return std::sqrt (re * re + im * im) * 2.0 / tableSize;
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
            expectLessThan (lum[0][63], 0.05f);              // bottom of left scanline
            expectLessThan (lum[31][0], 0.05f);              // top of right scanline
            expectLessThan (lum[31][63], 0.05f);
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

            const float* f1 = table->frame (1, 3);   // mip 1: up to 32 harmonics
            expectLessThan (harmonicMagnitude (f1, 41), 1.0e-3);
            expectGreaterThan (harmonicMagnitude (f1, 3), 0.1);

            const float* f6 = table->frame (6, 3);   // mip 6: fundamental only
            expectLessThan (harmonicMagnitude (f6, 3), 1.0e-3);
        }

        beginTest ("Lower mips are not clipped when removing harmonics raises the peak");
        {
            // h1 + 0.3 h3 has a lower peak than h1 alone: the fundamental-only mip overshoots.
            HarmonicMatrix amps {};
            for (auto& fr : amps) { fr[0] = 1.0f; fr[2] = 0.3f; }
            renderer.render (amps, *table);
            float peak = 0.0f;
            for (int m = 0; m < numMips; ++m)
                for (int i = 0; i <= tableSize; ++i)
                    peak = juce::jmax (peak, std::abs (table->frame (m, 0)[i]));
            expectWithinAbsoluteError (peak, 1.0f, 1.0e-3f);
            const float* f6 = table->frame (6, 0);
            expectLessThan (harmonicMagnitude (f6, 3), 1.0e-4);
            expectLessThan (harmonicMagnitude (f6, 5), 1.0e-4);
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
            expectEquals (builder::mipForFrequency (100.0, 48000.0), 0);
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
            const double fullRatio = std::pow (0.5, 0.7);
            expectWithinAbsoluteError (ratio1 / fullRatio, 1.0 - std::exp (-0.040 / 0.150), 0.01);

            for (int i = 0; i < 100; ++i)
                engine.stepForTesting (0.040);

            // Drain: the reader takes the newest after finishing its crossfade.
            for (int i = 0; i < 10; ++i) { reader.advance (48000); reader.update(); }
            reader.advance (48000);
            const float* g = reader.getCurrent()->frame (0, 10);
            expectWithinAbsoluteError (harmonicMagnitude (g, 2) / harmonicMagnitude (g, 1), fullRatio, 0.01);
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
                v.render (out.data() + i, juce::jmin (64, (int) out.size() - i), ctl, table.get(), nullptr, 1.0f, 0.0f, 0.0f);

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
                    v.render (buf.data() + i, juce::jmin (64, samples - i), flat, table.get(), nullptr, 1.0f, 0.0f, 0.0f);
                for (auto x : buf) pk = juce::jmax (pk, std::abs (x));
                return pk;
            };

            const float attackPeak = peakOver (960);              // 20 ms: attack done, decay started
            expectGreaterThan (attackPeak, 0.8f);
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
                w.render (buf.data() + i, 64, flat, table.get(), nullptr, 1.0f, 0.0f, 0.0f);
            expect (! w.isActive(), "sustain 0 voice should end after the decay");
        }

        beginTest ("8 voices, oldest is stolen");
        {
            SynthEngine s;
            s.prepare (48000.0);
            for (int n = 0; n < 8; ++n) s.noteOn (60 + n, 1.0f, Envelope {});
            expectEquals (s.getNumActiveVoices(), 8);
            s.noteOn (80, 1.0f, Envelope {});
            expectEquals (s.getNumActiveVoices(), 8);
            // note 60 (oldest) was stolen: releasing it must not affect anything
            s.noteOff (60);
            WavetableReader reader (engine.getPool());
            reader.prepare (48000.0);
            reader.update();
            for (auto& p : ctl.position) p = 0.5f;
            float buf[64];
            for (int i = 0; i < 2000; ++i) s.render (buf, 64, ctl, reader, 0.0f);
            expectEquals (s.getNumActiveVoices(), 8);
            s.allNotesOff();
            for (int i = 0; i < 2000; ++i) s.render (buf, 64, ctl, reader, 0.0f);
            expectEquals (s.getNumActiveVoices(), 0);
        }
    }
};

static ScanlineTests scanlineTests;
static HarmonicTests harmonicTests;
static EngineTests engineTests;
static VoiceTests voiceTests;
