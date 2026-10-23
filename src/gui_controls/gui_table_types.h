#pragma once

#include <gui_core/gui_id.h>
#include <gui_core/gui_base_types.h>
#include <gui_core/style/gui_style.h>
#include <gui_core/style/gui_style_manager.h>
#include <gui_core/layout/gui_layout_grid.h>
#include <gui_core/layout/gui_constraints.h>
#include <gui_core/elements/gui_element.h>
#include <gui_core/input/gui_input_handler_func.h>
#include <gui_core/input/gui_input_enums.h>

#include <gui_controls/gui_widget_state_types.h>   // MultiSelectState, ItemDragDropConfig, DoubleClickTracker, TreeDropPosition

#include <array>
#include <cassert>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

// Table widget data model: column descriptors, the user-owned persistent
// TableState, drag/drop state and the per-emit TableEmit.

namespace interstice
{
    // ---- Table widget: row × column data grid ----
    //
    // TableColumn describes a column; callers pass an array each frame.
    // TableState is user-owned and persists widths and selection/sort state;
    // underscore-prefixed members are internal.
    //
    // How a column's track is sized and whether it has a sizer at its right edge.
    enum class ColumnSizing : uint8_t {
        // Fixed track at columnWidths[c], resizable via its sizer.
        Resizable,
        // Fixed track at initialWidth; a 1px separator instead of a sizer.
        Fixed,
        // 1fr track with initialWidth as the floor; a 1px separator. The last
        // column is always Stretch.
        Stretch,
    };

    // How a section sizes itself in the bodies row; read from the section's first column.
    enum class SectionSizing : uint8_t {
        // Shares leftover width with its siblings and scrolls horizontally on overflow.
        // Its last column is implicitly Stretch.
        Scrollable,
        // Exactly the sum of its column widths (Stretch acts as Fixed); no H-scrollbar.
        FitContent,
    };

    struct TableColumn {
        std::string_view header;
        // Widths in CSS pixels.
        int32_t initialWidth = 120;
        int32_t minWidth     = 40;
        int32_t maxWidth     = 2000;
        bool    sortable     = false;
        // Can't be dragged, and other columns can't be dragged past it.
        bool    locked       = false;
        ColumnSizing sizing  = ColumnSizing::Resizable;
        // Keep cells hittable in row mode (where cells are NoInput so a click selects
        // the row), for columns with their own widgets, e.g. a visibility toggle.
        bool    interactiveContent = false;
        // Optional background strip spanning the whole body height, behind the cells and
        // above the row backdrops. Fixed-width columns only.
        StyleRef columnBackground = StyleRef::Invalid();
    };

    // Column info plus generateCell (VirtualTable; null in the BeginTable/Cell flow) and
    // internal per-frame fields. Reorder columns by reordering the array.
    // Internal fields live here, not in TableEmit, because end-of-frame handlers read them.
    struct VirtualTableColumn {
        TableColumn                       column;
        std::function<void(int rowIndex)> generateCell;

        // Ascending strict-weak ordering on DATASET row indices; required for VirtualTable sorting.
        std::function<bool(int rowA, int rowB)> sortAscending;

        // Consecutive columns with the same sectionId form a section, side by side and
        // scrolling independently. Reorder and implicit stretch stay within a section.
        int              sectionId        = 0;
        // Read from the section's first column only.
        SectionSizing    sectionSizing    = SectionSizing::Scrollable;

        // Widest cell content seen (text + tree prefix), for double-click-to-fit.
        int32_t          _maxContentWidth = 0;
        interstice::ID          _bodyID          = interstice::ID::Invalid();
        // Shared by the header and body copies of the boundary after this column, so their
        // hover state syncs. Unused for the last column.
        interstice::ID          _sizerID         = interstice::ID::Invalid();
        // Ranged, so it doesn't change when the dragged column is emitted last.
        interstice::ID          _headerCellID    = interstice::ID::Invalid();
        interstice::ElementRef  _headerRef       = interstice::ElementRef::Invalid();
        // Applied to every element of this column during a reorder drag.
        int16_t          _dragOffsetX     = 0;
        // Resolved once per frame.
        GridTrack        _columnTrack{};
    };

    // Whether clicks, hover and keyboard navigation work on rows or on cells.
    enum class TableSelectionMode : uint8_t
    {
        Row,   // Click any cell -> select row. Up/Down/Home/End navigate rows. Hovering any cell highlights the whole row.
        Cell,  // Click cell -> select that one cell. Up/Down/Left/Right/Home/End navigate cells. Only the hovered cell is highlighted.
    };

