#pragma once

#include "Theme.h"
#include "../PluginProcessor.h"

namespace axsynth
{
    // Semi-transparent camera setup form shown over the camera image.
    class SetupPanel : public juce::Component
    {
    public:
        explicit SetupPanel (AxSynthProcessor&);

        // Fills the fields from the current camera settings.
        void loadSettings();
        // Reflects the connector status (button state, error line). Message thread, ~30 Hz.
        void updateStatus();

        // Builds settings from the fields; clears learned scheme/auth/resolution when
        // host, user or password changed.
        static CameraSettings makeSettings (const CameraSettings& current, const juce::String& host,
                                            const juce::String& user, const juce::String& password,
                                            const juce::String& streamPath);

        std::function<void()> onConnect;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void connect();

        AxSynthProcessor& processor;
        juce::TextEditor host, user, password, streamPath;
        juce::TextButton connectButton;
        juce::String errorText;
        juce::Rectangle<int> form;
    };
}
