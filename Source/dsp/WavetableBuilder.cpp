#include "WavetableBuilder.h"
#include <cmath>

namespace axsynth::builder
{
    namespace
    {
        float sampleLuma (const juce::Image::BitmapData& bd, float x, float y) noexcept
        {
            const int w = bd.width, h = bd.height;
            x = juce::jlimit (0.0f, (float) (w - 1), x - 0.5f);
            y = juce::jlimit (0.0f, (float) (h - 1), y - 0.5f);
            const int x0 = (int) x, y0 = (int) y;
            const int x1 = juce::jmin (x0 + 1, w - 1), y1 = juce::jmin (y0 + 1, h - 1);
            const float fx = x - (float) x0, fy = y - (float) y0;

            auto l = [&] (int px, int py) { return luma601 (bd.getPixelColour (px, py).getPixelARGB()); };
            const float top = l (x0, y0) + fx * (l (x1, y0) - l (x0, y0));
            const float bot = l (x0, y1) + fx * (l (x1, y1) - l (x0, y1));
            return top + fy * (bot - top);
        }
    }

    void extractLuminance (const juce::Image& image, juce::Rectangle<float> normRect, HarmonicMatrix& lumOut, bool horizontal)
    {
        if (! image.isValid())
        {
            for (auto& f : lumOut) f.fill (0.0f);
            return;
        }

        const juce::Image::BitmapData bd (image, juce::Image::BitmapData::readOnly);
        const float iw = (float) bd.width, ih = (float) bd.height;

        normRect = normRect.getIntersection ({ 0.0f, 0.0f, 1.0f, 1.0f });
        const float x0 = normRect.getX() * iw, w = normRect.getWidth() * iw;
        const float y0 = normRect.getY() * ih, h = normRect.getHeight() * ih;

        // Line position runs across the rect, bands along the line (log spaced).
        const float lineStart = horizontal ? y0 : x0, lineSpan = horizontal ? h : w;
        const float bandStart = horizontal ? x0 : y0, bandSpan = horizontal ? w : h;

        // Each line averages across its whole strip (not one pixel column): camera noise
        // would otherwise make the harmonics flutter.
        const float strip = lineSpan / (float) numFrames;
        const int across = juce::jlimit (1, 6, (int) std::ceil (strip * 0.5f));

        for (int i = 0; i < numFrames; ++i)
        {
            const float lineCentre = scanlineX (i, lineStart, lineSpan);

            for (int s = 0; s < numHarmonics; ++s)
            {
                const float a = bandStart + bandEdge (s) * bandSpan;
                const float len = (bandEdge (s + 1) - bandEdge (s)) * bandSpan;
                const int samples = juce::jlimit (1, 64, (int) std::ceil (len));
                float sum = 0.0f;
                for (int k = 0; k < samples; ++k)
                {
                    const float along = a + ((float) k + 0.5f) * len / (float) samples;
                    for (int c = 0; c < across; ++c)
                    {
                        const float line = lineCentre + strip * (((float) c + 0.5f) / (float) across - 0.5f);
                        sum += horizontal ? sampleLuma (bd, along, line) : sampleLuma (bd, line, along);
                    }
                }

                lumOut[(size_t) i][(size_t) s] = juce::jlimit (0.0f, 1.0f, sum / (float) (samples * across));
            }
        }
    }

    void stretchContrast (const HarmonicMatrix& lum, HarmonicMatrix& out, float amount)
    {
        amount = juce::jlimit (0.0f, 1.0f, amount);
        float lo = 1.0f, hi = 0.0f;
        for (auto& f : lum)
            for (auto v : f) { lo = juce::jmin (lo, v); hi = juce::jmax (hi, v); }

        constexpr float minRange = 0.15f; // at most ~6.7x gain
        const float range = juce::jmax (hi - lo, minRange);

        for (size_t f = 0; f < lum.size(); ++f)
            for (size_t k = 0; k < lum[f].size(); ++k)
            {
                const float raw = lum[f][k];
                const float stretched = juce::jlimit (0.0f, 1.0f, (raw - lo) / range);
                out[f][k] = raw + amount * (stretched - raw);
            }
    }

    void weightHarmonics (const HarmonicMatrix& lum, HarmonicMatrix& ampsOut, float tilt)
    {
        for (size_t f = 0; f < (size_t) numFrames; ++f)
        {
            for (size_t k = 0; k < (size_t) numHarmonics; ++k)
                ampsOut[f][k] = lum[f][k] * std::pow (1.0f / (float) (k + 1), tilt);

            ampsOut[f][0] = juce::jmax (ampsOut[f][0], minFundamental);
        }
    }

    void sineHarmonics (HarmonicMatrix& ampsOut)
    {
        for (auto& f : ampsOut)
        {
            f.fill (0.0f);
            f[0] = 1.0f;
        }
    }

    int mipForFrequency (double freqHz, double sampleRate) noexcept
    {
        const double nyquist = 0.5 * sampleRate;
        for (int m = 0; m < numMips; ++m)
            if ((double) harmonicsInMip (m) * freqHz < nyquist)
                return m;
        return numMips - 1;
    }

    // ---------------------------------------------------------------------

    namespace
    {
        int orderOf (int size) noexcept { int o = 0; while ((1 << o) < size) ++o; return o; }
    }

    Renderer::Renderer() : work ((size_t) tableSize * 2, 0.0f)
    {
        for (int o = 0; o <= orderOf (tableSize); ++o)
            ffts.push_back (std::make_unique<juce::dsp::FFT> (o));
    }

    void Renderer::render (const HarmonicMatrix& amps, Wavetable& out)
    {
        for (int f = 0; f < numFrames; ++f)
            renderFrame (amps, out, f);
    }

    void Renderer::renderFrame (const HarmonicMatrix& amps, Wavetable& out, int f)
    {
        {
            // One scale per frame across all mips: removing harmonics can raise the
            // peak, and clipping a mip would put back harmonics above its limit.
            float peak = 0.0f;

            for (int m = 0; m < numMips; ++m)
            {
                const int size = mipSize (m);
                std::fill (work.begin(), work.begin() + 2 * size, 0.0f);

                // JUCE real-only inverse: interleaved (re, im) for bins 0..N/2.
                // A sine of amplitude a at bin k <=> im = -a * N / 2.
                const int nh = harmonicsInMip (m);
                for (int k = 1; k <= nh; ++k)
                    work[(size_t) (2 * k + 1)] = -amps[(size_t) f][(size_t) (k - 1)] * (float) size * 0.5f;

                ffts[(size_t) orderOf (size)]->performRealOnlyInverseTransform (work.data());

                float* dst = out.frame (m, f);
                double mean = 0.0;
                for (int i = 0; i < size; ++i)
                    mean += work[(size_t) i];
                mean /= size;

                for (int i = 0; i < size; ++i)
                {
                    dst[i] = work[(size_t) i] - (float) mean;
                    peak = juce::jmax (peak, std::abs (dst[i]));
                }
            }

            const float scale = peak > 1.0e-9f ? 1.0f / peak : 0.0f;
            for (int m = 0; m < numMips; ++m)
            {
                const int size = mipSize (m);
                float* dst = out.frame (m, f);
                for (int i = 0; i < size; ++i)
                    dst[i] *= scale;
                dst[size] = dst[0];
            }
        }
    }
}
