#pragma once

#include <gui_controls/gui_application.h>
#include <gui_controls/controls_toolbar_dock.h>

namespace GUI
{
    enum class EditMode : uint32_t;

    // Case-insensitive substring search for the panels' search boxes. Lowercase
    // the query once when it changes, then match each item against it.
    inline std::string SearchToLower(std::string_view s)
    {
        std::string r; r.reserve(s.size());
        for (char c : s) r += (c >= 'A' && c <= 'Z') ? char(c + 32) : c;
        return r;
    }
    inline bool SearchMatches(std::string_view haystack, std::string_view needleLower)
    {
        if (needleLower.empty()) return true;
        if (needleLower.size() > haystack.size()) return false;
        for (size_t i = 0; i + needleLower.size() <= haystack.size(); ++i)
        {
            bool ok = true;
            for (size_t j = 0; j < needleLower.size(); ++j)
            {
                char a = haystack[i + j];
                if (a >= 'A' && a <= 'Z') a = char(a + 32);
                if (a != needleLower[j]) { ok = false; break; }
            }
            if (ok) return true;
        }
        return false;
    }

    enum class ViewMode : uint32_t
    {
        Wireframe   = 0, 
        Solid       = 1, 
        Textured    = 2, 
		Lit         = 4, 
		SolidLit    = Solid | Lit,
        TexturedLit = Textured | Lit,
        Default     = TexturedLit
    };

    class Application : public GUIApplication
    {
    public:
        // ---- Toolbar model (used by the View panel) -----------------------------
        struct ToolbarLayoutModel
        {
            std::vector<ToolbarItem>         sides[kToolbarSideCount];
            ToolbarDockState                 dockState{};
            // Torn-off toolbars, each in its own OS window; dock-back moves the
            // item back into a side vector.
            std::vector<FloatingToolbarItem> floatingItems;
        };
        ToolbarLayoutModel toolbars;
        ToolbarDockCallbacks toolbarCallbacks;
        FloatingToolbarOptions toolbarOpts;

		bool            showGrid         = true;
        bool            enableAudio      = true;
		ViewMode        viewMode         = ViewMode::Default;   // View mode toolbar buttons toggle these flags
        EditMode        nodeViewEditMode = EditMode::Default;   // named mode for node-view shortcuts; copied into each NodeView's editMode

        std::vector<StyleRef> boxStyleRefs;
        StyleRef borderTestStyleRef;

        GUI::VisualColorStyleIndex triangleFillStyle;
        GUI::VisualColorStyleIndex triangleFillStyle2;
        GUI::VisualColorStyleIndex quadFillStyle;
        GUI::VisualColorStyleIndex animatedHueStyle = GUI::VisualColorStyleIndex::Invalid; // misc panel; recreated after a theme reload

        GUI::ColorStyle triangleFillColorStyle;
        GUI::ColorStyle quadFillColorStyle;

        ImageIndex checkerboardIndex { ImageIndex::Invalid };
        ImageIndex demoImageIndex { ImageIndex::Invalid };   // sample image for the Image() element demo

        // ---- Demo / test state ----
        // State used by the demo panels for testing and prototyping GUI features.
        // This will be removed or moved to an examples/ directory once the framework matures.
        struct DemoState
        {
            ID windowLayoutID1 { };
            ID windowLayoutID2 { };
            ID dragGhostWindowID {};

            // Reorderable demo state
            ID reorderTabsID { ID::Invalid() };
            ID reorderListID { ID::Invalid() };
            ID reorderWrappedHorzID { ID::Invalid() };
            ID reorderWrappedVertID { ID::Invalid() };
            ID reorderStringListID { ID::Invalid() };
            ID reorderFloatListID { ID::Invalid() };

            int testBoxStyleCount = 0;

