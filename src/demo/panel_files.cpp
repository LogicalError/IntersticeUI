#include "application.h"
#include "../gui_controls/gui_tree_utils.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

//
// This file is a file system visualization panel demo, meant to test and
// demonstrate the grid view and drag & drop features — including dragging
// files and folders between the grid and the folder tree.
//

namespace GUI
{

    // Files have no stored ID: a grid entry's identity is VirtualGridView's
    // ranged sub-ID, which indexes gridVisible.
    struct GridDemoItem {
        std::string name;
        IconID icon = IconID::Invalid;
    };
    static GUI::ItemDragDropConfig gridDnD;
    static GUI::ItemDragDropConfig folderDnD;
    static GUI::GridViewVirtualState gridScrollState;
    constexpr uint32_t kGridPayload = 0x47524944; // 'GRID'

    // Drag payload: a file's index in the current folder's item list, or a
    // folder's flat folderRows index tagged with the top bit (folders can be
    // dragged from the grid and from the folder tree alike).
    constexpr uintptr_t kPayloadFolderBit = uintptr_t(1) << (sizeof(uintptr_t) * 8 - 1);
    static uintptr_t FolderPayload(int flat)        { return kPayloadFolderBit | static_cast<uintptr_t>(flat); }
    static bool      IsFolderPayload(uintptr_t v)   { return (v & kPayloadFolderBit) != 0; }
    static int       FolderPayloadIndex(uintptr_t v){ return static_cast<int>(v & ~kPayloadFolderBit); }

    // What the grid shows for the current folder: its child folders (flat
    // indices into folderRows) followed by its files (indices into the
    // folder's item list).
    struct GridEntry {
        int folderFlat = -1; // flat folderRows index when this entry is a sub-folder
        int itemIdx    = -1; // index into the current folder's items otherwise
        bool IsFolder() const { return folderFlat >= 0; }
    };
    static std::vector<GridEntry> gridEntries;
    // Icon (cell) size in px, driven by the slider under the grid. Its lowest
    // value (minCell-1, computed per frame) means "list view" instead of a grid.
    static int gridIconSize = 80;  // CSS pixels

    // Folder tree left of the grid (flat VirtualTreeView rows). folderItems is
    // parallel to folderRows and holds each folder's files.
    static std::vector<GUI::TreeViewRow>  folderRows;
    static std::vector<std::vector<GridDemoItem>>    folderItems;
    static std::vector<int>                          folderVisibleIdx;
    static GUI::TableState                folderTreeState;
    static int                                       folderTreeWidth = 180; // CSS pixels
    static int                                       currentFolder   = 0;

    // Search state, rebuilt only when the query, grid or selected folder changes.
    //   gridVisible      — indices into gridEntries that match.
    //   filteredFolder*  — folder tree reduced to folders with a matching item
    //                      (plus ancestors), shown while a query is active.
    static std::vector<int>                          gridVisible;
    static std::string                               filesSearch;
    static std::string                               filesQueryLower;
    static bool                                      gridFilterDirty = true;
    static std::vector<GUI::TreeViewRow>  filteredFolderRows;
    static std::vector<int>                          filteredFolderVisibleIdx;
    static GUI::TableState                filteredFolderState;
    static std::vector<int>                          filteredToMaster; // filtered flat idx → master flat idx

    static std::vector<GridDemoItem>& CurrentItems()
    {
        static std::vector<GridDemoItem> empty;
        return (currentFolder >= 0 && currentFolder < int(folderItems.size()))
             ? folderItems[currentFolder] : empty;
    }

    static int FilesEntryIndex(int visiblePos)
    {
        return (visiblePos >= 0 && visiblePos < static_cast<int>(gridVisible.size()))
             ? gridVisible[visiblePos] : -1;
    }

    // Visible position encoded in a grid item's ranged sub-ID (-1 if not one).
    static int GridItemIndex(GUI::ID id)
    {
        if (id.GetMode() != GUI::IDMode::SubRange) return -1;
        return static_cast<int>(id.value.subRangeParts.index);
    }

    // Flat row index a tree's single-selection points at (-1 if none).
    static int SelectedFlat(const GUI::TableState& state,
                            const std::vector<int>& visibleIdx)
    {
        if (state.selectedRowID.GetMode() != GUI::IDMode::SubRange) return -1;
        const int visIdx = static_cast<int>(state.selectedRowID.value.subRangeParts.index);
        if (visIdx < 0 || visIdx >= int(visibleIdx.size())) return -1;
        return visibleIdx[visIdx];
    }

