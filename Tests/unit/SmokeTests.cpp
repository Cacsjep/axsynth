#include "PluginProcessor.h"
#include "ui/PluginEditor.h"
#include <cmath>

namespace axsynth
{
    namespace
    {
        // Magnitude of one frequency via Goertzel on a Hann-windowed signal.
        double toneMagnitude (const std::vector<float>& x, double freq, double sampleRate)
        {
            const auto n = x.size();
            const double w = juce::MathConstants<double>::twoPi * freq / sampleRate;
            const double coeff = 2.0 * std::cos (w);
            double s1 = 0.0, s2 = 0.0;
            for (size_t i = 0; i < n; ++i)
            {
                const double win = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * (double) i / (double) (n - 1));
                const double s = (double) x[i] * win + coeff * s1 - s2;
                s2 = s1;
                s1 = s;
            }
            return std::sqrt (juce::jmax (0.0, s1 * s1 + s2 * s2 - coeff * s1 * s2));
        }

        // Something camera-like: sky gradient, horizon, a few bright and dark shapes.
        juce::Image makeTestImage()
        {
            juce::Image img (juce::Image::RGB, 640, 360, true);
            juce::Graphics g (img);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff8fb4d8), 0.0f, 0.0f,
                                                     juce::Colour (0xffd9e4ee), 0.0f, 200.0f, false));
            g.fillRect (0, 0, 640, 200);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff5d6b4a), 0.0f, 200.0f,
                                                     juce::Colour (0xff2c3324), 0.0f, 360.0f, false));
            g.fillRect (0, 200, 640, 160);

            g.setColour (juce::Colour (0xff3a3f47));
            g.fillRect (90, 110, 150, 130);
            g.fillRect (380, 70, 110, 170);
            g.setColour (juce::Colour (0xfff2e7c4));
            for (int r = 0; r < 4; ++r)
                for (int c = 0; c < 3; ++c)
                    g.fillRect (395 + c * 32, 85 + r * 38, 18, 22);
            for (int c = 0; c < 4; ++c)
                g.fillRect (102 + c * 35, 130, 20, 26);

            g.setColour (juce::Colour (0xfffff4d0));
            g.fillEllipse (540.0f, 30.0f, 50.0f, 50.0f);
            g.setColour (juce::Colour (0xff1c1f17));
            g.fillRect (270, 150, 8, 140);
            g.fillEllipse (240.0f, 110.0f, 70.0f, 70.0f);
            return img;
        }

        bool hasVariedPixels (const juce::Image& img)
        {
            const auto first = img.getPixelAt (0, 0);
            for (int y = 0; y < img.getHeight(); y += 7)
                for (int x = 0; x < img.getWidth(); x += 7)
                    if (img.getPixelAt (x, y) != first)
                        return true;
            return false;
        }

        void writeSnapshot (const juce::Image& img, const juce::String& name)
        {
            const auto dir = juce::SystemStats::getEnvironmentVariable ("AXSYNTH_SNAPSHOT_DIR", {});
            if (dir.isEmpty())
                return;

            juce::File f = juce::File (dir).getChildFile (name);
            f.getParentDirectory().createDirectory();
            f.deleteFile();
            juce::FileOutputStream out (f);
            if (out.openedOk())
                juce::PNGImageFormat().writeImageToStream (img, out);
        }

        void setParam (AxSynthProcessor& p, const char* id, float value)
        {
            auto* param = p.getState().getParameter (id);
            param->setValueNotifyingHost (param->convertTo0to1 (value));
        }
    }

    class SmokeTests : public juce::UnitTest
    {
    public:
        SmokeTests() : juce::UnitTest ("Smoke", "smoke") {}

        void runLiveEndToEnd();

        void runTest() override
        {
            beginTest ("Audio: A4 with no camera is a clean sine");
            {
                AxSynthProcessor proc;
                constexpr double sr = 48000.0;
                constexpr int block = 512;
                proc.setRateAndBufferSizeDetails (sr, block);
                proc.prepareToPlay (sr, block);

                juce::AudioBuffer<float> buffer (2, block);
                juce::MidiBuffer midi;
                std::vector<float> out;

                const int numBlocks = (int) (0.5 * sr) / block;
                for (int b = 0; b < numBlocks; ++b)
                {
                    buffer.clear();
                    midi.clear();
                    if (b == 0)
                        midi.addEvent (juce::MidiMessage::noteOn (1, 69, (juce::uint8) 100), 0);
                    proc.processBlock (buffer, midi);
                    out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + block);
                }

                bool finite = true;
                double sumSq = 0.0;
                for (auto s : out)
                {
                    finite = finite && std::isfinite (s);
                    sumSq += (double) s * s;
                }
                expect (finite, "NaN/inf in output");
                const double rms = std::sqrt (sumSq / (double) out.size());
                expect (rms > 0.01, "output is silent, rms=" + juce::String (rms));

                std::vector<float> tail (out.end() - 8192, out.end());
                const double fund = toneMagnitude (tail, 440.0, sr);
                expect (fund > 0.0);
                for (int k = 2; k <= 10; ++k)
                {
                    const double h = toneMagnitude (tail, 440.0 * k, sr);
                    const double db = juce::Decibels::gainToDecibels (h / fund, -200.0);
                    expect (db < -30.0, "harmonic " + juce::String (k) + " at " + juce::String (db, 1) + " dB");
                }
                // 440 Hz dominates nearby off-harmonic frequencies too.
                expect (toneMagnitude (tail, 300.0, sr) < fund * 0.03);
                expect (toneMagnitude (tail, 660.0, sr) < fund * 0.03);

                proc.releaseResources();
            }

            beginTest ("Editor renders and opens setup without credentials");
            {
                AxSynthProcessor proc;
                proc.prepareToPlay (48000.0, 512);

                std::unique_ptr<juce::AudioProcessorEditor> base (proc.createEditor());
                auto* editor = dynamic_cast<AxSynthEditor*> (base.get());
                expect (editor != nullptr);
                if (editor == nullptr)
                    return;

                editor->setSize (1000, 620);
                editor->refresh();
                expect (editor->isSetupPanelVisible(), "setup panel should be open without credentials");
                expect (! editor->isCameraRectVisible(), "rectangle must be hidden behind the setup panel");

                auto snap = editor->createComponentSnapshot (editor->getLocalBounds());
                expectEquals (snap.getWidth(), 1000);
                expect (hasVariedPixels (snap), "snapshot is a single colour");
                writeSnapshot (snap, "editor_setup.png");

                // Camera-like image (injected, not a camera stream): overlay drawn over it.
                // The panel only auto-closes on a frame of a real connection (live test).
                proc.getEngine().injectImageForTesting (makeTestImage());
                setParam (proc, params::position, 14.0f / 31.0f);
                juce::Thread::sleep (800);

                juce::AudioBuffer<float> buffer (2, 512);
                juce::MidiBuffer midi;
                midi.addEvent (juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100), 0);
                for (int i = 0; i < 4; ++i)
                {
                    buffer.clear();
                    proc.processBlock (buffer, midi);
                    midi.clear();
                }

                editor->refresh();
                expect (editor->isSetupPanelVisible(), "an injected image is not a camera connection");
                editor->setSetupPanelVisible (false);
                expect (! editor->isSetupPanelVisible());
                expect (editor->isCameraRectVisible(), "rectangle back when the setup panel closes");

                snap = editor->createComponentSnapshot (editor->getLocalBounds());
                expect (hasVariedPixels (snap));
                writeSnapshot (snap, "editor_image.png");

                editor->setSize (1500, 930);
                editor->refresh();
                snap = editor->createComponentSnapshot (editor->getLocalBounds());
                expectEquals (snap.getWidth(), 1500);
                writeSnapshot (snap, "editor_image_1500.png");

                editor->setSize (1000, 620);
                editor->setSetupPanelVisible (true);
                snap = editor->createComponentSnapshot (editor->getLocalBounds());
                writeSnapshot (snap, "editor_image_setup.png");

                base.reset(); // editor before processor
            }

            beginTest ("State round-trip keeps parameters and rectangle");
            {
                juce::MemoryBlock state;
                const juce::Rectangle<float> rect { 0.11f, 0.23f, 0.47f, 0.31f };
                {
                    AxSynthProcessor a;
                    setParam (a, params::position, 0.3f);
                    setParam (a, params::cutoff, 1234.0f);
                    setParam (a, params::release, 1.5f);
                    setParam (a, params::lfoSync, 1.0f);
                    setParam (a, params::lfoRateSync, 4.0f);
                    a.getEngine().setRect (rect);
                    a.getStateInformation (state);
                }

                AxSynthProcessor b;
                b.setStateInformation (state.getData(), (int) state.getSize());
                auto& s = b.getState();
                expectWithinAbsoluteError (s.getRawParameterValue (params::position)->load(), 0.3f, 1.0e-4f);
                expectWithinAbsoluteError (s.getRawParameterValue (params::cutoff)->load(), 1234.0f, 0.5f);
                expectWithinAbsoluteError (s.getRawParameterValue (params::release)->load(), 1.5f, 1.0e-3f);
                expectWithinAbsoluteError (s.getRawParameterValue (params::lfoSync)->load(), 1.0f, 1.0e-6f);
                expectWithinAbsoluteError (s.getRawParameterValue (params::lfoRateSync)->load(), 4.0f, 1.0e-6f);
                const auto r = b.getEngine().getRect();
                expectWithinAbsoluteError (r.getX(), rect.getX(), 1.0e-5f);
                expectWithinAbsoluteError (r.getY(), rect.getY(), 1.0e-5f);
                expectWithinAbsoluteError (r.getWidth(), rect.getWidth(), 1.0e-5f);
                expectWithinAbsoluteError (r.getHeight(), rect.getHeight(), 1.0e-5f);
            }

            beginTest ("State keeps camera settings, password encrypted");
            {
                juce::MemoryBlock state;
                {
                    AxSynthProcessor a;
                    CameraSettings cs;
                    cs.host = "192.0.2.10"; cs.user = "operator"; cs.password = "s3cret-pw"; cs.streamPath = "/custom";
                    a.getCamera().setSettings (cs);
                    a.getStateInformation (state);

                    juce::MemoryBlock again;
                    a.getStateInformation (again);
                    expect (again == state, "saving twice must give identical state (no spurious 'modified')");
                }

                expect (! state.toString().contains ("s3cret-pw"), "password stored in plain text");

                AxSynthProcessor b;
                b.setStateInformation (state.getData(), (int) state.getSize());
                const auto s = b.getCamera().getSettings();
                expectEquals (s.host, juce::String ("192.0.2.10"));
                expectEquals (s.user, juce::String ("operator"));
                expectEquals (s.password, juce::String ("s3cret-pw"));
                expectEquals (s.streamPath, juce::String ("/custom"));
                b.getCamera().disconnect();
            }

            beginTest ("Corrupt state: NaN parameter values are replaced, audio stays finite");
            {
                juce::MemoryBlock state;
                {
                    AxSynthProcessor a;
                    a.getStateInformation (state);
                }
                auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
                expect (xml != nullptr);
                if (xml == nullptr)
                    return;
                for (auto* p : xml->getChildIterator())
                    if (p->getStringAttribute ("id") == params::cutoff || p->getStringAttribute ("id") == params::lfoRate)
                        p->setAttribute ("value", "nan");
                juce::MemoryBlock bad;
                juce::AudioProcessor::copyXmlToBinary (*xml, bad);

                AxSynthProcessor b;
                b.prepareToPlay (48000.0, 512);
                b.setStateInformation (bad.getData(), (int) bad.getSize());
                expect (std::isfinite (b.getState().getRawParameterValue (params::cutoff)->load()));

                // A NaN that reaches the processor anyway (host) must not poison the output.
                b.getState().getRawParameterValue (params::lfoRate)->store (std::numeric_limits<float>::quiet_NaN());
                juce::AudioBuffer<float> buffer (2, 512);
                juce::MidiBuffer midi;
                midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
                bool finite = true;
                float mag = 0.0f;
                for (int i = 0; i < 20; ++i)
                {
                    buffer.clear();
                    b.processBlock (buffer, midi);
                    midi.clear();
                    for (int k = 0; k < 512; ++k)
                        finite = finite && std::isfinite (buffer.getSample (0, k));
                    mag = juce::jmax (mag, buffer.getMagnitude (0, 0, 512));
                }
                expect (finite, "output contains NaN/inf");
                expectGreaterThan (mag, 0.01f);
                b.releaseResources();
            }

            beginTest ("State without Rect child resets the rectangle to the default");
            {
                AxSynthProcessor a;
                a.getEngine().setRect ({ 0.1f, 0.1f, 0.3f, 0.3f });
                juce::ValueTree t ("AxSynth");
                auto xml = t.createXml();
                juce::MemoryBlock mb;
                juce::AudioProcessor::copyXmlToBinary (*xml, mb);
                a.setStateInformation (mb.getData(), (int) mb.getSize());
                expect (a.getEngine().getRect() == WavetableEngine::defaultRect());
            }

            beginTest ("Presets: names, apply sets listed values and resets the rest");
            {
                AxSynthProcessor proc;
                expectGreaterThan (proc.getNumPrograms(), 5);
                juce::StringArray names;
                for (int i = 0; i < proc.getNumPrograms(); ++i)
                    names.add (proc.getProgramName (i));
                for (auto n : { "Init", "Pad", "Synth", "Pluck", "Bass" })
                    expect (names.contains (n), juce::String ("missing preset ") + n);

                const int pluck = names.indexOf ("Pluck");
                setParam (proc, params::lfoAmount, 0.9f);     // not listed in Pluck -> back to default
                proc.setCurrentProgram (pluck);
                expectEquals (proc.getCurrentProgram(), pluck);
                auto& s = proc.getState();
                expectWithinAbsoluteError (s.getRawParameterValue (params::sustain)->load(), 0.0f, 1.0e-4f);
                expectWithinAbsoluteError (s.getRawParameterValue (params::filterEnv)->load(), 0.7f, 1.0e-3f);
                expectWithinAbsoluteError (s.getRawParameterValue (params::lfoAmount)->load(), 0.0f, 1.0e-4f);

                proc.setCurrentProgram (names.indexOf ("Init"));
                expectWithinAbsoluteError (s.getRawParameterValue (params::sustain)->load(), 1.0f, 1.0e-4f);
            }

            runLiveEndToEnd();

            beginTest ("Setup: changed identity clears learned connection details");
            {
                CameraSettings cur;
                cur.host = "192.0.2.10"; cur.user = "root"; cur.password = "pw";
                cur.scheme = "https"; cur.auth = "digest"; cur.resolution = "320x180";

                auto same = SetupPanel::makeSettings (cur, "192.0.2.10", "root", "pw", "/x");
                expectEquals (same.scheme, juce::String ("https"));
                expectEquals (same.streamPath, juce::String ("/x"));

                auto changed = SetupPanel::makeSettings (cur, "192.0.2.11", "root", "pw", "/x");
                expect (changed.scheme.isEmpty() && changed.auth.isEmpty() && changed.resolution.isEmpty());
            }
        }
    };

    // ---- Live end-to-end (only with AXSYNTH_TEST_CAMERA="host;user;password") ----

    namespace
    {
        // Relative level (dB) of harmonics 1..12 of a note, from 0.5 s of output.
        std::vector<double> harmonicProfile (AxSynthProcessor& proc, int note)
        {
            constexpr double sr = 48000.0;
            constexpr int block = 512;
            juce::AudioBuffer<float> buffer (2, block);
            juce::MidiBuffer midi;
            std::vector<float> out;
            midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);
            for (int b = 0; b < (int) (0.5 * sr) / block; ++b)
            {
                buffer.clear();
                proc.processBlock (buffer, midi);
                midi.clear();
                out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + block);
            }
            midi.addEvent (juce::MidiMessage::noteOff (1, note), 0);
            for (int b = 0; b < 100; ++b) { buffer.clear(); proc.processBlock (buffer, midi); midi.clear(); }

            const double f0 = 440.0 * std::pow (2.0, (note - 69) / 12.0);
            std::vector<float> tail (out.end() - 8192, out.end());
            const double fund = toneMagnitude (tail, f0, sr);
            std::vector<double> db;
            for (int k = 1; k <= 12; ++k)
                db.push_back (juce::Decibels::gainToDecibels (toneMagnitude (tail, f0 * k, sr) / fund, -120.0));
            return db;
        }

        bool waitFor (std::function<bool()> cond, int timeoutMs)
        {
            const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
            while (juce::Time::getMillisecondCounter() < end)
            {
                if (cond()) return true;
                juce::Thread::sleep (50);
            }
            return cond();
        }
    }

    void SmokeTests::runLiveEndToEnd()
    {
        const auto spec = juce::SystemStats::getEnvironmentVariable ("AXSYNTH_TEST_CAMERA", {});
        const auto parts = juce::StringArray::fromTokens (spec, ";", {});
        if (parts.size() != 3)
            return;

        beginTest ("Live end-to-end: camera image shapes the sound, state restores the connection");

        juce::MemoryBlock state;
        {
            AxSynthProcessor proc;
            proc.prepareToPlay (48000.0, 512);
            std::unique_ptr<juce::AudioProcessorEditor> base (proc.createEditor());
            auto* editor = dynamic_cast<AxSynthEditor*> (base.get());
            editor->setSize (1000, 620);
            expect (editor->isSetupPanelVisible());

            CameraSettings s;
            s.host = parts[0]; s.user = parts[1]; s.password = parts[2];
            proc.connectCamera (s);

            const bool streaming = waitFor ([&] { return proc.getCamera().getStatus() == CameraConnector::Status::streaming; }, 15000);
            expect (streaming, "camera did not start streaming: " + proc.getCamera().getErrorMessage());
            if (! streaming) return;

            juce::Thread::sleep (1500); // smoothing settles
            editor->refresh();
            expect (! editor->isSetupPanelVisible(), "setup should close on the first camera image");
            expect (proc.getEngine().getDisplayImage().isValid());
            writeSnapshot (editor->createComponentSnapshot (editor->getLocalBounds()), "editor_live.png");

            // Sound is no longer a pure sine: some upper harmonic within 40 dB.
            setParam (proc, params::cutoff, 20000.0f);
            setParam (proc, params::position, 0.5f);
            const auto a = harmonicProfile (proc, 57);
            double strongest = -120.0;
            for (size_t k = 1; k < a.size(); ++k) strongest = juce::jmax (strongest, a[k]);
            logMessage ("  live: strongest upper harmonic " + juce::String (strongest, 1) + " dB");
            expect (strongest > -40.0, "image did not add harmonics");

            // A different region of the image gives a different timbre.
            proc.getEngine().setRect ({ 0.02f, 0.02f, 0.2f, 0.3f });
            juce::Thread::sleep (1500);
            const auto b = harmonicProfile (proc, 57);
            double diff = 0.0;
            for (size_t k = 1; k < a.size(); ++k) diff += std::abs (a[k] - b[k]);
            logMessage ("  live: timbre change after moving rect " + juce::String (diff, 1) + " dB total");
            std::printf ("  live e2e: upper harmonic %.1f dB, timbre change %.1f dB\n", strongest, diff);
            expect (diff > 3.0, "moving the rectangle did not change the sound");

            proc.getStateInformation (state);
            base.reset();
        }

        // DAW restart: new instance restores settings and reconnects by itself.
        AxSynthProcessor restored;
        restored.setStateInformation (state.getData(), (int) state.getSize());
        const auto rs = restored.getCamera().getSettings();
        expectEquals (rs.host, parts[0]);
        expectEquals (rs.password, parts[2]);
        expect (rs.scheme.isNotEmpty() && rs.resolution.isNotEmpty(), "learned connection details not saved");
        expectWithinAbsoluteError (restored.getEngine().getRect().getX(), 0.02f, 1.0e-5f);
        const bool reconnected = waitFor ([&] { return restored.getCamera().getStatus() == CameraConnector::Status::streaming; }, 15000);
        expect (reconnected, "restored instance did not reconnect");
        std::printf ("  live e2e: restored instance reconnected=%d (%s/%s %s)\n", reconnected ? 1 : 0,
                     rs.scheme.toRawUTF8(), rs.auth.toRawUTF8(), rs.resolution.toRawUTF8());
    }

    static SmokeTests smokeTests;
}
