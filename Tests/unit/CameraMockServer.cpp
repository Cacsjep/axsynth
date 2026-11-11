#include "CameraMockServer.h"
#include <juce_cryptography/juce_cryptography.h>
#include <cstring>

namespace axsynth::test
{
    namespace
    {
        const char* const realm = "AXIS_MOCK";

        juce::String md5Hex (const juce::String& s)
        {
            return juce::MD5 (s.toUTF8()).toHexString();
        }

        juce::StringPairArray parseDigest (const juce::String& header)
        {
            juce::StringPairArray out;
            // Keep the parameter string alive: a CharPointer into a temporary String dangles,
            // which made the mock reject valid Digest responses at random (freed memory reused).
            const auto params = header.fromFirstOccurrenceOf (" ", false, false);
            auto p = params.getCharPointer();

            while (! p.isEmpty())
            {
                while (*p == ' ' || *p == ',')
                    ++p;

                juce::String key;
                while (! p.isEmpty() && *p != '=')
                    key << *p++;

                if (p.isEmpty())
                    break;
                ++p; // '='

                juce::String value;
                if (*p == '"')
                {
                    ++p;
                    while (! p.isEmpty() && *p != '"')
                        value << *p++;
                    if (! p.isEmpty())
                        ++p;
                }
                else
                {
                    while (! p.isEmpty() && *p != ',')
                        value << *p++;
                }

                out.set (key.trim(), value.trim());
            }
            return out;
        }

        bool writeString (juce::StreamingSocket& s, const juce::String& text)
        {
            const auto utf8 = text.toStdString();
            return s.write (utf8.data(), (int) utf8.size()) == (int) utf8.size();
        }

        bool sendResponse (juce::StreamingSocket& s, int status, const juce::String& reason,
                           const juce::String& extraHeaders, const juce::String& body)
        {
            juce::String r;
            r << "HTTP/1.1 " << status << " " << reason << "\r\n"
              << "Content-Type: text/plain\r\n"
              << "Content-Length: " << (int) body.getNumBytesAsUTF8() << "\r\n"
              << extraHeaders
              << "\r\n" << body;
            return writeString (s, r);
        }

        // Waits until the peer closes, the thread should exit or 'keepGoing' returns false.
        template <typename Fn>
        void idle (juce::StreamingSocket& s, juce::Thread& t, Fn keepGoing)
        {
            while (! t.threadShouldExit() && keepGoing())
            {
                const auto ready = s.waitUntilReady (true, 20);
                if (ready < 0)
                    return;
                if (ready > 0)
                {
                    char c;
                    if (s.read (&c, 1, false) <= 0)
                        return;
                }
            }
        }
    }

    // ------------------------------------------------------------------------

    class MockCameraServer::Connection : public juce::Thread
    {
    public:
        Connection (MockCameraServer& o, juce::StreamingSocket* s)
            : juce::Thread ("MockCamConn"), owner (o), socket (s) {}

        ~Connection() override { stopThread (4000); }

        void run() override
        {
            owner.handle (*socket, *this);
            socket->close();
            finished = true;
        }

        MockCameraServer& owner;
        std::unique_ptr<juce::StreamingSocket> socket;
        std::atomic<bool> finished { false };
    };

    class MockCameraServer::Listener : public juce::Thread
    {
    public:
        explicit Listener (MockCameraServer& o) : juce::Thread ("MockCamListen"), owner (o) {}

        ~Listener() override
        {
            signalThreadShouldExit();
            stopThread (4000);

            for (auto* c : connections)
                c->signalThreadShouldExit();
            connections.clear();
        }

        void run() override
        {
            while (! threadShouldExit())
            {
                if (socket.waitUntilReady (true, 20) <= 0)
                    continue;

                if (auto* s = socket.waitForNextConnection())
                {
                    for (int i = connections.size(); --i >= 0;)
                        if (connections[i]->finished)
                            connections.remove (i);

                    auto* c = connections.add (new Connection (owner, s));
                    c->startThread();
                }
            }
        }

