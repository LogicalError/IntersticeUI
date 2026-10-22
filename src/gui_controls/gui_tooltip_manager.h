#pragma once

#include <gui_core/gui_id.h>

namespace GUI
{
    class TooltipManager
    {
    public:
        float delaySeconds = 0.5f;

        void BeginFrame(ID currentHoveredID, ID currentFocusedID, float elapsedTimeSecs,
                        bool hasMouseMove, bool hasMousePress, bool hasKeyboard, bool hasScroll, bool menuIsOpen);

        // Returns true if the tooltip for targetID should be shown this frame.
        bool ShouldShow(ID targetID);

        // Call after OnGUI. Hides the tooltip if nothing declared it this frame.
        void EndFrame();

        bool IsVisible() const { return m_isVisible; }

    private:
        ID    m_activeTargetID   = ID::Invalid();
        ID    m_prevFocusedID    = ID::Invalid();
        float m_targetStartTime  = 0.0f;
        float m_currentTime      = 0.0f;
        bool  m_isReady          = false;
        bool  m_isVisible        = false;
        bool  m_declaredThisFrame = false;
        bool  m_triggeredByFocus = false; // dismiss on any mouse activity
        bool  m_suppressHover   = false; // after focus-trigger, suppress hover tooltips until mouse moves
        bool  m_pressedLastFrame = false; // a press's focus change lands on the next frame
    };

} // namespace GUI
