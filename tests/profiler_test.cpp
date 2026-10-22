// Unit tests for the hierarchical timing collector (GUI::Diagnostics), through the
// public API (no GPU, no window):
//   - nested scopes form a parent/child tree in depth-first order
//   - siblings keep call order
//   - recursion is path-keyed: a repeated name one level deeper is a DISTINCT
//     node, not a re-entry of the same node (no inclusive-time double count)
//   - call count accumulates within a frame; identity (key) is stable across
//     frames while per-frame counters reset
//   - the wait flag (vsync present) propagates
//   - while disabled, nothing is collected
//   - an unbalanced (never-exited) scope is force-closed and cannot corrupt the
//     next frame
//   - smoothed durations nest correctly (parent total >= child total)

#include <gtest/gtest.h>

#include <gui_core/diagnostics/gui_profiler.h>
#include <gui_core/gui_hash.h>

#include <chrono>
#include <span>
#include <string_view>
#include <thread>

namespace GUI::Diagnostics
{
    namespace
    {
        uint64_t H(std::string_view name) { return GUI::Hash::Calculate(name); }

        const ProfileSample* Find(std::span<const ProfileSample> samples, std::string_view name)
        {
            for (const auto& s : samples)
                if (name == s.name) return &s;
            return nullptr;
        }
    }

    // Each test starts from a clean, enabled collector. The collector is a
    // process-wide singleton, so Reset() is what isolates the tests.
    class ProfilerTest : public ::testing::Test
    {
    protected:
        ProfilerCollector& c = MainThreadCollector();
        void SetUp()    override { SetProfilingEnabled(true); Reset(c); }
        void TearDown() override { SetProfilingEnabled(true); Reset(c); }
    };

    TEST_F(ProfilerTest, NestedScopesFormParentChild)
    {
        BeginFrame(c);
        EnterScope(c, H("A"), "A");
        EnterScope(c, H("B"), "B");
        ExitScope(c);   // B
        ExitScope(c);   // A
        EndFrame(c);

        const auto s = LastFrameSamples(c);
        ASSERT_EQ(s.size(), 2u);
        EXPECT_STREQ(s[0].name, "A");
        EXPECT_EQ(s[0].depth, 0);
        EXPECT_EQ(s[0].parentKey, 0u);
        EXPECT_STREQ(s[1].name, "B");
        EXPECT_EQ(s[1].depth, 1);
        EXPECT_EQ(s[1].parentKey, s[0].key);
    }

    TEST_F(ProfilerTest, SiblingsKeepCallOrder)
    {
        BeginFrame(c);
        EnterScope(c, H("Root"), "Root");
            EnterScope(c, H("First"),  "First");  ExitScope(c);
            EnterScope(c, H("Second"), "Second"); ExitScope(c);
        ExitScope(c);
        EndFrame(c);

        const auto s = LastFrameSamples(c);
        ASSERT_EQ(s.size(), 3u);
        EXPECT_STREQ(s[0].name, "Root");
        EXPECT_STREQ(s[1].name, "First");
        EXPECT_STREQ(s[2].name, "Second");
        EXPECT_EQ(s[1].depth, 1);
        EXPECT_EQ(s[2].depth, 1);
        EXPECT_EQ(s[1].parentKey, s[0].key);
        EXPECT_EQ(s[2].parentKey, s[0].key);
    }

    TEST_F(ProfilerTest, RecursionIsPathKeyedIntoDistinctNodes)
    {
        BeginFrame(c);
        EnterScope(c, H("R"), "R");   // depth 0
        EnterScope(c, H("R"), "R");   // depth 1 — same name, different path
        ExitScope(c);
        ExitScope(c);
        EndFrame(c);

        const auto s = LastFrameSamples(c);
        ASSERT_EQ(s.size(), 2u);
        EXPECT_STREQ(s[0].name, "R");
        EXPECT_STREQ(s[1].name, "R");
        EXPECT_EQ(s[0].depth, 0);
        EXPECT_EQ(s[1].depth, 1);
        EXPECT_NE(s[0].key, s[1].key);          // distinct identities
        EXPECT_EQ(s[1].parentKey, s[0].key);
    }

    TEST_F(ProfilerTest, RepeatedCallsAccumulateOnOneNode)
    {
        BeginFrame(c);
        for (int i = 0; i < 3; ++i)
        {
            EnterScope(c, H("Loop"), "Loop");
            ExitScope(c);
        }
        EndFrame(c);

        const auto s = LastFrameSamples(c);
        ASSERT_EQ(s.size(), 1u);                // same path → one node
        EXPECT_STREQ(s[0].name, "Loop");
        EXPECT_EQ(s[0].callCount, 3);
    }

    TEST_F(ProfilerTest, IdentityStableAcrossFramesCountersReset)
    {
        BeginFrame(c);
        EnterScope(c, H("X"), "X");
        EnterScope(c, H("Y"), "Y");
        ExitScope(c);
        ExitScope(c);
        EndFrame(c);
        const uint64_t xKey = LastFrameSamples(c)[0].key;
        const uint64_t yKey = LastFrameSamples(c)[1].key;

        BeginFrame(c);
        EnterScope(c, H("X"), "X");
        EnterScope(c, H("Y"), "Y");
        ExitScope(c);
        ExitScope(c);
        EndFrame(c);

        const auto s = LastFrameSamples(c);
        ASSERT_EQ(s.size(), 2u);
        EXPECT_EQ(s[0].key, xKey);              // identity persists
        EXPECT_EQ(s[1].key, yKey);
        EXPECT_EQ(s[0].callCount, 1);           // counters reset each frame
        EXPECT_EQ(s[1].callCount, 1);
    }

