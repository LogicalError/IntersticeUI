#include <gui_core/diagnostics/gui_profiler.h>

#include <unordered_map>
#include <vector>

// rdtsc costs a few cycles vs ~20ns for QueryPerformanceCounter; scope timestamps
// land inside the parent's span, so a cheaper clock means less skew.
#if defined(_M_X64) || defined(_M_IX86)
#  include <intrin.h>
#  define PROF_USE_TSC 1
#else
#  define PROF_USE_TSC 0
#endif

namespace tachyon::Diagnostics
{
    namespace
    {
        constexpr int kMaxDepth = 4096;
        constexpr int64_t kWindowNs = 250'000'000;   // 0.25s window, matching the FPS panel

        bool g_enabled = true;

        inline uint64_t ProfTicks() noexcept
        {
#if PROF_USE_TSC
            return __rdtsc();
#else
            return uint64_t(SteadyClockNowNanos());
#endif
        }
    }

    // One call path, persistent across frames. Recursion lands on a distinct, deeper
    // node (different parent path); depth is capped at kMaxDepth.
    struct TimingNode
    {
        uint64_t    key         = 0;
        uint64_t    parentKey   = 0;
        uint64_t    nameHash    = 0;
        const char* name        = "";
        const char* file        = "";
        int32_t     line        = 0;
        int32_t     parent      = -1;
        int32_t     depth       = 0;
        int32_t     firstChild  = -1;
        int32_t     lastChild   = -1;
        int32_t     nextSibling = -1;
        bool        isWait      = false;

        // Per-frame accumulators, in raw ticks (reset in BeginFrame).
        int64_t totalTicks = 0;   // inclusive
        int64_t childTicks = 0;   // sum of direct children's inclusive ticks
        int32_t callCount  = 0;

        // Windowed accumulators (reset on flush), already in ms.
        double sumTotalMs = 0.0;
        double sumSelfMs  = 0.0;
        double winMinMs   = 0.0;
        double winMaxMs   = 0.0;
        bool   winHasData = false;

        // Smoothed values read by the display.
        double smTotalMs = 0.0;
        double smSelfMs  = 0.0;
        double smMinMs   = 0.0;
        double smMaxMs   = 0.0;
    };

    struct OpenScope
    {
        int32_t nodeIndex;   // -1 == over-depth sentinel
        uint64_t startTicks;
    };

    class ProfilerCollector
    {
    public:
        ProfilerCollector()
        {
            m_stack.reserve(kMaxDepth + 8);
            m_nodes.reserve(4096);
            m_index.reserve(4096);
            m_samples.reserve(4096);
        }

        void BeginFrame();
        void EndFrame();
        void Reset();
        void Enter(ScopeLocation& loc) noexcept;
        void Exit() noexcept;

        std::span<const ProfileSample> Samples() const { return { m_samples.data(), m_samples.size() }; }
        double TotalMs() const { return m_rootTotalMs; }

    private:
        int32_t FindOrCreate(uint64_t key, uint64_t parentKey, int32_t parentIndex,
                             const ScopeLocation& loc, int32_t depth);
        void   Flush();
        void   RebuildSamples();
        void   AppendSubtree(int32_t index);
        void   Calibrate();
        double NsFromTicks(int64_t ticks) const { return double(ticks) / m_tscPerNs; }

        std::vector<TimingNode>               m_nodes;
        std::unordered_map<uint64_t, int32_t> m_index;
        std::vector<OpenScope>                m_stack;
        std::vector<ProfileSample>            m_samples;

        int64_t m_windowStartNs = 0;
        int32_t m_windowFrames  = 0;
        bool    m_windowStarted = false;
        bool    m_frameArmed    = false;
        double  m_rootTotalMs   = 0.0;

        // Bumped by Reset(). Starts at 1 so a default ScopeLocation (0) never matches.
        uint32_t m_generation = 1;

        // Ticks per ns, calibrated against steady_clock at frame boundaries. Assumes
        // 3 GHz for the first ~50ms; without TSC, ticks are ns.
#if PROF_USE_TSC
        double   m_tscPerNs   = 3.0;
        bool     m_calStarted = false;
        int64_t  m_calNs0     = 0;
        uint64_t m_calTicks0  = 0;
#else
        double   m_tscPerNs   = 1.0;
#endif
    };

