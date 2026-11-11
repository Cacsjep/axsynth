#include "MjpegParser.h"
#include <algorithm>
#include <cstring>

namespace axsynth
{
    MjpegParser::MjpegParser (const juce::String& boundary, FrameCallback onFrame)
        : marker (boundary.toStdString()),
          callback (std::move (onFrame)),
          state (marker.empty() ? State::scanSoi : State::seekBoundary)
    {
    }

    void MjpegParser::reset()
    {
        buf.clear();
        pos = scan = bodyStart = 0;
        contentLength = -1;
        state = marker.empty() ? State::scanSoi : State::seekBoundary;
    }

    juce::String MjpegParser::boundaryFromContentType (const juce::String& contentType)
    {
        const auto idx = contentType.indexOfIgnoreCase ("boundary=");
        if (idx < 0)
            return {};

        auto value = contentType.substring (idx + 9).upToFirstOccurrenceOf (";", false, false).trim();
        value = value.unquoted().trim();

        while (value.startsWith ("--"))
            value = value.substring (2);

        return value;
    }

    void MjpegParser::push (const void* data, size_t size)
    {
        if (size == 0)
            return;

        compact();

        if (buf.size() - pos + size > maxBufferedBytes)
            reset(); // runaway part without a terminator: drop everything and resync

        const auto* bytes = static_cast<const juce::uint8*> (data);
        buf.insert (buf.end(), bytes, bytes + size);
        process();
    }

    void MjpegParser::compact()
    {
        if (pos == 0 || (pos < 65536 && pos * 2 < buf.size()))
            return;

        buf.erase (buf.begin(), buf.begin() + (std::ptrdiff_t) pos);
        scan = scan > pos ? scan - pos : 0;
        bodyStart = bodyStart > pos ? bodyStart - pos : 0;
        pos = 0;
    }

    size_t MjpegParser::find (const char* pattern, size_t patternSize, size_t from) const noexcept
    {
        if (patternSize == 0 || buf.size() < patternSize || from > buf.size() - patternSize)
            return std::string::npos;

        const auto* begin = buf.data() + from;
        const auto* end = buf.data() + buf.size();
        const auto* hit = std::search (begin, end, pattern, pattern + patternSize,
                                       [] (juce::uint8 a, char b) { return a == (juce::uint8) b; });
        return hit == end ? std::string::npos : (size_t) (hit - buf.data());
    }

    void MjpegParser::emitTrimmed (size_t begin, size_t end)
    {
        // Locate SOI at (or near) the start and the last EOI before 'end'.

        size_t s = std::string::npos;
        for (size_t i = begin; i + 1 < end && i < begin + 64; ++i)
            if (buf[i] == 0xFF && buf[i + 1] == 0xD8) { s = i; break; }

        if (s == std::string::npos)
            return;

        size_t e = std::string::npos;
        for (size_t i = end; i >= s + 3; --i)
            if (buf[i - 2] == 0xFF && buf[i - 1] == 0xD9) { e = i; break; }

        if (e != std::string::npos && callback)
            callback (buf.data() + s, e - s);
    }

    void MjpegParser::process()
    {
        const auto m = marker.size();

        for (;;)
        {
            switch (state)
            {
                case State::seekBoundary:
                {
                    const auto idx = find (marker.data(), m, std::max (pos, scan));
                    if (idx == std::string::npos)
                    {
                        // Keep the last m-1 bytes, a marker may straddle the chunk boundary.
                        const auto keep = buf.size() >= m ? buf.size() - (m - 1) : 0;
                        pos = scan = std::max (pos, keep);
                        return;
                    }
                    pos = scan = idx + m;
                    state = State::boundaryLine;
                    break;
                }

                case State::boundaryLine:
                {
                    // Rest of the boundary line ("\r\n", or "--" for the closing delimiter).
                    const auto nl = find ("\n", 1, pos);
                    if (nl == std::string::npos)
                    {
                        if (buf.size() - pos > 256)
                            state = State::seekBoundary;
                        return;
                    }
                    pos = scan = nl + 1;
                    contentLength = -1;
                    state = State::headers;
                    break;
                }

                case State::headers:
                {
                    // Some servers omit part headers and start the JPEG right away.
                    if (buf.size() - pos >= 2 && buf[pos] == 0xFF && buf[pos + 1] == 0xD8)
                    {
                        bodyStart = scan = pos;
                        state = State::bodyScan;
                        break;
                    }

                    const auto nl = find ("\n", 1, pos);
                    if (nl == std::string::npos)
                    {
                        if (buf.size() - pos > maxHeaderBytes)
                            state = State::seekBoundary;
                        return;
                    }

                    auto lineEnd = nl;
                    if (lineEnd > pos && buf[lineEnd - 1] == '\r')
                        --lineEnd;

                    const juce::String line (juce::CharPointer_UTF8 ((const char*) buf.data() + pos),
                                             juce::CharPointer_UTF8 ((const char*) buf.data() + lineEnd));
                    pos = scan = nl + 1;

                    if (line.isEmpty())
                    {
                        bodyStart = scan = pos;
                        state = contentLength > 0 ? State::bodyLength : State::bodyScan;
                        break;
                    }

                    if (line.startsWithIgnoreCase ("Content-Length:"))
                    {
                        const auto v = line.fromFirstOccurrenceOf (":", false, false).trim();
                        contentLength = v.containsOnly ("0123456789") && v.isNotEmpty() ? v.getLargeIntValue() : -1;
                        if (contentLength > (juce::int64) maxBufferedBytes)
                            contentLength = -1;
                    }
                    break;
                }

                case State::bodyLength:
                {
                    if ((juce::int64) (buf.size() - bodyStart) < contentLength)
                        return;

                    const auto end = bodyStart + (size_t) contentLength;
                    if (callback)
                        callback (buf.data() + bodyStart, (size_t) contentLength);

                    pos = scan = end;
                    state = State::seekBoundary;
                    break;
                }

                case State::bodyScan:
                {
                    const auto idx = find (marker.data(), m, std::max (bodyStart, scan));
                    if (idx == std::string::npos)
                    {
                        const auto keep = buf.size() >= m ? buf.size() - (m - 1) : 0;
                        scan = std::max (bodyStart, keep);
                        return;
                    }
                    emitTrimmed (bodyStart, idx);
                    pos = scan = idx;
                    state = State::seekBoundary;
                    break;
                }

                case State::scanSoi:
                {
                    size_t idx = std::string::npos;
                    for (size_t i = std::max (pos, scan); i + 1 < buf.size(); ++i)
                        if (buf[i] == 0xFF && buf[i + 1] == 0xD8) { idx = i; break; }

                    if (idx == std::string::npos)
                    {
                        pos = scan = buf.empty() ? 0 : buf.size() - 1;
                        return;
                    }
                    pos = bodyStart = idx;
                    scan = idx + 2;
                    state = State::scanEoi;
                    break;
                }

                case State::scanEoi:
                {
                    size_t idx = std::string::npos;
                    for (size_t i = scan; i + 1 < buf.size(); ++i)
                        if (buf[i] == 0xFF && buf[i + 1] == 0xD9) { idx = i; break; }

                    if (idx == std::string::npos)
                    {
                        scan = buf.size() > 0 ? buf.size() - 1 : 0;
                        return;
                    }
                    if (callback)
                        callback (buf.data() + bodyStart, idx + 2 - bodyStart);
                    pos = scan = idx + 2;
                    state = State::scanSoi;
                    break;
                }
            }
        }
    }
}
