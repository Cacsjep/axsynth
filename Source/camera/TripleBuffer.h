#pragma once

#include <atomic>

namespace axsynth
{
    // Lock-free single-producer / single-consumer triple buffer.
    // The producer fills writeSlot() and calls publish(); the consumer calls
    // consume() and, if it returned true, reads readSlot(). Each slot is owned by
    // exactly one side at a time; ownership moves only through the atomic 'middle'.
    template <typename T>
    class TripleBuffer
    {
    public:
        // ---- producer ----
        T& writeSlot() noexcept { return slots[writeIndex]; }

        void publish() noexcept
        {
            writeIndex = middle.exchange (writeIndex | dirtyBit, std::memory_order_acq_rel) & indexMask;
        }

        // ---- consumer ----
        bool consume() noexcept
        {
            if ((middle.load (std::memory_order_acquire) & dirtyBit) == 0)
                return false;

            readIndex = middle.exchange (readIndex, std::memory_order_acq_rel) & indexMask;
            return true;
        }

        T& readSlot() noexcept { return slots[readIndex]; }

    private:
        static constexpr int dirtyBit = 4;
        static constexpr int indexMask = 3;

        T slots[3] {};
        int writeIndex = 0;            // producer only
        int readIndex = 1;             // consumer only
        std::atomic<int> middle { 2 }; // shared
    };
}