    // Deferred row-click state for the drag-drop path: MouseDown primes it,
    // MouseUp applies the selection unless a drag promoted in between.
    struct PendingRowClick
    {
        interstice::ID          rowID        = interstice::ID::Invalid();
        interstice::KeyModifier mods         = interstice::KeyModifier::None;
        bool             dragPromoted = false;
    };

    struct TableState
    {
        // Per-row metadata from TreeCell().
        struct TreeRowInfo
        {
            bool*        isOpen      = nullptr;          // null for leaves
            int          depth       = 0;
            std::string* renameBuf   = nullptr;          // null when not renameable
            std::string  renameSeed;                     // original label, seeds rename Start()
            bool         hasChildren = false;            // drives drag-drop zone shape (thirds vs halves)
        };
        using TreeRowMap = std::unordered_map<interstice::ID, TreeRowInfo>;

        // Cell-mode click dispatch: cell-element ID → (rowID, columnIdx).
        // Populated only when selectionMode == Cell.
        struct CellLookupEntry { interstice::ID rowID; int columnIdx; };
        using CellLookupMap = std::unordered_map<interstice::ID, CellLookupEntry>;

        using ReorderColumnsFn = std::function<void(int fromCol, int toCol)>;

        // std::function that costs one pointer when unset; assigns like one:
        // `state.onReorderColumns = [&](int, int){ ... };`
        class LazyReorderFn
        {
        public:
            LazyReorderFn() = default;
            LazyReorderFn(std::nullptr_t) noexcept {}

            template <typename F,
                      typename = std::enable_if_t<!std::is_same_v<std::decay_t<F>, LazyReorderFn>>>
            LazyReorderFn& operator=(F&& f)
            {
                if (!_fn) _fn = std::make_unique<ReorderColumnsFn>();
                *_fn = std::forward<F>(f);
                return *this;
            }
            LazyReorderFn& operator=(std::nullptr_t) noexcept { _fn.reset(); return *this; }

            explicit operator bool() const noexcept { return _fn && static_cast<bool>(*_fn); }
            void operator()(int from, int to) const { (*_fn)(from, to); }
            ReorderColumnsFn*       get()       noexcept { return _fn.get(); }
            const ReorderColumnsFn* get() const noexcept { return _fn.get(); }
            void reset() noexcept { _fn.reset(); }

        private:
            std::unique_ptr<ReorderColumnsFn> _fn;
        };

        static constexpr int kMaxTableSections = 8;

        // ---- HOT: read every frame ----------------------------------
        std::vector<int32_t>  columnWidths;                       // persisted per-column widths, CSS pixels (follow column reorders)
        MultiSelectState*     multiSelect      = nullptr;
        ItemDragDropConfig*   rowDragDrop      = nullptr;         // when non-null & payloadType != 0, rows become draggable + drop targets
        interstice::ID               selectedRowID    = interstice::ID::Invalid();
        // Stick to the bottom: start at (and select) the last row, and follow new rows while
        // at the bottom. Scrolling up detaches; scrolling back down re-attaches.
        bool                  followTail       = false;
        interstice::ID               _bodyID          = interstice::ID::Invalid();   // canonical scroll-Y
        interstice::ID               _horzScrollbarID = interstice::ID::Invalid();
        interstice::ID               _vertScrollbarID = interstice::ID::Invalid();
        int32_t               _lastViewportH   = 0;               // sampled by render-time draw
        int32_t               _lastViewportW   = 0;
        // Written by VirtualTable each frame, so keyboard navigation can address and
        // scroll to rows outside the virtualized window.
        interstice::ID               _rowsRangeID     = interstice::ID::Invalid();
        int                   _totalRowCount   = 0;
        int32_t               _actualRowHeight = 0;
        // Set at render time when any section overflows; next frame emits a scrollbar only
        // for an overflowing axis (a hidden bar would still eat clicks). _anyHorzOverflow
        // also adds a bottom pad so the H bar doesn't cover the last row.
        bool                  _anyHorzOverflow = false;
        bool                  _anyVertOverflow = false;
        // VirtualTable's showHeaders, for keyboard scroll-into-view.
        bool                  _hasHeaders      = true;
        // followTail bookkeeping.
        bool                  _followInit         = false;
        bool                  _followPinned       = true;
        int                   _prevFollowRowCount = 0;
        int                   selectedColumn   = 0;               // cell-mode only, ignored in row mode
        int                   sortColumn       = -1;              // -1 = unset; auto-resolved by VirtualTable
        TableSelectionMode    selectionMode    = TableSelectionMode::Row;
        bool                  sortDescending   = false;

