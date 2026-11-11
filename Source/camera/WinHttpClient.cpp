#include "WinHttpClient.h"

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#include <algorithm>

namespace axsynth::http
{
    // ---- Canceller ----------------------------------------------------------

    Canceller::~Canceller()
    {
        cancel();
    }

    bool Canceller::add (void* handle)
    {
        if (handle == nullptr)
            return false;

        const std::lock_guard<std::mutex> lg (mutex);

        if (cancelled.load())
        {
            WinHttpCloseHandle (handle);
            return false;
        }

        handles.push_back (handle);
        return true;
    }

    void Canceller::release (void*& handle)
    {
        if (handle == nullptr)
            return;

        const std::lock_guard<std::mutex> lg (mutex);

        const auto it = std::find (handles.begin(), handles.end(), handle);
        if (it != handles.end())
        {
            handles.erase (it);
            WinHttpCloseHandle (handle);
        }

        handle = nullptr;
    }

    void Canceller::cancel()
    {
        const std::lock_guard<std::mutex> lg (mutex);
        cancelled = true;

        // Children (request) first, then connection, then session.
        for (auto it = handles.rbegin(); it != handles.rend(); ++it)
            WinHttpCloseHandle (*it);

        handles.clear();
    }

    void Canceller::reset()
    {
        const std::lock_guard<std::mutex> lg (mutex);
        jassert (handles.empty());
        cancelled = false;
    }

    // ---- helpers --------------------------------------------------------------

    juce::String stripUserInfo (const juce::String& hostEntry)
    {
        const auto s = hostEntry.trim();
        const auto schemeEnd = s.indexOf ("://");
        const auto authorityStart = schemeEnd >= 0 ? schemeEnd + 3 : 0;
        auto authorityEnd = s.indexOfChar (authorityStart, '/');
        if (authorityEnd < 0)
            authorityEnd = s.length();

        const auto at = s.substring (authorityStart, authorityEnd).lastIndexOfChar ('@');
        if (at < 0)
            return s;

        return s.substring (0, authorityStart) + s.substring (authorityStart + at + 1);
    }

    void splitHostPort (const juce::String& in, juce::String& host, int& port, bool https)
    {
        auto s = stripUserInfo (in);

        if (s.containsIgnoreCase ("://"))
            s = s.fromFirstOccurrenceOf ("://", false, false);

        s = s.upToFirstOccurrenceOf ("/", false, false);

        port = https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;

        if (s.startsWithChar ('['))
        {
            host = s.substring (1).upToFirstOccurrenceOf ("]", false, false);
            const auto rest = s.fromFirstOccurrenceOf ("]", false, false);
            if (rest.startsWithChar (':') && rest.length() > 1)
            {
                const auto p = rest.substring (1).getIntValue();
                if (p > 0 && p < 65536)
                    port = p;
            }
            return;
        }

        const auto colon = s.lastIndexOfChar (':');
        if (colon > 0 && s.indexOfChar (':') == colon) // exactly one colon: host:port
        {
            host = s.substring (0, colon);
            const auto p = s.substring (colon + 1).getIntValue();
            if (p > 0 && p < 65536)
                port = p;
        }
        else
        {
            host = s;
        }
    }

    HostSpec parseHost (const juce::String& in)
    {
        HostSpec spec;
        const auto s = stripUserInfo (in);

        const bool forceHttps = s.startsWithIgnoreCase ("https://");
        const bool forceHttp = s.startsWithIgnoreCase ("http://");

        int portIfHttps = 0, portIfHttp = 0;
        splitHostPort (s, spec.host, portIfHttps, true);
        splitHostPort (s, spec.host, portIfHttp, false);

        // Same result for both schemes means the port was given explicitly.
        if (portIfHttps == portIfHttp)
            spec.port = portIfHttps;

        if (forceHttps)
            spec.allowHttp = false;
        else if (forceHttp)
            spec.allowHttps = false;
        else if (spec.port == INTERNET_DEFAULT_HTTPS_PORT)
            spec.allowHttp = false;
        else if (spec.port == INTERNET_DEFAULT_HTTP_PORT)
            spec.allowHttps = false;

        return spec;
    }

    static juce::String queryHeaderString (HINTERNET req, DWORD info)
    {
        DWORD size = 0;
        WinHttpQueryHeaders (req, info, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &size, WINHTTP_NO_HEADER_INDEX);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0)
            return {};

        juce::HeapBlock<wchar_t> buffer (size / sizeof (wchar_t) + 1, true);
        if (! WinHttpQueryHeaders (req, info, WINHTTP_HEADER_NAME_BY_INDEX, buffer.get(), &size, WINHTTP_NO_HEADER_INDEX))
            return {};

