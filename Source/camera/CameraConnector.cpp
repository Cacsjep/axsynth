#include "CameraConnector.h"
#include "MjpegParser.h"
#include "TripleBuffer.h"
#include "WinHttpClient.h"
#include <mutex>

namespace axsynth
{
    namespace
    {
        constexpr int requestTimeoutMs = 5000;   // connect / receive timeout per request
        constexpr int firstFrameTimeoutMs = 5000; // "connected but no image"
        constexpr int frameStallTimeoutMs = 10000;
        constexpr int minBackoffMs = 1000;
        constexpr int maxBackoffMs = 30000;

        const char* const resolutionQuery = "/axis-cgi/param.cgi?action=list&group=Properties.Image.Resolution";
        const char* const sensorQuery = "/axis-cgi/param.cgi?action=list&group=ImageSource.I0.Sensor";

        struct Combo
        {
            bool https;
            bool digest;

            juce::String scheme() const { return https ? "https" : "http"; }
            juce::String auth() const { return digest ? "digest" : "basic"; }
            bool operator== (const Combo& o) const { return https == o.https && digest == o.digest; }
        };

        enum class AttemptResult { streamedThenLost, cancelled, unreachable, nameNotResolved, unauthorized, notFound, noImage };

        int rank (ConnectFailure f)
        {
            switch (f)
            {
                case ConnectFailure::noImage:      return 4;
                case ConnectFailure::notFound:     return 3;
                case ConnectFailure::unauthorized: return 2;
                case ConnectFailure::unreachable:  return 1;
                case ConnectFailure::none:         break;
            }
            return 0;
        }

        bool sameTarget (const CameraSettings& a, const CameraSettings& b)
        {
            return a.host == b.host && a.user == b.user && a.password == b.password && a.streamPath == b.streamPath;
        }

        struct Size { int w = 0, h = 0; };

        Size parseSize (const juce::String& s)
        {
            const auto t = s.trim().toLowerCase();
            const auto ws = t.upToFirstOccurrenceOf ("x", false, false);
            const auto hs = t.fromFirstOccurrenceOf ("x", false, false);

            if (ws.isEmpty() || hs.isEmpty() || ! ws.containsOnly ("0123456789") || ! hs.containsOnly ("0123456789")
                || ws.length() > 5 || hs.length() > 5)
                return {};

            return { ws.getIntValue(), hs.getIntValue() };
        }

        // 169 for 16:9, 43 for 4:3, 0 otherwise
        int aspectCode (int w, int h)
        {
            if (w <= 0 || h <= 0) return 0;
            if (w * 9 == h * 16) return 169;
            if (w * 3 == h * 4) return 43;
            return 0;
        }

        int aspectCode (const juce::String& ratio)
        {
            const auto t = ratio.removeCharacters (" \t\r\n");
            if (t == "16:9") return 169;
            if (t == "4:3") return 43;
            return 0;
        }
    }

    // ==========================================================================

    struct CameraConnector::Impl : private juce::Thread
    {
        explicit Impl (CameraConnector& o) : juce::Thread ("AxSynth Camera"), owner (o) {}
        ~Impl() override { stop(); }

        CameraConnector& owner;

        mutable juce::CriticalSection lock; // settings + error
        CameraSettings settings;
        juce::String error;

        std::mutex controlMutex; // serialises start/stop
        bool userInitiated = false;

        http::Canceller canceller;
        juce::WaitableEvent wakeEvent;
        juce::WaitableEvent frameEvent;
        TripleBuffer<juce::Image> frames;

        // ---- control ------------------------------------------------------

        void start (bool user)
        {
            const std::lock_guard<std::mutex> lg (controlMutex);
            stopLocked();

            const auto s = getSettings();
            setError ({});

            if (! s.hasCredentials())
            {
                if (user)
                {
                    setError (errorMessageFor (ConnectFailure::unreachable, s.host));
                    owner.status = Status::failed;
                }
                else
                {
                    owner.status = Status::idle;
                }
                return;
            }

            canceller.reset();
            wakeEvent.reset();
            userInitiated = user;
            owner.status = user ? Status::connecting : Status::reconnecting;
            startThread();
        }

