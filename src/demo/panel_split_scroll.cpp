#include "application.h"

#include <algorithm>
#include <cstdio>
#include <vector>

//
// Demo panel for multi-section tables.
//
// Consecutive columns sharing a `sectionId` form a section. Each section gets
// its own H scrollbar when squeezed; Y scroll is shared across sections.
//
//   [ # ][ Type ] | [ Name ][ Hex ][ Multiple ] | [ Tag ][ Status ]
//   <- section 0 ->  <-- section 1 -->            <- section 2 ->
//
// What to check:
//   - Sections 0 and 2 are FitContent (frozen): pinned to their column widths,
//     no H scrollbar.
//   - Section 1 scrolls horizontally when squeezed.
//   - Column drag-reorder stays within its section.
//   - "Tag" is Stretch, so its trailing boundary is a separator, not a sizer.
//

namespace tachyon
{
    void Application::RenderSplitScrollPanel()
    {
        // Static so column drag-reorder rotations persist across frames.
        static std::vector<VirtualTableColumn> columns =
        {
            // ---- Section 0 (2 columns, fit-content - no H scrollbar) ----
            {
                .column = { .header = "#",     .initialWidth =  60, .minWidth = 40, .maxWidth = 160, .locked = true },
                .generateCell = [this](int r) { char b[16]; std::snprintf(b, sizeof(b), "%d", r); Cell(b); },
                .sectionId = 0,
                .sectionSizing = SectionSizing::FitContent
            },
            {
                .column = { .header = "Type",  .initialWidth =  80, .minWidth = 40, .maxWidth = 200, .locked = true },
                .generateCell = [this](int r){ static const char* kT[]={"int","f32","str","bool"}; Cell(kT[r & 3]); },
                .sectionId = 0
            },
            // ---- Section 1 (3 columns) ----------------------------------
            {
                .column = { .header = "Name",     .initialWidth = 160, .minWidth = 60, .maxWidth = 400 },
                .generateCell = [this](int r) { char b[32]; std::snprintf(b, sizeof(b), "Row %05d", r); Cell(b); },
                .sectionId = 1
            },
            {
                .column = { .header = "Hex", .initialWidth = 120, .minWidth = 60, .maxWidth = 200 },
                .generateCell = [this](int r) { char b[16]; std::snprintf(b, sizeof(b), "0x%06X", r); Cell(b); },
                .sectionId = 1
            },
            {
                .column = { .header = "Multiple", .initialWidth = 140, .minWidth = 60, .maxWidth = 400 },
                .generateCell = [this](int r){ char b[40]; std::snprintf(b, sizeof(b), "%d x 7 = %d", r, r * 7); Cell(b); },
                .sectionId = 1
            },
            // ---- Section 2 (2 columns) ----------------------------------
            // Tag is Stretch - its trailing boundary becomes a 1px
            // separator (Stretch columns stop sizers).
            {
                .column = { .header = "Tag",    .initialWidth = 60, .minWidth = 40, .maxWidth = 200, .locked = true, .sizing = ColumnSizing::Stretch },
                .generateCell = [this](int r) { static const char* kG[] = { "A","B","C" }; Cell(kG[r % 3]); },
                .sectionId = 2,
                .sectionSizing = SectionSizing::FitContent
            },
            {
                .column = { .header = "Status", .initialWidth =  80, .minWidth = 60, .maxWidth = 200, .locked = true },
                .generateCell = [this](int r){ Cell((r % 7 == 0) ? "ALERT" : "ok"); },
                .sectionId = 2
            }
        };

        auto& tableStyle = demo.frozenTableState;
        // Drag-to-reorder rotates the static columns; the table moves the
        // persisted widths itself.
        tableStyle.onReorderColumns = [colsPtr = &columns](int from, int to)
        {
            if (from == to) return;
            if (from < to) std::rotate(colsPtr->begin() + from, colsPtr->begin() + from + 1, colsPtr->begin() + to + 1);
            else           std::rotate(colsPtr->begin() + to, colsPtr->begin() + from, colsPtr->begin() + from + 1);
        };

        constexpr int kTotalRows = 50000;

        std::span<VirtualTableColumn> columnSpan(columns.data(), columns.size());


        BeginHorizontal(LayoutOverride{}.NoGrow().NoShrink());
            Text(idManager.AllocateDynamicID(), "Selection mode:", theme.staticTextStyleRef);
            Toggle(tableStyle.selectionMode == TableSelectionMode::Row, "Row", [&tableStyle](bool) { tableStyle.selectionMode = TableSelectionMode::Row; });
            Toggle(tableStyle.selectionMode == TableSelectionMode::Cell, "Cell", [&tableStyle](bool) { tableStyle.selectionMode = TableSelectionMode::Cell; });
        EndHorizontal(); 
        
        // flex-fill-row + table-fill (flex-basis:0) bound the table to the
        // panel, so its middle section scrolls inside it and the section's
        // H scrollbar stays in view.
        BeginHorizontal(idManager.AllocateDynamicID("splitScrollFill"),
                        theme.flexFillRowStyleRef,
                        LayoutOverride::Auto().FlexGrow(1));
        VirtualTable(idManager.AllocateDynamicID("multiSectionTable"),
                     tableStyle,
                     columnSpan, kTotalRows, 0, theme.tableFillStyleRef);
        EndHorizontal();
    }
}