    // The grid's entry list for the current folder (child folders first, then
    // files), reduced to the entries that match the search.
    static void RebuildGridVisible()
    {
        gridEntries.clear();
        if (currentFolder >= 0 && currentFolder < int(folderRows.size()))
        {
            const int end = currentFolder + 1 + folderRows[currentFolder].skipCount;
            for (int i = currentFolder + 1; i < end; i += 1 + folderRows[i].skipCount)
                gridEntries.push_back({ .folderFlat = i });
        }
        const auto& items = CurrentItems();
        for (int i = 0; i < int(items.size()); ++i)
            gridEntries.push_back({ .itemIdx = i });

        gridVisible.clear();
        for (int e = 0; e < int(gridEntries.size()); ++e)
        {
            const auto& entry = gridEntries[e];
            const std::string& name = entry.IsFolder() ? folderRows[entry.folderFlat].name
                                                       : items[entry.itemIdx].name;
            if (SearchMatches(name, filesQueryLower))
                gridVisible.push_back(e);
        }
    }

    // Whether `payload` may be dropped into the folder at flat index `dst`.
    // A folder can't be dropped into itself or its own subtree.
    static bool CanDropIntoFolder(uintptr_t payload, int dst)
    {
        if (dst < 0 || dst >= int(folderRows.size())) return false;
        if (!IsFolderPayload(payload)) return true;
        const int src = FolderPayloadIndex(payload);
        if (src < 0 || src >= int(folderRows.size())) return false;
        return dst < src || dst > src + folderRows[src].skipCount;
    }

    // Move the folder subtree at flat index `src` to become the last child of
    // `dst`, keeping folderItems in lockstep with folderRows and re-targeting
    // currentFolder. No-op when rejected (self-drop / own descendant).
    static void DropFolderIntoFolder(int src, int dst)
    {
        const int n = static_cast<int>(folderRows.size());
        if (src < 0 || src >= n || dst < 0 || dst >= n) return;

        const GUI::ID curID = folderRows[currentFolder].id;
        const int     count = 1 + folderRows[src].skipCount;
        const int     newFlat = TreeUtils::ReorderTreeRow(
            folderRows, src, dst, GUI::TreeDropPosition::InsideLast);
        if (newFlat < 0) return;

        // Apply the identical splice to the parallel per-folder item lists.
        {
            auto first = folderItems.begin() + src;
            std::vector<std::vector<GridDemoItem>> lifted(
                std::make_move_iterator(first), std::make_move_iterator(first + count));
            folderItems.erase(first, first + count);
            folderItems.insert(folderItems.begin() + newFlat,
                               std::make_move_iterator(lifted.begin()),
                               std::make_move_iterator(lifted.end()));
        }
        TreeUtils::RebuildTreeIndex(folderRows);

        for (int i = 0; i < int(folderRows.size()); ++i)
            if (folderRows[i].id == curID) { currentFolder = i; break; }
        // Visible indices shifted — the render pass re-selects currentFolder.
        folderTreeState.selectedRowID = GUI::ID::Invalid();
        gridFilterDirty = true;
    }

    // The reduced folder tree shown while searching: folders with at least one
    // matching item, plus their ancestors (forced open). Depths are preserved,
    // so the copy is a valid forest for RebuildTreeViewIndex.
    static void RebuildFolderFilter()
    {
        filteredFolderRows.clear();
        filteredToMaster.clear();
        if (filesQueryLower.empty()) return;

        std::vector<char> keep(folderRows.size(), 0);
        std::vector<int>  chain; // chain[d] = index of the current ancestor at depth d
        for (int i = 0; i < int(folderRows.size()); ++i)
        {
            chain.resize(folderRows[i].depth);
            chain.push_back(i);
            bool hasHit = false;
            for (const auto& item : folderItems[i])
                if (SearchMatches(item.name, filesQueryLower)) { hasHit = true; break; }
            if (hasHit)
                for (int a : chain) keep[a] = 1;
        }
        for (int i = 0; i < int(folderRows.size()); ++i)
        {
            if (!keep[i]) continue;
            GUI::TreeViewRow r = folderRows[i];
            r.isOpen       = true;
            r.renameBuffer = nullptr; // copies don't write back — no rename
            filteredFolderRows.push_back(std::move(r));
            filteredToMaster.push_back(i);
        }
        GUIApplication::RebuildTreeViewIndex(filteredFolderRows);
    }