    TEST_F(ProfilerTest, WaitFlagPropagates)
    {
        BeginFrame(c);
        EnterScope(c, H("Frame"), "Frame");
            EnterScope(c, H("Present"), "Present", /*isWait*/ true);
            ExitScope(c);
        ExitScope(c);
        EndFrame(c);

        const auto s = LastFrameSamples(c);
        const ProfileSample* present = Find(s, "Present");
        const ProfileSample* frame   = Find(s, "Frame");
        ASSERT_NE(present, nullptr);
        ASSERT_NE(frame,   nullptr);
        EXPECT_TRUE(present->isWait);
        EXPECT_FALSE(frame->isWait);
    }

    TEST_F(ProfilerTest, DisabledCollectsNothing)
    {
        SetProfilingEnabled(false);
        BeginFrame(c);
        EnterScope(c, H("A"), "A");
        EnterScope(c, H("B"), "B");
        ExitScope(c);
        ExitScope(c);
        EndFrame(c);
        EXPECT_TRUE(LastFrameSamples(c).empty());
    }

    TEST_F(ProfilerTest, UnbalancedScopeIsForceClosedAndCannotLeak)
    {
        BeginFrame(c);
        EnterScope(c, H("Leaky"), "Leaky");   // never exited
        EndFrame(c);                          // must force-close, not crash

        ASSERT_NE(Find(LastFrameSamples(c), "Leaky"), nullptr);

        // The next frame's stack must be clean: a new root scope must land at
        // depth 0, not nested under the leaked one.
        BeginFrame(c);
        EnterScope(c, H("Clean"), "Clean");
        ExitScope(c);
        EndFrame(c);

        const ProfileSample* clean = Find(LastFrameSamples(c), "Clean");
        ASSERT_NE(clean, nullptr);
        EXPECT_EQ(clean->depth, 0);
    }

    TEST_F(ProfilerTest, SmoothedDurationsNest)
    {
        using namespace std::chrono_literals;

        // Run frames (each doing measurable work in a nested scope) until the
        // averaging window flushes and durations become non-zero.
        for (int i = 0; i < 2000 && LastFrameTotalMs(c) == 0.0; ++i)
        {
            BeginFrame(c);
            EnterScope(c, H("Outer"), "Outer");
            EnterScope(c, H("Inner"), "Inner");
            std::this_thread::sleep_for(1ms);
            ExitScope(c);
            ExitScope(c);
            EndFrame(c);
        }

        EXPECT_GT(LastFrameTotalMs(c), 0.0);
        const auto s = LastFrameSamples(c);
        const ProfileSample* outer = Find(s, "Outer");
        const ProfileSample* inner = Find(s, "Inner");
        ASSERT_NE(outer, nullptr);
        ASSERT_NE(inner, nullptr);
        EXPECT_GT(inner->totalMs, 0.0);
        EXPECT_GE(outer->totalMs, inner->totalMs);   // parent encloses child
        EXPECT_GE(inner->selfMs, outer->selfMs);     // the leaf holds the cost
    }

    // Call-path keys must not cancel out: A->A and A->B->A are distinct paths.
    TEST_F(ProfilerTest, RecursivePathsGetDistinctNodes)
    {
        BeginFrame(c);
        EnterScope(c, H("A"), "A");
            EnterScope(c, H("A"), "A");
                EnterScope(c, H("B"), "B");
                    EnterScope(c, H("A"), "A"); ExitScope(c);
                ExitScope(c);
            ExitScope(c);
        ExitScope(c);
        EnterScope(c, H("B"), "B"); ExitScope(c);
        EndFrame(c);

        const auto s = LastFrameSamples(c);
        ASSERT_EQ(s.size(), 5u);
        for (size_t i = 0; i < s.size(); ++i)
            for (size_t j = i + 1; j < s.size(); ++j)
                EXPECT_NE(s[i].key, s[j].key) << i << " vs " << j;
        EXPECT_EQ(s[0].callCount, 1);
    }


    // Order matters in a call path: A->B and B->A are different nodes.
    TEST_F(ProfilerTest, SwappedPathsGetDistinctNodes)
    {
        BeginFrame(c);
        EnterScope(c, H("A"), "A"); EnterScope(c, H("B"), "B"); ExitScope(c); ExitScope(c);
        EnterScope(c, H("B"), "B"); EnterScope(c, H("A"), "A"); ExitScope(c); ExitScope(c);
        EndFrame(c);

        const auto s = LastFrameSamples(c);
        ASSERT_EQ(s.size(), 4u);
        for (size_t i = 0; i < s.size(); ++i)
            for (size_t j = i + 1; j < s.size(); ++j)
                EXPECT_NE(s[i].key, s[j].key) << i << " vs " << j;
    }

}
