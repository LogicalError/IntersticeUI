#include "application.h"
#include "../gui_controls/gui_tree_utils.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

//
// This file is a file system visualization panel demo, meant to test and
// demonstrate the grid view and drag & drop features - including dragging
// files and folders between the grid and the folder tree.
//

namespace interstice
{

    // Files have no stored ID: a grid entry's identity is VirtualGridView's
    // ranged sub-ID, which indexes gridVisible.
    struct GridDemoItem {
        std::string name;
        IconID icon = IconID::Invalid;
    };
    static interstice::ItemDragDropConfig gridDnD;
    static interstice::ItemDragDropConfig folderDnD;
    static interstice::GridViewVirtualState gridScrollState;
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
    // Grid of icons, or a list (the toolbar's grid/list toggles).
    static bool filesListMode = false;

    // Folder tree left of the grid (flat VirtualTreeView rows). folderItems is
    // parallel to folderRows and holds each folder's files.
    static std::vector<interstice::TreeViewRow>  folderRows;
    static std::vector<std::vector<GridDemoItem>>    folderItems;
    static std::vector<int>                          folderVisibleIdx;
    static interstice::TableState                folderTreeState;
    static int                                       folderTreeWidth = 180; // CSS pixels
    static int                                       currentFolder   = 0;

    // Search state, rebuilt only when the query, grid or selected folder changes.
    //   gridVisible      - indices into gridEntries that match.
    //   filteredFolder*  - folder tree reduced to folders with a matching item
    //                      (plus ancestors), shown while a query is active.
    static std::vector<int>                          gridVisible;
    static std::string                               filesSearch;
    static std::string                               filesQueryLower;
    static bool                                      gridFilterDirty = true;
    static std::vector<interstice::TreeViewRow>  filteredFolderRows;
    static std::vector<int>                          filteredFolderVisibleIdx;
    static interstice::TableState                filteredFolderState;
    static std::vector<int>                          filteredToMaster; // filtered flat idx -> master flat idx

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
    static int GridItemIndex(interstice::ID id)
    {
        if (id.GetMode() != interstice::IDMode::SubRange) return -1;
        return static_cast<int>(id.value.subRangeParts.index);
    }