            float sliderValue = 0.5f;
            float sliderFieldValue = 50.0f; // SliderField demo (range [0, 100])
            GUI::color4f demoColor { 0.2f, 0.5f, 0.8f, 1.0f }; // ColorField demo
            GUI::Gradient demoGradient = GUI::Gradient::Linear(
                GUI::color4f{1.0f, 0.0f, 0.0f, 1.0f},
                GUI::color4f{0.0f, 0.0f, 1.0f, 1.0f}); // GradientField demo
            int listSelected = 0;

            // ListView
            int listViewSelected = 0;
            std::vector<std::string> listViewItems = {
                "Documents", "Downloads", "Pictures", "Videos",
                "Music", "Desktop", "Projects", "Backup"
            };

            int buttonClickCount = 0;
            bool toggleValue = false;

            // ButtonGroup demo: segmented alignment picker (one selected at a time).
            int alignment = 1; // 0 = left, 1 = center, 2 = right
            // ToggleGroup demo: text style flags (independent toggles).
            bool bold      = false;
            bool italic    = true;
            bool underline = false;

            std::string textBox1;
            std::string textBox2;

            // Inline-edit
            std::string inlineEditText = "Click me to edit";
            std::string inlineEditBuffer;  // separate edit buffer for validation
            bool        inlineEditActive = false;
            bool        inlineEditFirstFrame = false;

            float floatValue = 0.5f;
            double doubleValue = 0.6;
            int64_t int64Value = -43;
            int32_t int32Value = -42;
            int16_t int16Value = -45;
            int8_t  int8Value = -44;
            uint64_t uint64Value = 43;
            uint32_t uint32Value = 42;
            uint16_t uint16Value = 45;
            uint8_t  uint8Value = 44;

            float clampedFloat = 0.5f;
            uint8_t clampedUInt8 = 50;

            float nanFloat = std::numeric_limits<float>::quiet_NaN();
            float posInfFloat = std::numeric_limits<float>::infinity();
            float negInfFloat = -std::numeric_limits<float>::infinity();

            int2  int2Value = { 1,2 };
            int3  int3Value = { 1,2,3 };
            int4  int4Value = { 1,2,3,4 };

            // Collection
            std::vector<std::string> stringList = { "Apple", "Banana", "Cherry" };
            std::vector<float> floatList = { 1.0f, 2.5f, 3.14f };
            std::vector<std::pair<std::string, float>> dictionary = { {"speed", 1.0f}, {"health", 100.0f}, {"damage", 25.0f} };

            ID textBoxId1 { ID::Invalid() };
            ID textBoxId2 { ID::Invalid() };

            // Each NodeView is an independent graph with its own pan/zoom and drag state.
            NodeView nodeView;

            // Values bound by the node properties' renderBody lambdas.
            float       nodeStrength = 0.5f;
            float       nodeGain     = 1.0f;
            int32_t     nodeSeed     = 7;
            std::string nodeName     = "Source";
            bool        nodeEnabled  = true;

            // Table demos (Table panel + Shortcuts settings page)
            TableState tablePanelTableState;         // resizable Name/Type/Value table in panel_table.cpp
            TableState tablePanelSimpleState;        // 3-column A/B/C demo table in panel_table.cpp
            TableState tablePanelSizingState;        // column-sizing demo (fixed / resizable / stretch) in panel_table.cpp
            TableState tablePanelVirtualTableState;  // virtual-scrolled 50k-row demo in panel_table.cpp
            TableState frozenTableState;             // virtual table with frozenLeftColumns demo in panel_split_scroll.cpp
            TableState shortcutsTableState;          // Category/Command/Shortcut in the Shortcuts settings page