    void ProfilerCollector::BeginFrame()
    {
        m_frameArmed = g_enabled;
        if (!m_frameArmed) return;   // disabled: keep the last captured frame

        for (auto& n : m_nodes)
        {
            n.totalTicks = 0;
            n.childTicks = 0;
            n.callCount  = 0;
        }
        m_stack.clear();

        if (!m_windowStarted)
        {
            m_windowStartNs = SteadyClockNowNanos();
            m_windowStarted = true;
        }
    }

    void ProfilerCollector::Reset()
    {
        m_nodes.clear();
        m_index.clear();
        m_samples.clear();
        m_stack.clear();
        m_windowStartNs = 0;
        m_windowFrames  = 0;
        m_windowStarted = false;
        m_frameArmed    = false;
        m_rootTotalMs   = 0.0;
        ++m_generation;
    }

    int32_t ProfilerCollector::FindOrCreate(uint64_t key, uint64_t parentKey, int32_t parentIndex,
                                            const ScopeLocation& loc, int32_t depth)
    {
        auto it = m_index.find(key);
        if (it != m_index.end())
        {
            if (loc.isWait) m_nodes[it->second].isWait = true;
            return it->second;
        }

        const int32_t index = static_cast<int32_t>(m_nodes.size());
        TimingNode node;
        node.key       = key;
        node.parentKey = parentKey;
        node.nameHash  = loc.nameHash;
        node.name      = loc.name;
        node.file      = loc.file;
        node.line      = loc.line;
        node.parent    = parentIndex;
        node.depth     = depth;
        node.isWait    = loc.isWait;
        m_nodes.push_back(node);   // may reallocate; take parent ref afterwards
        m_index.emplace(key, index);

        // Append to the parent's child list so siblings keep call order.
        if (parentIndex >= 0)
        {
            TimingNode& parent = m_nodes[parentIndex];
            if (parent.lastChild >= 0) m_nodes[parent.lastChild].nextSibling = index;
            else                       parent.firstChild = index;
            parent.lastChild = index;
        }
        return index;
    }

    void ProfilerCollector::Enter(ScopeLocation& loc) noexcept
    {
        if (!m_frameArmed) return;

        if (static_cast<int>(m_stack.size()) >= kMaxDepth)
        {
            m_stack.push_back({ -1, 0 });   // over-depth: stay balanced, create no node
            return;
        }

        const int32_t  parentIndex = m_stack.empty() ? -1 : m_stack.back().nodeIndex;
        const uint64_t parentKey   = parentIndex >= 0 ? m_nodes[parentIndex].key : 0;
        const uint64_t key         = Hash::Combine(parentKey, loc.nameHash);

        int32_t index;
        if (loc.cachedGeneration == m_generation && loc.cachedKey == key)
        {
            index = loc.cachedIndex;        // same site, same call path
        }
        else
        {
            index = FindOrCreate(key, parentKey, parentIndex, loc, static_cast<int32_t>(m_stack.size()));
            loc.cachedGeneration = m_generation;
            loc.cachedKey        = key;
            loc.cachedIndex      = index;
        }

        m_nodes[index].callCount++;
        m_stack.push_back({ index, ProfTicks() });
    }

    void ProfilerCollector::Exit() noexcept
    {
        if (!m_frameArmed || m_stack.empty()) return;

        const OpenScope top = m_stack.back();
        m_stack.pop_back();
        if (top.nodeIndex < 0) return;   // over-depth sentinel

        int64_t dt = int64_t(ProfTicks() - top.startTicks);
        if (dt < 0) dt = 0;

        m_nodes[top.nodeIndex].totalTicks += dt;
        if (!m_stack.empty())
        {
            const int32_t parentIndex = m_stack.back().nodeIndex;
            if (parentIndex >= 0) m_nodes[parentIndex].childTicks += dt;
        }
    }

    void ProfilerCollector::Calibrate()
    {
#if PROF_USE_TSC
        const int64_t  ns  = SteadyClockNowNanos();
        const uint64_t tsc = ProfTicks();
        if (!m_calStarted) { m_calNs0 = ns; m_calTicks0 = tsc; m_calStarted = true; return; }
        const int64_t dns = ns - m_calNs0;
        if (dns > 50'000'000)   // >=50ms baseline -> stable ratio
        {
            const double ratio = double(tsc - m_calTicks0) / double(dns);
            if (ratio > 0.0) m_tscPerNs = ratio;
        }
#endif
    }

