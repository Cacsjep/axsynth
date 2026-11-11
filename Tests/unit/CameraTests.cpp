#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include "camera/CameraConnector.h"
#include "camera/MjpegParser.h"
#include "camera/TripleBuffer.h"
#include "camera/WinHttpClient.h"
#include "CameraMockServer.h"
#include <functional>
#include <thread>
#include <cstdio>
#include <vector>

namespace axsynth::test
{
    namespace
    {
        using Status = CameraConnector::Status;

        juce::MemoryBlock makeJpeg (int w, int h, int seed)
        {
            juce::Image img (juce::Image::RGB, w, h, false, juce::SoftwareImageType());
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                    img.setPixelAt (x, y, juce::Colour ((juce::uint8) (x * 7 + seed * 31), (juce::uint8) (y * 11 + seed),
                                                        (juce::uint8) ((x + y) * 5)));

            juce::MemoryOutputStream out;
            juce::JPEGImageFormat fmt;
            fmt.setQuality (0.8f);
            fmt.writeImageToStream (img, out);
            return out.getMemoryBlock();
        }

        // Axis-style multipart body.
        juce::MemoryBlock buildStream (const std::vector<juce::MemoryBlock>& jpegs, bool withLength,
                                       const juce::String& delimiter = "--myboundary")
        {
            juce::MemoryOutputStream out;
            out << "preamble garbage\r\n";
            for (const auto& j : jpegs)
            {
                out << delimiter << "\r\nContent-Type: image/jpeg\r\n";
                if (withLength)
                    out << "Content-Length: " << (int) j.getSize() << "\r\n";
                out << "\r\n";
                out.write (j.getData(), j.getSize());
                out << "\r\n";
            }
            out << delimiter << "\r\n"; // next part starts (needed to terminate the last part without length)
            return out.getMemoryBlock();
        }

        std::vector<juce::MemoryBlock> parseInChunks (const juce::String& boundary, const juce::MemoryBlock& data,
                                                      juce::Random& rng, int maxChunk)
        {
            std::vector<juce::MemoryBlock> frames;
            MjpegParser parser (boundary, [&] (const juce::uint8* d, size_t n) { frames.emplace_back (d, n); });

            size_t pos = 0;
            while (pos < data.getSize())
            {
                const auto n = juce::jmin ((size_t) (1 + rng.nextInt (maxChunk)), data.getSize() - pos);
                parser.push (static_cast<const char*> (data.getData()) + pos, n);
                pos += n;
            }
            return frames;
        }

        bool waitFor (const std::function<bool()>& pred, int timeoutMs)
        {
            const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
            while (juce::Time::getMillisecondCounter() < end)
            {
                if (pred())
                    return true;
                juce::Thread::sleep (10);
            }
            return pred();
        }

        bool waitForSettled (CameraConnector& c, int timeoutMs)
        {
            return waitFor ([&] { const auto s = c.getStatus(); return s == Status::streaming || s == Status::failed; }, timeoutMs);
        }

        CameraSettings settingsFor (const MockCameraServer& server, const juce::String& user = "user",
                                    const juce::String& password = "secret")
        {
            CameraSettings s;
            s.host = "127.0.0.1:" + juce::String (server.getPort());
            s.user = user;
            s.password = password;
            return s;
        }

        const juce::String axisList = "root.Properties.Image.Resolution=1920x1080,1440x1080,1280x960,1280x720,1024x768,"
                                      "1024x576,800x600,800x450,640x480,640x360,480x360,480x270,320x240,320x200,320x180,"
                                      "240x180,160x120,160x100,160x90\n";
    }

    class CameraTests : public juce::UnitTest
    {
    public:
        CameraTests() : juce::UnitTest ("Camera", "camera") {}

        void runTest() override
        {
            testPickResolution();
            testApplyResolution();
            testErrorMessages();
            testDpapi();
            testHostParsing();
            testTripleBuffer();
            testParser();
            testMockCascade();
            testLiveCamera();
        }

    private:
        void note (const juce::String& text)
        {
            logMessage (text);
            std::printf ("%s\n", text.toRawUTF8());
            std::fflush (stdout);
        }

        // ----------------------------------------------------------------------