        MockCameraServer& owner;
        juce::StreamingSocket socket;
        juce::OwnedArray<Connection> connections;
    };

    // ------------------------------------------------------------------------

    MockCameraServer::MockCameraServer (Config c) : config (std::move (c))
    {
        listener = std::make_unique<Listener> (*this);
        if (listener->socket.createListener (0, "127.0.0.1"))
            port = listener->socket.getBoundPort();
        listener->startThread();
    }

    MockCameraServer::~MockCameraServer()
    {
        listener.reset();
    }

    juce::StringArray MockCameraServer::getRequestLog() const
    {
        const juce::ScopedLock sl (logLock);
        return log;
    }

    juce::StringArray MockCameraServer::getAuthLog() const
    {
        const juce::ScopedLock sl (logLock);
        return authLog;
    }

    void MockCameraServer::dropConnections()
    {
        ++dropGeneration;
    }

    bool MockCameraServer::readRequest (juce::StreamingSocket& s, juce::Thread& t, juce::String& head)
    {
        juce::MemoryOutputStream data;

        while (! t.threadShouldExit())
        {
            const auto ready = s.waitUntilReady (true, 20);
            if (ready < 0)
                return false;
            if (ready == 0)
                continue;

            char c;
            if (s.read (&c, 1, false) <= 0)
                return false;

            if (data.getDataSize() == 0 && ! (c >= 'A' && c <= 'Z'))
            {
                ++tlsRejects; // TLS ClientHello (0x16) or garbage: hang up like a plain HTTP server would
                return false;
            }

            data.writeByte (c);

            const auto size = data.getDataSize();
            const auto* d = static_cast<const char*> (data.getData());
            if (size >= 4 && std::memcmp (d + size - 4, "\r\n\r\n", 4) == 0)
            {
                head = data.toString();
                return true;
            }

            if (size > 16384)
                return false;
        }
        return false;
    }

    bool MockCameraServer::checkAuth (const juce::StringPairArray& headers, const juce::String& uri) const
    {
        const auto auth = headers["Authorization"];

        const auto basicOk = [&]
        {
            const auto up = (config.user + ":" + config.password).toStdString();
            return auth == "Basic " + juce::Base64::toBase64 (up.data(), up.size());
        };

        const auto digestOk = [&]
        {
            if (! auth.startsWithIgnoreCase ("Digest "))
                return false;

            const auto p = parseDigest (auth);
            if (p["username"] != config.user || p["realm"] != realm || p["qop"] != "auth" || p["uri"] != uri)
                return false;

            const auto ha1 = md5Hex (config.user + ":" + realm + ":" + config.password);
            const auto ha2 = md5Hex ("GET:" + p["uri"]);
            const auto expected = md5Hex (ha1 + ":" + p["nonce"] + ":" + p["nc"] + ":" + p["cnonce"] + ":auth:" + ha2);
            return p["response"].equalsIgnoreCase (expected);
        };

        switch (config.auth)
        {
            case Auth::none:   return true;
            case Auth::basic:  return basicOk();
            case Auth::digest: return digestOk();
            case Auth::both:   return basicOk() || digestOk();
        }
        return false;
    }

