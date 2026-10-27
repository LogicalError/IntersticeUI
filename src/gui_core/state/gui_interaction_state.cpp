#include <gui_core/state/gui_interaction_state.h>

namespace tachyon
{
    void InteractionState::EndFrame(ID overID, MouseButtons mouseButtons)
    {
        m_clickID = ID::Invalid();
        if (m_activeFlashID != ID::Invalid()
            && (m_frameTimeSecs - m_activeFlashStartSecs) >= kActiveFlashDuration)
            m_activeFlashID = ID::Invalid();

        // Delayed one frame: Dispatch runs after EndFrame and must still deliver the release.
        if (m_pendingCaptureClear)
        {
            m_captureID           = ID::Invalid();
            m_pendingCaptureClear = false;
        }
        const bool anyButtonDown = mouseButtons           != MouseButtons::None;
        const bool prevAnyDown   = m_prevMouseButtonFlags != MouseButtons::None;
        if (m_captureID != ID::Invalid() && !anyButtonDown && prevAnyDown)
            m_pendingCaptureClear = true;

        m_hoveredID = overID;

        // Latches to hovered on the left-press edge, clears on release.
        const bool leftButtonDown     = (mouseButtons           & MouseButtons::Left) != MouseButtons::None;
        const bool prevLeftButtonDown = (m_prevMouseButtonFlags & MouseButtons::Left) != MouseButtons::None;
        if (leftButtonDown && !prevLeftButtonDown)
            m_activeID = m_hoveredID;
        else if (!leftButtonDown)
            m_activeID = ID::Invalid();

        m_prevMouseButtonFlags = mouseButtons;
    }
}
