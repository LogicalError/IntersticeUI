// Public table API: BeginTable/EndTable + BeginRow/EndRow. Thin recording
// shell — the real work happens when EndTable hands the recorded cells to
// VirtualTable (controls_table_virtual.cpp).
//
// Cell record-builders live in controls_table_cells.cpp; event-handler
// builders in controls_table_handlers.cpp; shared helpers in
// controls_table_internal.h.

#include "controls_table_internal.h"

#include <algorithm>
#include <span>

namespace interstice
{
    using namespace interstice::TableInternal;

    // BeginTable opens a recording. Cell()/TreeCell() push records onto
    // m_tableEmit->columnCells; EndTable hands the populated optional to
    // VirtualTable, which drains them per row.
    bool GUIApplication::BeginTable(ID id, VirtualTableColumn* columns, size_t columnCount,
                                    TableState& state, StyleRef style,
                                    LayoutOverride&& constraints)
    {
        m_outerTableEmits.push_back(std::move(m_tableEmit));
        m_tableEmit.emplace();
        m_tableEmit->containerID = id;
        m_tableEmit->columns     = columns;
        m_tableEmit->columnCount = columnCount;
        m_tableEmit->userState   = &state;
        m_tableEmit->style       = style;
        m_tableEmit->constraints = std::move(constraints);
        m_tableEmit->isRecording = true;
        m_tableEmit->columnCells.assign(columnCount, {});

        // Lazy-init persisted widths.
        if (state.columnWidths.size() != columnCount)
        {
            state.columnWidths.assign(columnCount, 0);
            for (size_t c = 0; c < columnCount; c++)
                state.columnWidths[c] = columns[c].column.initialWidth;
        }

        // Clamp persisted selectedColumn.
        if (columnCount == 0)
            state.selectedColumn = 0;
        else if (state.selectedColumn < 0 || state.selectedColumn >= int(columnCount))
            state.selectedColumn = std::clamp(state.selectedColumn, 0, int(columnCount) - 1);

        // Reset per-column scratch on the user-owned VirtualTableColumn array.
        // Cell()/TreeCell() grow internal.maxContentWidth during replay.
        for (size_t c = 0; c < columnCount; c++)
        {
            columns[c].internal.maxContentWidth = 0;
            columns[c].internal.headerRef       = interstice::ElementRef::Invalid();
            columns[c].internal.dragOffsetX     = 0;
        }

        // Reset per-frame maps for TreeCell; clear() keeps the buckets allocated.
        if (state.internal.treeRowInfo) state.internal.treeRowInfo->clear();

        return true;
    }


    void GUIApplication::EndTable()
    {
        TableState&         state       = *m_tableEmit->userState;
        VirtualTableColumn* columns     = m_tableEmit->columns;
        const size_t        columnCount = m_tableEmit->columnCount;
        const interstice::ID       containerID = m_tableEmit->containerID;
        const StyleRef      style       = m_tableEmit->style;
        interstice::LayoutOverride constraints = std::move(m_tableEmit->constraints);
        const int           rowCount    = m_tableEmit->rowCount;
        const auto          recorded    = std::move(m_tableEmit->columnCells);

        // Replay the recorded cells through the same per-column path a
        // direct VirtualTable caller drives with its own generateCell.
        // The lambdas point into `recorded`, which outlives the call.
        for (size_t c = 0; c < columnCount; c++)
        {
            columns[c].generateCell = [cells = &recorded[c]](int row)
            {
                if (row < 0 || size_t(row) >= cells->size()) return;
                if (const auto& replay = (*cells)[size_t(row)]) replay();
            };
        }

        VirtualTable(containerID, state, std::span(columns, columnCount),
                     rowCount, /*rowHeight*/0, style, std::move(constraints));

        for (size_t c = 0; c < columnCount; c++)
            columns[c].generateCell = nullptr;
    }


    void GUIApplication::BeginRow()
    {
        m_tableEmit->currentCol = 0;
        m_tableEmit->rowCount++;
    }


    void GUIApplication::EndRow()
    {
        m_tableEmit->currentCol = 0;
    }
}
