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

namespace interstice
{

    // ---- TabBar: reorderable tabs ----
    /// Construction state of the current tab bar; internal.
    struct TabBarState {
        interstice::ID barID = interstice::ID::Invalid();
        int*    selectedIndex = nullptr;
        int     nextTabIndex = 0;
        std::function<void(int)>               onClose;    ///< tab index, when its close button is clicked
        std::function<void(int, interstice::int2)>    onDragOut;  ///< tab index and mouse release pos
    };

    /// A tab bar's keyboard navigation, kept across frames (element data store, by bar ID).
    struct TabBarKeyboard {
        int tabCount   = 0;  ///< last frame's
        int focusIndex = -1; ///< the tab that takes focus as it's built, after an arrow key
    };

    // ---- Panel + DockingContainer ----
    // Panel: a unit of UI content, built by onPanelGUI during OnGUI. DockNodes store only
    // panel IDs; the Panels live in the DockingLayout.
    /// A unit of docked UI content: a title, an optional icon, and the callback that builds it.
    struct Panel {
        interstice::ID               id = interstice::ID::Invalid();      ///< key in DockingLayout::panels
        std::string           title;                                      ///< tab label
        interstice::IconID           icon = interstice::IconID::Invalid;  ///< tab icon

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
        interstice::int2             scrollOffset { 0, 0 };
    };

    /// A docking area: the split/tab tree and the panels its leaves refer to by ID.
    struct DockingLayout {
        std::unique_ptr<interstice::DockNode>     root;    ///< the dock tree; leaves hold panel tabs
        std::unordered_map<interstice::ID, Panel> panels;  ///< every panel of this layout, by ID

        /// OS window hosting this layout, refreshed each frame. A cross-window tab drop
        /// raises it, since the OS may otherwise focus an arbitrary window when the source closes.
        interstice::ID hostWindowID = interstice::ID::Invalid();

        /// A torn-off window's layout: a root leaf puts its tabs in the window's title bar.
        bool                  tabsInTitleBar = false;
        std::function<void()> onDock;   ///< the title bar's Dock button (none when unset)
    };

    /// Construction state of the current docking container. Valid only between
    /// BeginDockingContainer and EndDockingContainer; handlers must capture pointers instead.
    struct DockingConstructionState {
        interstice::ID        containerID = interstice::ID::Invalid();
        DockingLayout* layout      = nullptr;
    };

    /// Result of FindDockLeafAtScreenPos: the leaf and its layout, both null on a miss.
    struct DockLeafResult {
        interstice::DockNode* leaf   = nullptr;
        DockingLayout* layout = nullptr;
    };

    /// Result of FindDockDropTargetAtScreenPos: the drop target and its layout.
    struct DockDropResult {
        interstice::DockDropTarget target;
        DockingLayout*      layout = nullptr;
    };

    // ---- Floating dock windows ----
    // A torn-off tab dropped outside every dock area gets its own OS window.
    /// A floating OS window holding torn-off panels in its own layout.
    struct FloatingDockWindow {
        interstice::ID       windowID    = interstice::ID::Invalid();  ///< static, persists across frames
        DockingLayout layout;                                     ///< the window's own dock tree and panels
        interstice::int2     spawnPosition {};                   ///< screen pixels, used on the first frame only
        bool          positioned = false;                 ///< once true, the OS manages the position
        DockingLayout*  home       = nullptr;                ///< layout Dock returns the panels to
        interstice::ID  homeLeafID = interstice::ID::Invalid(); ///< leaf the panels were torn from
        bool            dockRequested = false;                ///< Dock clicked; done next frame
    };

}