        void testPickResolution()
        {
            beginTest ("pickResolution");
            expectEquals (pickResolution (axisList), juce::String ("320x180"));
            expectEquals (pickResolution (axisList, "16:9"), juce::String ("320x180"));
            expectEquals (pickResolution (axisList, "4:3"), juce::String ("320x240"));
            expectEquals (pickResolution ("Properties.Image.Resolution=1280x960,640x480,320x240,160x120"), juce::String ("320x240"));
            expectEquals (pickResolution ("Properties.Image.Resolution=1280x960,640x480,320x240", "16:9"), juce::String ("320x240"));
            expectEquals (pickResolution ("1280x720,640x360"), juce::String ("640x360"));
            expectEquals (pickResolution ("root.Properties.Image.Resolution=160x120,160x90,240x180"), juce::String());
            expectEquals (pickResolution (""), juce::String());
            expectEquals (pickResolution ("# Error: Error -1 getting param in group 'Properties.Image.Resolution'\n"), juce::String());
            expectEquals (pickResolution ("Properties.Image.Resolution=abc,x,320x,x180,-320x180,1000x1"), juce::String());
            expectEquals (pickResolution ("<html><body>404</body></html>"), juce::String());
            expectEquals (pickResolution ("Properties.Image.Resolution=1280x720,800x600\r\n"), juce::String ("800x600"));
        }

        void testApplyResolution()
        {
            beginTest ("applyResolution");
            const auto def = CameraSettings::defaultStreamPath();
            expectEquals (applyResolution (def, "320x180"), juce::String ("/axis-cgi/mjpg/video.cgi?resolution=320x180&fps=25"));
            expectEquals (applyResolution (def, ""), juce::String ("/axis-cgi/mjpg/video.cgi?fps=25"));
            expectEquals (applyResolution ("/axis-cgi/mjpg/video.cgi", "320x180"), juce::String ("/axis-cgi/mjpg/video.cgi?resolution=320x180"));
            expectEquals (applyResolution ("/axis-cgi/mjpg/video.cgi?fps=10", "320x180"), juce::String ("/axis-cgi/mjpg/video.cgi?fps=10&resolution=320x180"));
            expectEquals (applyResolution ("/video.mjpg", ""), juce::String ("/video.mjpg"));
            expectEquals (applyResolution ("/v.cgi?resolution=640x480", ""), juce::String ("/v.cgi"));
            expectEquals (applyResolution ("/v.cgi?a=1&Resolution=640x480&b=2", "320x240"), juce::String ("/v.cgi?a=1&resolution=320x240&b=2"));
            expectEquals (applyResolution ("/v.cgi?resolution=1&resolution=2", "320x240"), juce::String ("/v.cgi?resolution=320x240"));
            expectEquals (applyResolution ("/v.cgi?", "320x240"), juce::String ("/v.cgi?resolution=320x240"));
        }

        void testErrorMessages()
        {
            beginTest ("errorMessageFor");
            expectEquals (errorMessageFor (ConnectFailure::unreachable, "10.0.0.48"), juce::String ("Kamera unter 10.0.0.48 nicht erreichbar"));
            expectEquals (errorMessageFor (ConnectFailure::unauthorized, "x"), juce::String ("Benutzername oder Passwort falsch"));
            expectEquals (errorMessageFor (ConnectFailure::notFound, "x"), juce::String ("Stream-Pfad nicht gefunden"));
            const auto noImage = errorMessageFor (ConnectFailure::noImage, "x");
            expectEquals (noImage, juce::String (juce::CharPointer_UTF8 ("Verbunden, aber kein Bild \xe2\x80\x93 Stream-Pfad pr\xc3\xbc" "fen")));
            expect (noImage.containsChar ((juce::juce_wchar) 0x2013), "en dash");
            expect (noImage.containsChar ((juce::juce_wchar) 0xfc), "u umlaut");
            expect (errorMessageFor (ConnectFailure::none, "x").isEmpty());
        }

