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

    void extractLuminance (const juce::Image& image, juce::Rectangle<float> normRect, HarmonicMatrix& lumOut)
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
        const float secH = h / (float) numHarmonics;
        const int samplesPerSection = juce::jmax (1, (int) std::ceil (secH));

        for (int i = 0; i < numFrames; ++i)
        {
            const float x = scanlineX (i, x0, w);

            for (int s = 0; s < numHarmonics; ++s)
            {
                const float ya = y0 + (float) s * secH;
                float sum = 0.0f;
                for (int k = 0; k < samplesPerSection; ++k)
                    sum += sampleLuma (bd, x, ya + ((float) k + 0.5f) * secH / (float) samplesPerSection);

                lumOut[(size_t) i][(size_t) s] = juce::jlimit (0.0f, 1.0f, sum / (float) samplesPerSection);
            }
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

    static constexpr int fftOrder = 11;
    static_assert ((1 << fftOrder) == tableSize);

    Renderer::Renderer() : fft (fftOrder), work ((size_t) tableSize * 2, 0.0f) {}

    void Renderer::render (const HarmonicMatrix& amps, Wavetable& out)
    {
        for (int f = 0; f < numFrames; ++f)
        {
            // One scale per frame across all mips: removing harmonics can raise the
            // peak, and clipping a mip would put back harmonics above its limit.
            float peak = 0.0f;

            for (int m = 0; m < numMips; ++m)
            {
                std::fill (work.begin(), work.end(), 0.0f);

                // JUCE real-only inverse: interleaved (re, im) for bins 0..N/2.
                // A sine of amplitude a at bin k <=> im = -a * N / 2.
                const int nh = harmonicsInMip (m);
                for (int k = 1; k <= nh; ++k)
                    work[(size_t) (2 * k + 1)] = -amps[(size_t) f][(size_t) (k - 1)] * (float) tableSize * 0.5f;

                fft.performRealOnlyInverseTransform (work.data());

                float* dst = out.frame (m, f);
                double mean = 0.0;
                for (int i = 0; i < tableSize; ++i)
                    mean += work[(size_t) i];
                mean /= tableSize;

                for (int i = 0; i < tableSize; ++i)
                {
                    dst[i] = work[(size_t) i] - (float) mean;
                    peak = juce::jmax (peak, std::abs (dst[i]));
                }
            }

            const float scale = peak > 1.0e-9f ? 1.0f / peak : 0.0f;
            for (int m = 0; m < numMips; ++m)
            {
                float* dst = out.frame (m, f);
                for (int i = 0; i < tableSize; ++i)
                    dst[i] *= scale;
                dst[tableSize] = dst[0];
            }
        }
    }
}
