#pragma once

#include <juce_core/juce_core.h>
#include <functional>
#include <string>
#include <vector>

namespace axsynth
{
    // Incremental multipart/x-mixed-replace (MJPEG) parser. Data may be pushed in
    // arbitrary chunk sizes (down to single bytes); every complete JPEG part is
    // delivered through the callback. Uses the part's Content-Length when present,
    // otherwise the next boundary (or, with no boundary at all, JPEG SOI/EOI markers).
    class MjpegParser
    {
    public:
        using FrameCallback = std::function<void (const juce::uint8* data, size_t size)>;

        // 'boundary' as returned by boundaryFromContentType(); empty = SOI/EOI scanning.
        MjpegParser (const juce::String& boundary, FrameCallback onFrame);

        void push (const void* data, size_t size);
        void reset();

        // "multipart/x-mixed-replace; boundary=myboundary" -> "myboundary".
        // Accepts quoted values and strips a leading "--".
        static juce::String boundaryFromContentType (const juce::String& contentType);

        static constexpr size_t maxBufferedBytes = 8 * 1024 * 1024;
        static constexpr size_t maxHeaderBytes = 16 * 1024;

    private:
        enum class State { seekBoundary, boundaryLine, headers, bodyLength, bodyScan, scanSoi, scanEoi };

        void process();
        size_t find (const char* pattern, size_t patternSize, size_t from) const noexcept;
        void emitTrimmed (size_t begin, size_t end);
        void compact();

        std::string marker;
        FrameCallback callback;
        std::vector<juce::uint8> buf;
        State state;
        size_t pos = 0;        // start of unconsumed data
        size_t scan = 0;       // resume position for incremental searches
        size_t bodyStart = 0;
        juce::int64 contentLength = -1;
    };
}
