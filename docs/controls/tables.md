# Tables

Tables keep their column widths, selection and sort state in a `TableState` that the app
owns. Columns are `VirtualTableColumn`s, described each frame; reorder the array to
reorder the columns.

`VirtualTableColumn` fields: `column` (a `TableColumn`), `generateCell` (for
`VirtualTable`), `sortAscending(rowA, rowB)` (required for sorting a `VirtualTable`),
`sectionId` and `sectionSizing` (columns with the same section scroll together; see
"frozen columns" below).

`TableColumn` fields: `header`, `initialWidth`, `minWidth`, `maxWidth` (CSS pixels),
`sortable`, `locked` (can't be dragged or passed), `sizing` (`ColumnSizing::Resizable`,
`Fixed` or `Stretch`), `interactiveContent` (keep cell widgets clickable in row mode),
`columnBackground` and `cellStyle`.

`TableState` options: `selectionMode` (`TableSelectionMode::Row` or `Cell`),
`selectedRowID`, `selectedColumn`, `multiSelect`, `rowDragDrop`, `sortColumn`,
`sortDescending`, `followTail` (stick to the bottom as rows are added), `selectionBar`,
`rowStyle`, `cellStyle`, `showHeaders`, `columnSeparators`, `onReorderColumns` (headers
become drag-to-reorder when set) and `onRowDoubleClick`.

A header that sorts or moves its column is a Tab stop. With it focused, Space or Enter
sorts by the column (again: the other way), and Alt+Left / Right moves the column, the
focus going with it.

Classes: `table` (`table-fill` fills its parent), `table-header`, `table-row`,
`table-row-alt`, `table-cell`, `table-cell-number`, `table-sizer`, `table-separator`.

## BeginTable / BeginRow / Cell

Rows and cells emitted directly. Cells advance to the next column; `EndTable` lays them
out.

```cpp
bool BeginTable(ID id, VirtualTableColumn* columns, size_t columnCount, TableState& state, StyleRef style,
                LayoutOverride&& constraints = {});
template <size_t N>
bool BeginTable(ID id, VirtualTableColumn (&columns)[N], TableState& state, LayoutOverride&& constraints = {});
void EndTable();
bool BeginRow();
void EndRow();
void Cell(std::string_view text);
void Cell(std::function<void()> emit);
```

`Cell(emit)` builds custom content in the cell; it runs during `EndTable`, so its
captures must live until then.

```cpp
VirtualTableColumn columns[] = {
    { .column = { .header = "Name", .initialWidth = 160 } },
    { .column = { .header = "Size" } },
};
if (BeginTable(tableID, columns, tableState, LayoutOverride{}.Size(400, 200)))
{
    for (const File& file : files)
    {
        BeginRow();
        Cell(file.name);
        Cell(file.sizeText);
        EndRow();
    }
    EndTable();
}
```

## VirtualTable

A table that builds only the visible rows, so it handles very many. Each column's
`generateCell(rowIndex)` emits that column's cell, usually with `Cell` or `TreeCell`.
With `sortAscending` set on the columns, clicking a header sorts; the table keeps the
order as a permutation and the data is not moved.

```cpp
void VirtualTable(ID id, TableState& state, std::span<VirtualTableColumn> columns, int totalRowCount,
                  CssPx rowHeight = 0, StyleRef style = {}, LayoutOverride&& constraints = {});
```

`rowHeight` is a floor; 0 uses the cell style's height. Give it a bounded height:
sized to all of its rows it would be past the layout range.

```cpp
std::vector<VirtualTableColumn> columns = {
    { .column = { .header = "Name", .initialWidth = 160 },
      .generateCell  = [this](int row) { Cell(rows[row].name); },
      .sortAscending = [this](int a, int b) { return rows[a].name < rows[b].name; } },
};
VirtualTable(tableID, tableState, columns, int(rows.size()), 0, theme.tableFillStyleRef,
             LayoutOverride::Auto().FlexGrow(1).MinHeight(0));
```

Frozen columns: consecutive columns with the same `sectionId` form a section, side by
side with the others. The sections scroll vertically together and horizontally on their
own; column reorder stays within a section. A section with `SectionSizing::FitContent`
is exactly as wide as its columns and has no horizontal scroll bar.

## TreeCell

A tree-style cell: indent, expand arrow, optional icon and text. Arrow clicks toggle
`*isOpen` without selecting; other clicks select the row.

```cpp
void TreeCell(std::string_view text, int depth, bool* isOpen, IconID icon = IconID::Invalid,
              uint32_t ancestorLineMask = 0, bool isLastSibling = true,
              std::string* renameBuffer = nullptr, bool reserveExpander = true);
```

`isOpen` is null for leaves. `renameBuffer` enables in-place rename (F2,
Ctrl+Shift+Click or a slow second click). `ancestorLineMask` and `isLastSibling` draw the
guide lines; `RebuildTreeViewIndex` computes them. Classes: `tree-table-cell`,
`tree-table-arrow`, `tree-table-icon`, `tree-table-text`, `tree-table-text-edit`.

---

[← Lists and reorderable groups](lists.md) · [Controls](../controls.md) · [Tree and grid views →](trees-and-grids.md)
