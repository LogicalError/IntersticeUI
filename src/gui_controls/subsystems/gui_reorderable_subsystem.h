#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include <gui_core/gui_id.h>
#include <gui_core/gui_base_types.h>
#include <gui_core/elements/gui_element.h>
#include <gui_core/input/gui_pointer_drag.h>

namespace tachyon
{
    /// @addtogroup controls
    /// @{

    /// Main axis of a reorderable group.
    enum class ReorderDirection : uint8_t { Horizontal, Vertical };

    /// Persistent drag state; one drag at a time application-wide.
    struct ReorderableDragState
    {
        PointerDrag press;              ///< the item's press; a drag once it moves the drag distance
        tachyon::int2 cursorLocal{};    ///< latest pointer, layout space (like originalRects, groupRect)
        tachyon::int2 cursorScreen{};   ///< latest pointer, screen pixels
        bool     isWrapped     = false; ///< the group wraps its items onto several lines
        tachyon::ID  groupID       = tachyon::ID::Invalid();
        int      draggedIndex   = -1;   ///< slot the item was dragged from
        int      insertionIndex = -1;   ///< slot it would drop into
        int      itemCount      = 0;
        std::vector<tachyon::IntRect> originalRects; ///< item rects at drag start, layout space
        /// Child IDs at drag start, in slot order, so MouseUp can resolve slots by ID
        /// (storage is already visually reordered by then).
        std::vector<tachyon::ID> originalIDs;
        ReorderDirection direction = ReorderDirection::Vertical;
        tachyon::IntRect groupRect{};      ///< group bounds at drag start, layout space

        /// An item is pressed (maybe not yet dragged).
        bool IsPressed() const { return press.id != tachyon::ID::Invalid(); }
        /// An item is pressed and has moved the drag distance.
        bool IsDragging() const { return IsPressed() && press.dragging; }
        /// The pointer's travel since the press, layout space.
        tachyon::int2 Offset() const { return cursorLocal - press.pressLocal; }
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
        /// Alt + an arrow along the group with the focus in item `keyboardFrom`: moved by
        /// `keyboardStep` in EndReorderableGroup, after the caller's items are built.
        int keyboardFrom = -1;
        int keyboardStep = 0;
    };

    /// After a keyboard move: where the focus was in the moved item, to put it back there
    /// next frame, in the item's new slot, unless its ID moved with the item.
    struct ReorderableFocusFollow
    {
        tachyon::ID           groupID = tachyon::ID::Invalid();
        int                   toSlot  = -1;
        tachyon::ID           focusID = tachyon::ID::Invalid();
        std::vector<uint32_t> path;   ///< child positions from the item down to the focused element
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
        ReorderableFocusFollow&       FocusFollow()  { return m_focusFollow; }    ///< after a keyboard move

        /// True iff every BeginReorderableGroup was matched by EndReorderableGroup.
        bool IsBalanced() const { return m_construction.groupID == tachyon::ID::Invalid(); }

    private:
        void OnPreConstruct(FrameContext& ctx);
        void OnEndFrame();

        ReorderableDragState         m_drag{};
        ReorderableConstructionState m_construction{};
        ReorderableFocusFollow       m_focusFollow{};
    };
    /// @}
}
