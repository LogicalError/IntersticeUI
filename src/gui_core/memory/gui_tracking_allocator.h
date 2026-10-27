#pragma once

#include <gui_core/memory/gui_allocation_hooks.h>

#include <cstddef>

namespace tachyon
{
    /// @addtogroup diagnostics
    /// @{

    /// Allocation hooks that count what is live, for finding leaks. Logs a warning
    /// for every allocation still live when it is destroyed. Thread-safe.
    class TrackingAllocator
    {
    public:
        TrackingAllocator();
        ~TrackingAllocator();
        TrackingAllocator(const TrackingAllocator&) = delete;
        TrackingAllocator& operator=(const TrackingAllocator&) = delete;

        /// Hooks that allocate through this allocator; valid while it lives.
        AllocationHooks Hooks();

        size_t LiveAllocations() const;    ///< Allocations not freed yet
        size_t LiveBytes() const;          ///< Their total size in bytes, as requested
        size_t TotalAllocations() const;   ///< Ever made, including freed ones

        /// Logs a warning per live allocation; returns how many there are.
        size_t ReportLeaks() const;

    private:
        struct State;
        State* m_state;
    };
    /// @}
}
