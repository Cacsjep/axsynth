// STUB - replaced by the real implementation (spec section 4).
#include "CameraConnector.h"

namespace axsynth
{
    struct CameraConnector::Impl
    {
        mutable juce::CriticalSection lock;
        CameraSettings settings;
        juce::String error;
        juce::WaitableEvent frameEvent;
    };

    CameraConnector::CameraConnector() : impl (std::make_unique<Impl>()) {}
    CameraConnector::~CameraConnector() = default;

    void CameraConnector::setSettings (const CameraSettings& s) { const juce::ScopedLock sl (impl->lock); impl->settings = s; }
    CameraSettings CameraConnector::getSettings() const { const juce::ScopedLock sl (impl->lock); return impl->settings; }
    void CameraConnector::connect (bool) {}
    void CameraConnector::disconnect() { status = Status::idle; }
    juce::String CameraConnector::getErrorMessage() const { const juce::ScopedLock sl (impl->lock); return impl->error; }
    bool CameraConnector::popLatestFrame (juce::Image&) { return false; }
    bool CameraConnector::waitForFrame (int timeoutMs) { return impl->frameEvent.wait (timeoutMs); }

    juce::String pickResolution (const juce::String&, const juce::String&) { return {}; }
    juce::String applyResolution (const juce::String& p, const juce::String&) { return p; }
    juce::String errorMessageFor (ConnectFailure, const juce::String&) { return {}; }
    juce::String dpapiProtect (const juce::String& s) { return s; }
    juce::String dpapiUnprotect (const juce::String& s) { return s; }
}