    // Flat row index a tree's single-selection points at (-1 if none).
    static int SelectedFlat(const interstice::TableState& state,
                            const std::vector<int>& visibleIdx)
    {
        if (state.selectedRowID.GetMode() != interstice::IDMode::SubRange) return -1;
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

        const interstice::ID curID = folderRows[currentFolder].id;
        const int     count = 1 + folderRows[src].skipCount;
        const int     newFlat = TreeUtils::ReorderTreeRow(
            folderRows, src, dst, interstice::TreeDropPosition::InsideLast);
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
        // Visible indices shifted - the render pass re-selects currentFolder.
        folderTreeState.selectedRowID = interstice::ID::Invalid();
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
            interstice::TreeViewRow r = folderRows[i];
            r.isOpen       = true;
            r.renameBuffer = nullptr; // copies don't write back - no rename
            filteredFolderRows.push_back(std::move(r));
            filteredToMaster.push_back(i);
        }
        GUIApplication::RebuildTreeViewIndex(filteredFolderRows);
    }

    void Application::EnableFilesPanel()
    {
        // Seed the folder tree.
        auto addFolder = [this](const char* name, int depth, bool isOpen) {
            interstice::TreeViewRow row{};
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
        auto ghost = [this](const interstice::DragVisualContext& visual) {
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
            }
            EndGhostWindow();
        };

        gridDnD.payloadType = kGridPayload;
        // The item ID's sub-ID index is a position in gridVisible; the payload
        // is the underlying file index or tagged folder index, so drops stay
        // valid across refilters.
        gridDnD.getPayload = [](interstice::ID id) -> uintptr_t {
            const int e = FilesEntryIndex(GridItemIndex(id));
            if (e < 0) return static_cast<uintptr_t>(-1);
            const auto& entry = gridEntries[e];
            return entry.IsFolder() ? FolderPayload(entry.folderFlat)
                                    : static_cast<uintptr_t>(entry.itemIdx);
        };
        gridDnD.canDrop = [](uintptr_t payloadValue, interstice::ID targetID) {
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
        gridDnD.onDrop = [](uintptr_t payloadValue, interstice::ID targetID) {
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
                gridFilterDirty = true; // indices shifted - rebuild the filtered view
            }
        };

        // The folder tree shares the grid's payload space: rows drag their
        // tagged flat index and accept anything the grid drags. Every drop
        // zone on a row means "into this folder".
        folderDnD.payloadType    = kGridPayload;
        folderDnD.dropInsideOnly = true;
        folderDnD.getPayload = [](interstice::ID rowID) -> uintptr_t {
            if (rowID.GetMode() != interstice::IDMode::SubRange) return static_cast<uintptr_t>(-1);
            const int visIdx = static_cast<int>(rowID.value.subRangeParts.index);
            if (visIdx < 0 || visIdx >= static_cast<int>(folderVisibleIdx.size())) return static_cast<uintptr_t>(-1);
            return FolderPayload(folderVisibleIdx[visIdx]);
        };
        folderDnD.canDrop = [](uintptr_t payloadValue, interstice::ID targetID) {
            if (targetID.GetMode() != interstice::IDMode::SubRange) return false;
            const int targetVis = static_cast<int>(targetID.value.subRangeParts.index);
            if (targetVis < 0 || targetVis >= static_cast<int>(folderVisibleIdx.size())) return false;
            const int dst = folderVisibleIdx[targetVis];
            if (!IsFolderPayload(payloadValue))
                return dst != currentFolder; // a file is already in its own folder
            return CanDropIntoFolder(payloadValue, dst);
        };
        folderDnD.visualizer = ghost;
        folderDnD.onReorder = [](uintptr_t srcPayload, interstice::ID targetID, TreeDropPosition) {
            if (targetID.GetMode() != interstice::IDMode::SubRange) return;
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
                        interstice::LayoutOverride::Auto().FlexGrow(1));

        if (searching)
        {
            // Reduced tree: only folders with search hits. Own state; clicks
            // map back to the master folder through filteredToMaster.
            VirtualTreeView(idManager.AllocateDynamicID("filesFolderTree"),
                            filteredFolderRows, filteredFolderVisibleIdx, filteredFolderState,
                            theme.treeViewStyleRef,
                            interstice::LayoutOverride::Auto().FixedWidth(styleManager.Px(folderTreeWidth)).NoGrow());

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
                && TreeDropTargetRow() != interstice::ID::Invalid())
            {
                for (size_t vi = 0; vi < folderVisibleIdx.size(); ++vi)
                    if (folderTreeState.RowIDForIndex(static_cast<int>(vi)) == TreeDropTargetRow())
                    {
                        folderRows[folderVisibleIdx[vi]].icon = demo.folderOpenIcon;
                        break;
                    }
            }

            VirtualTreeView(idManager.AllocateDynamicID("filesFolderTree"),
                            folderRows, folderVisibleIdx, folderTreeState,
                            theme.treeViewStyleRef,
                            interstice::LayoutOverride::Auto().FixedWidth(styleManager.Px(folderTreeWidth)).NoGrow());

            const int selFlat = SelectedFlat(folderTreeState, folderVisibleIdx);
            if (selFlat >= 0 && selFlat != currentFolder)
            {
                currentFolder = selFlat;
                RebuildGridVisible();
            }
            // First frame: highlight the initial folder (the sub-ID needs the
            // range the table just allocated).
            else if (folderTreeState.selectedRowID == interstice::ID::Invalid())
            {
                for (size_t i = 0; i < folderVisibleIdx.size(); ++i)
                    if (folderVisibleIdx[i] == currentFolder)
                    {
                        folderTreeState.selectedRowID = folderTreeState.RowIDForIndex(static_cast<int>(i));
                        break;
                    }
            }
        }

        // 4px draggable sizer reusing the dock-sizer style (same cursor and hit
        // region as the dock splitter).
        {
            interstice::ID sizerID = idManager.AllocateDynamicID("filesSizer");
            interstice::Element sizer{};
            sizer.id       = sizerID;
            sizer.styleRef = theme.dockSizerHorzStyleRef;
            sizer.state    = guiStateManager.GetCurrentState(sizerID);
            sizer.eventHandler = interstice::MakeHorizontalSizerHandler(
                guiStateManager, folderTreeWidth, 120, 480, nullptr, nullptr, false, styleManager.GetUIScale());
            auto sRes = elementStorage.Add(std::move(sizer), interstice::LayoutOverride::FixedWidth(styleManager.Px(4)));
            VERIFY(sRes.status);
            VERIFY(windowRegistry.Add(sRes.ref));
        }

        // Right pane: a toolbar (path, search, grid/list) over the virtualized file grid.
        // flex-fill-col (flex-basis:0) bounds it to its share of the split width,
        // otherwise the stretched cells floor its min width and it can't shrink.
        // MinHeight(0) bounds it to the split height so the grid scrolls.
        BeginVertical(idManager.AllocateDynamicID("filesGridPane"),
                      theme.flexFillColStyleRef,
                      interstice::LayoutOverride::Auto().FlexGrow(1).MinHeight(0));

        BeginHorizontal(idManager.AllocateDynamicID("filesToolbar"), filesToolbarStyleRef,
                        interstice::LayoutOverride::Auto().NoShrink());
        {
            // The open folder's path; a segment opens that folder.
            std::vector<std::string> path;
            std::vector<int>         pathFolders;
            if (currentFolder >= 0 && currentFolder < int(folderRows.size()))
            {
                int depth = folderRows[currentFolder].depth + 1;
                for (int f = currentFolder; f >= 0 && depth > 0; --f)
                    if (folderRows[f].depth < depth)
                    {
                        depth = folderRows[f].depth;
                        path.insert(path.begin(), folderRows[f].name);
                        pathFolders.insert(pathFolders.begin(), f);
                    }
            }
            Breadcrumb(idManager.AllocateDynamicID("filesPath"), path,
                       [pathFolders](int segment) {
                           currentFolder = pathFolders[size_t(segment)];
                           folderTreeState.selectedRowID = interstice::ID::Invalid();   // re-selected from currentFolder
                           gridFilterDirty = true;
                       },
                       interstice::LayoutOverride{}.FlexGrow(1));

            if (SearchField(idManager.AllocateDynamicID("filesSearch"), filesSearch, demo.findIcon,
                            TextEditBehavior::ContinuousUpdate,
                            interstice::LayoutOverride{}.Width(styleManager.Px(150)).NoGrow()))
                gridFilterDirty = true;
            Toggle(idManager.AllocateDynamicID("filesGridMode"), !filesListMode, demo.layoutGridIcon,
                   [](bool) { filesListMode = false; }, iconToggleStyleRef);
            SetTooltip("Grid");
            Toggle(idManager.AllocateDynamicID("filesListMode"), filesListMode, demo.listIcon,
                   [](bool) { filesListMode = true; }, iconToggleStyleRef);
            SetTooltip("List");
        }
        EndHorizontal();

        const bool listMode = filesListMode;
        const int  cellW    = styleManager.Px(96);
        const int  cellH    = styleManager.Px(86);
        const int  rowH     = std::max(1, theme.gridViewListRowHeight);   // measured list row height

        // Grid items: 34 px icons, a file's tinted by its kind (folders keep their color).
        auto itemStyleFor = [this](IconID icon) -> StyleRef {
            const char* color = (icon == demo.fileModelIcon) ? "--text-link"
                              : (icon == demo.fileImageIcon) ? "--ok"
                              :                                "--text-2";
            Style def = styleManager.ToPixels(theme.gridViewItemBaseStyle);
            def.iconSize = static_cast<uint16_t>(styleManager.Px(34));
            for (auto& st : def.states)
                st.iconColor = ColorStyle::Create(theme.GetColor(color));
            return styleManager.Register(def, StyleUnits::Pixels);   // Register dedupes, so per frame is cheap
        };

        // Only the visible rows are built, so the count can run into the thousands.
        // List mode forces one column by making the cell wider than any viewport.
        const interstice::int2 cellSize = listMode ? interstice::int2{ 1 << 20, rowH }
                                            : interstice::int2{ cellW, cellH };
        VirtualGridView(idManager.AllocateDynamicID("filesGrid"), gridScrollState,
                        static_cast<int>(gridVisible.size()), cellSize,
                        [this, listMode, itemStyleFor, cellW, cellH, rowH](int i, interstice::ID itemID)
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
                                GridViewItem(itemID, icon, name, interstice::ElementState::None,
                                             renameBuffer,
                                             theme.gridViewListItemStyleRef,
                                             LayoutOverride{}.Height(rowH).FlexGrow(1));
                            else
                                // Stretched width: the grid's columns split the
                                // viewport evenly, so the cells fill each row
                                // instead of leaving a gap on the right.
                                GridViewItem(itemID, icon, name, interstice::ElementState::None,
                                             renameBuffer,
                                             itemStyleFor(icon),
                                             LayoutOverride{}.Size(
                                                 std::max(cellW, int(gridScrollState.cellStretchedW)),
                                                 cellH));
                        },
                        nullptr,                   // selectedIndex (multi-select used instead)
                        &demo.gridViewMultiSelect,
                        &gridDnD,
                        {}, {},                    // default scroll / row styles
                        LayoutOverride{}.FlexGrow(1));

        EndVertical();

        EndHorizontal();
    }

}
