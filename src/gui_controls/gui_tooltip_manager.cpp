#include <gui_controls/gui_tooltip_manager.h>

namespace GUI
{
    void TooltipManager::BeginFrame(ID currentHoveredID, ID currentFocusedID, float elapsedTimeSecs,
                                    bool hasMouseMove, bool hasMousePress, bool hasKeyboard, bool hasScroll, bool menuIsOpen)
    {
        m_currentTime = elapsedTimeSecs;
        m_declaredThisFrame = false;
        // A press moves focus during that frame's dispatch (after this runs), so
        // the focus change shows up one frame later; it is still mouse-driven.
        const bool pressedLastFrame = m_pressedLastFrame;
        m_pressedLastFrame = hasMousePress;

        // Menu open always kills tooltip
        if (menuIsOpen)
        {
            m_activeTargetID = ID::Invalid();
            m_isReady = false;
            m_isVisible = false;
            m_triggeredByFocus = false;
            m_suppressHover = false;
            m_prevFocusedID = currentFocusedID;
            return;
        }

        bool anyDismiss = hasMousePress || hasKeyboard || hasScroll;
        if (m_triggeredByFocus)
            anyDismiss = anyDismiss || hasMouseMove; // mouse move also dismisses focus tooltips

        if (anyDismiss)
        {
            m_activeTargetID = ID::Invalid();
            m_isReady = false;
            m_isVisible = false;
            m_triggeredByFocus = false;
            // After any dismissal, suppress hover tooltips until mouse moves
            m_suppressHover = !hasMouseMove;
            m_prevFocusedID = currentFocusedID;
            return;
        }

        // Mouse movement lifts hover suppression
        if (m_suppressHover && hasMouseMove)
            m_suppressHover = false;

        // Focus changed without a mouse press: keyboard navigation.
        bool focusChangedByKeyboard = (currentFocusedID != m_prevFocusedID &&
                                       currentFocusedID != ID::Invalid() &&
                                       !hasMousePress && !pressedLastFrame);
        m_prevFocusedID = currentFocusedID;

        if (focusChangedByKeyboard)
        {
            // Show the focused element's tooltip right away.
            m_activeTargetID = currentFocusedID;
            m_targetStartTime = elapsedTimeSecs;
            m_isReady = true;
            m_isVisible = false;
            m_triggeredByFocus = true;
            m_suppressHover = false;
            return;
        }

        // Hover tracking
        if (m_suppressHover || m_triggeredByFocus)
            return;

        if (currentHoveredID != m_activeTargetID)
        {
            m_activeTargetID = currentHoveredID;
            m_targetStartTime = elapsedTimeSecs;
            m_isReady = false;
            m_isVisible = false;
        }
        else if (m_activeTargetID != ID::Invalid() && !m_isReady)
        {
            if (elapsedTimeSecs - m_targetStartTime >= delaySeconds)
                m_isReady = true;
        }
    }

    bool TooltipManager::ShouldShow(ID targetID)
    {
        if (targetID == ID::Invalid()) return false;
        if (targetID != m_activeTargetID) return false;
        if (!m_isReady) return false;

        m_declaredThisFrame = true;
        m_isVisible = true;
        return true;
    }

    void TooltipManager::EndFrame()
    {
        if (m_isVisible && !m_declaredThisFrame)
        {
            m_isVisible = false;
        }
    }

} // namespace GUI
