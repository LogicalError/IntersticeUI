#pragma once

#include <cstdint>

namespace GUI
{
    // Animation time locked to the display refresh. Frames reach the screen on
    // vsyncs, a whole number of refresh periods after the previous one, but the CPU
    // sees those intervals with jitter. The clock advances by whole periods,
    // rounded against the wall clock, so animations step evenly and never drift
    // from real time by more than half a period. Without a known refresh rate it
    // follows the wall clock.
    class FrameClock
    {
    public:
        // Once per frame, before anything reads the time. nowNanos is a monotonic
        // timestamp (SteadyClockNowNanos); refreshHz is 0 when unknown.
        void Tick(int64_t nowNanos, float refreshHz);

        // Seconds since the first Tick.
        double Seconds() const { return m_seconds; }
        // Seconds the last Tick advanced; 0 on the first.
        double DeltaSeconds() const { return m_deltaSeconds; }

    private:
        int64_t m_originNanos  = 0;
        bool    m_started      = false;
        double  m_seconds      = 0.0;
        double  m_deltaSeconds = 0.0;
    };
}
