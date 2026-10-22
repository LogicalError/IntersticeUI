#include "application.h"
#include "../gui_controls/gui_tree_utils.h"

#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

//
// This file is for testing and prototyping the VirtualTreeView control.
// Data is a flat std::vector<TreeViewRow> with a depth field; reorders call
// RebuildTreeViewIndex to refresh the derived fields.
//

namespace GUI
{

    namespace {

    // Panel data — built by EnableHierarchyPanel, torn down by DisableHierarchyPanel.
    std::vector<GUI::TreeViewRow> treeRows;
    std::vector<int>                         treeVisibleIdx;
    GUI::TableState               treeViewState;
    GUI::MultiSelectState         treeMultiSelect;
    std::string                              renameBuffer; // TreeCell rename scratch

    // Search: a filtered copy of treeRows (matches plus ancestors, forced open),
    // rebuilt when the query changes. Rendered with its own state, no drag-drop
    // or rename.
    std::vector<GUI::TreeViewRow> filteredRows;
    std::vector<int>                         filteredVisibleIdx;
    GUI::TableState               filteredState;
    GUI::MultiSelectState         filteredMultiSelect;
    std::string                              hierarchySearch;
    bool                                     filterDirty = true;

    constexpr uint32_t kHierarchyPayloadType = 0x54524545; // 'TREE'
    GUI::ItemDragDropConfig hierDnD;
    int pendingSelectFlat = -1;

    // ---- Per-node visibility ("eye") state -----------------------------------
    // Only leaves store state (in hiddenLeaves == invisible); a branch's state
    // derives from its leaves. Keyed by static row ID so it survives reorders.
    // idToFlat maps into the master treeRows, so it also works while filtered.
    std::unordered_set<GUI::ID>      hiddenLeaves;
    std::unordered_map<GUI::ID, int> idToFlat;

    constexpr int      kVizColWidth     = 24;
    constexpr uint32_t kVizPayloadType  = 0x56495A21; // 'VIZ!' — paint-drag, not item reorder

    enum class VizAgg { On, Off, Mixed };

    void RebuildHierarchyIdToFlat()
    {
        idToFlat.clear();
        idToFlat.reserve(treeRows.size());
        for (int i = 0; i < int(treeRows.size()); ++i)
            idToFlat[treeRows[i].id] = i;
    }

    // Aggregate visibility of the subtree rooted at `id`, over its leaf
    // descendants (a leaf is its own subtree). All visible → On, all
    // invisible → Off, otherwise → Mixed. Unknown / empty → On.
    VizAgg HierComputeAgg(GUI::ID id)
    {
        auto it = idToFlat.find(id);
        if (it == idToFlat.end()) return VizAgg::On;
        const int i = it->second;
        if (i < 0 || i >= int(treeRows.size())) return VizAgg::On;
        const int last = i + treeRows[i].skipCount; // inclusive subtree end
        bool anyVis = false, anyInv = false;
        for (int j = i; j <= last && j < int(treeRows.size()); ++j)
        {
            if (treeRows[j].HasChildren()) continue; // only leaves store state
            if (hiddenLeaves.count(treeRows[j].id)) anyInv = true;
            else                                    anyVis = true;
            if (anyVis && anyInv) return VizAgg::Mixed;
        }
        return anyInv ? VizAgg::Off : VizAgg::On;
    }

    // Apply a visibility to every leaf in the subtree rooted at `id`.
    void HierApplyViz(GUI::ID id, bool makeVisible)
    {
        auto it = idToFlat.find(id);
        if (it == idToFlat.end()) return;
        const int i = it->second;
        if (i < 0 || i >= int(treeRows.size())) return;
        const int last = i + treeRows[i].skipCount;
        for (int j = i; j <= last && j < int(treeRows.size()); ++j)
        {
            if (treeRows[j].HasChildren()) continue;
            if (makeVisible) hiddenLeaves.erase(treeRows[j].id);
            else             hiddenLeaves.insert(treeRows[j].id);
        }
    }

    } // namespace

