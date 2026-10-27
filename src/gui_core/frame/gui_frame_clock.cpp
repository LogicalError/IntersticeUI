#include <gui_core/frame/gui_frame_clock.h>

#include <cmath>

namespace tachyon
{
    void FrameClock::Tick(int64_t nowNanos, float refreshHz)
    {
        if (!m_started)
        {
            m_started      = true;
            m_originNanos  = nowNanos;
            m_seconds      = 0.0;
            m_deltaSeconds = 0.0;
            return;
        }

        const double wallSeconds = double(nowNanos - m_originNanos) * 1e-9;
        double next = wallSeconds;
        if (refreshHz > 0.0f)
        {
            // Rounded against the accumulated clock, not the last frame, so a
            // slightly-off refresh rate can't accumulate drift.
            const double period  = 1.0 / double(refreshHz);
            const double periods = std::round((wallSeconds - m_seconds) / period);
            next = m_seconds + (periods > 0.0 ? periods : 0.0) * period;
        }
        if (next < m_seconds)
            next = m_seconds;   // the wall clock can trail by up to half a period

        m_deltaSeconds = next - m_seconds;
        m_seconds      = next;
    }
}