    void Application::EnableFilesPanel()
    {
        // Seed the folder tree.
        auto addFolder = [this](const char* name, int depth, bool isOpen) {
            GUI::TreeViewRow row{};
            row.id     = idManager.CreateStaticID();
            row.name   = name;
            row.depth  = depth;
            row.icon   = demo.folderClosedIcon;
            row.isOpen = isOpen;
            folderRows.push_back(std::move(row));
        };
        addFolder("Home",         0, true);   // 0
        addFolder("Documents",    1, false);  // 1
        addFolder("Images",       1, false);  // 2
        addFolder("Music",        1, false);  // 3
        addFolder("Projects",     1, true);   // 4
        addFolder("IntersticeUI", 2, false);  // 5
        addFolder("Vulkan",       2, false);  // 6
        addFolder("Downloads",    1, false);  // 7
        RebuildTreeViewIndex(folderRows);

        // Per-folder example contents, parallel to folderRows.
        folderItems.assign(folderRows.size(), {});

        folderItems[0] = // Home (its sub-folders come from the tree)
        {
            { "character.fbx", demo.fileModelIcon   },
            { "photo.jpg",     demo.fileImageIcon   },
            { "song.mp3",      demo.fileAudioIcon   },
            { "video.mp4",     demo.fileVideoIcon   },
            { "notes.txt",     demo.fileTextIcon    },
            { "archive.zip",   demo.fileArchiveIcon },
        };

        folderItems[1] = // Documents
        {
            { "report.txt",        demo.fileTextIcon },
            { "todo.txt",          demo.fileTextIcon },
            { "meeting_notes.txt", demo.fileTextIcon },
        };
        for (int i = 0; i < 40; ++i)
        {
            char name[32];
            std::snprintf(name, sizeof(name), "document_%02d.txt", i);
            folderItems[1].push_back({ name, demo.fileTextIcon });
        }

        for (int i = 0; i < 200; ++i) // Images
        {
            char name[32];
            std::snprintf(name, sizeof(name), "photo_%03d%s", i, (i % 3) ? ".jpg" : ".png");
            folderItems[2].push_back({ name, demo.fileImageIcon });
        }

        for (int i = 0; i < 60; ++i) // Music
        {
            char name[32];
            std::snprintf(name, sizeof(name), "track_%02d.mp3", i);
            folderItems[3].push_back({ name, demo.fileAudioIcon });
        }

        folderItems[4] = // Projects
        {
            { "readme.txt", demo.fileTextIcon },
        };

        folderItems[5] = // IntersticeUI
        {
            { "application.cpp",  demo.fileTextIcon },
            { "application.h",    demo.fileTextIcon },
            { "panel_files.cpp",  demo.fileTextIcon },
            { "styles.css",       demo.fileTextIcon },
            { "CMakeLists.txt",   demo.fileTextIcon },
        };

        folderItems[6] = // Vulkan
        {
            { "vk_renderer.cpp", demo.fileTextIcon },
            { "vk_renderer.h",   demo.fileTextIcon },
            { "vk_rhi.cpp",      demo.fileTextIcon },
            { "shader.vert",     demo.fileTextIcon },
            { "shader.frag",     demo.fileTextIcon },
        };

        // Downloads: thousands of generated files to exercise grid virtualization.
        {
            auto& dl = folderItems[7];
            dl.push_back({ "installer.zip", demo.fileArchiveIcon });
            const char*  exts[]  = { ".fbx", ".jpg", ".png", ".mp3", ".mp4", ".txt", ".zip" };
            const IconID icons[] = { demo.fileModelIcon, demo.fileImageIcon, demo.fileImageIcon, demo.fileAudioIcon, demo.fileVideoIcon, demo.fileTextIcon, demo.fileArchiveIcon };
            dl.reserve(dl.size() + 4000);
            for (int i = 0; i < 4000; ++i)
            {
                char name[32];
                std::snprintf(name, sizeof(name), "file_%04d%s", i, exts[i % 7]);
                dl.push_back({ name, icons[i % 7] });
            }
        }

        currentFolder   = 0;
        gridFilterDirty = true;

        // Ghost icon that follows the mouse cursor during the drag. The
        // payload identifies the icon for either source view (grid or tree).
        auto ghost = [this](const GUI::DragVisualContext& visual) {
            IconID icon = IconID::Invalid;
            if (IsFolderPayload(visual.payloadValue))
            {
                const int flat = FolderPayloadIndex(visual.payloadValue);
                if (flat < 0 || flat >= static_cast<int>(folderRows.size())) return;
                icon = demo.folderClosedIcon;
            }
            else
            {
                const auto& items = CurrentItems();
                const int i = static_cast<int>(visual.payloadValue);
                if (i < 0 || i >= static_cast<int>(items.size())) return;
                icon = items[i].icon;
            }
            if (icon == IconID::Invalid) return;
            if (BeginCursorGhostWindow(demo.dragGhostWindowID, visual.screenPos))
            {
                AddGhostIcon(icon);
                EndGhostWindow();
            }
        };

        gridDnD.payloadType = kGridPayload;
        // The item ID's sub-ID index is a position in gridVisible; the payload
        // is the underlying file index or tagged folder index, so drops stay
        // valid across refilters.
        gridDnD.getPayload = [](GUI::ID id) -> uintptr_t {
            const int e = FilesEntryIndex(GridItemIndex(id));
            if (e < 0) return static_cast<uintptr_t>(-1);
            const auto& entry = gridEntries[e];
            return entry.IsFolder() ? FolderPayload(entry.folderFlat)
                                    : static_cast<uintptr_t>(entry.itemIdx);
        };
        gridDnD.canDrop = [](uintptr_t payloadValue, GUI::ID targetID) {
            if (!IsFolderPayload(payloadValue)) return true;
            // Folder targets take the payload; files and the background route
            // it into the folder on view.
            const int e = FilesEntryIndex(GridItemIndex(targetID));
            const GridEntry* target = (e >= 0 && e < int(gridEntries.size()))
                                    ? &gridEntries[e] : nullptr;
            const int dst = (target && target->IsFolder()) ? target->folderFlat : currentFolder;
            return CanDropIntoFolder(payloadValue, dst);
        };
        gridDnD.visualizer = ghost;
        gridDnD.onDrop = [](uintptr_t payloadValue, GUI::ID targetID) {
            // A non-item target (the grid background) drops into the folder on view.
            const int targetEntry = FilesEntryIndex(GridItemIndex(targetID));
            const GridEntry* target = (targetEntry >= 0 && targetEntry < static_cast<int>(gridEntries.size()))
                                    ? &gridEntries[targetEntry] : nullptr;

            if (IsFolderPayload(payloadValue))
            {
                const int dst = (target && target->IsFolder()) ? target->folderFlat : currentFolder;
                DropFolderIntoFolder(FolderPayloadIndex(payloadValue), dst);
                return;
            }

            auto& items = CurrentItems();
            const size_t from = static_cast<size_t>(payloadValue);
            if (from >= items.size()) return;

            if (target && target->IsFolder())
            {
                // Move the file into the target folder
                folderItems[target->folderFlat].push_back(std::move(items[from]));
                items.erase(items.begin() + from);
                gridFilterDirty = true;
            }
            else if (target)
            {
                // Reorder: move `from` before the target file
                size_t to = static_cast<size_t>(target->itemIdx);
                if (to == from) return;
                auto item = std::move(items[from]);
                items.erase(items.begin() + from);
                if (from < to) to--;
                items.insert(items.begin() + to, std::move(item));
                gridFilterDirty = true; // indices shifted — rebuild the filtered view
            }
        };

        // The folder tree shares the grid's payload space: rows drag their
        // tagged flat index and accept anything the grid drags. Every drop
        // zone on a row means "into this folder".
        folderDnD.payloadType    = kGridPayload;
        folderDnD.dropInsideOnly = true;
        folderDnD.getPayload = [](GUI::ID rowID) -> uintptr_t {
            if (rowID.GetMode() != GUI::IDMode::SubRange) return static_cast<uintptr_t>(-1);
            const int visIdx = static_cast<int>(rowID.value.subRangeParts.index);
            if (visIdx < 0 || visIdx >= static_cast<int>(folderVisibleIdx.size())) return static_cast<uintptr_t>(-1);
            return FolderPayload(folderVisibleIdx[visIdx]);
        };
        folderDnD.canDrop = [](uintptr_t payloadValue, GUI::ID targetID) {
            if (targetID.GetMode() != GUI::IDMode::SubRange) return false;
            const int targetVis = static_cast<int>(targetID.value.subRangeParts.index);
            if (targetVis < 0 || targetVis >= static_cast<int>(folderVisibleIdx.size())) return false;
            const int dst = folderVisibleIdx[targetVis];
            if (!IsFolderPayload(payloadValue))
                return dst != currentFolder; // a file is already in its own folder
            return CanDropIntoFolder(payloadValue, dst);
        };
        folderDnD.visualizer = ghost;
        folderDnD.onReorder = [](uintptr_t srcPayload, GUI::ID targetID, TreeDropPosition) {
            if (targetID.GetMode() != GUI::IDMode::SubRange) return;
            const int targetVis = static_cast<int>(targetID.value.subRangeParts.index);
            if (targetVis < 0 || targetVis >= static_cast<int>(folderVisibleIdx.size())) return;
            const int dst = folderVisibleIdx[targetVis];

            if (IsFolderPayload(srcPayload))
            {
                DropFolderIntoFolder(FolderPayloadIndex(srcPayload), dst);
                return;
            }
            auto& items = CurrentItems();
            const size_t from = static_cast<size_t>(srcPayload);
            if (from >= items.size() || dst == currentFolder) return;
            folderItems[dst].push_back(std::move(items[from]));
            items.erase(items.begin() + from);
            gridFilterDirty = true;
        };
        folderTreeState.rowDragDrop = &folderDnD;
    }