        void stop()
        {
            const std::lock_guard<std::mutex> lg (controlMutex);
            stopLocked();
        }

        void stopLocked()
        {
            signalThreadShouldExit();
            canceller.cancel();
            wakeEvent.signal();
            stopThread (5000);
        }

        CameraSettings getSettings() const
        {
            const juce::ScopedLock sl (lock);
            return settings;
        }

        void setError (const juce::String& e)
        {
            const juce::ScopedLock sl (lock);
            error = e;
        }

        bool shouldStop() const { return threadShouldExit() || canceller.isCancelled(); }

        // ---- network thread ---------------------------------------------------

        void run() override
        {
            bool user = userInitiated;
            int backoff = minBackoffMs;

            while (! shouldStop())
            {
                const auto snapshot = getSettings();
                bool streamed = false;
                const auto failure = connectAndStream (snapshot, streamed);

                if (shouldStop())
                    return;

                if (streamed)
                {
                    // Lost a running stream: reconnect silently, regardless of how we started.
                    user = false;
                    backoff = minBackoffMs;
                }
                else if (user)
                {
                    setError (errorMessageFor (failure, snapshot.host));
                    owner.status = Status::failed;
                    return;
                }

                owner.status = Status::reconnecting;

                if (wakeEvent.wait (backoff) || shouldStop())
                    return;

                backoff = juce::jmin (backoff * 2, maxBackoffMs);
            }
        }

        // Runs the cascade (remembered combo first). Streams on success until the
        // connection drops (streamed = true). Otherwise returns the best failure.
        ConnectFailure connectAndStream (const CameraSettings& s, bool& streamed)
        {
            const std::vector<Combo> cascade { { true, true }, { true, false }, { false, true }, { false, false } };
            std::vector<Combo> order;

            const bool hasRemembered = (s.scheme == "https" || s.scheme == "http") && (s.auth == "digest" || s.auth == "basic");
            if (hasRemembered)
                order.push_back ({ s.scheme == "https", s.auth == "digest" });

            for (const auto& c : cascade)
                if (std::find (order.begin(), order.end(), c) == order.end())
                    order.push_back (c);

            bool skipHttps = false, skipHttp = false;
            ConnectFailure best = ConnectFailure::unreachable;

            for (size_t i = 0; i < order.size(); ++i)
            {
                if (shouldStop())
                    return ConnectFailure::none;

                const auto& c = order[i];
                if (c.https ? skipHttps : skipHttp)
                    continue;

                const bool useRememberedResolution = hasRemembered && i == 0 && s.resolution.isNotEmpty();
                const auto r = attempt (s, c, useRememberedResolution, streamed);

                ConnectFailure f = ConnectFailure::unreachable;

                switch (r)
                {
                    case AttemptResult::streamedThenLost:
                    case AttemptResult::cancelled:
                        return ConnectFailure::none;

                    case AttemptResult::nameNotResolved:
                        skipHttps = skipHttp = true;
                        break;

                    case AttemptResult::unreachable:
                        // Transport / TLS failure: the other auth variant of this scheme would fail the same way.
                        (c.https ? skipHttps : skipHttp) = true;
                        break;

                    case AttemptResult::unauthorized: f = ConnectFailure::unauthorized; break;
                    case AttemptResult::notFound:     f = ConnectFailure::notFound; break;
                    case AttemptResult::noImage:      f = ConnectFailure::noImage; break;
                }

                if (rank (f) > rank (best))
                    best = f;

                if (f == ConnectFailure::noImage)
                    break; // authenticated and got a stream; another variant won't produce images either
            }

            return best;
        }

        static AttemptResult fromTransport (http::Outcome o)
        {
            switch (o)
            {
                case http::Outcome::cancelled:       return AttemptResult::cancelled;
                case http::Outcome::nameNotResolved: return AttemptResult::nameNotResolved;
                case http::Outcome::ok:
                case http::Outcome::transportError:  break;
            }
            return AttemptResult::unreachable;
        }