            // ---- TreeTable demo (hierarchical rows in a table) ----
            // Flat row list; a closed node is skipped by advancing skipCount + 1.
            struct TreeTableRow {
                std::string  name;
                std::string  size;
                std::string  type;
                int          depth       = 0;
                bool         hasChildren = false;
                bool         isOpen      = true;
                IconID       icon        = IconID::Invalid;
                GUI::ID      id          = GUI::ID::Invalid();
                // Descendant count. Recomputed by RebuildTreeTableIndex() after edits.
                int          skipCount   = 0;
                // Guide-line metadata (also recomputed by RebuildTreeTableIndex):
                //   ancestorLineMask — bit d set iff the ancestor at depth d+1 has
                //       a next sibling, so its connector line passes this row at column d.
                //   isLastSibling    — last child of its parent; the own-depth guide
                //       line ends at the row center as an L-tick.
                uint32_t     ancestorLineMask = 0;
                bool         isLastSibling    = true;
            };
            std::vector<TreeTableRow> treeTableRows;
            TableState                treeTableState;
            MultiSelectState          treeTableMultiSelect;
            // Row drag-and-drop (TableState.rowDragDrop); payload is the dataset index.
            ItemDragDropConfig        treeTableDragDrop;
            // Visible row -> flat row index, rebuilt each frame.
            std::vector<int>          treeTableVisibleIdx;
            // Moved row's new flat index, applied to the selection after the next
            // VirtualTable call (TableState._rowsRangeID is reallocated inside it).
            int                       treeTablePendingSelectFlatIdx = -1;

            // Icon IDs
            IconID circleIcon = IconID::Invalid;
            IconID heartIcon = IconID::Invalid;
            IconID starIcon = IconID::Invalid;

            // Console log-level icons (+ monochrome variants for the filter toggles)
            IconID infoIcon      = IconID::Invalid;
            IconID warningIcon   = IconID::Invalid;
            IconID errorIcon     = IconID::Invalid;
            IconID infoBwIcon    = IconID::Invalid;
            IconID warningBwIcon = IconID::Invalid;
            IconID errorBwIcon   = IconID::Invalid;

            IconID folderClosedIcon = IconID::Invalid;
            IconID folderOpenIcon   = IconID::Invalid;
                                    
                                    
            IconID audioOnIcon      = IconID::Invalid;
            IconID audioOffIcon     = IconID::Invalid;
                                    
            IconID gridOnIcon       = IconID::Invalid;
            IconID gridOffIcon      = IconID::Invalid;
                                    
            IconID lightOnIcon      = IconID::Invalid;
            IconID lightOffIcon     = IconID::Invalid;
                                    
            IconID toolSelectIcon   = IconID::Invalid;
            IconID toolMoveIcon     = IconID::Invalid;
            IconID toolScaleIcon    = IconID::Invalid;
            IconID toolRotateIcon   = IconID::Invalid;
                                    
            IconID viewWireframe    = IconID::Invalid;
            IconID viewSolid        = IconID::Invalid;
            IconID viewTextured     = IconID::Invalid;

			IconID panelFilesIcon      = IconID::Invalid;
			IconID panelGameIcon       = IconID::Invalid;
            IconID panelHierarchyIcon  = IconID::Invalid;
			IconID panelInspectorIcon  = IconID::Invalid;
			IconID panelLogIcon        = IconID::Invalid;
			IconID panelNodeViewIcon   = IconID::Invalid;
			IconID panelProfilerIcon   = IconID::Invalid;
			IconID panelSettingsIcon   = IconID::Invalid;
			IconID panelViewportIcon   = IconID::Invalid;
            IconID panelMiscIcon       = IconID::Invalid;
                                       
            IconID objectIcon          = IconID::Invalid;
            IconID object2Icon         = IconID::Invalid;
                                       
            IconID fileImageIcon       = IconID::Invalid;
            IconID fileAudioIcon       = IconID::Invalid;
            IconID fileVideoIcon       = IconID::Invalid;
            IconID fileModelIcon       = IconID::Invalid;
            IconID fileTextIcon        = IconID::Invalid;
            IconID fileArchiveIcon     = IconID::Invalid;
                                       