    void Application::EnableHierarchyPanel()
    {
        // ---- Dataset ------------------------------------------------------
        auto add = [&](const char* name, int depth, IconID icon, bool isOpen) {
            TreeViewRow r{};
            r.id     = idManager.CreateStaticID();
            r.name   = name;
            r.depth  = depth;
            r.icon   = icon;
            r.isOpen = isOpen;
            r.renameBuffer = &renameBuffer;
            treeRows.push_back(std::move(r));
        };
        add("Scene",     0, demo.folderOpenIcon, true);
        add("Meshes",    1, demo.object2Icon, true);
        add("Cube",      2, demo.objectIcon,   false);
        add("Meshes",    2, demo.object2Icon, true);
        add("Cube",      3, demo.objectIcon,   false);
        add("Sphere",    3, demo.objectIcon,   false);
        add("Plane",     3, demo.objectIcon,   false);
        add("Sphere",    2, demo.objectIcon,   false);
        add("Plane",     2, demo.objectIcon,   false);
        add("Camera",    1, demo.objectIcon,  false);
        add("Light",     1, demo.objectIcon,  false);
        add("Materials", 0, demo.folderOpenIcon, false);
        add("Default",   1, demo.objectIcon, false);
        add("Metal",     1, demo.objectIcon, false);
        add("Glass",     1, demo.objectIcon, false);
        add("Settings",  0, demo.folderOpenIcon, false);
        RebuildTreeViewIndex(treeRows);
        RebuildHierarchyIdToFlat();
        hiddenLeaves.clear();
        treeViewState.multiSelect = &treeMultiSelect;
        filterDirty = true;

        // ---- Drag-drop wiring ----------------------------------------------
        // The lambdas are referenced by handlers that fire end-of-frame; the
        // captured pointers must outlive construction (file statics do).
        hierDnD.payloadType = kHierarchyPayloadType;
        hierDnD.getPayload  = [](GUI::ID rowID) -> uintptr_t {
            if (rowID.GetMode() != GUI::IDMode::SubRange) return UINTPTR_MAX;
            const int visIdx = static_cast<int>(rowID.value.subRangeParts.index);
            if (visIdx < 0 || visIdx >= int(treeVisibleIdx.size())) return UINTPTR_MAX;
            return static_cast<uintptr_t>(treeVisibleIdx[visIdx]);
        };
        hierDnD.visualizer = [this](const GUI::DragVisualContext& visual) {
            if (visual.sourceID.GetMode() != GUI::IDMode::SubRange) return;
            const int visIdx = static_cast<int>(visual.sourceID.value.subRangeParts.index);
            if (visIdx < 0 || visIdx >= int(treeVisibleIdx.size())) return;
            const int flatIdx = treeVisibleIdx[visIdx];
            if (flatIdx < 0 || flatIdx >= int(treeRows.size())) return;
            const IconID icon = treeRows[flatIdx].icon != IconID::Invalid
                ? treeRows[flatIdx].icon : demo.circleIcon;
            if (BeginCursorGhostWindow(demo.dragGhostWindowID, visual.screenPos))
            {
                AddGhostIcon(icon);
                EndGhostWindow();
            }
        };
        hierDnD.onReorder = [this](uintptr_t srcPayload, GUI::ID targetID,
                                   TreeDropPosition pos) {
            if (targetID.GetMode() != GUI::IDMode::SubRange) return;
            if (srcPayload == UINTPTR_MAX) return;
            const int targetVis = static_cast<int>(targetID.value.subRangeParts.index);
            if (targetVis < 0 || targetVis >= int(treeVisibleIdx.size())) return;
            const int srcDs = static_cast<int>(srcPayload);
            const int dstDs = treeVisibleIdx[targetVis];
            const int newFlatIdx = TreeUtils::ReorderTreeRow(treeRows, srcDs, dstDs, pos);
            if (newFlatIdx < 0) return;
            RebuildTreeViewIndex(treeRows);
            RebuildHierarchyIdToFlat();
            // Deferred: the matching sub-range rowID needs next frame's
            // TableState._rowsRangeID.
            pendingSelectFlat = newFlatIdx;
            filterDirty = true;
            treeViewState.selectedRowID        = GUI::ID::Invalid();
            treeMultiSelect.selectedIDs.clear();
            treeMultiSelect.lastClickedID       = GUI::ID::Invalid();
            guiStateManager.Interaction().SetFocusID(GUI::ID::Invalid());
            guiStateManager.TextEditorFocus().Clear();
            transitionManager.Clear();
        };
        treeViewState.rowDragDrop = &hierDnD;
    }

    void Application::DisableHierarchyPanel()
    {
        for (const auto& row : treeRows)
            idManager.ReleaseStaticID(row.id);
        treeRows.clear();
        treeVisibleIdx.clear();
        filteredRows.clear();
        filteredVisibleIdx.clear();
        treeMultiSelect.selectedIDs.clear();
        filteredMultiSelect.selectedIDs.clear();
        treeViewState.rowDragDrop = nullptr;
        pendingSelectFlat = -1;
        hiddenLeaves.clear();
        idToFlat.clear();
    }

