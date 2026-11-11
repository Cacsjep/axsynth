#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <mutex>
#include <vector>

namespace axsynth::http
{
    // Shared between the network thread (which creates WinHTTP handles) and a
    // stopping thread. cancel() closes every registered handle, which makes any
    // blocking synchronous WinHTTP call on the network thread return at once.
    // A handle is closed exactly once: either by cancel() or by release().
    class Canceller
    {
    public:
        ~Canceller();

        // Registers a freshly created handle. Returns false (and closes it) if
        // cancel() already happened.
        bool add (void* handle);
        // Closes a handle owned by the network thread unless cancel() already did.
        void release (void*& handle);

        void cancel();
        void reset();
        bool isCancelled() const noexcept { return cancelled.load(); }

    private:
        std::mutex mutex;
        std::vector<void*> handles;
        std::atomic<bool> cancelled { false };
    };

    enum class AuthMode { none, basic, digest };

    enum class Outcome
    {
        ok,              // got an HTTP status code
        transportError,  // connect refused / timeout / TLS failure / bad response
        nameNotResolved, // DNS failure
        cancelled
    };

    struct Response
    {
        Outcome outcome = Outcome::transportError;
        int status = 0;
        juce::String contentType;
    };

    // Splits "host", "host:port", "[v6]:port", "http://host:port/..." into host + port.
    void splitHostPort (const juce::String& in, juce::String& host, int& port, bool https);

    // Synchronous WinHTTP client for one host/scheme. Not thread safe except for
    // the cancellation through the Canceller.
    class Client
    {
    public:
        Client (Canceller&, bool https, const juce::String& hostWithPort, int timeoutMs = 5000);
        ~Client();

        // Basic is sent preemptively. Digest: first request without credentials,
        // on a 401 Digest challenge WinHttpSetCredentials + resend.
        Response get (const juce::String& path, AuthMode, const juce::String& user, const juce::String& password);

        // Body of the last response. >0 bytes read, 0 end of body, -1 error / cancelled.
        int read (void* dest, int maxBytes);
        juce::String readAll (int maxBytes = 1 << 20);

    private:
        bool open();
        void closeRequest();

        Canceller& canceller;
        const bool https;
        juce::String host;
        int port = 0;
        int timeoutMs;
        void* session = nullptr;
        void* connection = nullptr;
        void* request = nullptr;

        JUCE_DECLARE_NON_COPYABLE (Client)
    };
}
