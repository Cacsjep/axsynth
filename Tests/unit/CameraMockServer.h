#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <memory>
#include <vector>

namespace axsynth::test
{
    // Tiny HTTP/1.1 server on 127.0.0.1 (random port) that mimics an Axis camera:
    // Basic or Digest (MD5, qop=auth) auth, param.cgi, MJPEG stream. TLS client hellos
    // are rejected immediately (connection closed) so HTTPS attempts fail fast.
    class MockCameraServer
    {
    public:
        enum class Auth { none, basic, digest };
        enum class StreamMode { frames, noImage, hang };

        struct Config
        {
            Auth auth = Auth::basic;
            juce::String user = "user";
            juce::String password = "secret";
            bool axisParams = true;          // false: param.cgi -> 404 (non-Axis source)
            juce::String streamPath = "/axis-cgi/mjpg/video.cgi";
            StreamMode mode = StreamMode::frames;
            bool contentLength = true;       // per-part Content-Length header
            std::vector<juce::MemoryBlock> jpegs;
            int frameIntervalMs = 40;
            bool hangAll = false;            // read requests, never answer
        };

        explicit MockCameraServer (Config);
        ~MockCameraServer();

        int getPort() const noexcept { return port; }
        juce::StringArray getRequestLog() const;   // "GET <path>" per request
        int getTlsRejects() const noexcept { return tlsRejects.load(); }
        int getStreamsServed() const noexcept { return streamsServed.load(); }
        void dropConnections();                    // ends all running streams

    private:
        class Listener;
        class Connection;
        friend class Connection;

        void handle (juce::StreamingSocket&, juce::Thread&);
        bool readRequest (juce::StreamingSocket&, juce::Thread&, juce::String& head);
        bool checkAuth (const juce::StringPairArray& headers, const juce::String& uri) const;

        Config config;
        int port = 0;
        std::unique_ptr<Listener> listener;
        mutable juce::CriticalSection logLock;
        juce::StringArray log;
        std::atomic<int> tlsRejects { 0 }, streamsServed { 0 }, dropGeneration { 0 };
    };
}
