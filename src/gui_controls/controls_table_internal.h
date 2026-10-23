#pragma once

// Helpers shared between controls_table.cpp, controls_table_cells.cpp and
// controls_table_handlers.cpp. Internal — not in gui_application.h.

#include <gui_controls/gui_application.h>

#include <algorithm>
#include <chrono>
#include <vector>

namespace interstice::TableInternal
{
    // Hit-strip width of an active (draggable) sizer overlay (CSS pixels).
    constexpr int32_t kActiveSizerWidth   = 4;
    // Cross-axis size for full-height sizers/separators (clipped by parent).
    constexpr int32_t kStretchHeight      = 32768;


    // Double-click / slow-re-click tracker; each TableState holds its own.
    using DoubleClickTracker = interstice::DoubleClickTracker;


    // Column GridTrack: 1fr when stretchy, else Fixed. The table's last column
    // turns Fixed during a column drag so cells don't paint under the V scrollbar.
    // columnWidths are CSS pixels (laid out at the UI scale); lastColDragFixedWidth is pixels.
    inline GridTrack ComputeColumnTrack(const StyleManager& sm,
                                        int c,
                                        int globalColumnCount,
                                        const interstice::VirtualTableColumn* /*columns*/,
                                        const std::vector<int32_t>& columnWidths,
                                        bool dragActive,
                                        int32_t lastColDragFixedWidth,
                                        bool isStretch)
    {
        const bool isLastGlobal = (c + 1 == globalColumnCount);
        const float width = static_cast<float>(sm.Px(columnWidths[c]));
        if (isStretch)
        {
            if (isLastGlobal && dragActive)
                return GridTrack::Fixed(static_cast<float>(lastColDragFixedWidth));
            return GridTrack::Fractional(1.0f, width);
        }
        return GridTrack::Fixed(width);
    }

    // Overload with implicit stretch on the table's last column. Frozen mode passes
    // `isStretch` explicitly (only the middle section's last column stretches).
    inline GridTrack ComputeColumnTrack(const StyleManager& sm, int c, int globalColumnCount,
                                        const interstice::VirtualTableColumn* columns,
                                        const std::vector<int32_t>& columnWidths,
                                        bool dragActive, int32_t lastColDragFixedWidth)
    {
        const bool isLastGlobal = (c + 1 == globalColumnCount);
        const bool isStretch    = isLastGlobal
                               || columns[c].column.sizing == interstice::ColumnSizing::Stretch;
        return ComputeColumnTrack(sm, c, globalColumnCount, columns, columnWidths,
                                  dragActive, lastColDragFixedWidth, isStretch);
    }

    // Persistent hover/focus state for the cell ID + cell-mode Selected.
    // Callers add NoInput themselves when needed (Cell does in row mode;
    // TreeCell stays hittable so its arrow/text can be clicked).
    struct CellStateBits
    {
        interstice::ElementState state;
        bool              selected;
    };

    inline CellStateBits ComputeCellSelectionBits(GUIStateManager& sm,
                                                  const interstice::TableState* userState,
                                                  interstice::ID cellID, interstice::ID currentRowID,
                                                  int c, bool cellMode)
    {
        interstice::ElementState st = sm.GetCurrentState(cellID);
        const bool selected = cellMode
            && userState->selectedRowID  == currentRowID
            && userState->selectedColumn == c;
        if (selected) st |= interstice::ElementState::Selected;
        return { st, selected };
    }
}

namespace interstice
{
    // Tree-cell flex-row children — emitted at drain time by EndTable's
    // body-section drain. Defined in controls_table_cells.cpp.
    void EmitTreeCellIndent(GUIApplication& app, int depth);
    void EmitTreeCellArrowOrLeafSpacer(GUIApplication& app, bool* isOpen);
    void EmitTreeCellIcon(GUIApplication& app, IconID icon, bool* isOpen,
                          interstice::DoubleClickTracker* tracker);
}
