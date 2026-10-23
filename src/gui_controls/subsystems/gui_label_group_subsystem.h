#pragma once

#include <unordered_map>
#include <vector>

#include <gui_core/gui_id.h>
#include <gui_core/elements/gui_element.h>
#include <gui_core/text/gui_text_system.h>

namespace interstice
{
    class FramePipeline;
    struct FrameContext;
    class ElementStorage;
    class GUIStateManager;
    class GUITheme;

    // Labels and DragLabels in a label group share a width; dragging any label's right
    // edge resizes them all. Per-group state persists across frames, keyed by group ID.
    //
    //   OnPreConstruct  → reset transient per-group fields
    //   OnPostConstruct → finalize edge state once all labels have emitted
    class LabelGroupSubsystem
    {
    public:
        struct State
        {
            // Widths in CSS pixels (laid out at the UI scale).
            int32_t labelWidth = 120;
            int32_t minWidth   = 40;
            int32_t maxWidth   = 500;
            bool    resizable  = true;
            bool    edgeHovered     = false;
            bool    edgeDragging    = false;
            bool    anyLabelHovered = false; // transient: set during construction if any label has hover
            std::vector<interstice::TextID> labelTextIDs; // transient: rebuilt each frame for auto-size
            int32_t maxLabelWidthThisFrame = 0;    // transient: accumulator for non-resizable auto-width
        };

        LabelGroupSubsystem(FramePipeline& frame,
                            ElementStorage& elementStorage,
                            GUIStateManager& guiStateManager,
                            GUITheme& theme);

        // Called from GUIApplication's Begin/End/Label/DragLabel methods.
        void   BeginGroup(interstice::ID id, int32_t defaultWidth, int32_t minWidth, int32_t maxWidth, bool resizable = true);
        void   EndGroup();
        State* GetActiveGroup();
        void   AddDragger(interstice::ElementRef labelRef);

        // True iff every BeginGroup was matched by EndGroup.
        bool IsBalanced() const { return m_stack.empty(); }

    private:
        void OnPreConstruct();
        void OnPostConstruct();
        void OnEndFrame();

        struct StackEntry {
            State*   state;
            uint32_t nestingDepth;
        };

        std::unordered_map<interstice::ID, State> m_states;
        std::vector<StackEntry>            m_stack;

        ElementStorage&  m_elementStorage;
        GUIStateManager& m_guiStateManager;
        GUITheme&        m_theme;
    };
}