        void testDpapi()
        {
            beginTest ("DPAPI round trip");
            const juce::String pw (juce::CharPointer_UTF8 ("p\xc3\xa4ss w\xc3\xb6rd;1qay"));
            const auto enc = dpapiProtect (pw);
            expect (enc.isNotEmpty());
            expect (! enc.contains ("rd;1qay"));
            expectEquals (dpapiUnprotect (enc), pw);
            expect (dpapiProtect (pw) != pw);
            expectEquals (dpapiUnprotect (dpapiProtect ("x")), juce::String ("x"));
            expectEquals (dpapiProtect (""), juce::String());
            expectEquals (dpapiUnprotect (""), juce::String());
            expectEquals (dpapiUnprotect ("bm90IGVuY3J5cHRlZA=="), juce::String());
            expectEquals (dpapiUnprotect ("%%% not base64 %%%"), juce::String());
        }

        void testHostParsing()
        {
            beginTest ("host:port parsing");
            juce::String h; int p = 0;
            http::splitHostPort ("10.0.0.48", h, p, true);           expectEquals (h, juce::String ("10.0.0.48")); expectEquals (p, 443);
            http::splitHostPort ("10.0.0.48", h, p, false);          expectEquals (p, 80);
            http::splitHostPort ("127.0.0.1:8080", h, p, true);      expectEquals (h, juce::String ("127.0.0.1")); expectEquals (p, 8080);
            http::splitHostPort ("http://cam.local:81/x", h, p, false); expectEquals (h, juce::String ("cam.local")); expectEquals (p, 81);
            http::splitHostPort ("[fe80::1]:8443", h, p, true);      expectEquals (h, juce::String ("fe80::1")); expectEquals (p, 8443);
            http::splitHostPort (" cam ", h, p, false);              expectEquals (h, juce::String ("cam"));
        }

        void testTripleBuffer()
        {
            beginTest ("TripleBuffer");
            TripleBuffer<int> tb;
            expect (! tb.consume());
            tb.writeSlot() = 1; tb.publish();
            tb.writeSlot() = 2; tb.publish();
            expect (tb.consume());
            expectEquals (tb.readSlot(), 2);
            expect (! tb.consume());
            tb.writeSlot() = 3; tb.publish();
            expect (tb.consume());
            expectEquals (tb.readSlot(), 3);

            // concurrent smoke test: values must be monotonic
            TripleBuffer<int> shared;
            std::atomic<bool> done { false };
            std::thread producer ([&] { for (int i = 1; i <= 200000; ++i) { shared.writeSlot() = i; shared.publish(); } done = true; });
            int last = 0; bool monotonic = true;
            for (;;)
            {
                const bool finished = done.load();
                while (shared.consume()) { monotonic = monotonic && shared.readSlot() > last; last = shared.readSlot(); }
                if (finished)
                    break;
            }
            producer.join();
            expect (monotonic);
            expectEquals (last, 200000);
        }