            IconID openFileIcon        = IconID::Invalid;
            IconID saveIcon            = IconID::Invalid;
			IconID saveAsIcon          = IconID::Invalid;
			IconID closeIcon           = IconID::Invalid;
			IconID addIcon             = IconID::Invalid;
			IconID removeIcon          = IconID::Invalid;
			IconID undoIcon            = IconID::Invalid;
			IconID redoIcon            = IconID::Invalid;
			IconID cutIcon             = IconID::Invalid;
			IconID copyIcon            = IconID::Invalid;
			IconID pasteIcon           = IconID::Invalid;
			IconID blockIcon           = IconID::Invalid;
			IconID syncIcon            = IconID::Invalid;
			IconID downloadIcon        = IconID::Invalid;
            IconID uploadIcon          = IconID::Invalid;
			IconID publishIcon         = IconID::Invalid;   
			IconID openInWindowIcon    = IconID::Invalid;
                                       
            IconID duplicateIcon       = IconID::Invalid;
            IconID newIcon             = IconID::Invalid;
            IconID selectAllIcon       = IconID::Invalid;
            IconID deleteIcon          = IconID::Invalid;
            IconID zoomInIcon          = IconID::Invalid;
            IconID zoomOutIcon         = IconID::Invalid;
            IconID findIcon            = IconID::Invalid;
            IconID playIcon            = IconID::Invalid;
            IconID stopIcon            = IconID::Invalid;
            IconID recenterIcon        = IconID::Invalid;
            IconID frameSelectedIcon   = IconID::Invalid;
            IconID brushIcon           = IconID::Invalid;
            IconID eraserIcon          = IconID::Invalid;
            IconID draggableIcon       = IconID::Invalid;

            IconID darkModeIcon        = IconID::Invalid;
            IconID lightModeIcon       = IconID::Invalid;
            IconID lightModeAutoIcon   = IconID::Invalid;
            IconID fullscreenIcon      = IconID::Invalid;
            IconID fullscreenExitIcon  = IconID::Invalid;
            IconID viewSelectedIcon    = IconID::Invalid;
            IconID dropperIcon         = IconID::Invalid;
            IconID editIcon            = IconID::Invalid;
            IconID tagIcon             = IconID::Invalid;
            IconID targetIcon          = IconID::Invalid;
            IconID visibilityLockIcon  = IconID::Invalid;
            IconID visibilityMixedIcon = IconID::Invalid;
            IconID visibilityOffIcon   = IconID::Invalid;
            IconID visibilityOnIcon    = IconID::Invalid;
            
            IconID dropdownArrowIcon   = IconID::Invalid;
            IconID iconCollapseAllIcon = IconID::Invalid;

            IconID keyCommandIcon = IconID::Invalid;


            // Command handles shared by the main menu and toolbar.
            // cmdDocumentation / cmdAbout have no shortcut key.
            CommandID cmdNew = 0, cmdOpen = 0, cmdSave = 0, cmdSaveAs = 0, cmdExit = 0;
            CommandID cmdUndo = 0, cmdRedo = 0, cmdCut = 0, cmdCopy = 0, cmdPaste = 0;
            CommandID cmdDocumentation = 0, cmdAbout = 0;
            CommandID cmdDuplicate = 0, cmdSelectAll = 0;
            CommandID cmdDelete = 0, cmdZoomIn = 0, cmdZoomOut = 0;
            CommandID cmdFind = 0, cmdPlay = 0, cmdStop = 0;
            CommandID cmdRecenter = 0, cmdFrameSelected = 0;
            CommandID cmdBrush = 0, cmdEraser = 0;

            ID smallMenuID{ ID::Invalid() };
            ID bigMenuID{ ID::Invalid() };

            // GridView demo
            int gridViewSelected = -1;
            MultiSelectState gridViewMultiSelect;

            // TabBar demo
            int tabBarSelected = 0;
            std::vector<std::string> tabNames = { "File", "Edit", "View", "Help" };
            ID tabBarID { ID::Invalid() };

            // DockingContainer demo
            ID            dockContainerID { ID::Invalid() };
            DockingLayout dockingLayout;
            std::string   dockNotes;
            bool          dockToggle = false;
            bool          dockInitialized = false;