        // ---- COLD: mode-specific, lazy-allocated --------------------
        std::unique_ptr<TreeRowMap>    _treeRowInfo;               // populated by TreeCell()
        std::vector<int>               _treeVisibleIdx;            // VirtualTreeView: last frame's visible → flat rows
        std::unique_ptr<CellLookupMap> _cellLookup;                // populated in cell mode
        LazyReorderFn                  onReorderColumns;           // headers become drag-to-reorder when set
        // Replaces the default double-click branch toggle (the chevron still toggles).
        std::function<void(interstice::ID rowID)> onRowDoubleClick;

        // Per-table gesture state, so tables don't share click detection.
        DoubleClickTracker _rowClickTracker;
        DoubleClickTracker _treeCellClickTracker;
        PendingRowClick    _pendingRowClick;

        // Visual → dataset row permutation while sorted, else null. Rebuilt only when the
        // sort state or row count changes (or _rowOrderDirty).
        std::unique_ptr<std::vector<int>> _rowOrder;
        int                               _sortedColumn     = -2;     // -2 = unbuilt; -1 = built-as-identity
        bool                              _sortedDescending = false;
        int                               _sortedRowCount   = -1;
        // Set via InvalidateRowOrder().
        bool                              _rowOrderDirty    = false;

        // The first section body inline, others on the heap.
        interstice::ID                                                          _firstSectionBodyID = interstice::ID::Invalid();
        std::unique_ptr<std::array<interstice::ID, kMaxTableSections - 1>>      _extraSectionBodyIDs;
        int8_t                                                           _sectionCount = 0;
        // Header cell refs of the current frame, by column. Column-reorder handlers read these
        // when they run, after the caller's column array may be gone.
        std::vector<interstice::ElementRef>                                     _headerRefs;

        // ---- Accessors ----------------------------------------------
        // Invalid when out of range.
        interstice::ID SectionBodyID(int i) const noexcept
        {
            if (i == 0) return _firstSectionBodyID;
            if (i < 1 || i >= kMaxTableSections || !_extraSectionBodyIDs) return interstice::ID::Invalid();
            return (*_extraSectionBodyIDs)[size_t(i - 1)];
        }
        void SetSectionBodyID(int i, interstice::ID id)
        {
            assert(i >= 0 && i < kMaxTableSections);
            if (i == 0) { _firstSectionBodyID = id; return; }
            if (!_extraSectionBodyIDs)
                _extraSectionBodyIDs = std::make_unique<std::array<interstice::ID, kMaxTableSections - 1>>();
            (*_extraSectionBodyIDs)[size_t(i - 1)] = id;
        }

        // Allocate on write; reads use the Find* helpers.
        TreeRowMap&    EnsureTreeRowInfo()
        {
            if (!_treeRowInfo) _treeRowInfo = std::make_unique<TreeRowMap>();
            return *_treeRowInfo;
        }
        CellLookupMap& EnsureCellLookup()
        {
            if (!_cellLookup) _cellLookup = std::make_unique<CellLookupMap>();
            return *_cellLookup;
        }

        // A row's sub-range ID, usable outside a frame (unlike IDManager::GetSubID).
        // Invalid when out of range.
        interstice::ID RowIDForIndex(int rowIdx) const noexcept
        {
            if (rowIdx >= _totalRowCount) return interstice::ID::Invalid();
            return RowIDInRange(rowIdx);
        }

        // As RowIDForIndex, without the bound on last frame's row count.
        interstice::ID RowIDInRange(int rowIdx) const noexcept
        {
            if (_rowsRangeID == interstice::ID::Invalid() || rowIdx < 0) return interstice::ID::Invalid();
            interstice::ID id{};
            id.value.subRangeParts.mode = interstice::IDMode::SubRange;
            id.value.subRangeParts.index = static_cast<uint64_t>(rowIdx);
            id.value.subRangeParts.parentHashValue = _rowsRangeID.value.rangedParts.hashValue;
            return id;
        }

        // Identity when not sorted.
        int VisualToDataset(int visualIdx) const noexcept
        {
            if (!_rowOrder || visualIdx < 0 || visualIdx >= int(_rowOrder->size()))
                return visualIdx;
            return (*_rowOrder)[visualIdx];
        }

