#pragma once

#include <gui_core/gui_id.h>
#include <gui_core/state/gui_scroll_management.h>
#include <gui_core/input/gui_input_enums.h>

#include <vector>

namespace interstice
{
    /// The hovered, active, focused and captured elements, the active window, the one-shot
    /// click, hover suppression and the focus scroll axis. Owned by GUIStateManager.
    class InteractionState
    {
        ID m_hoveredID      = ID::Invalid();
        ID m_activeID       = ID::Invalid();
        ID m_captureID      = ID::Invalid();
        ID m_focusedID      = ID::Invalid();
        ID m_activeWindowID = ID::Invalid();
        ID m_clickID        = ID::Invalid();

        /// Pressed look for keyboard/programmatic activation, which has no held button.
        /// Timed on the wall clock: one frame is imperceptible at high refresh rates.
        ID    m_activeFlashID        = ID::Invalid();
        float m_activeFlashStartSecs = 0.0f;
        static constexpr float kActiveFlashDuration = 0.12f; ///< ~120ms

        /// Seeded at BeginFrame so const queries can evaluate the flash window.
        float m_frameTimeSecs = 0.0f;

        /// PropagatesDescendantState ancestors of m_hoveredID / m_focusedID, recomputed each frame.
        std::vector<ID> m_propagatedHoverIDs;
        std::vector<ID> m_propagatedFocusedIDs;

        /// Hides mouse hover during keyboard navigation. Set by widgets that consume a
        /// nav key, cleared by InputManager on the next MouseMove.
        bool m_hoverSuppressed = false;

        /// Capture is released one frame after all buttons go up, so the captured
        /// element receives the release event.
        bool m_pendingCaptureClear = false;

        /// Axes for focus-change scroll-into-view; sizers narrow it so they don't
        /// scroll perpendicular to the drag.
        ScrollAxis m_focusScrollAxis = ScrollAxis::Both;

        /// Focus came from the keyboard (CSS :focus-visible): Tab sets it, a mouse press clears it.
        bool m_focusVisible = false;

        MouseButtons m_prevMouseButtonFlags = MouseButtons::None;

    public:
        ID GetHoveredID()      const { return m_hoveredID; }
        ID GetActiveID()       const { return m_activeID;  }
        ID GetCaptureID()      const { return m_captureID; }
        ID GetFocusedID()      const { return m_focusedID; }
        ID GetActiveWindowID() const { return m_activeWindowID; }

        void SetActiveWindowID(ID id) { m_activeWindowID = id; }

        void SetCaptureID(ID id) { m_captureID = id; m_pendingCaptureClear = false; }
        void ReleaseCaptureID()  { m_captureID = ID::Invalid(); m_pendingCaptureClear = false; }

        void SetFocusID(ID id)                         { m_focusedID = id; m_focusScrollAxis = ScrollAxis::Both; }
        void SetFocusIDNoScroll(ID id)                 { m_focusedID = id; m_focusScrollAxis = ScrollAxis::None; }
        void SetFocusIDScrollAxis(ID id, ScrollAxis a) { m_focusedID = id; m_focusScrollAxis = a; }
        ScrollAxis ConsumeFocusScrollAxis()            { ScrollAxis v = m_focusScrollAxis; m_focusScrollAxis = ScrollAxis::Both; return v; }

        /// Shows the focus ring (:focus-visible), e.g. on a dialog's default button.
        void SetFocusVisible(bool visible) { m_focusVisible = visible; }
        bool IsFocusVisible() const        { return m_focusVisible; }

        void SuppressHover()           { m_hoverSuppressed = true; }
        void ClearHoverSuppression()   { m_hoverSuppressed = false; }
        bool IsHoverSuppressed() const { return m_hoverSuppressed; }

        bool HasBeenClicked(ID id) const { return m_clickID == id; }
        void SetClicked(ID id)           { m_clickID = id; }

        /// The frame time in seconds that FlashActive is timed against; set once at BeginFrame.
        void  SetFrameTime(float secs) { m_frameTimeSecs = secs; }
        float GetFrameTime() const     { return m_frameTimeSecs; }

        /// Reports `id` as Active for about 120 ms: the pressed look of a keyboard or
        /// programmatic activation.
        void FlashActive(ID id)            { m_activeFlashID = id; m_activeFlashStartSecs = m_frameTimeSecs; }
        ID   GetActiveFlashID() const      { return m_activeFlashID; }
        bool IsActiveFlashing(ID id) const { return id == m_activeFlashID && (m_frameTimeSecs - m_activeFlashStartSecs) < kActiveFlashDuration; }

        const std::vector<ID>& GetPropagatedHoverIDs()   const { return m_propagatedHoverIDs;   }
        const std::vector<ID>& GetPropagatedFocusedIDs() const { return m_propagatedFocusedIDs; }
        void SetPropagatedHoverIDs(std::vector<ID> v)   { m_propagatedHoverIDs   = std::move(v); }
        void SetPropagatedFocusedIDs(std::vector<ID> v) { m_propagatedFocusedIDs = std::move(v); }

        /// Applies hover, active-ID edges and deferred capture release; clears the click
        /// and expired flash.
        void EndFrame(ID overID, MouseButtons mouseButtons);
    };
}