            // Settings panel chrome (page list, selection, tree width); page content
            // state lives in the fields below.
            SettingsPanelState settings;

            // Appearance page bound values
            int     settingsThemeIndex = 0; // 0 = Dark, 1 = Light
            float   settingsFontScalePct = 100.0f; // percent; 100 = unscaled

            // Misc dummy page values
            std::string settingsUserName = "Player One";
            bool        settingsAutosave = true;
            int         settingsMaxUndo = 64;

            // Shortcuts page state. Lists shortcuts whose mode is in the category
            // selection; an empty selection re-selects "All".
            std::string      settingsShortcutsSearch;
            std::string      settingsShortcutsSearchLower; // cached lowercased copy of the search
            MultiSelectState settingsShortcutsCatSelection;
            int              settingsShortcutsCategoryWidth = 160; // CSS pixels
            // Category-list VirtualTreeView. Rows / VisibleIdx are rebuilt each
            // frame; InternalMulti is the table's sub-range selection, mapped
            // to/from settingsShortcutsCatSelection's static IDs.
            std::vector<TreeViewRow> settingsShortcutsCatRows;
            std::vector<int>         settingsShortcutsCatVisibleIdx;
            TableState               settingsShortcutsCatTableState;
            MultiSelectState         settingsShortcutsCatInternalMulti;

            // Must persist across frames: the column-sizer double-click handler
            // holds &column._maxContentWidth and fires after OnGUI returns.
            struct ShortcutVisRow { uint32_t mode; uint32_t entryIdx; };
            std::vector<VirtualTableColumn> settingsShortcutsColumns;        // persistent column template (built once)
            std::vector<VirtualTableColumn> settingsShortcutsActiveColumns;  // per-frame active subset (Context dropped in single-category view)
            std::vector<ShortcutVisRow>     settingsShortcutsVisRows;
            // Filter fingerprint: visRows are rebuilt only when it changes. A mask
            // of 0 never occurs, so it forces a rebuild on the first frame.
            uint64_t                        settingsShortcutsFilterMask = 0;
            std::string                     settingsShortcutsFilterQuery;
        };
        DemoState demo;


    public:
        bool Initialize(const InitializeOptions& options = {}) override;

        // Re-registers demo-owned styles after a theme reload.
        void OnStylesReloaded() override;
        void ApplyNodeHeaderTints();

        void Cleanup() override;

        bool InitializeDemoGUI();
        bool InitializeDemoStyles();
        bool InitializeDemoResources();

        void OnGUI() override;

    private:
        // Settings pages (panel_settings.cpp). InitSettingsPages() runs once after
        // icon registration. Appearance mutates global state (theme, font scale).
        void InitSettingsPages();
        void RenderSettingsAppearance();
        void RenderSettingsGeneral();
        void RenderSettingsAdvanced();
        void RenderSettingsShortcuts();
        void RenderSettingsAbout();

        // Dock-panel body renderers (one per panel_*.cpp), called each frame
        // while the panel is the active tab.
        void RenderReorderablePanel();
        void RenderHierarchyPanel();
        void RenderNodeViewPanel();

        void RenderBoxTestPanel();
        void RenderInspectorPanel();
        void RenderMiscPanel();
        void RenderTablePanel();
        void RenderLogPanel();
        void RenderTreeTablePanel();
        void RenderSplitScrollPanel();
        void RenderViewportPanel();
        
        void EnableProfilerTreePanel();
        void RenderProfilerTreePanel();

        // Main menu bar across the top of the main window (File / Edit / View / Help).
        void RenderMainMenu();

        void EnableFilesPanel();
        void DisableFilesPanel();
        void RenderFilesPanel();

        // Build/tear down panel data; wired into CreatePanel's onEnable/onDisable.
        void EnableHierarchyPanel();
        void DisableHierarchyPanel();
        void EnableInspectorPanel();
        void DisableInspectorPanel();
        void EnableLogPanel();
        void DisableLogPanel();
    };

} // namespace GUI