        void testParser()
        {
            beginTest ("MJPEG boundary from Content-Type");
            expectEquals (MjpegParser::boundaryFromContentType ("multipart/x-mixed-replace; boundary=myboundary"), juce::String ("myboundary"));
            expectEquals (MjpegParser::boundaryFromContentType ("multipart/x-mixed-replace;boundary=--myboundary"), juce::String ("myboundary"));
            expectEquals (MjpegParser::boundaryFromContentType ("multipart/x-mixed-replace; boundary=\"--abc def\"; charset=x"), juce::String ("abc def"));
            expectEquals (MjpegParser::boundaryFromContentType ("multipart/x-mixed-replace; Boundary=\"xyz\""), juce::String ("xyz"));
            expectEquals (MjpegParser::boundaryFromContentType ("image/jpeg"), juce::String());

            std::vector<juce::MemoryBlock> jpegs;
            for (int i = 0; i < 6; ++i)
                jpegs.push_back (makeJpeg (32 + i * 8, 18 + i * 4, i));

            auto checkFrames = [&] (const std::vector<juce::MemoryBlock>& got, const juce::String& what)
            {
                expectEquals ((int) got.size(), (int) jpegs.size(), what + ": frame count");
                for (size_t i = 0; i < juce::jmin (got.size(), jpegs.size()); ++i)
                    expect (got[i] == jpegs[i], what + ": frame " + juce::String ((int) i) + " bytes");
            };

            juce::Random rng (1234);

            beginTest ("MJPEG parser, Content-Length, random chunks");
            {
                const auto stream = buildStream (jpegs, true);
                for (const int maxChunk : { 1, 2, 3, 7, 64, 1000, 5000, 100000 })
                    checkFrames (parseInChunks ("myboundary", stream, rng, maxChunk), "CL max " + juce::String (maxChunk));
            }

            beginTest ("MJPEG parser, no Content-Length, random chunks");
            {
                const auto stream = buildStream (jpegs, false);
                for (const int maxChunk : { 1, 3, 17, 512, 100000 })
                    checkFrames (parseInChunks ("myboundary", stream, rng, maxChunk), "noCL max " + juce::String (maxChunk));
            }

            beginTest ("MJPEG parser, boundary declared with leading --");
            {
                // Content-Type said boundary=--myboundary and the parts use exactly that string.
                const auto stream = buildStream (jpegs, true, "--myboundary");
                const auto b = MjpegParser::boundaryFromContentType ("multipart/x-mixed-replace; boundary=--myboundary");
                checkFrames (parseInChunks (b, stream, rng, 50), "dashes");
                const auto stream2 = buildStream (jpegs, false, "myboundary"); // no dashes at all on the wire
                checkFrames (parseInChunks (b, stream2, rng, 50), "bare");
            }

            beginTest ("MJPEG parser, no boundary (SOI/EOI scan)");
            {
                juce::MemoryOutputStream raw;
                raw << "junk";
                for (const auto& j : jpegs) { raw.write (j.getData(), j.getSize()); raw << "\r\n"; }
                checkFrames (parseInChunks ({}, raw.getMemoryBlock(), rng, 1), "soi 1");
                checkFrames (parseInChunks ({}, raw.getMemoryBlock(), rng, 300), "soi 300");
            }

            beginTest ("MJPEG parser, frames decode");
            {
                int decoded = 0;
                MjpegParser parser ("myboundary", [&] (const juce::uint8* d, size_t n)
                {
                    juce::MemoryInputStream in (d, n, false);
                    juce::JPEGImageFormat fmt;
                    if (fmt.decodeImage (in).isValid())
                        ++decoded;
                });
                const auto stream = buildStream (jpegs, true);
                parser.push (stream.getData(), stream.getSize());
                expectEquals (decoded, (int) jpegs.size());
            }
        }

        // ----------------------------------------------------------------------