        AttemptResult attempt (const CameraSettings& s, const Combo& combo, bool useRememberedResolution, bool& streamed)
        {
            http::Client client (canceller, combo.https, s.host, requestTimeoutMs);
            const auto authMode = combo.digest ? http::AuthMode::digest : http::AuthMode::basic;

            juce::String resolution;

            if (useRememberedResolution)
            {
                resolution = s.resolution;
            }
            else
            {
                const auto r = client.get (resolutionQuery, authMode, s.user, s.password);
                if (r.outcome != http::Outcome::ok)
                    return fromTransport (r.outcome);

                if (r.status == 401 || r.status == 403)
                    return AttemptResult::unauthorized;

                if (r.status == 200)
                {
                    const auto list = client.readAll (64 * 1024);
                    juce::String aspect;

                    const auto sensor = client.get (sensorQuery, authMode, s.user, s.password);
                    if (sensor.outcome == http::Outcome::cancelled)
                        return AttemptResult::cancelled;

                    if (sensor.outcome == http::Outcome::ok && sensor.status == 200)
                    {
                        const auto body = client.readAll (64 * 1024);
                        const auto idx = body.indexOfIgnoreCase ("AspectRatio=");
                        if (idx >= 0)
                            aspect = body.substring (idx + 12).upToFirstOccurrenceOf ("\n", false, false).trim();
                    }

                    resolution = pickResolution (list, aspect);
                }
                // anything else (404, ...): not an Axis camera, open the stream without resolution
            }

            const auto path = applyResolution (s.streamPath.isNotEmpty() ? s.streamPath : CameraSettings::defaultStreamPath(),
                                               resolution);

            const auto r = client.get (path, authMode, s.user, s.password);
            if (r.outcome != http::Outcome::ok)
                return fromTransport (r.outcome);

            if (r.status == 401 || r.status == 403)
                return AttemptResult::unauthorized;

            if (r.status < 200 || r.status >= 300)
                return AttemptResult::notFound;

            return stream (client, s, combo, resolution, r.contentType, streamed);
        }

        AttemptResult stream (http::Client& client, const CameraSettings& s, const Combo& combo,
                              const juce::String& resolution, const juce::String& contentType, bool& streamed)
        {
            const auto start = juce::Time::getMillisecondCounter();
            auto lastFrame = start;
            bool gotFrame = false;
            juce::JPEGImageFormat jpeg;

            MjpegParser parser (MjpegParser::boundaryFromContentType (contentType),
                                [&] (const juce::uint8* data, size_t size)
            {
                juce::MemoryInputStream in (data, size, false);
                auto img = jpeg.decodeImage (in);
                if (! img.isValid())
                    return;

                frames.writeSlot() = std::move (img);
                frames.publish();
                ++owner.frameCount;
                frameEvent.signal();
                lastFrame = juce::Time::getMillisecondCounter();

                if (! gotFrame)
                {
                    gotFrame = true;
                    streamed = true;
                    onStreaming (s, combo, resolution);
                }
            });

            juce::HeapBlock<char> buffer (32768);

            while (! shouldStop())
            {
                const auto n = client.read (buffer.get(), 32768);
                if (n <= 0)
                    break;

                parser.push (buffer.get(), (size_t) n);

                const auto now = juce::Time::getMillisecondCounter();
                if (! gotFrame && now - start > (juce::uint32) firstFrameTimeoutMs)
                    break;
                if (gotFrame && now - lastFrame > (juce::uint32) frameStallTimeoutMs)
                    break;
            }

            if (shouldStop())
                return AttemptResult::cancelled;

            return gotFrame ? AttemptResult::streamedThenLost : AttemptResult::noImage;
        }

        void onStreaming (const CameraSettings& s, const Combo& combo, const juce::String& resolution)
        {
            {
                const juce::ScopedLock sl (lock);
                error = {};

                // Only store what we learned if the user hasn't changed the target meanwhile.
                if (sameTarget (settings, s))
                {
                    settings.scheme = combo.scheme();
                    settings.auth = combo.auth();
                    settings.resolution = resolution;
                }
            }

            owner.status = Status::streaming;
        }

