#pragma once

#include <gui_core/gui_id.h>
#include <gui_core/style/gui_style_text.h>          // IconID

#include <gui_controls/gui_widget_state_types.h>     // MultiSelectState, ItemDragDropConfig, DoubleClickTracker

#include <string>
#include <vector>

// TreeView / ListView / GridView data + construction state.

namespace tachyon
{
    // ---- Transient tree/list construction state (valid between Begin/End) ----
    //
    // Valid only between Begin*/End* (reset in End*). Event handlers run at end of frame,
    // so they must capture the user-owned pointers instead of reading m_treeState.
    //
    /// Construction state of the current TreeView / ListView; internal.
    struct TreeViewState {
        tachyon::ID   containerID = tachyon::ID::Invalid();
        int       nextItemIndex = 0;
        int       totalItemCount = 0;
        int       depth = 0;                        ///< nesting depth (0 = top-level, no guide lines)
        int*      selectedIndex = nullptr;          ///< for ListView index-based selection
        tachyon::ID*  selectedID = nullptr;             ///< for single-ID selection
        MultiSelectState* multiSelect = nullptr;    ///< for multi-selection
        ItemDragDropConfig* dragDrop = nullptr;     ///< drag & drop config
    };

    // ---- GridView: icon grid ----
    //
    // Like TreeViewState: valid only between BeginGridView/EndGridView; handlers must
    // capture user-owned pointers.
    //
    /// Construction state of the current GridView; internal.
    struct GridViewState {
        tachyon::ID containerID = tachyon::ID::Invalid();
        int     nextItemIndex = 0;
        int*    selectedIndex = nullptr;
        MultiSelectState* multiSelect = nullptr;
        ItemDragDropConfig* dragDrop = nullptr;
        /// Slow-re-click rename state in the user's GridViewVirtualState. Both pointers are
        /// null in the non-virtual grid, which offers only Ctrl+Shift+Click and F2.
        DoubleClickTracker* clickTracker = nullptr;
        tachyon::ID*            pendingRenameClick = nullptr;   ///< see clickTracker
    };

    // ---- GridView rename lookup ----
    // Filled by GridViewItem for items with a renameBuffer; read by end-of-frame rename handlers.
    /// Rename buffer and original name of a grid item; internal.
    struct GridItemRenameInfo { std::string* renameBuf = nullptr; std::string renameSeed; };

    // ---- TreeView (flat-data, virtualized) -----------------------------
    // Caller-owned flat rows shown through a VirtualTable. Call RebuildTreeViewIndex after
    // structural changes. Selection, scroll and drag-drop live on the TableState; a row ID
    // maps to its flat index via visibleIdx[rowID.value.subRangeParts.index].
    /// One row of a flat-data tree view (VirtualTreeView); rows are in depth-first order.
    struct TreeViewRow
    {
        /// The caller's own ID for the row; the view does not read it.
        tachyon::ID      id           = tachyon::ID::Invalid();
        std::string  name;                     ///< label
        int          depth        = 0;         ///< nesting level, 0 = root; defines the hierarchy
        bool         isOpen       = false;     ///< expanded
        IconID       icon         = IconID::Invalid;
        std::string* renameBuffer = nullptr;   ///< null = not renameable

        // Derived by RebuildTreeViewIndex

        /// Number of descendant rows (skipped when the row is closed); > 0 means it has children.
        int          skipCount        = 0;
        /// Bit d is set when the ancestor at depth d+1 has a next sibling; drives guide lines.
        uint32_t     ancestorLineMask = 0;
        bool         isLastSibling    = true;  ///< no later sibling at the same depth

        bool HasChildren() const { return skipCount > 0; }
    };

    // ---- Virtualized GridView ----
    //
    // Like BeginGridView, but only visible rows are emitted. Column count and visible rows
    // come from last frame's viewport (one-frame settle on first show / resize).
    /// User-owned, persistent state of a virtualized GridView; keep it alive across frames.
    struct GridViewVirtualState
    {
        int32_t lastViewportW = 0;   ///< last frame's viewport width, pixels
        int32_t lastViewportH = 0;   ///< last frame's viewport height, pixels
        /// Cell width (pixels) that makes the columns exactly fill the viewport; use it for
        /// fill-width items.
        int32_t cellStretchedW = 0;

        /// Written by the grid view each frame; not part of the API.
        struct Internal
        {
            // Slow-re-click rename state
            DoubleClickTracker itemClickTracker;
            tachyon::ID     pendingRenameClick = tachyon::ID::Invalid();
            /// Items range ID, so F2 can map selectedIndex to an ID.
            tachyon::ID     itemsRangeID       = tachyon::ID::Invalid();
            // Layout for keyboard navigation, which runs after layout
            int32_t            cols      = 1;
            int32_t            rowStride = 1;
            int32_t            cellH     = 1;
        } internal;  ///< not part of the API
    };

}
