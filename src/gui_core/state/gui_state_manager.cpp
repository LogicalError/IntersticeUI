#include <gui_core/elements/gui_element_storage.h>

#include <gui_core/input/gui_input_state.h>

#include <gui_core/state/gui_state_manager.h>

#include <assert.h>

namespace GUI
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

    void GUIStateManager::EndFrame(ID overID, MouseButtons mouseButtons, IDManager& idManager, ID rawOverID)
    {
        // Drag events last one frame.
        m_drag.ClearEvents();

        m_interaction.EndFrame(overID, mouseButtons);

        if (!m_construction.IsBalanced())
        {
            assert(false && "GUIStateManager construction phase levels not "
                            "balanced at EndFrame");
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
    }
}