        void testMockCascade()
        {
            std::vector<juce::MemoryBlock> jpegs;
            for (int i = 0; i < 4; ++i)
                jpegs.push_back (makeJpeg (320, 180, i));

            beginTest ("cascade: Basic only -> http/basic, resolution, frames");
            {
                MockCameraServer::Config cfg;
                cfg.auth = MockCameraServer::Auth::basic;
                cfg.jpegs = jpegs;
                MockCameraServer server (cfg);
                expect (server.getPort() > 0);

                CameraConnector cam;
                cam.setSettings (settingsFor (server));
                const auto t0 = juce::Time::getMillisecondCounter();
                cam.connect (true);
                expect (cam.getStatus() == Status::connecting);
                expect (waitForSettled (cam, 15000));
                note ("  basic cascade took " + juce::String (juce::Time::getMillisecondCounter() - t0) + " ms");
                expect (cam.getStatus() == Status::streaming, cam.getErrorMessage());

                const auto s = cam.getSettings();
                expectEquals (s.scheme, juce::String ("http"));
                expectEquals (s.auth, juce::String ("basic"));
                expectEquals (s.resolution, juce::String ("320x180"));
                expect (server.getTlsRejects() >= 1, "HTTPS was tried first");
                expect (server.getRequestLog().contains ("GET /axis-cgi/mjpg/video.cgi?resolution=320x180&fps=25"));

                expect (waitFor ([&] { return cam.getFrameCount() >= 5; }, 5000), "frames arrive");
                juce::Image img;
                expect (waitFor ([&] { return cam.popLatestFrame (img); }, 2000));
                expect (img.isValid() && img.getWidth() == 320 && img.getHeight() == 180);
                expect (cam.waitForFrame (1000));

                const auto t1 = juce::Time::getMillisecondCounter();
                cam.disconnect();
                const auto dt = juce::Time::getMillisecondCounter() - t1;
                expect (dt < 1000, "disconnect while streaming took " + juce::String (dt) + " ms");
                expect (cam.getStatus() == Status::idle);
            }

            beginTest ("cascade: Digest only -> http/digest");
            {
                MockCameraServer::Config cfg;
                cfg.auth = MockCameraServer::Auth::digest;
                cfg.jpegs = jpegs;
                cfg.contentLength = false;
                MockCameraServer server (cfg);

                CameraConnector cam;
                cam.setSettings (settingsFor (server));
                cam.connect (true);
                expect (waitForSettled (cam, 15000));
                expect (cam.getStatus() == Status::streaming, cam.getErrorMessage());
                expectEquals (cam.getSettings().scheme, juce::String ("http"));
                expectEquals (cam.getSettings().auth, juce::String ("digest"));
                expect (waitFor ([&] { return cam.getFrameCount() >= 3; }, 5000), "frames arrive (no Content-Length)");
            }

            beginTest ("remembered combo is used directly");
            {
                MockCameraServer::Config cfg;
                cfg.auth = MockCameraServer::Auth::basic;
                cfg.jpegs = jpegs;
                MockCameraServer server (cfg);

                auto s = settingsFor (server);
                s.scheme = "http"; s.auth = "basic"; s.resolution = "320x180";
                CameraConnector cam;
                cam.setSettings (s);
                cam.connect (true);
                expect (waitForSettled (cam, 10000));
                expect (cam.getStatus() == Status::streaming, cam.getErrorMessage());
                expectEquals (server.getTlsRejects(), 0);
                const auto log = server.getRequestLog();
                expectEquals (log.size(), 1);
                expectEquals (log[0], juce::String ("GET /axis-cgi/mjpg/video.cgi?resolution=320x180&fps=25"));
            }

            beginTest ("stale remembered combo falls back to cascade");
            {
                MockCameraServer::Config cfg;
                cfg.auth = MockCameraServer::Auth::digest;
                cfg.jpegs = jpegs;
                MockCameraServer server (cfg);

                auto s = settingsFor (server);
                s.scheme = "http"; s.auth = "basic"; s.resolution = "640x360";
                CameraConnector cam;
                cam.setSettings (s);
                cam.connect (true);
                expect (waitForSettled (cam, 15000));
                expect (cam.getStatus() == Status::streaming, cam.getErrorMessage());
                expectEquals (cam.getSettings().auth, juce::String ("digest"));
                expectEquals (cam.getSettings().resolution, juce::String ("320x180"));
            }

            beginTest ("non-Axis source: stream without resolution");
            {
                MockCameraServer::Config cfg;
                cfg.auth = MockCameraServer::Auth::none;
                cfg.axisParams = false;
                cfg.streamPath = "/video.mjpg";
                cfg.jpegs = jpegs;
                MockCameraServer server (cfg);

                auto s = settingsFor (server);
                s.streamPath = "/video.mjpg?resolution=640x360";
                CameraConnector cam;
                cam.setSettings (s);
                cam.connect (true);
                expect (waitForSettled (cam, 15000));
                expect (cam.getStatus() == Status::streaming, cam.getErrorMessage());
                expect (server.getRequestLog().contains ("GET /video.mjpg"));
                expectEquals (cam.getSettings().resolution, juce::String());
            }

            beginTest ("wrong password");
            {
                MockCameraServer::Config cfg;
                cfg.auth = MockCameraServer::Auth::digest;
                cfg.jpegs = jpegs;
                MockCameraServer server (cfg);

                CameraConnector cam;
                cam.setSettings (settingsFor (server, "user", "wrong"));
                cam.connect (true);
                expect (waitForSettled (cam, 15000));
                expect (cam.getStatus() == Status::failed);
                expectEquals (cam.getErrorMessage(), juce::String ("Benutzername oder Passwort falsch"));
            }

            beginTest ("stream path 404");
            {
                MockCameraServer::Config cfg;
                cfg.auth = MockCameraServer::Auth::basic;
                cfg.jpegs = jpegs;
                MockCameraServer server (cfg);

                auto s = settingsFor (server);
                s.streamPath = "/axis-cgi/mjpg/nope.cgi?fps=25";
                CameraConnector cam;
                cam.setSettings (s);
                cam.connect (true);
                expect (waitForSettled (cam, 15000));
                expect (cam.getStatus() == Status::failed);
                expectEquals (cam.getErrorMessage(), juce::String ("Stream-Pfad nicht gefunden"));
            }

            beginTest ("200 but no image");
            {
                MockCameraServer::Config cfg;
                cfg.auth = MockCameraServer::Auth::basic;
                cfg.mode = MockCameraServer::StreamMode::noImage;
                MockCameraServer server (cfg);

                CameraConnector cam;
                cam.setSettings (settingsFor (server));
                const auto t0 = juce::Time::getMillisecondCounter();
                cam.connect (true);
                expect (waitForSettled (cam, 15000));
                const auto dt = juce::Time::getMillisecondCounter() - t0;
                expect (cam.getStatus() == Status::failed);
                expectEquals (cam.getErrorMessage(), errorMessageFor (ConnectFailure::noImage, {}));
                expect (dt >= 4500 && dt < 9000, "no-image detection took " + juce::String (dt) + " ms");
            }

            beginTest ("unreachable (closed port)");
            {
                int port = 0;
                {
                    juce::StreamingSocket s;
                    s.createListener (0, "127.0.0.1");
                    port = s.getBoundPort();
                }
                CameraConnector cam;
                CameraSettings s;
                s.host = "127.0.0.1:" + juce::String (port);
                s.user = "user"; s.password = "secret";
                cam.setSettings (s);
                const auto t0 = juce::Time::getMillisecondCounter();
                cam.connect (true);
                expect (waitForSettled (cam, 15000));
                note ("  refused cascade took " + juce::String (juce::Time::getMillisecondCounter() - t0) + " ms");
                expect (cam.getStatus() == Status::failed);
                expectEquals (cam.getErrorMessage(), "Kamera unter " + s.host + " nicht erreichbar");
            }

            beginTest ("reconnect after drop");
            {
                MockCameraServer::Config cfg;
                cfg.auth = MockCameraServer::Auth::basic;
                cfg.jpegs = jpegs;
                MockCameraServer server (cfg);

                CameraConnector cam;
                cam.setSettings (settingsFor (server));
                cam.connect (true);
                expect (waitFor ([&] { return cam.getStatus() == Status::streaming; }, 15000));
                server.dropConnections();
                expect (waitFor ([&] { return cam.getStatus() == Status::reconnecting; }, 3000), "goes to reconnecting");
                expect (waitFor ([&] { return cam.getStatus() == Status::streaming; }, 6000), "streams again");
                expect (server.getStreamsServed() >= 2);
                const auto before = cam.getFrameCount();
                expect (waitFor ([&] { return cam.getFrameCount() > before + 3; }, 3000));
            }

            beginTest ("auto-connect (silent) keeps retrying, then streams");
            {
                // Server does not exist yet -> reconnecting; never 'failed'.
                MockCameraServer::Config cfg;
                cfg.auth = MockCameraServer::Auth::basic;
                cfg.jpegs = jpegs;
                auto server = std::make_unique<MockCameraServer> (cfg);
                const auto port = server->getPort();
                server.reset();

                CameraConnector cam;
                CameraSettings s;
                s.host = "127.0.0.1:" + juce::String (port);
                s.user = "user"; s.password = "secret";
                cam.setSettings (s);
                cam.connect (false);
                expect (cam.getStatus() == Status::reconnecting);
                juce::Thread::sleep (1500);
                expect (cam.getStatus() == Status::reconnecting);
                expect (cam.getErrorMessage().isEmpty());

                const auto t0 = juce::Time::getMillisecondCounter();
                cam.disconnect();
                expect (juce::Time::getMillisecondCounter() - t0 < 1000, "disconnect during backoff is fast");
                expect (cam.getStatus() == Status::idle);
            }

            beginTest ("disconnect / destroy mid-request is fast");
            {
                MockCameraServer::Config cfg;
                cfg.hangAll = true;
                MockCameraServer server (cfg);

                for (int i = 0; i < 3; ++i)
                {
                    auto cam = std::make_unique<CameraConnector>();
                    cam->setSettings (settingsFor (server));
                    cam->connect (i != 1);
                    juce::Thread::sleep (100 + i * 150);
                    const auto t0 = juce::Time::getMillisecondCounter();
                    if (i == 2)
                        cam->disconnect();
                    cam.reset();
                    const auto dt = juce::Time::getMillisecondCounter() - t0;
                    expect (dt < 1000, "teardown took " + juce::String ((int) dt) + " ms");
                }

                // cancel during TCP connect to a black-hole address
                {
                    CameraConnector cam;
                    CameraSettings s;
                    s.host = "10.255.255.1";
                    s.user = "user"; s.password = "secret";
                    cam.setSettings (s);
                    cam.connect (true);
                    juce::Thread::sleep (300);
                    const auto t0 = juce::Time::getMillisecondCounter();
                    cam.disconnect();
                    const auto dt = juce::Time::getMillisecondCounter() - t0;
                    expect (dt < 1000, "disconnect during TCP connect took " + juce::String ((int) dt) + " ms");
                }

                // many quick create/connect/destroy cycles (pluginval style)
                const auto t0 = juce::Time::getMillisecondCounter();
                for (int i = 0; i < 20; ++i)
                {
                    CameraConnector cam;
                    cam.setSettings (settingsFor (server));
                    cam.connect (false);
                    if (i % 2 == 0)
                        juce::Thread::sleep (5);
                }
                const auto dt = juce::Time::getMillisecondCounter() - t0;
                expect (dt < 5000, "20 cycles took " + juce::String ((int) dt) + " ms");
            }

            beginTest ("connector without settings");
            {
                CameraConnector cam;
                cam.connect (false);
                expect (cam.getStatus() == Status::idle);
                cam.connect (true);
                expect (cam.getStatus() == Status::failed);
                juce::Image img;
                expect (! cam.popLatestFrame (img));
                expect (! cam.waitForFrame (1));
            }
        }

