#include "application.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <vector>

//
// Demo panel for the Table widget. The first two sub-sections are temporarily
// disabled (BeginTable API); the third sub-section uses VirtualTable directly
// and is the only live demo here.
//
// Sorting: each column's sortAscending compares dataset row indices and the
// table builds the visual-order permutation; the data is never reordered.
//

namespace interstice
{
    namespace
    {
        struct DemoRow
        {
            int         index;     // 0..N-1, kept for the # column
            std::string name;
            int         hex;
            int         multiple;
        };

        // 50k rows to exercise virtualization, sort and column reorder.
        const std::vector<DemoRow>& GetDemoRows()
        {
            static const std::vector<DemoRow> rows = []() {
                std::vector<DemoRow> v;
                v.reserve(50000);
                char buf[32];
                for (int i = 0; i < 50000; i++)
                {
                    std::snprintf(buf, sizeof(buf), "Row %05d", i);
                    v.push_back(DemoRow{ i, std::string(buf), i, i * 7 });
                }
                return v;
            }();
            return rows;
        }
    }

    void Application::RenderTablePanel()
    {
        // ---- Virtual-scrolled table (50k rows, click-to-sort, drag-to-reorder)

        const auto& rows = GetDemoRows();

        static std::vector<VirtualTableColumn> columns =
        {
            // The row-index column is locked - fixed landmark on the
            // left, and other columns can't be dragged past it.
            {
                .column = { .header = "#", .initialWidth = 80,  .minWidth = 40, .maxWidth = 160, .locked = true },
                .generateCell = [this, &rows](int rowIdx) {
                    char buf[16]; std::snprintf(buf, sizeof(buf), "%d", rows[rowIdx].index); Cell(buf);
                },
                .sortAscending = [&rows](int a, int b) { return rows[a].index < rows[b].index; },
            },
            {
                .column = { .header = "Name", .initialWidth = 160, .minWidth = 60, .maxWidth = 400 },
                .generateCell = [this, &rows](int rowIdx) {
                    Cell(rows[rowIdx].name.c_str());
                },
                .sortAscending = [&rows](int a, int b) { return rows[a].name < rows[b].name; },
            },
            {
                .column = { .header = "Hex", .initialWidth = 120, .minWidth = 60, .maxWidth = 200 },
                .generateCell = [this, &rows](int rowIdx) {
                    char buf[16]; std::snprintf(buf, sizeof(buf), "0x%06X", rows[rowIdx].hex); Cell(buf);
                },
                .sortAscending = [&rows](int a, int b) { return rows[a].hex < rows[b].hex; },
            },
            {
                .column = { .header = "Multiple", .initialWidth = 120, .minWidth = 60, .maxWidth = 400 },
                .generateCell = [this, &rows](int rowIdx) {
                    char buf[40]; std::snprintf(buf, sizeof(buf), "%d x 7 = %d", rows[rowIdx].index, rows[rowIdx].multiple); Cell(buf);
                },
                .sortAscending = [&rows](int a, int b) { return rows[a].multiple < rows[b].multiple; },
            },
        };

        // Drag-to-reorder: rotate the column list (the table moves the
        // persisted widths itself). `columns` is function-local-static, so
        // the captured pointer stays valid.
        auto& ts = demo.tablePanelVirtualTableState;
        ts.selectionBar = true;
        ts.onReorderColumns = [colsPtr = &columns](int from, int to)
        {
            if (from == to) return;
            if (from < to) std::rotate(colsPtr->begin() + from, colsPtr->begin() + from + 1, colsPtr->begin() + to + 1);
            else           std::rotate(colsPtr->begin() + to, colsPtr->begin() + from, colsPtr->begin() + from + 1);
        };

        // Every column but Name holds a number; set each frame as styles reload.
        for (VirtualTableColumn& c : columns)
            c.column.cellStyle = c.column.header == "Name" ? StyleRef::Invalid() : dataTableNumberStyleRef;

        const int kTotalRows = int(rows.size());
        std::span<VirtualTableColumn> columnSpan(columns.data(), columns.size());


        // ---- Mode switcher -------------------------------------------------
        // Row mode selects whole rows; Cell mode selects single cells, with
        // Left/Right arrow navigation.
        BeginHorizontal(LayoutOverride{}.NoGrow().NoShrink());
            Text(idManager.AllocateDynamicID(), "Selection mode:", theme.staticTextStyleRef);
            Toggle(demo.tablePanelVirtualTableState.selectionMode == TableSelectionMode::Row, "Row", [this](bool) { demo.tablePanelVirtualTableState.selectionMode = TableSelectionMode::Row; });
            Toggle(demo.tablePanelVirtualTableState.selectionMode == TableSelectionMode::Cell, "Cell", [this](bool) { demo.tablePanelVirtualTableState.selectionMode = TableSelectionMode::Cell; });
        EndHorizontal();

        // Fills the panel and scrolls: sized to its 50k rows it would be past the
        // 16-bit layout range and not show at all.
        VirtualTable(idManager.AllocateDynamicID("tablePanelVirtual"),
                     demo.tablePanelVirtualTableState,
                     columnSpan, kTotalRows, 0, theme.tableFillStyleRef,
                     LayoutOverride::Auto().FlexGrow(1).MinHeight(0),
                     /*showHeaders*/ true, dataTableCellStyleRef);
    }

}
