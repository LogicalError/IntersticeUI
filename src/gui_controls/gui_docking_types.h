#pragma once

#include <gui_core/gui_id.h>
#include <gui_core/gui_base_types.h>
#include <gui_core/style/gui_style_text.h>      // IconID

#include <gui_controls/gui_dock_node.h>          // DockNode, DockDropTarget

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// Docking + panel data model: Panel, DockingLayout (tree + panel registry),
// per-frame construction state, hit-test results and floating windows.

namespace tachyon
{
    /// @addtogroup controls_docking
    /// @{


    // ---- TabBar: reorderable tabs ----
    /// Construction state of the current tab bar; internal.
    struct TabBarState {
        tachyon::ID barID = tachyon::ID::Invalid();
        int*    selectedIndex = nullptr;
        int     nextTabIndex = 0;
        std::function<void(int)>               onClose;    ///< tab index, when its close button is clicked
        std::function<void(int, tachyon::int2)>    onDragOut;  ///< tab index and mouse release pos
        std::vector<bool> shown;                ///< by tab index; empty: every tab is shown
        struct HiddenTab { int index = 0; tachyon::IconID icon = tachyon::IconID::Invalid; std::string text; };
        std::vector<HiddenTab> hidden;          ///< the tabs left out, listed by the overflow button
    };

    /// What a tab bar keeps across frames (element data store, by bar ID): its keyboard
    /// navigation, and the sizes it measured, in pixels, that decide which tabs fit.
    struct TabBarMemory {
        int tabCount   = 0;  ///< last frame's
        int focusIndex = -1; ///< the tab that takes focus as it's built, after an arrow key

        std::vector<int32_t> widths;        ///< by tab index; a hidden tab keeps its last width
        int32_t              barWidth = 0;  ///< the bar's inner width
        int32_t              overflowWidth = 0; ///< the overflow button's width
    };

    // ---- Panel + DockingContainer ----
    // Panel: a unit of UI content, built by onPanelGUI during OnGUI. DockNodes store only
    // panel IDs; the Panels live in the DockingLayout.
    /// A unit of docked UI content: a title, an optional icon, and the callback that builds it.
    struct Panel {
        tachyon::ID               id = tachyon::ID::Invalid();      ///< key in DockingLayout::panels
        std::string           title;                                      ///< tab label
        tachyon::IconID           icon = tachyon::IconID::Invalid;  ///< tab icon

        /// Optional kind tag that presets serialize to pick a factory on load.
        /// Empty for one-off panels.
        std::string           panelType;

        /// Builds the panel's content (inside the panel's area); not called while hidden.
        std::function<void()> onPanelGUI;

        /// Docked panels are cached by default (rebuilt on resize and input inside them).
        ///   - return a value -> also rebuild whenever it changes (data the cache can't observe);
        ///   - return nullopt -> don't cache this frame (e.g. a live profiler).
        std::function<std::optional<uint64_t>()> cacheSignature;

        /// Called when the panel becomes visible (its leaf's selected tab).
        std::function<void()> onEnable;
        /// Called when the panel is hidden. Hidden panels get no onPanelGUI and should release
        /// lazily created resources.
        std::function<void()> onDisable;

        /// Kept by the docking; not part of the API.
        struct Internal
        {
            /// onEnable fired without a matching onDisable.
            bool enabled = false;
        } internal;  ///< not part of the API

        /// Scroll position, kept here so a hidden tab keeps it (ScrollManagement prunes
        /// handles of elements not in the tree).
        tachyon::int2             scrollOffset { 0, 0 };
    };

    /// A docking area: the split/tab tree and the panels its leaves refer to by ID.
    ///
    ///     const ID scene = CreatePanel(layout.panels, "Scene", IconID::Invalid, [this] { ScenePanel(); });
    ///     const ID log   = CreatePanel(layout.panels, "Log",   IconID::Invalid, [this] { LogPanel(); });
    ///     layout.root = MakeSplit(DockSplitMode::LeftPinned, 220, MakeLeaf({ scene }), MakeLeaf({ log }));
    ///
    ///     // In OnGUI, inside a window:
    ///     BeginDockingContainer(dockID, &layout, LayoutOverride::Auto());
    ///     EndDockingContainer();
    ///     EmitFloatingDockWindows();
    struct DockingLayout {
        std::unique_ptr<tachyon::DockNode>     root;    ///< the dock tree; leaves hold panel tabs
        std::unordered_map<tachyon::ID, Panel> panels;  ///< every panel of this layout, by ID

        /// OS window hosting this layout, refreshed each frame. A cross-window tab drop
        /// raises it, since the OS may otherwise focus an arbitrary window when the source closes.
        tachyon::ID hostWindowID = tachyon::ID::Invalid();

        /// A torn-off window's layout: a root leaf puts its tabs in the window's title bar.
        bool                  tabsInTitleBar = false;
        std::function<void()> onDock;   ///< the title bar's Dock button (none when unset)
    };

    /// Construction state of the current docking container. Valid only between
    /// BeginDockingContainer and EndDockingContainer; handlers must capture pointers instead.
    struct DockingConstructionState {
        tachyon::ID        containerID = tachyon::ID::Invalid();
        DockingLayout* layout      = nullptr;
    };

    /// Result of FindDockLeafAtScreenPos: the leaf and its layout, both null on a miss.
    struct DockLeafResult {
        tachyon::DockNode* leaf   = nullptr;
        DockingLayout* layout = nullptr;
    };

    /// Result of FindDockDropTargetAtScreenPos: the drop target and its layout.
    struct DockDropResult {
        tachyon::DockDropTarget target;
        DockingLayout*      layout = nullptr;
    };

    // ---- Floating dock windows ----
    // A torn-off tab dropped outside every dock area gets its own OS window.
    /// A floating OS window holding torn-off panels in its own layout.
    struct FloatingDockWindow {
        tachyon::ID       windowID    = tachyon::ID::Invalid();  ///< static, persists across frames
        DockingLayout layout;                                     ///< the window's own dock tree and panels
        tachyon::int2     spawnPosition {};                   ///< screen pixels, used on the first frame only
        bool          positioned = false;                 ///< once true, the OS manages the position
        DockingLayout*  home       = nullptr;                ///< layout Dock returns the panels to
        tachyon::ID  homeLeafID = tachyon::ID::Invalid(); ///< leaf the panels were torn from
        bool            dockRequested = false;                ///< Dock clicked; done next frame
    };

    /// @}
}
