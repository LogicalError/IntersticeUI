#pragma once

// Hierarchical CPU timing: scoped zones accumulated into a per-frame tree, keyed by
// the hash of their call path (parent path + name) so nodes are stable across frames.
//
// Hot path: each TACHYON_PROFILE_SCOPE site caches its node index, and timestamps are raw
// ticks (rdtsc on x86) converted to ms only at frame end.
//
// See engine design doc 28 (Diagnostics).

#include <stdint.h>
#include <span>

#include <gui_core/gui_base_types.h>   // SteadyClockNowNanos
#include <gui_core/gui_hash.h>         // Hash::CalculateConst / Combine

namespace tachyon::Diagnostics
{
    /// @addtogroup diagnostics
    /// @{

    /// Collects scope timings for one thread; opaque. The TACHYON_PROFILE_* macros use
    /// MainThreadCollector.
    class ProfilerCollector;

    ProfilerCollector& MainThreadCollector();

    bool IsProfilingEnabled();
    void SetProfilingEnabled(bool enabled);

    /// Frame boundaries: call once each, around all scopes for the frame.
    ///@{
    void BeginFrame(ProfilerCollector& collector);
    void EndFrame  (ProfilerCollector& collector);
    ///@}

    /// Discards all nodes and history; cached scope sites re-resolve.
    void Reset(ProfilerCollector& collector);

    /// Per-call-site descriptor, `static` at each TACHYON_PROFILE_SCOPE site. The cached* fields
    /// skip the node lookup when hit from the same call path; Reset invalidates them.
    /// Single-threaded.
    struct ScopeLocation
    {
        uint64_t    nameHash = 0;
        const char* name     = "";
        const char* file     = "";
        int         line     = 0;
        bool        isWait   = false;

        uint64_t    cachedKey        = 0;
        int32_t     cachedIndex      = -1;
        uint32_t    cachedGeneration = 0;
    };

    /// Cached scope entry / exit, as the macros use. `loc` is normally a `static`
    /// ScopeLocation; its strings must outlive the collector.
    ///@{
    void EnterScope(ProfilerCollector& collector, ScopeLocation& loc) noexcept;
    void ExitScope (ProfilerCollector& collector) noexcept;
    ///@}

    /// Uncached scope entry, for direct use and tests; pair with ExitScope.
    void EnterScope(ProfilerCollector& collector, uint64_t nameHash, const char* name,
                    bool isWait = false, const char* file = "", int line = 0) noexcept;

    /// Times its own lifetime as a scope; a disabled profiler costs one branch.
    class ScopedTimer
    {
    public:
        ScopedTimer(ProfilerCollector& collector, ScopeLocation& loc) noexcept
            : m_collector(collector), m_armed(IsProfilingEnabled())
        {
            if (m_armed) EnterScope(m_collector, loc);
        }
        ~ScopedTimer() noexcept { if (m_armed) ExitScope(m_collector); }

        ScopedTimer(const ScopedTimer&)            = delete;
        ScopedTimer& operator=(const ScopedTimer&) = delete;

    private:
        ProfilerCollector& m_collector;
        bool               m_armed;
    };

    /// One finalized node, depth-first. Durations are windowed averages (~4 updates/s).
    struct ProfileSample
    {
        uint64_t    key       = 0;
        uint64_t    parentKey = 0;
        const char* name      = "";
        const char* file      = "";
        int32_t     line      = 0;
        int32_t     depth     = 0;
        int32_t     callCount = 0;
        double      totalMs   = 0.0;
        double      selfMs    = 0.0;
        double      minMs     = 0.0;
        double      maxMs     = 0.0;
        bool        isWait    = false;
    };

    /// The nodes of the last finalized frame, depth-first. Valid until the next EndFrame.
    std::span<const ProfileSample> LastFrameSamples(const ProfilerCollector& collector);

