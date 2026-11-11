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
        // Closes one registered handle from any thread (e.g. a watchdog) without
        // cancelling everything. The owner's later release() then finds it gone.
        void closeIfRegistered (void* handle);

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
        bool credentialsSent = false; // false on a 401: the server did not offer the requested scheme
    };

    // Splits "host", "host:port", "[v6]:port", "http://host:port/..." into host + port.
    void splitHostPort (const juce::String& in, juce::String& host, int& port, bool https);

    // What the user typed into the host field, split into the parts the cascade needs.
    struct HostSpec
    {
        juce::String host;       // without scheme, credentials, port or path
        int port = 0;            // explicit port, 0 = scheme default
        bool allowHttps = true;  // "http://" or port 80 -> false
        bool allowHttp = true;   // "https://" or port 443 -> false
    };

    // "https://cam" -> HTTPS only, "http://cam" -> HTTP only, "cam:443" -> HTTPS only,
    // "cam:80" -> HTTP only, "cam:8080" -> both schemes on port 8080, "cam" -> both, default ports.
    HostSpec parseHost (const juce::String& in);

    // Removes "user:password@" from a host entry, keeps scheme, port and the rest.
    juce::String stripUserInfo (const juce::String& hostEntry);

    // Synchronous WinHTTP client for one host/scheme. Not thread safe except for
    // the cancellation through the Canceller. Redirects are never followed.
    class Client
    {
    public:
        Client (Canceller&, bool https, const juce::String& hostWithPort, int timeoutMs = 5000);
        ~Client();

        // Credentials are never sent unsolicited: the request goes out without them,
        // and only if the server answers 401 with a challenge for the requested
        // scheme (Basic or Digest) are they set and the request resent.
        Response get (const juce::String& path, AuthMode, const juce::String& user, const juce::String& password);

        // Body of the last response. >0 bytes read, 0 end of body, -1 error / cancelled.
        int read (void* dest, int maxBytes);
        juce::String readAll (int maxBytes = 1 << 20);

        // Handle of the current request, for Canceller::closeIfRegistered from another thread.
        void* requestHandle() const noexcept { return request; }

        // Overrides the receive timeout of the current request (e.g. for a long-running stream
        // whose deadlines are enforced by the caller).
        void setReceiveTimeout (int ms);

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