        bool pop (juce::Image& out)
        {
            if (! frames.consume())
                return false;

            out = frames.readSlot();
            return out.isValid();
        }
    };

    // ==========================================================================

    CameraConnector::CameraConnector() : impl (std::make_unique<Impl> (*this)) {}

    CameraConnector::~CameraConnector()
    {
        impl->stop();
        status = Status::idle;
    }

    void CameraConnector::setSettings (const CameraSettings& s)
    {
        const juce::ScopedLock sl (impl->lock);
        impl->settings = s;
    }

    CameraSettings CameraConnector::getSettings() const
    {
        return impl->getSettings();
    }

    void CameraConnector::connect (bool userInitiated)
    {
        impl->start (userInitiated);
    }

    void CameraConnector::disconnect()
    {
        impl->stop();
        status = Status::idle;
    }

    juce::String CameraConnector::getErrorMessage() const
    {
        const juce::ScopedLock sl (impl->lock);
        return impl->error;
    }

    bool CameraConnector::popLatestFrame (juce::Image& out)
    {
        return impl->pop (out);
    }

    bool CameraConnector::waitForFrame (int timeoutMs)
    {
        return impl->frameEvent.wait (timeoutMs);
    }

    // ==========================================================================

    juce::String pickResolution (const juce::String& response, const juce::String& sensorAspect)
    {
        juce::String list;
        const auto idx = response.indexOfIgnoreCase ("Resolution=");
        list = idx >= 0 ? response.substring (idx + 11) : response;
        list = list.upToFirstOccurrenceOf ("\n", false, false);

        juce::StringArray tokens;
        tokens.addTokens (list, ",", "");

        const auto preferred = aspectCode (sensorAspect);

        auto pick = [&] (bool onlyPreferred) -> juce::String
        {
            juce::String best;
            juce::int64 bestArea = 0;

            for (const auto& t : tokens)
            {
                const auto sz = parseSize (t);
                const auto a = aspectCode (sz.w, sz.h);

                if (a == 0 || sz.w < 320 || (onlyPreferred && a != preferred))
                    continue;

                const auto area = (juce::int64) sz.w * sz.h;
                if (best.isEmpty() || area < bestArea)
                {
                    best = juce::String (sz.w) + "x" + juce::String (sz.h);
                    bestArea = area;
                }
            }
            return best;
        };

        if (preferred != 0)
            if (auto r = pick (true); r.isNotEmpty())
                return r;

        return pick (false);
    }

    juce::String applyResolution (const juce::String& streamPath, const juce::String& resolution)
    {
        const auto q = streamPath.indexOfChar ('?');
        const auto base = q < 0 ? streamPath : streamPath.substring (0, q);
        const auto query = q < 0 ? juce::String() : streamPath.substring (q + 1);

        juce::StringArray params;
        params.addTokens (query, "&", "");
        params.removeEmptyStrings();

        bool replaced = false;

        for (int i = 0; i < params.size(); ++i)
        {
            if (! params[i].upToFirstOccurrenceOf ("=", false, false).trim().equalsIgnoreCase ("resolution"))
                continue;

            if (resolution.isNotEmpty() && ! replaced)
            {
                params.set (i, "resolution=" + resolution);
                replaced = true;
            }
            else
            {
                params.remove (i--);
            }
        }

        if (resolution.isNotEmpty() && ! replaced)
            params.add ("resolution=" + resolution);

        return params.isEmpty() ? base : base + "?" + params.joinIntoString ("&");
    }

    juce::String errorMessageFor (ConnectFailure f, const juce::String& host)
    {
        switch (f)
        {
            case ConnectFailure::unreachable:  return "Kamera unter " + host + " nicht erreichbar";
            case ConnectFailure::unauthorized: return "Benutzername oder Passwort falsch";
            case ConnectFailure::notFound:     return "Stream-Pfad nicht gefunden";
            case ConnectFailure::noImage:
                return juce::String (juce::CharPointer_UTF8 ("Verbunden, aber kein Bild \xe2\x80\x93 Stream-Pfad pr\xc3\xbc" "fen"));
            case ConnectFailure::none:         break;
        }
        return {};
    }
}
