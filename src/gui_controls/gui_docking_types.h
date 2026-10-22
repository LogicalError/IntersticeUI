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

namespace GUI
{

    // ---- TabBar: reorderable tabs ----
    struct TabBarState {
        GUI::ID barID = GUI::ID::Invalid();
        int*    selectedIndex = nullptr;
        int     nextTabIndex = 0;
        std::function<void(int)>               onClose;    // tab index when close button clicked
        std::function<void(int, GUI::int2)>    onDragOut;  // tab index + mouse release pos
    };

    // ---- Panel + DockingContainer ----
    // Panel: a unit of UI content, built by onPanelGUI during OnGUI. DockNodes store only
    // panel IDs; the Panels live in the DockingLayout.
    struct Panel {
        GUI::ID               id = GUI::ID::Invalid();
        std::string           title;
        GUI::IconID           icon = GUI::IconID::Invalid;

        // Optional kind tag that presets serialize to pick a factory on load.
        // Empty for one-off panels.
        std::string           panelType;

        std::function<void()> onPanelGUI;

        // Docked panels are cached by default (rebuilt on resize and input inside them).
        //   • return a value → also rebuild whenever it changes (data the cache can't observe);
        //   • return nullopt → don't cache this frame (e.g. a live profiler).
        std::function<std::optional<uint64_t>()> cacheSignature;

        // onEnable when the panel becomes visible (a leaf's selectedTab), onDisable when hidden.
        // Hidden panels get no onPanelGUI and should release lazily created resources.
        std::function<void()> onEnable;
        std::function<void()> onDisable;

        // onEnable fired without a matching onDisable.
        bool                  _enabled = false;

        // Kept here so a hidden tab keeps its scroll position (ScrollManagement prunes
        // handles of elements not in the tree).
        GUI::int2             scrollOffset { 0, 0 };
    };

    struct DockingLayout {
        std::unique_ptr<GUI::DockNode>     root;
        std::unordered_map<GUI::ID, Panel> panels;

        // OS window hosting this layout, refreshed each frame. A cross-window tab drop
        // raises it, since the OS may otherwise focus an arbitrary window when the source closes.
        GUI::ID hostWindowID = GUI::ID::Invalid();
    };

    // Valid only between BeginDockingContainer / EndDockingContainer; handlers must capture pointers.
    struct DockingConstructionState {
        GUI::ID        containerID = GUI::ID::Invalid();
        DockingLayout* layout      = nullptr;
    };

    struct DockLeafResult {
        GUI::DockNode* leaf   = nullptr;
        DockingLayout* layout = nullptr;
    };

    struct DockDropResult {
        GUI::DockDropTarget target;
        DockingLayout*      layout = nullptr;
    };

    // ---- Floating dock windows ----
    // A torn-off tab dropped outside every dock area gets its own OS window.
    struct FloatingDockWindow {
        GUI::ID       windowID    = GUI::ID::Invalid();  // static, persists across frames
        DockingLayout layout;
        GUI::int2     spawnPosition {};                   // screen coords, first frame only
        bool          positioned = false;                 // once true, the OS manages the position
    };

}
