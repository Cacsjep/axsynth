#include "SetupPanel.h"

namespace axsynth
{
    namespace
    {
        constexpr int formWidth = 300;
        constexpr int labelH = 18, fieldH = 30, gap = 10, buttonH = 32, errorH = 20;

        const char* const labels[] { "Host / IP", "Benutzer", "Passwort", "Stream-Pfad" };
    }

    SetupPanel::SetupPanel (AxSynthProcessor& p) : processor (p)
    {
        for (auto* te : { &host, &user, &password, &streamPath })
        {
            te->setFont (theme::font (14.0f));
            te->setIndents (8, 7);
            te->setSelectAllWhenFocused (true);
            te->onReturnKey = [this] { connect(); };
            te->onEscapeKey = [te] { te->giveAwayKeyboardFocus(); };
            addAndMakeVisible (te);
        }
        password.setPasswordCharacter ((juce::juce_wchar) 0x2022);

        setMouseClickGrabsKeyboardFocus (false);
        connectButton.setWantsKeyboardFocus (false);
        connectButton.setButtonText ("Verbinden");
        connectButton.onClick = [this] { connect(); };
        addAndMakeVisible (connectButton);

        loadSettings();
        updateStatus();
    }

    void SetupPanel::loadSettings()
    {
        const auto s = processor.getCamera().getSettings();
        host.setText (s.host, false);
        user.setText (s.user, false);
        password.setText (s.password, false);
        streamPath.setText (s.streamPath.isNotEmpty() ? s.streamPath : CameraSettings::defaultStreamPath(), false);
        for (auto* te : { &host, &user, &password, &streamPath })
            te->moveCaretToTop (false);
    }

    CameraSettings SetupPanel::makeSettings (const CameraSettings& current, const juce::String& h,
                                             const juce::String& u, const juce::String& pw,
                                             const juce::String& path)
    {
        auto s = current;
        const bool identityChanged = s.host != h || s.user != u || s.password != pw;
        s.host = h;
        s.user = u;
        s.password = pw;
        s.streamPath = path.isNotEmpty() ? path : CameraSettings::defaultStreamPath();
        if (identityChanged)
        {
            s.scheme = {};
            s.auth = {};
            s.resolution = {};
        }
        return s;
    }

    void SetupPanel::connect()
    {
        if (! connectButton.isEnabled())
            return;

        const auto s = makeSettings (processor.getCamera().getSettings(), host.getText().trim(), user.getText().trim(),
                                     password.getText(), streamPath.getText().trim());
        if (! s.hasCredentials())
        {
            // Focus the first empty field.
            for (auto* te : { &host, &user, &password })
                if (te->getText().trim().isEmpty())
                {
                    te->grabKeyboardFocus();
                    break;
                }
            return;
        }

        processor.connectCamera (s);
        if (onConnect)
            onConnect();
        updateStatus();
    }

    void SetupPanel::updateStatus()
    {
        using S = CameraConnector::Status;
        const auto status = processor.getCamera().getStatus();
        const bool connecting = status == S::connecting;

        connectButton.setEnabled (! connecting);
        connectButton.setButtonText (connecting ? juce::String (juce::CharPointer_UTF8 ("Verbinde\xe2\x80\xa6"))
                                                : juce::String ("Verbinden"));

        const auto err = status == S::failed ? processor.getCamera().getErrorMessage() : juce::String();
        if (err != errorText)
        {
            errorText = err;
            repaint();
        }
    }

    void SetupPanel::resized()
    {
        const int formH = 4 * (labelH + fieldH + gap) + gap + buttonH + errorH;
        form = juce::Rectangle<int> (formWidth, formH).withCentre (getLocalBounds().getCentre());

        auto r = form;
        for (auto* te : { &host, &user, &password, &streamPath })
        {
            r.removeFromTop (labelH);
            te->setBounds (r.removeFromTop (fieldH));
            r.removeFromTop (gap);
        }
        r.removeFromTop (gap);
        connectButton.setBounds (r.removeFromTop (buttonH));
    }

    void SetupPanel::paint (juce::Graphics& g)
    {
        g.fillAll (theme::cameraBg.withAlpha (0.90f));

        int y = form.getY();
        for (auto* l : labels)
        {
            theme::drawCapsLabel (g, l, juce::Rectangle<int> (form.getX(), y, form.getWidth(), labelH - 4).toFloat(),
                                  juce::Justification::centredLeft);
            y += labelH + fieldH + gap;
        }

        if (errorText.isNotEmpty())
        {
            g.setColour (theme::text);
            g.setFont (theme::font (12.5f));
            g.drawText (errorText, connectButton.getBounds().translated (0, buttonH + 4).withHeight (errorH)
                                       .expanded (60, 0),
                        juce::Justification::centred, true);
        }
    }
}