    void ProfilerCollector::EndFrame()
    {
        if (!m_frameArmed) return;   // disabled: keep last frame's samples frozen

        // Close scopes left open (e.g. profiling toggled mid-frame).
        while (!m_stack.empty()) Exit();

        Calibrate();

        for (auto& n : m_nodes)
        {
            const double totalMs = NsFromTicks(n.totalTicks) / 1.0e6;
            double       selfMs  = NsFromTicks(n.totalTicks - n.childTicks) / 1.0e6;
            if (selfMs < 0.0) selfMs = 0.0;

            n.sumTotalMs += totalMs;
            n.sumSelfMs  += selfMs;
            if (!n.winHasData) { n.winMinMs = totalMs; n.winMaxMs = totalMs; n.winHasData = true; }
            else               { if (totalMs < n.winMinMs) n.winMinMs = totalMs;
                                 if (totalMs > n.winMaxMs) n.winMaxMs = totalMs; }
        }
        m_windowFrames++;

        const int64_t now = SteadyClockNowNanos();
        if (now - m_windowStartNs >= kWindowNs && m_windowFrames > 0) Flush();

        RebuildSamples();
    }

    void ProfilerCollector::Flush()
    {
        const double inv = 1.0 / static_cast<double>(m_windowFrames);
        for (auto& n : m_nodes)
        {
            n.smTotalMs = n.sumTotalMs * inv;
            n.smSelfMs  = n.sumSelfMs  * inv;
            n.smMinMs   = n.winHasData ? n.winMinMs : 0.0;
            n.smMaxMs   = n.winHasData ? n.winMaxMs : 0.0;
            n.sumTotalMs = 0.0;
            n.sumSelfMs  = 0.0;
            n.winHasData = false;
        }
        m_windowFrames  = 0;
        m_windowStartNs = SteadyClockNowNanos();
    }

    void ProfilerCollector::AppendSubtree(int32_t index)
    {
        const TimingNode& n = m_nodes[index];
        ProfileSample s;
        s.key       = n.key;
        s.parentKey = n.parentKey;
        s.name      = n.name;
        s.file      = n.file;
        s.line      = n.line;
        s.depth     = n.depth;
        s.callCount = n.callCount;
        s.totalMs   = n.smTotalMs;
        s.selfMs    = n.smSelfMs;
        s.minMs     = n.smMinMs;
        s.maxMs     = n.smMaxMs;
        s.isWait    = n.isWait;
        m_samples.push_back(s);

        for (int32_t c = n.firstChild; c >= 0; c = m_nodes[c].nextSibling)
            AppendSubtree(c);
    }

    void ProfilerCollector::RebuildSamples()
    {
        m_samples.clear();
        m_rootTotalMs = 0.0;

        // Roots in creation order, each subtree pre-order.
        for (int32_t i = 0; i < static_cast<int32_t>(m_nodes.size()); ++i)
        {
            if (m_nodes[i].parent != -1) continue;
            m_rootTotalMs += m_nodes[i].smTotalMs;
            AppendSubtree(i);
        }
    }

    ProfilerCollector& MainThreadCollector()
    {
        static ProfilerCollector instance;
        return instance;
    }

    bool IsProfilingEnabled()          { return g_enabled; }
    void SetProfilingEnabled(bool on)  { g_enabled = on; }

    void BeginFrame(ProfilerCollector& c) { c.BeginFrame(); }
    void EndFrame  (ProfilerCollector& c) { c.EndFrame(); }
    void Reset     (ProfilerCollector& c) { c.Reset(); }

    void EnterScope(ProfilerCollector& c, ScopeLocation& loc) noexcept { c.Enter(loc); }
    void ExitScope (ProfilerCollector& c) noexcept { c.Exit(); }

    void EnterScope(ProfilerCollector& c, uint64_t nameHash, const char* name,
                    bool isWait, const char* file, int line) noexcept
    {
        // Throwaway location: always does the map lookup.
        ScopeLocation loc{ nameHash, name, file, line, isWait };
        c.Enter(loc);
    }

    std::span<const ProfileSample> LastFrameSamples(const ProfilerCollector& c) { return c.Samples(); }
    double LastFrameTotalMs(const ProfilerCollector& c) { return c.TotalMs(); }
}
