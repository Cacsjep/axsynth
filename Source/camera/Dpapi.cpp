#include "CameraConnector.h"

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincrypt.h>

namespace axsynth
{
    juce::String dpapiProtect (const juce::String& plain)
    {
        if (plain.isEmpty())
            return {};

        const auto utf8 = plain.toStdString();
        DATA_BLOB in { (DWORD) utf8.size(), (BYTE*) const_cast<char*> (utf8.data()) };
        DATA_BLOB out {};

        if (! CryptProtectData (&in, L"AxSynth", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
            return {};

        auto result = juce::Base64::toBase64 (out.pbData, out.cbData);
        SecureZeroMemory (out.pbData, out.cbData);
        LocalFree (out.pbData);
        return result;
    }

    juce::String dpapiUnprotect (const juce::String& base64)
    {
        if (base64.isEmpty())
            return {};

        juce::MemoryOutputStream decoded;
        if (! juce::Base64::convertFromBase64 (decoded, base64) || decoded.getDataSize() == 0)
            return {};

        DATA_BLOB in { (DWORD) decoded.getDataSize(), (BYTE*) const_cast<void*> (decoded.getData()) };
        DATA_BLOB out {};

        if (! CryptUnprotectData (&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
            return {};

        auto result = juce::String::fromUTF8 ((const char*) out.pbData, (int) out.cbData);
        SecureZeroMemory (out.pbData, out.cbData);
        LocalFree (out.pbData);
        return result;
    }
}
