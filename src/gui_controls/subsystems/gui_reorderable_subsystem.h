#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include <gui_core/gui_id.h>
#include <gui_core/gui_base_types.h>
#include <gui_core/elements/gui_element.h>

namespace GUI
{
    enum class ReorderDirection : uint8_t { Horizontal, Vertical };

    // Persistent drag state; one drag at a time application-wide.
    struct ReorderableDragState
    {
        bool     isDragging    = false;
        bool     dragActivated = false; // true once movement exceeds the drag threshold
        bool     isWrapped     = false;
        GUI::ID  groupID       = GUI::ID::Invalid();
        int      draggedIndex   = -1;
        int      insertionIndex = -1;
        int      itemCount      = 0;
        std::vector<GUI::IntRect> originalRects; // snapshot at drag start
        // Child IDs at drag start, in slot order, so MouseUp can resolve slots by ID
        // (storage is already visually reordered by then).
        std::vector<GUI::ID> originalIDs;
        GUI::int2  dragStartMousePos{};
        GUI::int2  dragOffset{};       // cumulative mouse delta from drag start
        ReorderDirection direction = ReorderDirection::Vertical;
        GUI::IntRect groupRect{};      // snapshot of group bounds at drag start
    };

    // Valid only between BeginReorderableGroup / EndReorderableGroup; handlers must capture.
    struct ReorderableConstructionState
    {
        GUI::ID groupID         = GUI::ID::Invalid();
        ReorderDirection direction = ReorderDirection::Vertical;
        bool    isWrapped       = false;
        bool    reorderApplied  = false;
        std::function<void(int, int)>             onReorder;
        std::function<void(int, GUI::int2)>       onDragOut;       // captured into item lambdas
        std::function<void(int, GUI::int2)>       dragVisualizer;
        int                          nextItemIndex = 0;
        GUI::ElementRef              groupRef      = GUI::ElementRef::Invalid();
        std::vector<GUI::ElementRef> itemRefs;
    };

    class FramePipeline;
    struct FrameContext;

    // Drag and construction state of drag-to-reorder groups, used by the
    // Begin/EndReorderableGroup and Begin/EndReorderableItem widgets.
    class ReorderableSubsystem
    {
    public:
        explicit ReorderableSubsystem(FramePipeline& frame);

        ReorderableDragState&         Drag()         { return m_drag; }
        ReorderableConstructionState& Construction() { return m_construction; }

        // True iff every BeginReorderableGroup was matched by End.
        bool IsBalanced() const { return m_construction.groupID == GUI::ID::Invalid(); }

    private:
        void OnPreConstruct(FrameContext& ctx);
        void OnEndFrame();

        ReorderableDragState         m_drag{};
        ReorderableConstructionState m_construction{};
    };
}
