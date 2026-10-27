// Unit tests for FrameClock, the vsync-paced animation clock:
//   - the first tick starts at 0
//   - jittered frame intervals step by exactly one refresh period
//   - a missed vsync advances two periods
//   - a slightly wrong refresh rate can't drift from the wall clock
//   - two ticks within one vsync don't advance the clock
//   - without a refresh rate it follows the wall clock, never backwards

#include <gtest/gtest.h>

#include <gui_core/frame/gui_frame_clock.h>

#include <cmath>

namespace tachyon
{
    namespace
    {
        constexpr int64_t kStart = 5'000'000'000;   // arbitrary origin
        int64_t Ms(double ms) { return kStart + int64_t(std::llround(ms * 1e6)); }
    }

    TEST(FrameClockTest, FirstTickStartsAtZero)
    {
        FrameClock clock;
        clock.Tick(kStart, 60.0f);
        EXPECT_EQ(clock.Seconds(), 0.0);
        EXPECT_EQ(clock.DeltaSeconds(), 0.0);
    }

    TEST(FrameClockTest, JitteredFramesStepByExactlyOnePeriod)
    {
        const double period = 1.0 / 120.0;
        const double jitterMs[] = { +1.2, -0.9, +2.5, -2.0, +0.4, -1.6, +3.0, -3.0 };
        FrameClock clock;
        clock.Tick(kStart, 120.0f);
        for (int i = 1; i <= 8; ++i)
        {
            clock.Tick(Ms(i * period * 1000.0 + jitterMs[i - 1]), 120.0f);
            EXPECT_NEAR(clock.DeltaSeconds(), period, 1e-12) << "frame " << i;
            EXPECT_NEAR(clock.Seconds(), i * period, 1e-12) << "frame " << i;
        }
    }

    TEST(FrameClockTest, MissedVsyncAdvancesTwoPeriods)
    {
        const double period = 1.0 / 60.0;
        FrameClock clock;
        clock.Tick(kStart, 60.0f);
        clock.Tick(Ms(1000.0 * period), 60.0f);
        clock.Tick(Ms(1000.0 * 3.0 * period + 0.7), 60.0f);
        EXPECT_NEAR(clock.DeltaSeconds(), 2.0 * period, 1e-12);
        EXPECT_NEAR(clock.Seconds(), 3.0 * period, 1e-12);
    }

    TEST(FrameClockTest, WrongRefreshRateDoesNotDrift)
    {
        // Display really runs at 59.94 Hz; the clock is told 60.
        const double realPeriod = 1.0 / 59.94;
        FrameClock clock;
        clock.Tick(kStart, 60.0f);
        for (int i = 1; i <= 6000; ++i)
        {
            const double wall = i * realPeriod;
            clock.Tick(Ms(wall * 1000.0), 60.0f);
            ASSERT_LE(std::abs(clock.Seconds() - wall), 0.5 / 60.0 + 1e-9) << "frame " << i;
        }
    }

    TEST(FrameClockTest, TicksWithinOneVsyncDoNotAdvance)
    {
        FrameClock clock;
        clock.Tick(kStart, 60.0f);
        clock.Tick(Ms(2.0), 60.0f);
        EXPECT_EQ(clock.Seconds(), 0.0);
        EXPECT_EQ(clock.DeltaSeconds(), 0.0);
    }

    TEST(FrameClockTest, UnknownRefreshRateFollowsWallClockMonotonically)
    {
        FrameClock clock;
        clock.Tick(kStart, 0.0f);
        clock.Tick(Ms(13.0), 0.0f);
        EXPECT_NEAR(clock.Seconds(), 0.013, 1e-9);

        // Paced ahead of the wall clock, then the rate becomes unknown.
        clock.Tick(Ms(25.0), 60.0f);   // 12 ms behind rounds up to one period
        const double paced = clock.Seconds();
        EXPECT_GT(paced, 0.027);
        clock.Tick(Ms(27.0), 0.0f);
        EXPECT_EQ(clock.Seconds(), paced);
        EXPECT_EQ(clock.DeltaSeconds(), 0.0);
    }
}
