#include "CameraConnector.h"
#include "MjpegParser.h"
#include "TripleBuffer.h"
#include "WinHttpClient.h"
#include <algorithm>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace axsynth
{
    namespace
    {
        constexpr int requestTimeoutMs = 5000;      // connect / receive timeout per request
        constexpr int reachabilityDeadlineMs = 2000; // "Kamera nicht erreichbar" after at most this long
        constexpr int firstFrameTimeoutMs = 5000;   // "connected but no image"
        constexpr int frameStallTimeoutMs = 10000;
        constexpr int minBackoffMs = 1000;
        constexpr int maxBackoffMs = 30000;

        const char* const probePath = "/";
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

        enum class AttemptResult
        {
            streamedThenLost, cancelled, unreachable, nameNotResolved,
            authNotOffered,   // 401, but the server does not offer this cascade step's scheme
            unauthorized,     // 401 after sending credentials, or 403
            notFound,         // a real 404
            httpError,        // any other non-2xx answer (400, 3xx, 5xx, ...)
            noImage
        };

        // Internal failure classes, ordered by how much they tell the user (higher wins).
        enum class Failure { unreachable = 0, authNotOffered, httpError, unauthorized, notFound, noImage };

        ConnectFailure toPublic (Failure f)
        {
            switch (f)
            {
                case Failure::unreachable:  return ConnectFailure::unreachable;
                case Failure::authNotOffered: // no variant could even send the credentials
                case Failure::unauthorized: return ConnectFailure::unauthorized;
                case Failure::httpError:    // the server answered but refused the stream request
                case Failure::notFound:     return ConnectFailure::notFound;
                case Failure::noImage:      return ConnectFailure::noImage;
            }
            return ConnectFailure::unreachable;
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

        bool saneAspect (int w, int h)
        {
            return w > 0 && h > 0 && w <= 4 * h && h <= 4 * w;
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

        std::mutex controlMutex; // serialises start/stop and the status changes they make
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
                    setError (errorMessageFor (s.host.isEmpty() ? ConnectFailure::unreachable : ConnectFailure::unauthorized, s.host));
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

        void stop (bool setIdle = false)
        {
            const std::lock_guard<std::mutex> lg (controlMutex);
            stopLocked();

            if (setIdle)
                owner.status = Status::idle;
        }

        void stopLocked()
        {
            signalThreadShouldExit();
            canceller.cancel();
            wakeEvent.signal();

            // Wait without a timeout: every blocking point of the network thread is
            // cancellable (WinHTTP handles are closed by the canceller, probe threads are
            // cancelled and joined, the backoff wait is woken) and JPEG decoding is bounded
            // by MjpegParser::isDecodableSize, so the thread exits within milliseconds.
            // A timeout would end in TerminateThread, which can leave the heap locked.
            stopThread (-1);
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
                else if (user || failure == ConnectFailure::unauthorized)
                {
                    // Silent mode gives up on wrong credentials too: retrying forever would
                    // only pile up failed logins on the camera.
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

        // Sends one unauthenticated request per allowed scheme in parallel. A scheme that
        // answers with any HTTP status is reachable. Waiting stops when the first scheme of
        // the cascade order has answered, when all probes are done, or at the overall
        // reachability deadline. Schemes still pending when waiting stopped early (because
        // a preferred scheme answered) count as "unknown" and stay in the cascade.
        enum class Reach { unknown, reachable, unreachable };

        void probeSchemes (const juce::String& host, const std::vector<bool>& schemeOrder, Reach& https, Reach& http)
        {
            struct Probe
            {
                bool useHttps = false;
                http::Canceller canceller;
                std::atomic<int> state { 0 }; // 0 pending, 1 answered, 2 failed
                std::thread thread;
            };

            juce::WaitableEvent anyDone;
            std::vector<std::unique_ptr<Probe>> probes;

            for (const bool useHttps : schemeOrder)
            {
                auto p = std::make_unique<Probe>();
                p->useHttps = useHttps;
                auto* raw = p.get();

                p->thread = std::thread ([raw, host, &anyDone]
                {
                    http::Response r;
                    {
                        http::Client client (raw->canceller, raw->useHttps, host, requestTimeoutMs);
                        r = client.get (probePath, http::AuthMode::none, {}, {});
                    }
                    raw->state = r.outcome == http::Outcome::ok ? 1 : 2;
                    anyDone.signal();
                });

                probes.push_back (std::move (p));
            }

            const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) reachabilityDeadlineMs;

            for (;;)
            {
                bool allDone = true, preferredAnswered = false, preferredPending = false;

                for (const auto& p : probes)
                {
                    const auto st = p->state.load();
                    allDone = allDone && st != 0;

                    if (! preferredPending && ! preferredAnswered)
                    {
                        if (st == 1) preferredAnswered = true;
                        else if (st == 0) preferredPending = true;
                    }
                }

                if (allDone || preferredAnswered || shouldStop() || juce::Time::getMillisecondCounter() >= deadline)
                    break;

                anyDone.wait (20);
            }

            const bool deadlinePassed = juce::Time::getMillisecondCounter() >= deadline;

            // Snapshot before cancelling: a probe we cancel while pending must not count as
            // a real transport failure unless the deadline is what stopped it.
            std::vector<int> states;
            for (const auto& p : probes)
                states.push_back (p->state.load());

            for (auto& p : probes)
                p->canceller.cancel();

            for (size_t i = 0; i < probes.size(); ++i)
            {
                probes[i]->thread.join();

                const auto reach = states[i] == 1 ? Reach::reachable
                                 : states[i] == 2 ? Reach::unreachable
                                 : deadlinePassed ? Reach::unreachable
                                                  : Reach::unknown;

                (probes[i]->useHttps ? https : http) = reach;
            }
        }

        // Runs the cascade (remembered combo first). Streams on success until the
        // connection drops (streamed = true). Otherwise returns the best failure.
        ConnectFailure connectAndStream (const CameraSettings& s, bool& streamed)
        {
            const auto spec = http::parseHost (s.host);

            const std::vector<Combo> cascade { { true, true }, { true, false }, { false, true }, { false, false } };
            std::vector<Combo> order;

            const bool hasRemembered = (s.scheme == "https" || s.scheme == "http") && (s.auth == "digest" || s.auth == "basic");
            if (hasRemembered)
                order.push_back ({ s.scheme == "https", s.auth == "digest" });

            for (const auto& c : cascade)
                if (std::find (order.begin(), order.end(), c) == order.end())
                    order.push_back (c);

            order.erase (std::remove_if (order.begin(), order.end(), [&] (const Combo& c)
                                         { return c.https ? ! spec.allowHttps : ! spec.allowHttp; }),
                         order.end());

            if (order.empty() || spec.host.isEmpty())
                return ConnectFailure::unreachable;

            // ---- reachability: one deadline across both schemes ----
            std::vector<bool> schemeOrder;
            for (const auto& c : order)
                if (std::find (schemeOrder.begin(), schemeOrder.end(), c.https) == schemeOrder.end())
                    schemeOrder.push_back (c.https);

            Reach httpsReach = Reach::unreachable, httpReach = Reach::unreachable;
            probeSchemes (s.host, schemeOrder, httpsReach, httpReach);

            if (shouldStop())
                return ConnectFailure::none;

            bool skipHttps = httpsReach == Reach::unreachable;
            bool skipHttp = httpReach == Reach::unreachable;
            Failure best = Failure::unreachable;

            for (size_t i = 0; i < order.size(); ++i)
            {
                if (shouldStop())
                    return ConnectFailure::none;

                const auto& c = order[i];
                if (c.https ? skipHttps : skipHttp)
                    continue;

                const bool useRememberedResolution = hasRemembered && i == 0 && s.resolution.isNotEmpty();
                const auto r = attempt (s, c, useRememberedResolution, streamed);

                Failure f = Failure::unreachable;

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

                    case AttemptResult::authNotOffered: f = Failure::authNotOffered; break;
                    case AttemptResult::unauthorized: f = Failure::unauthorized; break;
                    case AttemptResult::notFound:     f = Failure::notFound; break;
                    case AttemptResult::httpError:    f = Failure::httpError; break;
                    case AttemptResult::noImage:      f = Failure::noImage; break;
                }

                if ((int) f > (int) best)
                    best = f;

                if (f == Failure::noImage)
                    break; // authenticated and got a stream; another variant won't produce images either
            }

            return toPublic (best);
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

                if (r.status == 401)
                    return r.credentialsSent ? AttemptResult::unauthorized : AttemptResult::authNotOffered;

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
                // Anything else (403 for a user without parameter rights, 404 on a
                // non-Axis source, ...): open the stream path as entered.
            }

            const auto path = applyResolution (s.streamPath.isNotEmpty() ? s.streamPath : CameraSettings::defaultStreamPath(),
                                               resolution);

            const auto r = client.get (path, authMode, s.user, s.password);
            if (r.outcome != http::Outcome::ok)
                return fromTransport (r.outcome);

            if (r.status == 401)
                return r.credentialsSent ? AttemptResult::unauthorized : AttemptResult::authNotOffered;

            if (r.status == 403)
                return AttemptResult::unauthorized;

            if (r.status == 404)
                return AttemptResult::notFound;

            if (r.status < 200 || r.status >= 300)
                return AttemptResult::httpError;

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
                // Never let a declared size reach the decoder unchecked.
                if (! MjpegParser::isDecodableSize (data, size))
                    return;

                juce::MemoryInputStream in (data, size, false);
                auto img = jpeg.decodeImage (in);
                if (! img.isValid())
                    return;

                // JUCE 8 decodes into a native (Direct2D) image whose internals are not safe
                // to share between threads; hand out a plain software copy instead.
                img = juce::SoftwareImageType().convert (img);

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
        impl->stop (true);
    }

    void CameraConnector::setSettings (const CameraSettings& s)
    {
        auto copy = s;
        copy.host = http::stripUserInfo (s.host);

        const juce::ScopedLock sl (impl->lock);
        impl->settings = copy;
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
        impl->stop (true);
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

        enum class Filter { preferredAspect, standardAspect, anySaneAspect };

        auto pick = [&] (Filter filter) -> juce::String
        {
            juce::String best;
            juce::int64 bestArea = 0;

            for (const auto& t : tokens)
            {
                const auto sz = parseSize (t);
                const auto a = aspectCode (sz.w, sz.h);

                if (sz.w < 320 || ! saneAspect (sz.w, sz.h))
                    continue;
                if (filter == Filter::preferredAspect && a != preferred)
                    continue;
                if (filter == Filter::standardAspect && a == 0)
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
            if (auto r = pick (Filter::preferredAspect); r.isNotEmpty())
                return r;

        if (auto r = pick (Filter::standardAspect); r.isNotEmpty())
            return r;

        return pick (Filter::anySaneAspect);
    }

    juce::String applyResolution (const juce::String& streamPath, const juce::String& resolution)
    {
        if (resolution.isEmpty())
            return streamPath; // nothing to replace it with: keep what the user entered

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

            if (! replaced)
            {
                params.set (i, "resolution=" + resolution);
                replaced = true;
            }
            else
            {
                params.remove (i--);
            }
        }

        if (! replaced)
            params.add ("resolution=" + resolution);

        return base + "?" + params.joinIntoString ("&");
    }

    juce::String errorMessageFor (ConnectFailure f, const juce::String& host)
    {
        switch (f)
        {
            case ConnectFailure::unreachable:  return "Kamera unter " + http::stripUserInfo (host) + " nicht erreichbar";
            case ConnectFailure::unauthorized: return "Benutzername oder Passwort falsch";
            case ConnectFailure::notFound:     return "Stream-Pfad nicht gefunden";
            case ConnectFailure::noImage:
                return juce::String (juce::CharPointer_UTF8 ("Verbunden, aber kein Bild \xe2\x80\x93 Stream-Pfad pr\xc3\xbc" "fen"));
            case ConnectFailure::none:         break;
        }
        return {};
    }
}