        return juce::String (buffer.get());
    }

    // ---- Client -----------------------------------------------------------------

    Client::Client (Canceller& c, bool useHttps, const juce::String& hostWithPort, int timeout)
        : canceller (c), https (useHttps), timeoutMs (timeout)
    {
        splitHostPort (hostWithPort, host, port, https);
    }

    Client::~Client()
    {
        canceller.release (request);
        canceller.release (connection);
        canceller.release (session);
    }

    bool Client::open()
    {
        if (connection != nullptr)
            return true;

        if (host.isEmpty())
            return false;

        if (session == nullptr)
        {
            auto* s = WinHttpOpen (L"AxSynth/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                   WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
            if (s == nullptr)
                return false;

            WinHttpSetTimeouts (s, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

            if (! canceller.add (s))
                return false;

            session = s;
        }

        auto* c = WinHttpConnect (session, host.toWideCharPointer(), (INTERNET_PORT) port, 0);
        if (c == nullptr || ! canceller.add (c))
            return false;

        connection = c;
        return true;
    }

    void Client::closeRequest()
    {
        canceller.release (request);
    }

    Response Client::get (const juce::String& path, AuthMode auth, const juce::String& user, const juce::String& password)
    {
        Response r;

        if (canceller.isCancelled() || ! open())
        {
            r.outcome = canceller.isCancelled() ? Outcome::cancelled : Outcome::transportError;
            return r;
        }

        closeRequest();

        auto* req = WinHttpOpenRequest (connection, L"GET", path.toWideCharPointer(), nullptr,
                                        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                        https ? WINHTTP_FLAG_SECURE : 0);
        if (req == nullptr || ! canceller.add (req))
        {
            r.outcome = canceller.isCancelled() ? Outcome::cancelled : Outcome::transportError;
            return r;
        }

        request = req;

        if (https)
        {
            DWORD flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID
                        | SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
            WinHttpSetOption (req, WINHTTP_OPTION_SECURITY_FLAGS, &flags, sizeof (flags));
        }

        // Never send the Windows logon credentials to a camera.
        DWORD autologon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
        WinHttpSetOption (req, WINHTTP_OPTION_AUTOLOGON_POLICY, &autologon, sizeof (autologon));

        // Never follow redirects: a redirect could carry the request (and, after a
        // challenge, the credentials) to another host or from HTTPS to HTTP.
        DWORD noRedirects = WINHTTP_DISABLE_REDIRECTS;
        WinHttpSetOption (req, WINHTTP_OPTION_DISABLE_FEATURE, &noRedirects, sizeof (noRedirects));

        const DWORD wantedScheme = auth == AuthMode::basic  ? WINHTTP_AUTH_SCHEME_BASIC
                                 : auth == AuthMode::digest ? WINHTTP_AUTH_SCHEME_DIGEST
                                                            : 0;
        bool credentialsSet = false, certRetried = false;

        for (int attempt = 0; attempt < 4; ++attempt)
        {
            if (canceller.isCancelled())
            {
                r.outcome = Outcome::cancelled;
                return r;
            }

            BOOL ok = WinHttpSendRequest (req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
            if (ok)
                ok = WinHttpReceiveResponse (req, nullptr);

            if (! ok)
            {
                const auto err = GetLastError();

                if (canceller.isCancelled())
                    r.outcome = Outcome::cancelled;
                else if (err == ERROR_WINHTTP_CLIENT_AUTH_CERT_NEEDED && ! certRetried)
                {
                    certRetried = true;
                    WinHttpSetOption (req, WINHTTP_OPTION_CLIENT_CERT_CONTEXT, WINHTTP_NO_CLIENT_CERT_CONTEXT, 0);
                    continue;
                }
                else if (err == ERROR_WINHTTP_NAME_NOT_RESOLVED)
                    r.outcome = Outcome::nameNotResolved;
                else
                    r.outcome = Outcome::transportError;

                return r;
            }

            DWORD status = 0, size = sizeof (status);
            if (! WinHttpQueryHeaders (req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                       WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX))
            {
                r.outcome = canceller.isCancelled() ? Outcome::cancelled : Outcome::transportError;
                return r;
            }

            r.status = (int) status;

            // Answer a challenge only with the scheme this cascade step stands for, and
            // only if the server offered it: no Basic to a server that asked for Digest.
            if (status == 401 && wantedScheme != 0 && ! credentialsSet)
            {
                DWORD supported = 0, first = 0, target = 0;
                if (WinHttpQueryAuthSchemes (req, &supported, &first, &target)
                    && (supported & wantedScheme) != 0
                    && WinHttpSetCredentials (req, target, wantedScheme,
                                              user.toWideCharPointer(), password.toWideCharPointer(), nullptr))
                {
                    credentialsSet = true;
                    r.credentialsSent = true;
                    continue;
                }
            }

            break;
        }

        r.outcome = Outcome::ok;
        r.contentType = queryHeaderString (req, WINHTTP_QUERY_CONTENT_TYPE);
        return r;
    }

    int Client::read (void* dest, int maxBytes)
    {
        if (request == nullptr || canceller.isCancelled())
            return -1;

        DWORD available = 0;
        if (! WinHttpQueryDataAvailable (request, &available))
            return -1;

        if (available == 0)
            return 0;

        DWORD got = 0;
        if (! WinHttpReadData (request, dest, juce::jmin (available, (DWORD) maxBytes), &got))
            return -1;

        return (int) got;
    }

    juce::String Client::readAll (int maxBytes)
    {
        juce::MemoryOutputStream out;
        char buffer[4096];

        while ((int) out.getDataSize() < maxBytes)
        {
            const auto n = read (buffer, (int) sizeof (buffer));
            if (n <= 0)
                break;
            out.write (buffer, (size_t) n);
        }

        return out.toString();
    }
}
