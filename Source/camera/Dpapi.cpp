#include "CameraConnector.h"
#include <juce_cryptography/juce_cryptography.h>
#include <map>
#include <mutex>

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
    namespace
    {
        // CryptProtectData salts every call, so the same password would give new bytes
        // on every getStateInformation. Remember the blob per password (keyed by a hash,
        // the plain text is not kept here) so the plugin state stays byte-identical.
        struct BlobCache
        {
            std::mutex mutex;
            std::map<juce::String, juce::String> blobs; // SHA-256(password) -> base64 blob

            static juce::String key (const juce::String& plain)
            {
                return juce::SHA256 (plain.toUTF8()).toHexString();
            }

            juce::String find (const juce::String& plain)
            {
                const std::lock_guard<std::mutex> lg (mutex);
                const auto it = blobs.find (key (plain));
                return it != blobs.end() ? it->second : juce::String();
            }

            void store (const juce::String& plain, const juce::String& blob)
            {
                const std::lock_guard<std::mutex> lg (mutex);
                if (blobs.size() >= 32)
                    blobs.clear();
                blobs[key (plain)] = blob;
            }
        };

        BlobCache& cache()
        {
            static BlobCache c;
            return c;
        }
    }

    juce::String dpapiProtect (const juce::String& plain)
    {
        if (plain.isEmpty())
            return {};

        if (auto cached = cache().find (plain); cached.isNotEmpty())
            return cached;

        const auto utf8 = plain.toStdString();
        DATA_BLOB in { (DWORD) utf8.size(), (BYTE*) const_cast<char*> (utf8.data()) };
        DATA_BLOB out {};

        if (! CryptProtectData (&in, L"AxSynth", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
            return {};

        auto result = juce::Base64::toBase64 (out.pbData, out.cbData);
        SecureZeroMemory (out.pbData, out.cbData);
        LocalFree (out.pbData);

        cache().store (plain, result);
        return result;
    }

    bool dpapiTryUnprotect (const juce::String& base64, juce::String& plainOut)
    {
        plainOut = {};

        if (base64.isEmpty())
            return true;

        juce::MemoryOutputStream decoded;
        if (! juce::Base64::convertFromBase64 (decoded, base64) || decoded.getDataSize() == 0)
            return false;

        DATA_BLOB in { (DWORD) decoded.getDataSize(), (BYTE*) const_cast<void*> (decoded.getData()) };
        DATA_BLOB out {};

        if (! CryptUnprotectData (&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
            return false;

        plainOut = juce::String::fromUTF8 ((const char*) out.pbData, (int) out.cbData);
        SecureZeroMemory (out.pbData, out.cbData);
        LocalFree (out.pbData);

        // Saving the same password again writes back exactly the blob that was loaded.
        if (plainOut.isNotEmpty())
            cache().store (plainOut, base64);

        return true;
    }

    juce::String dpapiUnprotect (const juce::String& base64)
    {
        juce::String plain;
        return dpapiTryUnprotect (base64, plain) ? plain : juce::String();
    }

    juce::String passwordForState (const CameraSettings& s)
    {
        return s.password.isNotEmpty() ? dpapiProtect (s.password) : s.undecryptablePassword;
    }

    void passwordFromState (CameraSettings& s, const juce::String& blob)
    {
        juce::String plain;
        if (dpapiTryUnprotect (blob, plain))
        {
            s.password = plain;
            s.undecryptablePassword = {};
        }
        else
        {
            s.password = {};
            s.undecryptablePassword = blob;
        }
    }
}
