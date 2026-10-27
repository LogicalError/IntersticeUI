#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include <gui_core/gui_id.h>
#include <gui_core/gui_base_types.h>
#include <gui_core/elements/gui_element.h>

namespace tachyon
{
    /// Main axis of a reorderable group.
    enum class ReorderDirection : uint8_t { Horizontal, Vertical };

    /// Persistent drag state; one drag at a time application-wide.
    struct ReorderableDragState
    {
        bool     isDragging    = false;
        bool     dragActivated = false; ///< true once movement exceeds the drag threshold
        bool     isWrapped     = false; ///< the group wraps its items onto several lines
        tachyon::ID  groupID       = tachyon::ID::Invalid();
        int      draggedIndex   = -1;   ///< slot the item was dragged from
        int      insertionIndex = -1;   ///< slot it would drop into
        int      itemCount      = 0;
        std::vector<tachyon::IntRect> originalRects; ///< item rects at drag start, layout space
        /// Child IDs at drag start, in slot order, so MouseUp can resolve slots by ID
        /// (storage is already visually reordered by then).
        std::vector<tachyon::ID> originalIDs;
        tachyon::int2  dragStartMousePos{};
        tachyon::int2  dragOffset{};       ///< cumulative mouse delta from drag start
        ReorderDirection direction = ReorderDirection::Vertical;
        tachyon::IntRect groupRect{};      ///< group bounds at drag start, layout space
    };

    /// Construction state of the current reorderable group. Valid only between
    /// BeginReorderableGroup and EndReorderableGroup; handlers must capture what they need.
    struct ReorderableConstructionState
    {
        tachyon::ID groupID         = tachyon::ID::Invalid();
        ReorderDirection direction = ReorderDirection::Vertical;
        bool    isWrapped       = false;
        bool    reorderApplied  = false;   ///< the dragged item was moved in storage this frame
        std::function<void(int, int)>             onReorder;   ///< from index, to index
        /// Item index and screen cursor when an item is released outside the group.
        std::function<void(int, tachyon::int2)>       onDragOut;
        /// Draws a ghost for the dragged item (index, cursor) while it is outside the group.
        std::function<void(int, tachyon::int2)>       dragVisualizer;
        int                          nextItemIndex = 0;
        tachyon::ElementRef              groupRef      = tachyon::ElementRef::Invalid();
        std::vector<tachyon::ElementRef> itemRefs;
    };

    class FramePipeline;
    struct FrameContext;

    /// Drag and construction state of drag-to-reorder groups, used by the
    /// Begin/EndReorderableGroup and Begin/EndReorderableItem widgets.
    class ReorderableSubsystem
    {
    public:
        explicit ReorderableSubsystem(FramePipeline& frame);

        ReorderableDragState&         Drag()         { return m_drag; }           ///< the drag in progress
        ReorderableConstructionState& Construction() { return m_construction; }   ///< the group being built

        /// True iff every BeginReorderableGroup was matched by EndReorderableGroup.
        bool IsBalanced() const { return m_construction.groupID == tachyon::ID::Invalid(); }

    private:
        void OnPreConstruct(FrameContext& ctx);
        void OnEndFrame();

        ReorderableDragState         m_drag{};
        ReorderableConstructionState m_construction{};
    };
}
