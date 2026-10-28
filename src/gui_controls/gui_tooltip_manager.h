#pragma once

#include <gui_core/gui_id.h>

#include <vector>

namespace tachyon
{
    /// @addtogroup controls_menus
    /// @{

    /// Decides which element's tooltip shows: the hovered one after a delay, or at once
    /// for focus moved by the keyboard. Presses, keys, scrolling and open menus dismiss it.
    class TooltipManager
    {
    public:
        float delaySeconds = 0.5f;   ///< hover time before a tooltip shows

        /// Updates the target from this frame's hover, focus and input; call before OnGUI.
        void BeginFrame(ID currentHoveredID, ID currentFocusedID, float elapsedTimeSecs,
                        bool hasMouseMove, bool hasMousePress, bool hasKeyboard, bool hasScroll, bool menuIsOpen);

        /// The element the tooltip is for: the hovered one, or the focused one after a Tab.
        ID GetActiveTargetID() const { return m_activeTargetID; }
        /// The elements that take the active target's states (PropagatesDescendantState); a
        /// tooltip on any of them shows too. Call after BeginFrame.
        void SetTargetAncestors(std::vector<ID> ancestors) { m_targetAncestors = std::move(ancestors); }

        /// True when the tooltip for `targetID` should be shown this frame: it is the
        /// active target or one around it, and no other tooltip was shown this frame, so
        /// a control's own tooltip (declared first) wins over its container's. A true
        /// result declares it, keeping it visible.
        bool ShouldShow(ID targetID);

        /// Call after OnGUI. Hides the tooltip if nothing declared it this frame.
        void EndFrame();

        /// True while a tooltip is shown.
        bool IsVisible() const { return m_isVisible; }

    private:
        ID    m_activeTargetID   = ID::Invalid();
        std::vector<ID> m_targetAncestors;
        ID    m_prevFocusedID    = ID::Invalid();
        float m_targetStartTime  = 0.0f;
        float m_currentTime      = 0.0f;
        bool  m_isReady          = false;
        bool  m_isVisible        = false;
        bool  m_declaredThisFrame = false;
        bool  m_triggeredByFocus = false; ///< dismiss on any mouse activity
        bool  m_suppressHover   = false; ///< after focus-trigger, suppress hover tooltips until mouse moves
        bool  m_pressedLastFrame = false; ///< a press's focus change lands on the next frame
    };

    /// @}
} // namespace tachyon
