#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <atomic>
#include <memory>

namespace axsynth
{
    // Connection settings. scheme/auth/resolution are the remembered result of the
    // last successful connect (empty = unknown, run the full cascade).
    struct CameraSettings
    {
        juce::String host;        // "10.0.0.48" or "host:port"
        juce::String user;
        juce::String password;    // plain text in memory; DPAPI-encrypted only in plugin state
        juce::String streamPath { defaultStreamPath() };
        juce::String scheme;      // "https" | "http" | ""
        juce::String auth;        // "digest" | "basic" | ""
        juce::String resolution;  // e.g. "320x180" | ""

        bool hasCredentials() const { return host.isNotEmpty(); }
        static juce::String defaultStreamPath() { return "/axis-cgi/mjpg/video.cgi?resolution=640x360&fps=25"; }
    };

    // Owns the network thread: auth cascade, resolution discovery, MJPEG streaming,
    // JPEG decoding, reconnect with backoff. Decoded frames are handed out through a
    // lock-free triple buffer. Never touched by the audio thread.
    class CameraConnector
    {
    public:
        enum class Status
        {
            idle,          // no settings / disconnected
            connecting,    // user-initiated connect in progress (button shows "Verbinde...")
            streaming,     // receiving images
            reconnecting,  // lost connection or auto-connect, silent exponential backoff 1 s -> 30 s
            failed         // user-initiated connect failed; getErrorMessage() has the one-line text
        };

        CameraConnector();
        ~CameraConnector();

        // Message thread or any non-audio thread.
        void setSettings (const CameraSettings&);
        CameraSettings getSettings() const;     // includes learned scheme/auth/resolution

        // userInitiated = true: one attempt sequence, on failure status=failed with message.
        // userInitiated = false (plugin load with stored settings): silent retry with backoff forever.
        void connect (bool userInitiated);
        void disconnect();

        Status getStatus() const noexcept { return status.load(); }
        juce::String getErrorMessage() const;

        // Consumer side (wavetable worker). Returns true and fills 'out' if a frame
        // newer than the last popped one exists. Non-blocking.
        bool popLatestFrame (juce::Image& out);
        // Blocks up to timeoutMs for a new frame (worker uses this as its pacing).
        bool waitForFrame (int timeoutMs);
        juce::uint64 getFrameCount() const noexcept { return frameCount.load(); }

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
        std::atomic<Status> status { Status::idle };
        std::atomic<juce::uint64> frameCount { 0 };
        friend struct Impl;
    };

    // ---- Pure helpers, unit tested ------------------------------------------

    // Parses "Properties.Image.Resolution=1920x1080,...,320x180,..." and picks the smallest
    // resolution with 16:9 or 4:3 aspect and width >= 320. If sensorAspect ("16:9"/"4:3")
    // is given, that aspect is preferred. Returns "" if nothing suitable.
    juce::String pickResolution (const juce::String& paramCgiResponse, const juce::String& sensorAspect = {});

    // Sets/replaces the resolution query parameter in a stream path. Empty resolution removes it.
    juce::String applyResolution (const juce::String& streamPath, const juce::String& resolution);

    enum class ConnectFailure { none, unreachable, unauthorized, notFound, noImage };

    // One line, plain German, no codes (spec section 4).
    juce::String errorMessageFor (ConnectFailure, const juce::String& host);

    // Windows DPAPI (CurrentUser). Returns base64; empty on failure.
    juce::String dpapiProtect (const juce::String& plain);
    juce::String dpapiUnprotect (const juce::String& base64);
}
