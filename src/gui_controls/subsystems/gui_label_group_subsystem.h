#pragma once

#include <unordered_map>
#include <vector>

#include <gui_core/gui_id.h>
#include <gui_core/elements/gui_element.h>
#include <gui_core/text/gui_text_system.h>

namespace tachyon
{
    class FramePipeline;
    struct FrameContext;
    class ElementStorage;
    class GUIStateManager;
    class GUITheme;

    /// Labels and DragLabels in a label group share a width; dragging any label's right
    /// edge resizes them all. Per-group state persists across frames, keyed by group ID.
    ///
    ///   OnPreConstruct  -> reset transient per-group fields
    ///   OnPostConstruct -> finalize edge state once all labels have emitted
    class LabelGroupSubsystem
    {
    public:
        /// State of one label group, kept across frames by group ID.
        struct State
        {
            int32_t labelWidth = 120;   ///< shared label width, CSS px
            int32_t minWidth   = 40;    ///< CSS px
            int32_t maxWidth   = 500;   ///< CSS px
            bool    resizable  = true;  ///< false: the width fits the widest label instead
            bool    edgeHovered     = false;   ///< a label's resize edge is hovered
            bool    edgeDragging    = false;   ///< a label's resize edge is being dragged
            bool    anyLabelHovered = false; ///< transient: set during construction if any label has hover
            std::vector<tachyon::TextID> labelTextIDs; ///< transient: rebuilt each frame for auto-size
            int32_t maxLabelWidthThisFrame = 0;    ///< transient: widest label (pixels), for auto-width
            std::vector<tachyon::ElementRef> labelRefs; ///< transient: this frame's labels, in order
        };

        LabelGroupSubsystem(FramePipeline& frame,
                            ElementStorage& elementStorage,
                            GUIStateManager& guiStateManager,
                            GUITheme& theme);

        /// Opens group `id` at the current nesting depth; widths are CSS px, and `defaultWidth`
        /// applies only when the group is new. Called by GUIApplication's label-group methods.
        void   BeginGroup(tachyon::ID id, int32_t defaultWidth, int32_t minWidth, int32_t maxWidth, bool resizable = true);
        /// Closes the innermost group.
        void   EndGroup();
        /// The innermost group when it was opened at the current nesting depth, else nullptr.
        State* GetActiveGroup();
        /// Adds a label to the innermost group and draws its resize edge; a group must be open.
        void   AddDragger(tachyon::ElementRef labelRef);

        /// True iff every BeginGroup was matched by EndGroup.
        bool IsBalanced() const { return m_stack.empty(); }

    private:
        void OnPreConstruct();
        void OnPostConstruct();
        void OnEndFrame();

        struct StackEntry {
            State*   state;
            uint32_t nestingDepth;
        };

        std::unordered_map<tachyon::ID, State> m_states;
        std::vector<StackEntry>            m_stack;

        ElementStorage&  m_elementStorage;
        GUIStateManager& m_guiStateManager;
        GUITheme&        m_theme;
    };
}