    void Application::DisableFilesPanel()
    {
        for (const auto& row : folderRows)
            idManager.ReleaseStaticID(row.id);
        folderRows.clear();
        folderItems.clear();
        folderVisibleIdx.clear();
        gridEntries.clear();
        gridVisible.clear();
        filteredFolderRows.clear();
        filteredFolderVisibleIdx.clear();
        filteredToMaster.clear();
        folderTreeState.rowDragDrop = nullptr;
        currentFolder = 0;
    }

    void Application::RenderFilesPanel()
    {
        // Search box (top of panel).
        BeginHorizontal(idManager.AllocateDynamicID("filesSearchRow"),
                        theme.searchRowStyleRef, GUI::LayoutOverride::Auto().NoShrink());
        if (SearchField(filesSearch, demo.findIcon, TextEditBehavior::ContinuousUpdate,
                        GUI::LayoutOverride{}.FlexGrow(1)))
            gridFilterDirty = true;
        EndHorizontal();

        // Refilter on demand: only when the search changed or the grid mutated.
        if (gridFilterDirty)
        {
            filesQueryLower = SearchToLower(filesSearch);
            RebuildFolderFilter();
            RebuildGridVisible();
            gridFilterDirty = false;
        }

        const bool searching = !filesQueryLower.empty();

        // Horizontal split: folder tree (left) | draggable sizer | file grid (right).
        // flexFillRow (flex-basis:0) bounds the split to the panel height so the
        // grid's virtual content scrolls inside it instead of stretching the row.
        BeginHorizontal(idManager.AllocateDynamicID("filesSplit"),
                        theme.flexFillRowStyleRef,
                        GUI::LayoutOverride::Auto().FlexGrow(1));

        if (searching)
        {
            // Reduced tree: only folders with search hits. Own state; clicks
            // map back to the master folder through filteredToMaster.
            VirtualTreeView(idManager.AllocateDynamicID("filesFolderTree"),
                            filteredFolderRows, filteredFolderVisibleIdx, filteredFolderState,
                            theme.treeViewStyleRef,
                            GUI::LayoutOverride::Auto().FixedWidth(styleManager.Px(folderTreeWidth)).NoGrow());

            const int selFiltered = SelectedFlat(filteredFolderState, filteredFolderVisibleIdx);
            const int selMaster   = (selFiltered >= 0 && selFiltered < int(filteredToMaster.size()))
                                  ? filteredToMaster[selFiltered] : -1;
            if (selMaster >= 0 && selMaster != currentFolder)
            {
                currentFolder = selMaster;
                RebuildGridVisible();
            }
        }
        else
        {
            // Open-folder icon while expanded or hovered as a drop target. The
            // rename pointer is refreshed every frame because drops move rows.
            for (auto& row : folderRows)
            {
                row.icon = (row.HasChildren() && row.isOpen) ? demo.folderOpenIcon
                                                             : demo.folderClosedIcon;
                row.renameBuffer = &row.name;
            }
            if (guiStateManager.Drag().IsDragging()
                && m_treeDropState.targetRowID != GUI::ID::Invalid()
                && folderTreeState._rowsRangeID != GUI::ID::Invalid())
            {
                for (size_t vi = 0; vi < folderVisibleIdx.size(); ++vi)
                    if (idManager.GetSubID(folderTreeState._rowsRangeID, static_cast<uint32_t>(vi))
                        == m_treeDropState.targetRowID)
                    {
                        folderRows[folderVisibleIdx[vi]].icon = demo.folderOpenIcon;
                        break;
                    }
            }

            VirtualTreeView(idManager.AllocateDynamicID("filesFolderTree"),
                            folderRows, folderVisibleIdx, folderTreeState,
                            theme.treeViewStyleRef,
                            GUI::LayoutOverride::Auto().FixedWidth(styleManager.Px(folderTreeWidth)).NoGrow());

            const int selFlat = SelectedFlat(folderTreeState, folderVisibleIdx);
            if (selFlat >= 0 && selFlat != currentFolder)
            {
                currentFolder = selFlat;
                RebuildGridVisible();
            }
            // First frame: highlight the initial folder (the sub-ID needs the
            // range the table just allocated).
            else if (folderTreeState.selectedRowID == GUI::ID::Invalid()
                     && folderTreeState._rowsRangeID != GUI::ID::Invalid())
            {
                for (size_t i = 0; i < folderVisibleIdx.size(); ++i)
                    if (folderVisibleIdx[i] == currentFolder)
                    {
                        folderTreeState.selectedRowID = idManager.GetSubID(
                            folderTreeState._rowsRangeID, static_cast<uint32_t>(i));
                        break;
                    }
            }
        }

        // 4px draggable sizer reusing the dock-sizer style (same cursor and hit
        // region as the dock splitter).
        {
            GUI::ID sizerID = idManager.AllocateDynamicID("filesSizer");
            GUI::Element sizer{};
            sizer.id       = sizerID;
            sizer.styleRef = theme.dockSizerHorzStyleRef;
            sizer.state    = guiStateManager.GetCurrentState(sizerID);
            sizer.eventHandler = GUI::MakeHorizontalSizerHandler(
                guiStateManager, folderTreeWidth, 120, 480, nullptr, nullptr, false, styleManager.GetUIScale());
            auto sRes = elementStorage.Add(std::move(sizer), GUI::LayoutOverride::FixedWidth(styleManager.Px(4)));
            VERIFY(sRes.status);
            VERIFY(windowRegistry.Add(sRes.ref));
        }

        // Right pane: virtualized file grid above an icon-size slider.
        // flex-fill-col (flex-basis:0) bounds it to its share of the split width,
        // otherwise the stretched cells floor its min width and it can't shrink.
        // MinHeight(0) bounds it to the split height so the grid scrolls.
        BeginVertical(idManager.AllocateDynamicID("filesGridPane"),
                      theme.flexFillColStyleRef,
                      GUI::LayoutOverride::Auto().FlexGrow(1).MinHeight(0));

        // Smallest grid cell where a minimum icon plus the filename line both fit
        // (so the label never clips). The slider's lowest tick is one below that
        // and switches to a single-column list instead of a grid.
        const Style& gis    = theme.gridViewItemBaseStyle;
        // The cell is laid out in pixels; the slider value gridIconSize is CSS pixels.
        const Style gisPx   = styleManager.ToPixels(gis);
        const int minIcon   = styleManager.Px(16);
        const int lineH     = std::max(1, theme.gridViewItemLineHeight);   // measured
        const int labelRoom = static_cast<int>(gisPx.padding.Vertical() + gisPx.borderWidths.Vertical()
                                               + gisPx.iconTextGap) + lineH;
        const int minCell   = minIcon + labelRoom;
        const int maxCell   = 160;
        const int listValue = int(std::ceil(float(minCell) / styleManager.GetUIScale())) - 1;   // the single "list view" tick
        const bool listMode = (gridIconSize <= listValue);

        const int cell = std::max(minCell, styleManager.Px(gridIconSize));
        const int rowH = std::max(1, theme.gridViewListRowHeight);         // measured list row height

        // Grid mode: clone the grid-item style with an icon scaled to fill the
        // cell above its reserved label room (Register dedupes → cheap per frame).
        StyleRef itemStyle{};
        if (!listMode)
        {
            Style def = gisPx;
            def.iconSize = static_cast<uint16_t>(std::clamp(cell - labelRoom, minIcon, styleManager.Px(480)));
            itemStyle = styleManager.Register(def, StyleUnits::Pixels);
        }

        // Only the visible rows are built, so the count can run into the thousands.
        // List mode forces one column by making the cell wider than any viewport.
        const GUI::int2 cellSize = listMode ? GUI::int2{ 1 << 20, rowH }
                                            : GUI::int2{ cell, cell };
        VirtualGridView(idManager.AllocateDynamicID("filesGrid"), gridScrollState,
                        static_cast<int>(gridVisible.size()), cellSize,
                        [this, listMode, itemStyle, cell, rowH](int i, GUI::ID itemID)
                        {
                            const int e = FilesEntryIndex(i);
                            if (e < 0 || e >= int(gridEntries.size())) return;
                            const auto& entry = gridEntries[e];

                            auto& items = CurrentItems();
                            std::string_view name;
                            IconID icon;
                            std::string* renameBuffer; // rename edits the name in place
                            if (entry.IsFolder())
                            {
                                if (entry.folderFlat >= int(folderRows.size())) return;
                                name = folderRows[entry.folderFlat].name;
                                renameBuffer = &folderRows[entry.folderFlat].name;
                                // Open-folder icon while hovered by a drag this folder accepts.
                                icon = guiStateManager.Drag().IsDropTargetFor(itemID, kGridPayload)
                                        && CanDropIntoFolder(guiStateManager.Drag().GetPayloadValue(),
                                                             entry.folderFlat)
                                     ? demo.folderOpenIcon : demo.folderClosedIcon;
                            }
                            else
                            {
                                if (entry.itemIdx >= int(items.size())) return;
                                name = items[entry.itemIdx].name;
                                renameBuffer = &items[entry.itemIdx].name;
                                icon = items[entry.itemIdx].icon;
                            }

                            if (listMode)
                                GridViewItem(itemID, name, icon, GUI::ElementState::None,
                                             theme.gridViewListItemStyleRef,
                                             LayoutOverride{}.Height(rowH).FlexGrow(1),
                                             renameBuffer);
                            else
                                // Stretched width: the grid's columns split the
                                // viewport evenly, so the cells fill each row
                                // instead of leaving a gap on the right.
                                GridViewItem(itemID, name, icon, GUI::ElementState::None,
                                             itemStyle,
                                             LayoutOverride{}.Size(
                                                 std::max(cell, int(gridScrollState.cellStretchedW)),
                                                 cell),
                                             renameBuffer);
                        },
                        nullptr,                   // selectedIndex (multi-select used instead)
                        &demo.gridViewMultiSelect,
                        &gridDnD,
                        {}, {},                    // default scroll / row styles
                        LayoutOverride{}.FlexGrow(1));

        // Bottom bar: small integer size slider, pushed to the right edge.
        BeginHorizontal(idManager.AllocateDynamicID("filesIconSizeBar"),
                        GUI::LayoutOverride::Auto().NoGrow());
        {
            GUI::ID spacerID = idManager.AllocateDynamicID("filesIconSizeSpacer");
            GUI::Element spacer{};
            spacer.id       = spacerID;
            spacer.styleRef = theme.emptyHorzStyleRef;
            spacer.flags    = GUI::ElementFlags::NoInput;
            spacer.state    = guiStateManager.GetCurrentState(spacerID);
            auto spRes = elementStorage.Add(std::move(spacer), GUI::LayoutOverride{}.FlexGrow(1));
            VERIFY(spRes.status);
            VERIFY(windowRegistry.Add(spRes.ref));
        }
        Slider(idManager.AllocateDynamicID("filesIconSizeSlider"),
               gridIconSize, listValue, maxCell,
               GUI::LayoutOverride{}.Width(styleManager.Px(150)).Height(styleManager.Px(21)).NoGrow());

        EndHorizontal();

        EndVertical();

        EndHorizontal();
    }

}