    void MockCameraServer::handle (juce::StreamingSocket& s, juce::Thread& t)
    {
        while (! t.threadShouldExit())
        {
            juce::String head;
            if (! readRequest (s, t, head))
                return;

            const auto lines = juce::StringArray::fromLines (head.trim());
            const auto uri = lines[0].fromFirstOccurrenceOf (" ", false, false).upToLastOccurrenceOf (" ", false, false);

            juce::StringPairArray headers; // case-insensitive keys
            for (int i = 1; i < lines.size(); ++i)
                headers.set (lines[i].upToFirstOccurrenceOf (":", false, false).trim(),
                             lines[i].fromFirstOccurrenceOf (":", false, false).trim());

            {
                const auto authHeader = headers["Authorization"];
                const juce::ScopedLock sl (logLock);
                log.add ("GET " + uri);
                authLog.add (uri + " " + (authHeader.isEmpty() ? juce::String ("-")
                                                               : authHeader.upToFirstOccurrenceOf (" ", false, false)));
            }

            if (config.hangAll)
            {
                idle (s, t, [] { return true; });
                return;
            }

            if (! checkAuth (headers, uri))
            {
                juce::String challenge;
                if (config.auth == Auth::digest || config.auth == Auth::both)
                    challenge << "WWW-Authenticate: Digest realm=\"" << realm << "\", nonce=\""
                              << juce::String::toHexString (juce::Random::getSystemRandom().nextInt64())
                              << "\", algorithm=MD5, qop=\"auth\"\r\n";
                if (config.auth == Auth::basic || config.auth == Auth::both)
                    challenge << "WWW-Authenticate: Basic realm=\"" << realm << "\"\r\n";

                if (! sendResponse (s, 401, "Unauthorized", challenge, "Unauthorized"))
                    return;
                continue;
            }

            const auto path = uri.upToFirstOccurrenceOf ("?", false, false);

            if (path == "/axis-cgi/param.cgi" && config.axisParams && config.paramStatus != 200)
            {
                if (! sendResponse (s, config.paramStatus, "Error", {}, "Error"))
                    return;
                continue;
            }

            if (path == "/axis-cgi/param.cgi" && config.axisParams)
            {
                juce::String body;
                if (uri.contains ("Properties.Image.Resolution"))
                    body = "root.Properties.Image.Resolution=1920x1080,1440x1080,1280x960,1280x720,1024x768,1024x576,"
                           "800x600,800x450,640x480,640x360,480x360,480x270,320x240,320x200,320x180,240x180,"
                           "160x120,160x100,160x90\n";
                else if (uri.contains ("ImageSource.I0.Sensor"))
                    body = "root.ImageSource.I0.Sensor.AspectRatio=16:9\n";
                else
                    body = "# Error: Error -1 getting param in group\n";

                if (! sendResponse (s, 200, "OK", {}, body))
                    return;
                continue;
            }

            if (path != config.streamPath)
            {
                if (! sendResponse (s, 404, "Not Found", {}, "Not Found"))
                    return;
                continue;
            }

            if (config.streamStatus != 200)
            {
                if (! sendResponse (s, config.streamStatus, "Error", {}, "Error"))
                    return;
                continue;
            }

            // ---- MJPEG stream (Axis style: no Content-Length, connection close) ----
            ++streamsServed;
            const auto generation = dropGeneration.load();
            const auto notDropped = [this, generation] { return dropGeneration.load() == generation; };

            if (! writeString (s, "HTTP/1.1 200 OK\r\nCache-Control: no-cache\r\nPragma: no-cache\r\nConnection: close\r\n"
                                  "Content-Type: multipart/x-mixed-replace; boundary=myboundary\r\n\r\n"))
                return;

            if (config.mode == StreamMode::hang)
            {
                idle (s, t, notDropped);
                return;
            }

            if (config.mode == StreamMode::noImage || config.jpegs.empty())
            {
                writeString (s, "--myboundary\r\nContent-Type: text/plain\r\n\r\nno image here\r\n");
                idle (s, t, notDropped);
                return;
            }

            for (size_t i = 0; ! t.threadShouldExit() && notDropped(); ++i)
            {
                const auto& jpeg = config.jpegs[i % config.jpegs.size()];

                juce::String partHead;
                partHead << "--myboundary\r\nContent-Type: image/jpeg\r\n";
                if (config.contentLength)
                    partHead << "Content-Length: " << (int) jpeg.getSize() << "\r\n";
                partHead << "\r\n";

                if (! writeString (s, partHead)
                    || s.write (jpeg.getData(), (int) jpeg.getSize()) != (int) jpeg.getSize()
                    || ! writeString (s, "\r\n"))
                    return;

                t.wait (config.frameIntervalMs);
            }
            return;
        }
    }
}
