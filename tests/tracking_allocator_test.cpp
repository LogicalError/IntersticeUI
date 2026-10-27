#include <gtest/gtest.h>

#include <gui_core/memory/gui_tracking_allocator.h>

#include <cstdint>
#include <cstring>

namespace
{
    using tachyon::AllocationHooks;
    using tachyon::TrackingAllocator;

    TEST(TrackingAllocator, CountsLiveAllocationsAndBytes)
    {
        TrackingAllocator tracker;
        const AllocationHooks hooks = tracker.Hooks();
        ASSERT_TRUE(hooks.IsSet());

        void* a = hooks.allocate(100, 16, hooks.user);
        void* b = hooks.allocate(28, 8, hooks.user);
        EXPECT_EQ(tracker.LiveAllocations(), 2u);
        EXPECT_EQ(tracker.LiveBytes(), 128u);
        EXPECT_EQ(tracker.TotalAllocations(), 2u);

        hooks.free(a, hooks.user);
        EXPECT_EQ(tracker.LiveAllocations(), 1u);
        EXPECT_EQ(tracker.LiveBytes(), 28u);
        hooks.free(b, hooks.user);
        EXPECT_EQ(tracker.LiveAllocations(), 0u);
        EXPECT_EQ(tracker.ReportLeaks(), 0u);
    }

    TEST(TrackingAllocator, HonoursAlignment)
    {
        TrackingAllocator tracker;
        const AllocationHooks hooks = tracker.Hooks();
        for (size_t alignment : { 8u, 16u, 64u, 256u })
        {
            void* p = hooks.allocate(10, alignment, hooks.user);
            ASSERT_NE(p, nullptr);
            EXPECT_EQ(reinterpret_cast<uintptr_t>(p) % alignment, 0u) << alignment;
            hooks.free(p, hooks.user);
        }
    }

    TEST(TrackingAllocator, ReallocateKeepsContentsAndUpdatesSize)
    {
        TrackingAllocator tracker;
        const AllocationHooks hooks = tracker.Hooks();
        auto* p = static_cast<char*>(hooks.reallocate(nullptr, 4, 16, hooks.user));
        ASSERT_NE(p, nullptr);
        std::memcpy(p, "abc", 4);
        EXPECT_EQ(tracker.LiveBytes(), 4u);

        p = static_cast<char*>(hooks.reallocate(p, 1000, 16, hooks.user));
        ASSERT_NE(p, nullptr);
        EXPECT_STREQ(p, "abc");
        EXPECT_EQ(reinterpret_cast<uintptr_t>(p) % 16, 0u);
        EXPECT_EQ(tracker.LiveAllocations(), 1u);
        EXPECT_EQ(tracker.LiveBytes(), 1000u);

        hooks.free(p, hooks.user);
        hooks.free(nullptr, hooks.user);
        EXPECT_EQ(tracker.LiveAllocations(), 0u);
    }

    TEST(TrackingAllocator, ReportsLeaks)
    {
        TrackingAllocator tracker;
        const AllocationHooks hooks = tracker.Hooks();
        void* leaked = hooks.allocate(48, 16, hooks.user);
        EXPECT_EQ(tracker.ReportLeaks(), 1u);
        hooks.free(leaked, hooks.user);
    }
}
