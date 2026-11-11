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

    void splitHostPort (const juce::String& in, juce::String& host, int& port, bool https)
    {
        auto s = in.trim();

        if (s.containsIgnoreCase ("://"))
            s = s.fromFirstOccurrenceOf ("://", false, false);

        s = s.upToFirstOccurrenceOf ("/", false, false);

        if (s.containsChar ('@')) // user:pass@host - credentials come from the settings
            s = s.fromLastOccurrenceOf ("@", false, false);

        port = https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;

        if (s.startsWithChar ('['))
        {
            host = s.substring (1).upToFirstOccurrenceOf ("]", false, false);
            const auto rest = s.fromFirstOccurrenceOf ("]", false, false);
            if (rest.startsWithChar (':') && rest.length() > 1)
                port = rest.substring (1).getIntValue();
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

        juce::String headers;
        if (auth == AuthMode::basic)
        {
            const auto userPass = (user + ":" + password).toStdString();
            headers = "Authorization: Basic " + juce::Base64::toBase64 (userPass.data(), userPass.size()) + "\r\n";
        }

        bool digestSent = false, certRetried = false;

        for (int attempt = 0; attempt < 4; ++attempt)
        {
            if (canceller.isCancelled())
            {
                r.outcome = Outcome::cancelled;
                return r;
            }

            BOOL ok = WinHttpSendRequest (req,
                                          headers.isEmpty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.toWideCharPointer(),
                                          headers.isEmpty() ? 0 : (DWORD) -1L,
                                          WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
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

            if (status == 401 && auth == AuthMode::digest && ! digestSent)
            {
                DWORD supported = 0, first = 0, target = 0;
                if (WinHttpQueryAuthSchemes (req, &supported, &first, &target)
                    && (supported & WINHTTP_AUTH_SCHEME_DIGEST) != 0
                    && WinHttpSetCredentials (req, target, WINHTTP_AUTH_SCHEME_DIGEST,
                                              user.toWideCharPointer(), password.toWideCharPointer(), nullptr))
                {
                    digestSent = true;
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