    /// Smoothed total of the root scope(s): the frame total, in milliseconds.
    double LastFrameTotalMs(const ProfilerCollector& collector);
    /// @}
}

// Define TACHYON_PROFILE_ENABLED=0 to compile every macro out.
#ifndef TACHYON_PROFILE_ENABLED
#  define TACHYON_PROFILE_ENABLED 1
#endif

#if TACHYON_PROFILE_ENABLED
#  define TACHYON_PROFILE_CONCAT2(a, b) a##b
#  define TACHYON_PROFILE_CONCAT(a, b)  TACHYON_PROFILE_CONCAT2(a, b)

   // name must be a string literal so the hash folds at compile time.
#  define TACHYON_PROFILE_SCOPE(name)                                                                           \
       static ::tachyon::Diagnostics::ScopeLocation TACHYON_PROFILE_CONCAT(s_profLoc_, __LINE__)             \
           { ::tachyon::Hash::CalculateConst(name), name, __FILE__, __LINE__, false };                      \
       ::tachyon::Diagnostics::ScopedTimer TACHYON_PROFILE_CONCAT(profTimer_, __LINE__)(                     \
           ::tachyon::Diagnostics::MainThreadCollector(), TACHYON_PROFILE_CONCAT(s_profLoc_, __LINE__))

#  define TACHYON_PROFILE_SCOPE_WAIT(name)                                                                      \
       static ::tachyon::Diagnostics::ScopeLocation TACHYON_PROFILE_CONCAT(s_profLoc_, __LINE__)             \
           { ::tachyon::Hash::CalculateConst(name), name, __FILE__, __LINE__, true };                       \
       ::tachyon::Diagnostics::ScopedTimer TACHYON_PROFILE_CONCAT(profTimer_, __LINE__)(                     \
           ::tachyon::Diagnostics::MainThreadCollector(), TACHYON_PROFILE_CONCAT(s_profLoc_, __LINE__))

   // Times a scope from a caller-owned ScopeLocation (e.g. an entry of a per-stage table).
#  define TACHYON_PROFILE_SCOPE_REF(locationLValue)                                                             \
       ::tachyon::Diagnostics::ScopedTimer TACHYON_PROFILE_CONCAT(profTimer_, __LINE__)(                     \
           ::tachyon::Diagnostics::MainThreadCollector(), (locationLValue))

   // Root frame scope plus collector frame boundaries; wrap the whole loop iteration.
#  define TACHYON_PROFILE_FRAME_BEGIN(name)                                                                     \
       do {                                                                                             \
           static ::tachyon::Diagnostics::ScopeLocation TACHYON_PROFILE_CONCAT(s_frameLoc_, __LINE__)        \
               { ::tachyon::Hash::CalculateConst(name), name, __FILE__, __LINE__, false };                  \
           ::tachyon::Diagnostics::BeginFrame(::tachyon::Diagnostics::MainThreadCollector());                   \
           ::tachyon::Diagnostics::EnterScope(::tachyon::Diagnostics::MainThreadCollector(),                    \
               TACHYON_PROFILE_CONCAT(s_frameLoc_, __LINE__));                                           \
       } while (0)
#  define TACHYON_PROFILE_FRAME_END()                                                                           \
       do {                                                                                             \
           ::tachyon::Diagnostics::ExitScope(::tachyon::Diagnostics::MainThreadCollector());                    \
           ::tachyon::Diagnostics::EndFrame(::tachyon::Diagnostics::MainThreadCollector());                     \
       } while (0)
#else
#  define TACHYON_PROFILE_SCOPE(name)              ((void)0)
#  define TACHYON_PROFILE_SCOPE_WAIT(name)         ((void)0)
#  define TACHYON_PROFILE_SCOPE_REF(locationLValue) ((void)0)
#  define TACHYON_PROFILE_FRAME_BEGIN(name)        ((void)0)
#  define TACHYON_PROFILE_FRAME_END()              ((void)0)
#endif
