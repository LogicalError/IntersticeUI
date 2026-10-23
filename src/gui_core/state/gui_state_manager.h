// GUI State Manager: owns the per-frame state sub-objects and runs their
// BeginFrame / EndFrame in order. Callers use the narrow accessors
// (`sm.Interaction()`, `sm.Drag()`, ...) to depend only on what they use.
//   - InteractionState     - hover / active / capture / focus / click / active window
//   - DragDropState        - drag & drop, visualizers, drag events
//   - ConstructionScopes   - push/pop disabled / selected / indeterminate
//   - TextEditorFocusState - focused text editor + previous-frame value
//   - LabelBinding         - pending label ID + focused-associated label IDs
//   - CursorState          - requested mouse cursor
//   - ScrollManagement     - per-element scroll offsets, pruned at EndFrame

#pragma once

#include <gui_core/gui_id.h>

#include <gui_core/elements/gui_element_data_store.h>

#include <gui_core/style/gui_style.h>

#include <gui_core/input/gui_input_enums.h>

#include <gui_core/state/gui_interaction_state.h>
#include <gui_core/state/gui_drag_drop_state.h>
#include <gui_core/state/gui_scroll_management.h>
#include <gui_core/state/gui_state_atomic_values.h>

#include <gui_controls/gui_label_binding.h>

#include <functional>

namespace interstice
{
    class IDManager;

    // What the window layer reports at the end of a frame's construction;
    // GUIStateManager::EndFrame turns it into the hovered element and the active window.
    struct FrameInput
    {
        ID           activeWindowID   = ID::Invalid();   // the OS's active window; Invalid while the app has no focus
        ID           modalWindowID    = ID::Invalid();   // top modal window, which blocks the others
        ID           windowUnderMouse = ID::Invalid();
        int2         mousePos {};                        // screen pixels
        MouseButtons mouseButtons     = MouseButtons::None;

        // The element at pos in window, or Invalid.
        std::function<ID(ID window, int2 pos)> hitTest;
        // While text is dragged: the text drop target at pos in window. Optional.
        std::function<bool(ID window, int2 pos, ID& target)> textDragTarget;
    };

    // Where the frame's input goes, from GUIStateManager::EndFrame.
    struct FrameHover
    {
        ID overID        = ID::Invalid();   // the element under the cursor
        ID hoverID       = ID::Invalid();   // what counts as hovered: the capture, the active element, a drop target
        ID hoverWindowID = ID::Invalid();   // the window the pointer input belongs to
    };

    class GUIStateManager
    {
        InteractionState     m_interaction;
        DragDropState        m_drag;
        ConstructionScopes   m_construction;
        TextEditorFocusState m_textEditorFocus;
        LabelBinding         m_labels;
        CursorState          m_cursor;
        ScrollManagement     m_scrolling;

        // See gui_element_data_store.h.
        ElementDataStore m_elementDataStore;

        // Transient state changed this frame.
        bool m_modified = false;

        // The window an interaction (a press, a drag) started in; it keeps the
        // pointer until the interaction ends, wherever the cursor goes.
        ID m_interactionWindowID = ID::Invalid();

    public:
        // -----------------------------------------------------------------
        // Sub-state accessors
        // -----------------------------------------------------------------
        InteractionState&           Interaction()             { return m_interaction; }
        const InteractionState&     Interaction() const       { return m_interaction; }

        DragDropState&              Drag()                    { return m_drag; }
        const DragDropState&        Drag() const              { return m_drag; }

        ConstructionScopes&         Construction()            { return m_construction; }
        const ConstructionScopes&   Construction() const      { return m_construction; }

        TextEditorFocusState&       TextEditorFocus()         { return m_textEditorFocus; }
        const TextEditorFocusState& TextEditorFocus() const   { return m_textEditorFocus; }

        LabelBinding&               Labels()                  { return m_labels; }
        const LabelBinding&         Labels() const            { return m_labels; }

        CursorState&                Cursor()                  { return m_cursor; }
        const CursorState&          Cursor() const            { return m_cursor; }

        ScrollManagement&           GetScrollManagement()       { return m_scrolling; }
        const ScrollManagement&     GetScrollManagement() const { return m_scrolling; }

        ElementDataStore&           GetElementDataStore()       { return m_elementDataStore; }
        const ElementDataStore&     GetElementDataStore() const { return m_elementDataStore; }

        // -----------------------------------------------------------------
        // Cross-cutting state
        // -----------------------------------------------------------------
        void ClearModifiedFlag()       { m_modified = false; }
        bool IsModified() const        { return m_modified; }
        void SetModified(bool v)       { m_modified = v; }

        // Composes id's state from Interaction, Construction and Labels.
        ElementState GetCurrentState(ID id) const;

        bool BeginFrame();
        // Resolves the active window, the window and element under the pointer and
        // the hovered element from `input`, then ends the frame's state.
        FrameHover EndFrame(const FrameInput& input, IDManager& idManager);
    };
}