    void Application::RenderHierarchyPanel()
    {
        // ---- Search box (top of panel) -----------------------------------
        BeginHorizontal(idManager.AllocateDynamicID("hierarchySearchRow"),
                        theme.searchRowStyleRef, GUI::LayoutOverride::Auto().NoShrink());
        if (SearchField(hierarchySearch, demo.findIcon, TextEditBehavior::ContinuousUpdate,
                        GUI::LayoutOverride{}.FlexGrow(1)))
            filterDirty = true;
        EndHorizontal();

        const bool searching = !hierarchySearch.empty();

        // Refilter on demand: matched rows plus all their ancestors, ancestors
        // forced open so matches under collapsed parents show. Depths are
        // preserved, so the copy is a valid forest for RebuildTreeViewIndex.
        if (searching && filterDirty)
        {
            const std::string q = SearchToLower(hierarchySearch);
            std::vector<char> keep(treeRows.size(), 0);
            std::vector<int>  chain; // chain[d] = index of the current ancestor at depth d
            for (int i = 0; i < int(treeRows.size()); ++i)
            {
                chain.resize(treeRows[i].depth);
                chain.push_back(i);
                if (SearchMatches(treeRows[i].name, q))
                    for (int a : chain) keep[a] = 1;
            }
            filteredRows.clear();
            for (int i = 0; i < int(treeRows.size()); ++i)
            {
                if (!keep[i]) continue;
                TreeViewRow r  = treeRows[i];
                r.isOpen       = true;
                r.renameBuffer = nullptr; // copies don't write back — no rename
                filteredRows.push_back(std::move(r));
            }
            RebuildTreeViewIndex(filteredRows);
            filteredState.multiSelect = &filteredMultiSelect;
            filterDirty = false;
        }

        // ---- Render the tree ---------------------------------------------
        // A visibility "eye" column, then the tree. Built on VirtualTable directly
        // to add the leading column. Neither column has a sizer, so the on-stack
        // `cols` array needn't outlive the VirtualTable call.
        auto renderTree = [&](GUI::ID id,
                              std::vector<TreeViewRow>& rows,
                              std::vector<int>& visibleIdx,
                              TableState& state)
        {
            WalkTreeViewVisibility(rows, visibleIdx);
            bool anyExpandable = false;
            for (TreeViewRow& r : rows)
            {
                if (r.HasChildren()) anyExpandable = true;
                if (r.icon == demo.folderOpenIcon || r.icon == demo.folderClosedIcon)
                    r.icon = (r.HasChildren() && r.isOpen) ? demo.folderOpenIcon : demo.folderClosedIcon;
            }

            VirtualTableColumn cols[2];

            // Col 0 — visibility toggle. Shows the "on" eye only on hover (so
            // a fully-visible tree shows no icons); "off"/"mixed" show always.
            cols[0].column = TableColumn{ .header = {},
                .initialWidth = kVizColWidth, .minWidth = kVizColWidth,
                .maxWidth = kVizColWidth, .sizing = ColumnSizing::Fixed,
                .interactiveContent = true,
                .columnBackground = theme.hierarchyVizBgStyleRef };
            cols[0].generateCell = [this, &rows, &visibleIdx, &state](int rowIdx)
            {
                if (rowIdx < 0 || rowIdx >= int(visibleIdx.size())) return;
                const GUI::ID nodeId = rows[visibleIdx[rowIdx]].id;
                const GUI::ID rowID  = idManager.GetSubID(state._rowsRangeID,
                                                          static_cast<uint32_t>(rowIdx));
                const VizAgg agg = HierComputeAgg(nodeId);
                Cell([this, nodeId, rowID, agg]()
                {
                    // Wrapper uses the row's id so hovering the eye highlights the row;
                    // the eye keeps its own id so clicks reach its toggle handler.
                    GUI::Element wrap{};
                    wrap.id       = rowID;
                    wrap.flags    = GUI::ElementFlags::PropagatesDescendantState;
                    wrap.styleRef = theme.hierarchyVisibilityStyleRef;
                    wrap.state    = guiStateManager.GetCurrentState(rowID);
                    auto wRes = elementStorage.Add(std::move(wrap), GUI::LayoutOverride::Auto().FlexGrow(1));
                    VERIFY(wRes.status);
                    VERIFY(windowRegistry.Add(wRes.ref));
                    VERIFY(elementStorage.BeginChildren());

                    // Stable parent so the eye's id can't drift with the visible row count.
                    (void)idManager.PushParent(rowID);
                    const GUI::ID vizID = idManager.AllocateDynamicID("hierViz");
                    const GUI::ElementState st = guiStateManager.GetCurrentState(vizID);
                    const bool hovered = (st & GUI::ElementState::Hover) != GUI::ElementState::None;
                    GUI::IconID icon = GUI::IconID::Invalid;
                    switch (agg)
                    {
                        case VizAgg::On:    icon = hovered ? demo.visibilityOnIcon : GUI::IconID::Invalid; break;
                        case VizAgg::Off:   icon = demo.visibilityOffIcon;   break;
                        case VizAgg::Mixed: icon = demo.visibilityMixedIcon; break;
                    }
                    GUI::Element el{};
                    el.id       = vizID;
                    el.state    = st;
                    el.styleRef = theme.hierarchyVisibilityStyleRef;
                    if (icon != GUI::IconID::Invalid)
                        el.textID = textSystem->RegisterText(vizID, icon, "", el.styleRef, el.state);
                    // Click toggles (hidden→show, otherwise hide); dragging paints that
                    // value over every row crossed (a VIZ-payload drag, not a reorder).
                    el.eventHandler = [this, nodeId](GUI::ElementEvent&& ev) -> bool
                    {
                        switch (ev.type)
                        {
                            case GUI::EventType::MouseDown:
                                if (!ev.state.LeftPressed()) break;
                                {
                                    const bool makeVisible = (HierComputeAgg(nodeId) == VizAgg::Off);
                                    HierApplyViz(nodeId, makeVisible);
                                    guiStateManager.Drag().Begin(ev.id, kVizPayloadType,
                                        makeVisible ? 1u : 0u, ev.state.screenPos);
                                }
                                return true;
                            case GUI::EventType::MouseMove:
                                if ((ev.state.buttons & GUI::MouseButtons::Left) == GUI::MouseButtons::None) break;
                                guiStateManager.Drag().UpdateFromInput(ev.state);
                                return guiStateManager.Drag().GetDraggedID() == ev.id;
                            case GUI::EventType::DragEnter:
                            case GUI::EventType::DragOver:
                                if (ev.state.dragPayloadType != kVizPayloadType) return false;
                                HierApplyViz(nodeId, ev.state.dragPayloadValue != 0);
                                return true;
                            default: break;
                        }
                        return false;
                    };
                    auto res = elementStorage.Add(std::move(el), GUI::LayoutOverride::Auto().FlexGrow(1));
                    VERIFY(res.status);
                    VERIFY(windowRegistry.Add(res.ref));

                    (void)idManager.PopParent();
                    VERIFY(elementStorage.EndChildren());
                });
            };

            // Col 1 — the tree (mirrors VirtualTreeView's single column).
            cols[1].column = TableColumn{ .header = {} };
            cols[1].generateCell = [this, &rows, &visibleIdx, anyExpandable](int rowIdx)
            {
                if (rowIdx < 0 || rowIdx >= int(visibleIdx.size())) return;
                TreeViewRow& r = rows[visibleIdx[rowIdx]];
                TreeCell(r.name, r.depth,
                         r.HasChildren() ? &r.isOpen : nullptr,
                         r.icon, r.ancestorLineMask, r.isLastSibling,
                         r.renameBuffer, anyExpandable);
            };

            // tableFill (flex-basis:0 + grow:1) bounds the table to the remaining
            // panel height so its body scrolls; flex-basis:auto would grow to fit
            // every row and never scroll.
            VirtualTable(id, state, std::span<VirtualTableColumn>(cols, 2),
                         int(visibleIdx.size()), 0, theme.tableFillStyleRef,
                         GUI::LayoutOverride::Auto().FlexGrow(1).MinHeight(0), /*showHeaders*/false,
                         /*cellStyle*/theme.hierarchyVizCellStyleRef);
        };

        if (searching)
        {
            // Filtered view: own state, no drag-drop (a reorder of the copy
            // couldn't be mapped back unambiguously). Visibility still works —
            // aggregation/application run over the master tree by node ID.
            renderTree(idManager.AllocateDynamicID(),
                       filteredRows, filteredVisibleIdx, filteredState);
            return;
        }

        renderTree(idManager.AllocateDynamicID(),
                   treeRows, treeVisibleIdx, treeViewState);

        // Resolve a deferred post-drop selection now that VirtualTable has
        // (re)allocated TableState._rowsRangeID for this frame's row count.
        if (pendingSelectFlat >= 0
            && treeViewState._rowsRangeID != GUI::ID::Invalid())
        {
            const int targetFlat = pendingSelectFlat;
            int newVisIdx = -1;
            for (size_t i = 0; i < treeVisibleIdx.size(); ++i)
                if (treeVisibleIdx[i] == targetFlat) { newVisIdx = int(i); break; }
            if (newVisIdx >= 0)
            {
                const GUI::ID newRowID = idManager.GetSubID(
                    treeViewState._rowsRangeID,
                    static_cast<uint32_t>(newVisIdx));
                treeViewState.selectedRowID = newRowID;
                treeMultiSelect.selectedIDs.clear();
                treeMultiSelect.selectedIDs.insert(newRowID);
                treeMultiSelect.lastClickedID = newRowID;
            }
            pendingSelectFlat = -1;
        }
    }

}
