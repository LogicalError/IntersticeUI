#pragma once

#include <gui_core/gui_id.h>
#include <gui_core/style/gui_style_text.h>          // IconID

#include <gui_controls/gui_widget_state_types.h>     // MultiSelectState, ItemDragDropConfig, DoubleClickTracker

#include <string>
#include <vector>

// TreeView / ListView / GridView data + construction state.

namespace GUI
{
    // ---- Transient tree/list construction state (valid between Begin/End) ----
    //
    // Valid only between Begin*/End* (reset in End*). Event handlers run at end of frame,
    // so they must capture the user-owned pointers instead of reading m_treeState.
    //
    struct TreeViewState {
        GUI::ID   containerID = GUI::ID::Invalid();
        int       nextItemIndex = 0;
        int       totalItemCount = 0;
        int       depth = 0;                        // nesting depth (0 = top-level, no guide lines)
        int*      selectedIndex = nullptr;          // for ListView index-based selection
        GUI::ID*  selectedID = nullptr;             // for single-ID selection
        MultiSelectState* multiSelect = nullptr;    // for multi-selection
        ItemDragDropConfig* dragDrop = nullptr;     // drag & drop config
    };

    // ---- GridView: icon grid ----
    //
    // Like TreeViewState: valid only between BeginGridView/EndGridView; handlers must
    // capture user-owned pointers.
    //
    struct GridViewState {
        GUI::ID containerID = GUI::ID::Invalid();
        int     nextItemIndex = 0;
        int*    selectedIndex = nullptr;
        MultiSelectState* multiSelect = nullptr;
        ItemDragDropConfig* dragDrop = nullptr;
        // Slow-re-click rename state in the user's GridViewVirtualState (null in the
        // non-virtual grid, which offers only Ctrl+Shift+Click / F2).
        DoubleClickTracker* clickTracker = nullptr;
        GUI::ID*            pendingRenameClick = nullptr;
    };

    // ---- GridView rename lookup ----
    // Filled by GridViewItem for items with a renameBuffer; read by end-of-frame rename handlers.
    struct GridItemRenameInfo { std::string* renameBuf = nullptr; std::string renameSeed; };

    // ---- TreeView (flat-data, virtualized) -----------------------------
    // Caller-owned flat rows shown through a VirtualTable. Call RebuildTreeViewIndex after
    // structural changes. Selection, scroll and drag-drop live on the TableState; a row ID
    // maps to its flat index via visibleIdx[rowID.value.subRangeParts.index].
    struct TreeViewRow
    {
        GUI::ID      id           = GUI::ID::Invalid();
        std::string  name;
        int          depth        = 0;
        bool         isOpen       = false;
        IconID       icon         = IconID::Invalid;
        std::string* renameBuffer = nullptr;   // null = not renameable

        // Derived by RebuildTreeViewIndex. A row has children iff skipCount > 0.
        int          skipCount        = 0;
        uint32_t     ancestorLineMask = 0;
        bool         isLastSibling    = true;

        bool HasChildren() const { return skipCount > 0; }
    };

    // ---- Virtualized GridView ----
    //
    // Like BeginGridView, but only visible rows are emitted. Column count and visible rows
    // come from last frame's viewport (one-frame settle on first show / resize).
    struct GridViewVirtualState
    {
        int32_t lastViewportW = 0;
        int32_t lastViewportH = 0;
        // Cell width that makes the columns exactly fill the viewport; use it for fill-width items.
        int32_t cellStretchedW = 0;
        // Slow-re-click rename state, and the items range ID so F2 can map selectedIndex to an ID.
        DoubleClickTracker _itemClickTracker;
        GUI::ID            _pendingRenameClick = GUI::ID::Invalid();
        GUI::ID            _itemsRangeID       = GUI::ID::Invalid();
        // For keyboard navigation, which runs after layout.
        int32_t            _cols      = 1;
        int32_t            _rowStride = 1;
        int32_t            _cellH     = 1;
    };

}