        // ----------------------------------------------------------------------

        void testLiveCamera()
        {
            const auto env = juce::SystemStats::getEnvironmentVariable ("AXSYNTH_TEST_CAMERA", {});
            if (env.isEmpty())
                return;

            beginTest ("live camera (AXSYNTH_TEST_CAMERA)");
            juce::StringArray parts;
            parts.addTokens (env, ";", "");
            expectEquals (parts.size(), 3, "AXSYNTH_TEST_CAMERA=host;user;password");
            if (parts.size() != 3)
                return;

            CameraSettings s;
            s.host = parts[0];
            s.user = parts[1];
            s.password = parts[2];

            CameraConnector cam;
            cam.setSettings (s);
            const auto t0 = juce::Time::getMillisecondCounter();
            cam.connect (true);
            expect (waitFor ([&] { return cam.getStatus() == Status::streaming || cam.getStatus() == Status::failed; }, 10000));
            const auto connectMs = juce::Time::getMillisecondCounter() - t0;
            expect (cam.getStatus() == Status::streaming, "live: " + cam.getErrorMessage());

            const auto learned = cam.getSettings();
            note ("  live: scheme=" + learned.scheme + " auth=" + learned.auth + " resolution=" + learned.resolution
                        + " connect=" + juce::String (connectMs) + " ms");

            const auto f0 = cam.getFrameCount();
            expect (waitFor ([&] { return cam.getFrameCount() >= f0 + 10; }, 5000), "live: >= 10 frames");
            juce::Image img;
            expect (cam.popLatestFrame (img) && img.isValid());
            note ("  live: frame " + juce::String (img.getWidth()) + "x" + juce::String (img.getHeight())
                        + ", frames=" + juce::String ((juce::int64) cam.getFrameCount()));
            expectEquals (learned.resolution, juce::String ("320x180"));

            // reconnect using the remembered combo
            cam.disconnect();
            cam.setSettings (learned);
            const auto t1 = juce::Time::getMillisecondCounter();
            cam.connect (true);
            expect (waitFor ([&] { return cam.getStatus() == Status::streaming; }, 10000), "live: remembered reconnect");
            note ("  live: remembered reconnect " + juce::String (juce::Time::getMillisecondCounter() - t1) + " ms");

            const auto t2 = juce::Time::getMillisecondCounter();
            cam.disconnect();
            expect (juce::Time::getMillisecondCounter() - t2 < 1000, "live: disconnect fast");

            // wrong password
            auto bad = s;
            bad.password = "definitely-wrong";
            cam.setSettings (bad);
            cam.connect (true);
            expect (waitFor ([&] { return cam.getStatus() == Status::failed; }, 20000));
            expectEquals (cam.getErrorMessage(), juce::String ("Benutzername oder Passwort falsch"));
        }
    };

    static CameraTests cameraTests;
}
