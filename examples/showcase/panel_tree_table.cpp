#include "application.h"
#include <gui_controls/gui_tree_utils.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <numeric>
#include <span>
#include <string>

//
// Demo panel for TreeTable (hierarchical rows inside a VirtualTable).
//
// Data model: a flat row vector plus a per-row `skipCount` (descendants), so
// the visibility walk skips a closed subtree in one step. Each frame
// `visibleIdx` maps visible row -> flat row and its size is the row count.
//
// `skipCount` / `ancestorLineMask` / `isLastSibling` are derived from `depth`
// by TreeUtils::RebuildTreeIndex after adding or removing rows; toggling
// `isOpen` needs no rebuild.
//

namespace tachyon
{

    namespace {

    using Row = Application::ShowcaseState::TreeTableRow;

    // Stable payload tag so the row drop targets reject foreign drags.
    constexpr uint32_t kTreeTableRowPayloadType = 0x54725452u; // 'TrTR'

    } // namespace

    void Application::RenderTreeTablePanel()
    {
        // ---- One-shot dataset build ----------------------------------------
        if (showcase.treeTableRows.empty())
        {
            auto add = [&](const char* name, int depth, bool hasChildren, bool isOpen,
                           const char* size, const char* type, IconID icon) {
                ShowcaseState::TreeTableRow r;
                r.id          = idManager.CreateStaticID();
                r.name        = name;
                r.size        = size;
                r.type        = type;
                r.depth       = depth;
                r.hasChildren = hasChildren;
                r.isOpen      = isOpen;
                r.icon        = icon;
                showcase.treeTableRows.push_back(std::move(r));
            };

            add("Assets",           0, true,  true,  "",        "Folder",   showcase.folderClosedIcon);
            add("Meshes",           1, true,  true,  "",        "Folder",   showcase.folderClosedIcon);
            add("Characters",       2, true,  false, "",        "Folder",   showcase.folderClosedIcon);
            add("Hero.fbx",         3, false, false, "1.2 MB",  "Mesh",     showcase.starIcon);
            add("Enemy.fbx",        3, false, false, "800 KB",  "Mesh",     showcase.starIcon);
            add("Props",            2, true,  true,  "",        "Folder",   showcase.folderClosedIcon);
            add("Crate.fbx",        3, false, false, "120 KB",  "Mesh",     showcase.starIcon);
            add("Barrel.fbx",       3, false, false, "140 KB",  "Mesh",     showcase.starIcon);
            add("Sign.fbx",         3, false, false,  "48 KB",  "Mesh",     showcase.starIcon);
            add("Textures",         1, true,  true,  "",        "Folder",   showcase.folderClosedIcon);
            add("Albedo",           2, true,  false, "",        "Folder",   showcase.folderClosedIcon);
            add("wood.png",         3, false, false, "512 KB",  "Image",    showcase.heartIcon);
            add("metal.png",        3, false, false, "480 KB",  "Image",    showcase.heartIcon);
            add("Normal",           2, true,  false, "",        "Folder",   showcase.folderClosedIcon);
            add("wood_n.png",       3, false, false, "700 KB",  "Image",    showcase.heartIcon);
            add("metal_n.png",      3, false, false, "680 KB",  "Image",    showcase.heartIcon);
            add("Materials",        1, true,  false, "",        "Folder",   showcase.folderClosedIcon);
            add("Default.mat",      2, false, false,   "4 KB",  "Material", showcase.circleIcon);
            add("Metal.mat",        2, false, false,   "4 KB",  "Material", showcase.circleIcon);
            add("Glass.mat",        2, false, false,   "4 KB",  "Material", showcase.circleIcon);
            add("Scenes",           0, true,  true,  "",        "Folder",   showcase.folderClosedIcon);
            add("MainLevel.scene",  1, false, false, "256 KB",  "Scene",    showcase.circleIcon);
            add("Boss.scene",       1, false, false, "190 KB",  "Scene",    showcase.circleIcon);
            add("Docs",             0, true,  false, "",        "Folder",   showcase.folderClosedIcon);
            add("README.md",        1, false, false,   "8 KB",  "Text",     showcase.heartIcon);
            add("CHANGELOG.md",     1, false, false,   "3 KB",  "Text",     showcase.heartIcon);

            TreeUtils::RebuildTreeIndex(showcase.treeTableRows);

            showcase.treeTableState.multiSelect = &showcase.treeTableMultiSelect;

            // Row drag-and-drop. The payload is the flat row index; a rowID's
            // sub-range index is the visible index, so translate it through
            // treeTableVisibleIdx (closed branches would skew it otherwise).
            showcase.treeTableDragDrop.payloadType = kTreeTableRowPayloadType;
            showcase.treeTableDragDrop.getPayload  = [this](tachyon::ID rowID) -> uintptr_t {
                if (rowID.GetMode() != tachyon::IDMode::SubRange) return UINTPTR_MAX;
                const int visIdx = static_cast<int>(rowID.value.subRangeParts.index);
                const auto& v = showcase.treeTableVisibleIdx;
                if (visIdx < 0 || visIdx >= int(v.size())) return UINTPTR_MAX;
                return static_cast<uintptr_t>(v[visIdx]);
            };
            showcase.treeTableDragDrop.onReorder = [this](uintptr_t srcPayload, tachyon::ID targetID,
                                                     TreeDropPosition pos) {
                if (targetID.GetMode() != tachyon::IDMode::SubRange) return;
                if (srcPayload == UINTPTR_MAX) return;
                const int targetVis = static_cast<int>(targetID.value.subRangeParts.index);
                const auto& v = showcase.treeTableVisibleIdx;
                if (targetVis < 0 || targetVis >= int(v.size())) return;
                const int srcDs = static_cast<int>(srcPayload);
                const int dstDs = v[targetVis];
                auto& rows = showcase.treeTableRows;
                const int newFlatIdx = TreeUtils::ReorderTreeRow(rows, srcDs, dstDs, pos);
                if (newFlatIdx < 0) return;
                TreeUtils::RebuildTreeIndex(rows);
                // Deferred: the matching sub-range rowID needs the
                // row IDs VirtualTable allocates next frame.
                showcase.treeTablePendingSelectFlatIdx       = newFlatIdx;
                showcase.treeTableState.selectedRowID        = tachyon::ID::Invalid();
                showcase.treeTableMultiSelect.selectedIDs.clear();
                showcase.treeTableMultiSelect.lastClickedID  = tachyon::ID::Invalid();
            };
            showcase.treeTableState.rowDragDrop = &showcase.treeTableDragDrop;
        }

        // ---- Per-frame visibility walk -------------------------------------
        auto& visibleIdx = showcase.treeTableVisibleIdx;
        visibleIdx.clear();
        {
            const auto& rows = showcase.treeTableRows;
            for (size_t i = 0; i < rows.size(); )
            {
                visibleIdx.push_back(int(i));
                if (rows[i].hasChildren && !rows[i].isOpen)
                    i += 1 + rows[i].skipCount;
                else
                    i += 1;
            }
        }

        // ---- Columns -------------------------------------------------------
        // Static so the captures are stable across frames; column reorder is
        // left disabled.
        static std::vector<VirtualTableColumn> columns =
        {
            // Locked so it stays anchored on the left. TreeCell handles indent,
            // chevron, icon and inline rename.
            {
                { .header = "Name", .initialWidth = 240, .minWidth = 80, .maxWidth = 600, .locked = true },
                [this](int rowIdx) {
                    auto& v = showcase.treeTableVisibleIdx;
                    if (rowIdx < 0 || rowIdx >= int(v.size())) return;
                    auto& r = showcase.treeTableRows[v[rowIdx]];
                    // Arrow follows actual descendants, not `hasChildren`.
                    const bool branch = (r.skipCount > 0);
                    TreeCell(r.name, r.depth,
                             branch ? &r.isOpen : nullptr,
                             r.icon,
                             r.ancestorLineMask,
                             r.isLastSibling,
                             &r.name);
                }
            },
            {
                { .header = "Size", .initialWidth = 100, .minWidth = 40, .maxWidth = 200 },
                [this](int rowIdx) {
                    auto& v = showcase.treeTableVisibleIdx;
                    if (rowIdx < 0 || rowIdx >= int(v.size())) return;
                    Cell(showcase.treeTableRows[v[rowIdx]].size);
                }
            },
            {
                { .header = "Type", .initialWidth = 120, .minWidth = 40, .maxWidth = 200 },
                [this](int rowIdx) {
                    auto& v = showcase.treeTableVisibleIdx;
                    if (rowIdx < 0 || rowIdx >= int(v.size())) return;
                    Cell(showcase.treeTableRows[v[rowIdx]].type);
                }
            }
        };

        // ---- Mode switcher -------------------------------------------------
        // Row mode selects whole rows; Cell mode selects single cells, with
        // Left/Right arrow navigation.
        BeginHorizontal(LayoutOverride{}.NoGrow().NoShrink());
            Text(idManager.AllocateDynamicID(), "Selection mode:", theme.staticTextStyleRef);
            Button("Row",  [this]() { showcase.treeTableState.selectionMode = TableSelectionMode::Row;  });
            Button("Cell", [this]() { showcase.treeTableState.selectionMode = TableSelectionMode::Cell; });
            const char* modeLabel =
                (showcase.treeTableState.selectionMode == TableSelectionMode::Cell) ? "(Cell)" : "(Row)";
            Text(idManager.AllocateDynamicID(), modeLabel, theme.staticTextStyleRef);
        EndHorizontal();

        std::span<VirtualTableColumn> columnSpan(columns.data(), columns.size());
        VirtualTable(idManager.AllocateDynamicID("treeTable"),
                     showcase.treeTableState, columnSpan,
                     int(visibleIdx.size()), 0, theme.tableStyleRef,
                     LayoutOverride{}.Width(520).Height(400).BreakBefore());

        // Apply a pending post-drop selection now that VirtualTable has set
        // this frame's row IDs.
        if (showcase.treeTablePendingSelectFlatIdx >= 0
            && showcase.treeTableState.RowIDInRange(0) != tachyon::ID::Invalid())
        {
            const int targetFlat = showcase.treeTablePendingSelectFlatIdx;
            int newVisIdx = -1;
            for (size_t i = 0; i < visibleIdx.size(); ++i)
                if (visibleIdx[i] == targetFlat) { newVisIdx = int(i); break; }
            if (newVisIdx >= 0)
            {
                const tachyon::ID newRowID = showcase.treeTableState.RowIDForIndex(newVisIdx);
                showcase.treeTableState.selectedRowID = newRowID;
                showcase.treeTableMultiSelect.selectedIDs.clear();
                showcase.treeTableMultiSelect.selectedIDs.insert(newRowID);
                showcase.treeTableMultiSelect.lastClickedID = newRowID;
            }
            showcase.treeTablePendingSelectFlatIdx = -1;
        }
    }
}
