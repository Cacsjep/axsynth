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

        // Copy of a JPEG with the SOF0 dimensions rewritten (the pixel data stays small).
        juce::MemoryBlock withDimensions (const juce::MemoryBlock& jpeg, int w, int h)
        {
            juce::MemoryBlock b (jpeg);
            auto* d = static_cast<juce::uint8*> (b.getData());
            for (size_t i = 0; i + 9 < b.getSize(); ++i)
                if (d[i] == 0xFF && d[i + 1] == 0xC0)
                {
                    d[i + 5] = (juce::uint8) (h >> 8); d[i + 6] = (juce::uint8) h;
                    d[i + 7] = (juce::uint8) (w >> 8); d[i + 8] = (juce::uint8) w;
                    break;
                }
            return b;
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
            // nothing 16:9 or 4:3 with width >= 320: smallest sane aspect with width >= 320
            expectEquals (pickResolution ("Properties.Image.Resolution=2048x2048,1024x1024,640x640,320x320,160x160"), juce::String ("320x320"));
            expectEquals (pickResolution ("Properties.Image.Resolution=1280x1024,640x360x,2560x640,160x90", "16:9"), juce::String ("1280x1024"));
            expectEquals (pickResolution ("Properties.Image.Resolution=1600x200,4000x900"), juce::String());
        }

        void testApplyResolution()
        {
            beginTest ("applyResolution");
            const auto def = CameraSettings::defaultStreamPath();
            expectEquals (applyResolution (def, "320x180"), juce::String ("/axis-cgi/mjpg/video.cgi?resolution=320x180&fps=25"));
            expectEquals (applyResolution (def, ""), def); // no replacement: keep the user's parameter
            expectEquals (applyResolution ("/axis-cgi/mjpg/video.cgi", "320x180"), juce::String ("/axis-cgi/mjpg/video.cgi?resolution=320x180"));
            expectEquals (applyResolution ("/axis-cgi/mjpg/video.cgi?fps=10", "320x180"), juce::String ("/axis-cgi/mjpg/video.cgi?fps=10&resolution=320x180"));
            expectEquals (applyResolution ("/video.mjpg", ""), juce::String ("/video.mjpg"));
            expectEquals (applyResolution ("/v.cgi?resolution=640x480", ""), juce::String ("/v.cgi?resolution=640x480"));
            expectEquals (applyResolution ("/v.cgi?a=1&Resolution=640x480&b=2", "320x240"), juce::String ("/v.cgi?a=1&resolution=320x240&b=2"));
            expectEquals (applyResolution ("/v.cgi?resolution=1&resolution=2", "320x240"), juce::String ("/v.cgi?resolution=320x240"));
            expectEquals (applyResolution ("/v.cgi?", "320x240"), juce::String ("/v.cgi?resolution=320x240"));
        }

        void testErrorMessages()
        {
            beginTest ("errorMessageFor");
            expectEquals (errorMessageFor (ConnectFailure::unreachable, "192.0.2.10"), juce::String ("Kamera unter 192.0.2.10 nicht erreichbar"));
            expectEquals (errorMessageFor (ConnectFailure::unreachable, "http://bob:pw@cam:81"), juce::String ("Kamera unter http://cam:81 nicht erreichbar"));
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

            beginTest ("DPAPI deterministic + state helpers");
            expectEquals (dpapiProtect (pw), enc, "same password, same blob");
            juce::String plain;
            expect (! dpapiTryUnprotect ("bm90IGVuY3J5cHRlZA==", plain));
            expect (dpapiTryUnprotect ({}, plain) && plain.isEmpty());

            {
                CameraSettings s;
                passwordFromState (s, enc);
                expectEquals (s.password, pw);
                expect (s.undecryptablePassword.isEmpty());
                expectEquals (passwordForState (s), enc, "re-saving writes the loaded blob back");

                // A blob from another user/machine: password empty, blob kept for the next save.
                const juce::String foreign ("AQAAANCMnd8BFdERjHoAwE/Cl+sBAAAAbm90IG91cnM=");
                CameraSettings t;
                t.host = "cam"; t.user = "root";
                passwordFromState (t, foreign);
                expect (t.password.isEmpty());
                expectEquals (t.undecryptablePassword, foreign);
                expectEquals (passwordForState (t), foreign);
                expect (! t.hasCredentials(), "undecryptable password -> setup panel");

                t.password = "typed-again";
                expectEquals (dpapiUnprotect (passwordForState (t)), juce::String ("typed-again"));
            }

            {
                CameraSettings s;
                expect (! s.hasCredentials());
                s.host = "cam";            expect (! s.hasCredentials());
                s.user = "root";           expect (! s.hasCredentials());
                s.password = "x";          expect (s.hasCredentials());
                s.host = {};               expect (! s.hasCredentials());
            }
        }

        void testHostParsing()
        {
            beginTest ("host:port parsing");
            juce::String h; int p = 0;
            http::splitHostPort ("192.0.2.10", h, p, true);           expectEquals (h, juce::String ("192.0.2.10")); expectEquals (p, 443);
            http::splitHostPort ("192.0.2.10", h, p, false);          expectEquals (p, 80);
            http::splitHostPort ("127.0.0.1:8080", h, p, true);      expectEquals (h, juce::String ("127.0.0.1")); expectEquals (p, 8080);
            http::splitHostPort ("http://cam.local:81/x", h, p, false); expectEquals (h, juce::String ("cam.local")); expectEquals (p, 81);
            http::splitHostPort ("[fe80::1]:8443", h, p, true);      expectEquals (h, juce::String ("fe80::1")); expectEquals (p, 8443);
            http::splitHostPort (" cam ", h, p, false);              expectEquals (h, juce::String ("cam"));
            http::splitHostPort ("bob:pw@cam:81", h, p, true);       expectEquals (h, juce::String ("cam")); expectEquals (p, 81);

            beginTest ("host entry restricts the cascade");
            auto spec = http::parseHost ("192.0.2.10");
            expect (spec.allowHttps && spec.allowHttp); expectEquals (spec.port, 0); expectEquals (spec.host, juce::String ("192.0.2.10"));
            spec = http::parseHost ("192.0.2.10:443");   expect (spec.allowHttps && ! spec.allowHttp); expectEquals (spec.port, 443);
            spec = http::parseHost ("192.0.2.10:80");    expect (! spec.allowHttps && spec.allowHttp); expectEquals (spec.port, 80);
            spec = http::parseHost ("192.0.2.10:8080");  expect (spec.allowHttps && spec.allowHttp); expectEquals (spec.port, 8080);
            spec = http::parseHost ("https://cam");     expect (spec.allowHttps && ! spec.allowHttp); expectEquals (spec.host, juce::String ("cam"));
            spec = http::parseHost ("HTTP://cam/x");    expect (! spec.allowHttps && spec.allowHttp);
            spec = http::parseHost ("http://cam:443");  expect (! spec.allowHttps && spec.allowHttp); expectEquals (spec.port, 443);
            spec = http::parseHost ("[fe80::1]:443");   expect (spec.allowHttps && ! spec.allowHttp); expectEquals (spec.host, juce::String ("fe80::1"));

            beginTest ("credentials in the host entry are stripped");
            expectEquals (http::stripUserInfo ("root:secret@192.0.2.10"), juce::String ("192.0.2.10"));
            expectEquals (http::stripUserInfo ("https://root:se@cret@cam:8443/x"), juce::String ("https://cam:8443/x"));
            expectEquals (http::stripUserInfo ("cam/path@x"), juce::String ("cam/path@x"));
            expectEquals (http::stripUserInfo (" cam "), juce::String ("cam"));
            {
                CameraConnector cam;
                CameraSettings s;
                s.host = "http://root:secret@cam:81";
                cam.setSettings (s);
                expectEquals (cam.getSettings().host, juce::String ("http://cam:81"));
            }
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

            beginTest ("JPEG size guard");
            {
                int w = 0, h = 0;
                const auto j = makeJpeg (320, 180, 1);
                const auto* d = static_cast<const juce::uint8*> (j.getData());
                expect (MjpegParser::jpegDimensions (d, j.getSize(), w, h));
                expectEquals (w, 320); expectEquals (h, 180);
                expect (MjpegParser::isDecodableSize (d, j.getSize()));

                const auto huge = withDimensions (j, 65000, 65000);
                expect (MjpegParser::jpegDimensions (static_cast<const juce::uint8*> (huge.getData()), huge.getSize(), w, h));
                expectEquals (w, 65000);
                expect (! MjpegParser::isDecodableSize (static_cast<const juce::uint8*> (huge.getData()), huge.getSize()));

                const auto wide = withDimensions (j, 4097, 16);
                expect (! MjpegParser::isDecodableSize (static_cast<const juce::uint8*> (wide.getData()), wide.getSize()));
                const auto edge = withDimensions (j, 4096, 4096);
                expect (MjpegParser::isDecodableSize (static_cast<const juce::uint8*> (edge.getData()), edge.getSize()));

                const juce::uint8 garbage[] = { 0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x01, 0x00 };
                expect (! MjpegParser::jpegDimensions (garbage, sizeof (garbage), w, h));
                expect (! MjpegParser::jpegDimensions (d, 20, w, h), "header cut before SOF");
                expect (! MjpegParser::isDecodableSize (d + 2, j.getSize() - 2), "no SOI");
            }

            beginTest ("MJPEG parser, recorded Axis framing fixture");
            {
                // Framing, part headers and JPEG header segments (APP0, Exif APP1, COM) as sent
                // by an AXIS P3265-V at 320x180; picture content synthetic and Exif anonymised.
                // The recording ends in the middle of the sixth part.
                const auto file = juce::File (__FILE__).getSiblingFile ("fixtures").getChildFile ("axis_320x180.mjpg");
                juce::MemoryBlock data;
                expect (file.loadFileAsData (data), "fixture " + file.getFullPathName());

                for (const int maxChunk : { 1, 7, 1000, 100000 })
                {
                    const auto got = parseInChunks (MjpegParser::boundaryFromContentType ("multipart/x-mixed-replace; boundary=myboundary"),
                                                    data, rng, maxChunk);
                    expectEquals ((int) got.size(), 5, "fixture frames, chunk " + juce::String (maxChunk));

                    for (const auto& f : got)
                    {
                        juce::MemoryInputStream in (f, false);
                        juce::JPEGImageFormat fmt;
                        const auto img = fmt.decodeImage (in);
                        expect (img.getWidth() == 320 && img.getHeight() == 180);
                        expect (MjpegParser::isDecodableSize (static_cast<const juce::uint8*> (f.getData()), f.getSize()));
                    }
                }
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

                // Apart from the unauthenticated reachability probe ("GET /"), the first
                // request is the stream with the remembered resolution: no param.cgi discovery.
                juce::StringArray log;
                for (const auto& l : server.getRequestLog())
                    if (l != "GET /")
                        log.add (l);
                expect (log.size() >= 1);
                expectEquals (log[0], juce::String ("GET /axis-cgi/mjpg/video.cgi?resolution=320x180&fps=25"));
                expect (! server.getRequestLog().joinIntoString ("|").contains ("param.cgi"));
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
                expect (server.getRequestLog().contains ("GET /video.mjpg?resolution=640x360"), "path kept as entered");
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
                note ("  no-image detection took " + juce::String (dt) + " ms");
                expect (dt >= 4500 && dt < 7000, "no-image detection took " + juce::String (dt) + " ms");
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

            beginTest ("no Basic to a Digest-only server, credentials only after a challenge");
            {
                MockCameraServer::Config cfg;
                cfg.auth = MockCameraServer::Auth::digest;
                cfg.jpegs = jpegs;
                MockCameraServer server (cfg);

                CameraConnector cam;
                cam.setSettings (settingsFor (server, "user", "typo"));
                cam.connect (true);
                expect (waitForSettled (cam, 15000));
                expectEquals (cam.getErrorMessage(), juce::String ("Benutzername oder Passwort falsch"));

                const auto authLog = server.getAuthLog();
                expect (authLog.size() > 0);
                for (const auto& l : authLog)
                    expect (! l.endsWith (" Basic"), "Basic sent: " + l);
                expect (authLog.joinIntoString ("|").contains (" Digest"), "Digest was tried");
            }

            {
                MockCameraServer::Config cfg;
                cfg.auth = MockCameraServer::Auth::basic;
                cfg.jpegs = jpegs;
                MockCameraServer server (cfg);

                CameraConnector cam;
                cam.setSettings (settingsFor (server));
                cam.connect (true);
                expect (waitFor ([&] { return cam.getStatus() == Status::streaming; }, 15000));

                // Every path that got Basic was asked for without credentials first.
                juce::StringArray challenged;
                int basicCount = 0;
                for (const auto& l : server.getAuthLog())
                {
                    const auto path = l.upToLastOccurrenceOf (" ", false, false);
                    if (l.endsWith (" -"))
                        challenged.addIfNotAlreadyThere (path);
                    else if (l.endsWith (" Basic"))
                    {
                        ++basicCount;
                        expect (challenged.contains (path), "unsolicited Basic for " + path);
                    }
                }
                expect (basicCount > 0);
            }

            beginTest ("Basic + Digest offered (like Axis over HTTP): Digest wins");
            {
                MockCameraServer::Config cfg;
                cfg.auth = MockCameraServer::Auth::both;
                cfg.jpegs = jpegs;
                MockCameraServer server (cfg);

                CameraConnector cam;
                cam.setSettings (settingsFor (server));
                cam.connect (true);
                expect (waitForSettled (cam, 15000));
                expect (cam.getStatus() == Status::streaming, cam.getErrorMessage());
                expectEquals (cam.getSettings().auth, juce::String ("digest"));
                expect (! server.getAuthLog().joinIntoString ("|").contains (" Basic"));
            }

            beginTest ("status mapping: 403, 400, 500, param.cgi 403");
            {
                auto failWith = [&] (int streamStatus) -> juce::String
                {
                    MockCameraServer::Config cfg;
                    cfg.auth = MockCameraServer::Auth::basic;
                    cfg.jpegs = jpegs;
                    cfg.streamStatus = streamStatus;
                    MockCameraServer server (cfg);

                    CameraConnector cam;
                    cam.setSettings (settingsFor (server));
                    cam.connect (true);
                    expect (waitForSettled (cam, 15000));
                    expect (cam.getStatus() == Status::failed);
                    return cam.getErrorMessage();
                };

                expectEquals (failWith (403), juce::String ("Benutzername oder Passwort falsch"));
                expectEquals (failWith (400), juce::String ("Stream-Pfad nicht gefunden"));
                expectEquals (failWith (500), juce::String ("Stream-Pfad nicht gefunden"));

                // A user without rights for param.cgi still gets the stream (path as entered).
                MockCameraServer::Config cfg;
                cfg.auth = MockCameraServer::Auth::basic;
                cfg.jpegs = jpegs;
                cfg.paramStatus = 403;
                MockCameraServer server (cfg);
                CameraConnector cam;
                cam.setSettings (settingsFor (server));
                cam.connect (true);
                expect (waitForSettled (cam, 15000));
                expect (cam.getStatus() == Status::streaming, cam.getErrorMessage());
                expect (server.getRequestLog().contains ("GET " + CameraSettings::defaultStreamPath()));
                expectEquals (cam.getSettings().resolution, juce::String());
            }

            beginTest ("scheme prefix restricts the cascade");
            {
                MockCameraServer::Config cfg;
                cfg.auth = MockCameraServer::Auth::basic;
                cfg.jpegs = jpegs;
                MockCameraServer server (cfg);

                {
                    CameraConnector cam;
                    auto s = settingsFor (server);
                    s.host = "http://" + s.host;
                    cam.setSettings (s);
                    cam.connect (true);
                    expect (waitForSettled (cam, 15000));
                    expect (cam.getStatus() == Status::streaming, cam.getErrorMessage());
                    expectEquals (server.getTlsRejects(), 0, "no HTTPS attempt for http://");
                    cam.disconnect();
                }

                {
                    const auto before = server.getRequestLog().size();
                    CameraConnector cam;
                    auto s = settingsFor (server);
                    s.host = "https://" + s.host;
                    cam.setSettings (s);
                    cam.connect (true);
                    expect (waitForSettled (cam, 15000));
                    expect (cam.getStatus() == Status::failed);
                    expectEquals (cam.getErrorMessage(), "Kamera unter " + s.host + " nicht erreichbar");
                    expectEquals (server.getRequestLog().size(), before, "no plain HTTP request for https://");
                    expect (server.getTlsRejects() >= 1);
                }
            }

            beginTest ("oversized JPEG is never decoded");
            {
                MockCameraServer::Config cfg;
                cfg.auth = MockCameraServer::Auth::basic;
                cfg.jpegs = { withDimensions (jpegs[0], 65000, 65000) };
                MockCameraServer server (cfg);

                CameraConnector cam;
                cam.setSettings (settingsFor (server));
                const auto t0 = juce::Time::getMillisecondCounter();
                cam.connect (true);
                expect (waitForSettled (cam, 15000));
                const auto dt = juce::Time::getMillisecondCounter() - t0;
                expect (cam.getStatus() == Status::failed);
                expectEquals (cam.getErrorMessage(), errorMessageFor (ConnectFailure::noImage, {}));
                expect (dt < 7000, "oversized stream rejected after " + juce::String ((int) dt) + " ms");
                expectEquals ((int) cam.getFrameCount(), 0);

                // Silent mode keeps receiving the bad frames; teardown must stay fast.
                cam.connect (false);
                juce::Thread::sleep (1500);
                const auto t1 = juce::Time::getMillisecondCounter();
                cam.disconnect();
                const auto td = juce::Time::getMillisecondCounter() - t1;
                expect (td < 1000, "disconnect while receiving oversized frames took " + juce::String ((int) td) + " ms");
            }

            beginTest ("black hole (listening, never answering) fails within ~2 s");
            {
                juce::StreamingSocket listener; // completes TCP handshakes in the backlog, never answers
                expect (listener.createListener (0, "127.0.0.1"));

                CameraConnector cam;
                CameraSettings s;
                s.host = "127.0.0.1:" + juce::String (listener.getBoundPort());
                s.user = "user"; s.password = "secret";
                cam.setSettings (s);
                const auto t0 = juce::Time::getMillisecondCounter();
                cam.connect (true);
                expect (waitForSettled (cam, 20000));
                const auto dt = juce::Time::getMillisecondCounter() - t0;
                note ("  black hole cascade took " + juce::String ((int) dt) + " ms");
                expect (cam.getStatus() == Status::failed);
                expectEquals (cam.getErrorMessage(), "Kamera unter " + s.host + " nicht erreichbar");
                expect (dt <= 3000, "black hole took " + juce::String ((int) dt) + " ms");
            }

            beginTest ("silent mode stops on wrong credentials");
            {
                MockCameraServer::Config cfg;
                cfg.auth = MockCameraServer::Auth::digest;
                cfg.jpegs = jpegs;
                MockCameraServer server (cfg);

                CameraConnector cam;
                cam.setSettings (settingsFor (server, "user", "changed-on-camera"));
                cam.connect (false);
                expect (waitFor ([&] { return cam.getStatus() == Status::failed; }, 15000));
                expectEquals (cam.getErrorMessage(), juce::String ("Benutzername oder Passwort falsch"));
                const auto requests = server.getRequestLog().size();
                juce::Thread::sleep (1500);
                expectEquals (server.getRequestLog().size(), requests, "no further login attempts");
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
