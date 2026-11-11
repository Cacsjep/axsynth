#pragma once

#include "Wavetable.h"
#include <juce_dsp/juce_dsp.h>
#include <juce_graphics/juce_graphics.h>

namespace axsynth
{
    // Pure image -> wavetable math (spec 3.1 - 3.3). No threads, no state besides scratch buffers.
    namespace builder
    {
        inline constexpr float defaultTilt = 1.2f; // matches the Tone default
        inline constexpr float minFundamental = 0.05f;

        // x position (in pixels) of scanline i inside a rect starting at x0 with width w.
        inline float scanlineX (int i, float x0, float w) noexcept { return x0 + ((float) i + 0.5f) * w / (float) numFrames; }

        // Rec. 601 luma, 0..1.
        inline float luma601 (juce::PixelARGB p) noexcept
        {
            return (0.299f * p.getRed() + 0.587f * p.getGreen() + 0.114f * p.getBlue()) / 255.0f;
        }

        // Top edge (0..1 of the rect height) of the band that drives harmonic k (0-based).
        // Log spaced like an EQ: the top sixth drives the fundamental, the top half
        // harmonics 1..8, the bottom half 9..64, so every part of the rect is audible.
        inline float bandEdge (int k) noexcept
        {
            return std::log ((float) k + 1.0f) / std::log ((float) numHarmonics + 1.0f);
        }

        // Mean luminance of the 64 bands along each of the 32 scanlines.
        // normRect is in 0..1 image coordinates. Band 0 (fundamental) = top.
        void extractLuminance (const juce::Image& image, juce::Rectangle<float> normRect, HarmonicMatrix& lumOut);

        // Auto levels over the whole rect: amount 0 = raw luminance, 1 = darkest -> 0 and
        // brightest -> 1, so small changes (a hand, a shadow) change the sound a lot.
        // The stretch is limited so a flat, noisy wall is not blown up to random spectra.
        void stretchContrast (const HarmonicMatrix& lum, HarmonicMatrix& out, float amount);

        // amp_k = lum[k-1] * (1/k)^tilt, plus guaranteed minimum fundamental.
        void weightHarmonics (const HarmonicMatrix& lum, HarmonicMatrix& ampsOut, float tilt = defaultTilt);

        // Pure sine in every frame (first start, no camera image yet).
        void sineHarmonics (HarmonicMatrix& ampsOut);

        // Mip level for a note frequency: the lowest mip whose highest harmonic stays below Nyquist.
        int mipForFrequency (double freqHz, double sampleRate) noexcept;

        // Renders harmonic amplitudes into all frames and mip levels via inverse FFT.
        // DC removed, each frame normalised to +-1 (mips share the frame's base scale).
        class Renderer
        {
        public:
            Renderer();
            void render (const HarmonicMatrix& amps, Wavetable& out);

        private:
            juce::dsp::FFT fft;
            std::vector<float> work;
        };
    }
}
