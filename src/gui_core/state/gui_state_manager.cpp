#include <gui_core/elements/gui_element_storage.h>

#include <gui_core/input/gui_input_state.h>

#include <gui_core/state/gui_state_manager.h>

#include <assert.h>
#include <gui_core/diagnostics/gui_logging.h>

namespace interstice
{
    ElementState GUIStateManager::GetCurrentState(ID id) const
    {
        if (id == ID::Invalid())
            return ElementState::None;

        ElementState s = ElementState::None;
        if (m_construction.IsDisabled()) s |= ElementState::Disabled;
        if (m_construction.IsSelected()) s |= ElementState::Selected;

        // Transient states come from the previous frame's hit-test.
        const ID hoveredID      = m_interaction.GetHoveredID();
        const bool hoverHidden  = m_interaction.IsHoverSuppressed();
        if (id == hoveredID && !hoverHidden)        s |= ElementState::Hover;
        if (id == m_interaction.GetActiveID())      s |= ElementState::Active;
        if (m_interaction.IsActiveFlashing(id))     s |= ElementState::Active;
        if (id == m_interaction.GetFocusedID())     s |= ElementState::Focused;

        if (!hoverHidden) {
            for (ID pid : m_interaction.GetPropagatedHoverIDs())
                if (pid == id) { s |= ElementState::Hover; break; }
        }
        for (ID pid : m_interaction.GetPropagatedFocusedIDs())
            if (pid == id) { s |= ElementState::Focused; break; }

        // A label shows focus when its bound control is focused.
        bool forceFocus = false;
        const ID pendingLabelID = m_labels.GetPendingLabelID();
        const ID focusedID      = m_interaction.GetFocusedID();
        if (pendingLabelID == focusedID && pendingLabelID == id)
        {
            forceFocus = true;
        }
        else
        {
            const auto& assoc = m_labels.GetFocusedAssociatedLabelIDs();
            for (size_t i = 0; i < assoc.size(); i++)
            {
                if (assoc[i] == id)
                    forceFocus = true;
            }
        }

        if (forceFocus)
            s |= ElementState::Focused;

        return (s & ElementState::VisualMask);
    }

    bool GUIStateManager::BeginFrame()
    {
        m_construction.Reset();
        m_labels.ClearPendingLabel();
        m_drag.BeginFrame();
        m_scrolling.BeginFrame();
        ClearModifiedFlag();
        m_elementDataStore.BeginFrame();
        return true;
    }

    FrameHover GUIStateManager::EndFrame(const FrameInput& input, IDManager& idManager)
    {
        FrameHover hover;

        // A modal window blocks interaction with all others and becomes the active window.
        const ID modalWindowID = input.modalWindowID;
        m_interaction.SetActiveWindowID(modalWindowID != ID::Invalid() ? modalWindowID : input.activeWindowID);

        // The pointer goes to the window under it, except while an interaction that
        // started in another window is still going.
        if (modalWindowID != ID::Invalid())
        {
            m_interactionWindowID = modalWindowID;
            hover.hoverWindowID   = modalWindowID;
        }
        else if (m_interaction.GetActiveID() == ID::Invalid() || m_interactionWindowID == ID::Invalid())
        {
            hover.hoverWindowID   = input.windowUnderMouse;
            m_interactionWindowID = m_interaction.GetActiveID() != ID::Invalid() ? hover.hoverWindowID : ID::Invalid();
        }
        else
            hover.hoverWindowID = m_interactionWindowID;

        if (input.hitTest && hover.hoverWindowID != ID::Invalid())
            hover.overID = input.hitTest(hover.hoverWindowID, input.mousePos);

        // No hover while the application doesn't have OS focus.
        if (input.activeWindowID == ID::Invalid())
            hover.hoverID = ID::Invalid();
        else if (m_interaction.GetCaptureID() != ID::Invalid())
            hover.hoverID = m_interaction.GetCaptureID();
        else if (m_interaction.GetActiveID() == ID::Invalid() || m_drag.IsDragging())
            hover.hoverID = hover.overID;
        else
            hover.hoverID = m_interaction.GetActiveID();

        // During a drag only valid drop targets show hover; text drags have their own.
        ID textDragTarget = ID::Invalid();
        if (m_drag.IsDragging())
            hover.hoverID = m_drag.GetValidatedTargetID();
        else if (input.textDragTarget && input.textDragTarget(hover.hoverWindowID, input.mousePos, textDragTarget))
            hover.hoverID = textDragTarget;

        const ID           overID       = hover.hoverID;
        const ID           rawOverID    = hover.overID;
        const MouseButtons mouseButtons = input.mouseButtons;

        // Drag events last one frame.
        m_drag.ClearEvents();

        m_interaction.EndFrame(overID, mouseButtons);

        if (!m_construction.IsBalanced())
        {
            Diagnostics::ReportMisuse(Diagnostics::MakeLogCategory("state"),
                "GUIStateManager: PushDisabled/PushSelected/PushIndeterminate left unpopped at EndFrame (reset)");
            m_construction.Reset();
        }

        const ID   hoveredForDrop = (rawOverID == ID::Invalid()) ? overID : rawOverID;
        const bool leftButtonDown = (mouseButtons & MouseButtons::Left) != MouseButtons::None;
        m_drag.EndFrameUpdate(hoveredForDrop, leftButtonDown);

        // Keep data for IDs in a live interaction even if their handler didn't run
        // (e.g. a stationary drag), then evict everything untouched.
        m_elementDataStore.Touch(m_interaction.GetCaptureID());
        m_elementDataStore.Touch(m_interaction.GetFocusedID());
        m_elementDataStore.Touch(m_drag.GetDraggedID());
        m_elementDataStore.Touch(m_interaction.GetActiveID());
        m_elementDataStore.EndFrame();

        m_scrolling.EndFrame(idManager);
        return hover;
    }
}