        // O(N) when sorted.
        int DatasetToVisual(int datasetIdx) const noexcept
        {
            if (!_rowOrder) return datasetIdx;
            const auto& v = *_rowOrder;
            for (size_t i = 0; i < v.size(); ++i)
                if (v[i] == datasetIdx) return int(i);
            return -1;
        }

        // Rebuild the sort permutation next frame; call after changing sorted data.
        void InvalidateRowOrder() noexcept { _rowOrderDirty = true; }

        // nullptr when not found.
        const TreeRowInfo* FindTreeRow(interstice::ID id) const noexcept
        {
            if (!_treeRowInfo) return nullptr;
            auto it = _treeRowInfo->find(id);
            return (it == _treeRowInfo->end()) ? nullptr : &it->second;
        }
        const CellLookupEntry* FindCell(interstice::ID id) const noexcept
        {
            if (!_cellLookup) return nullptr;
            auto it = _cellLookup->find(id);
            return (it == _cellLookup->end()) ? nullptr : &it->second;
        }
    };

    // ---- Column-reorder drag state (survives across frames) ----
    //
    // Cells use explicit grid placement, so instead of reordering elements the dragged
    // column gets a Relative() offset. On drop, onReorderColumns fires.
    struct ColumnDragState {
        bool                      active    = false; // committed past click threshold
        interstice::ID                   tableID   = interstice::ID::Invalid();
        int                       fromCol   = -1;
        int                       toCol     = -1;
        interstice::int2                 startPos{};
        interstice::int2                 offset{};
        // Header rects at drag start (screen space), for edge-crossing.
        std::vector<interstice::IntRect> headerRects;
        // Inclusive target range, bounded by locked columns.
        int                       minTarget = 0;
        int                       maxTarget = 0;
        // Scroll-X at drag start, to correct the cursor for mid-drag scrolling.
        // Drags never leave their section.
        interstice::ID                   sectionBodyID = interstice::ID::Invalid();
        int32_t                   startScrollX  = 0;
    };

    // ---- Persistent tree reorder drop state (survives across frames) ----
    // Drives the drop indicator line; targetDepth indents it so After and Inside differ visibly.
    struct TreeDropState {
        interstice::ID           targetRowID  = interstice::ID::Invalid();
        TreeDropPosition  position     = TreeDropPosition::None;
        int               targetDepth  = 0;
    };

    // ---- Table: row × column data grid ----
    //
    // Transient state of one VirtualTable call. Event handlers run at end of frame and
    // must never read it; they capture TableState pointers instead.
    struct TableEmit
    {
        // Inputs.
        VirtualTableColumn*  columns     = nullptr;       // user-owned
        size_t               columnCount = 0;
        TableState*          userState   = nullptr;

        // Per-table derived (filled by VirtualTable's prologue).
        bool    dragActive            = false;
        bool    cellMode              = false;
        int32_t lastColDragFixedWidth = 0;
        // Reorder-dragged column, or -1; emitted last so it paints on top.
        int     draggedCol            = -1;
        // Copied from TableState::_anyHorzOverflow / _anyVertOverflow (last frame).
        bool    anyHorzOverflow      = false;
        bool    anyVertOverflow      = false;
        bool    showHeaders          = true;
        // Headerless tables draw thin static dividers at column boundaries;
        // set false to suppress them (e.g. the console log).
        bool    columnSeparators     = true;

        // Body text cell style; invalid falls back to theme.tableCellStyleRef.
        StyleRef cellStyleRef        = {};

        interstice::EventHandler            sharedRowHandler;
        interstice::EventHandler            sharedCellHandler;

        // Current position, read by Cell()/TreeCell().
        int               currentCol     = 0;
        interstice::ID           currentRowID   = interstice::ID::Invalid();
        interstice::EventHandler currentRowHandler;
        int               directGridRow  = 0;
        int               directColStart = 0;

        // Recording mode (BeginTable/Cell flow): cells are stored in columnCells[column][row]
        // and replayed by EndTable. An empty slot means the row skipped that column.
        bool                isRecording = false;
        interstice::ID             containerID = interstice::ID::Invalid();
        StyleRef            style       = {};
        LayoutOverride      constraints = {};
        int                 rowCount    = 0;
        std::vector<std::vector<std::function<void()>>> columnCells;
    };

}
