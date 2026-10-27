# Tabs, docking and toolbars

## Tabs

### BeginTabBar / Tab

A bar of tabs. `*selectedIndex` is the selected tab; clicking or the arrow keys change
it. The tabs can be dragged into a new order (`onReorder(from, to)`), wrap onto more
rows when the bar is narrow, and with `onClose(index)` set get a "Close Panel" context
menu. `onDragOut` and `dragVisualizer` work as in the reorderable group. The content
under the bar is up to the app.

```cpp
bool BeginTabBar(ID id, int* selectedIndex, std::function<void(int, int)> onReorder,
                 std::function<void(int)> onClose = nullptr,
                 std::function<void(int, int2)> onDragOut = nullptr,
                 std::function<void(int, int2)> dragVisualizer = nullptr,
                 StyleRef style = {}, LayoutOverride&& constraints = {});
void EndTabBar();
bool Tab(ID id, const GUIContent& content, StyleRef style, LayoutOverride&& constraints = {});
```

`BeginTabBar` has no form without an ID. Classes: `tab-bar`, `tab`, `tab-content`.

```cpp
BeginTabBar(idManager.AllocateDynamicID("tabs"), &selectedTab, nullptr);
Tab("First");
Tab("Second");
EndTabBar();
Text(selectedTab == 0 ? "Content of the first tab" : "Content of the second tab");
```


## Docking

### BeginDockingContainer / EndDockingContainer

Panels in a tree of splits and tabbed leaves, held in a `DockingLayout`. Drag a tab to
move a panel, onto another leaf to dock it there, or out of the window to float it.
Panels are created once with `CreatePanel`; each builds its content in its callback.

```cpp
ID   CreatePanel(std::unordered_map<ID, Panel>& panels, std::string title, IconID icon,
                 std::function<void()> onPanelGUI, std::string panelType = {},
                 std::function<void()> onEnable = {}, std::function<void()> onDisable = {});
bool BeginDockingContainer(ID id, DockingLayout* layout, StyleRef style, LayoutOverride&& constraints = {});
bool BeginDockingContainer(ID id, DockingLayout* layout, LayoutOverride&& constraints = {});
void EndDockingContainer();
void EmitFloatingDockWindows();
```

Build the tree with `MakeLeaf({ panelIDs... })` and `MakeSplit(mode, size, first,
second)` from `gui_dock_node.h`; `DockSplitMode` is `LeftPinned`, `RightPinned`,
`TopPinned` or `BottomPinned` (which child keeps the size). Call
`EmitFloatingDockWindows()` after the container; it gives floating panels their own
windows. A panel's `onEnable` / `onDisable` run when it becomes visible or hidden, and
its `cacheSignature` controls the render cache. Closing a panel destroys it.

Other calls: `FloatPanel(layout, panelID, screenPos)`, `CloseDockedPanel(layout, leaf,
tabIndex)`, `CloseFloatingDockWindow(index)`, `DisableAllPanels(layout)`.

Classes: `dock-container`, `dock-leaf`, `dock-content`, `dock-split-horz`,
`dock-split-vert`, `dock-sizer-horz`, `dock-sizer-vert`, `dock-window`,
`panel-drop-visualization`.

```cpp
// Once:
const ID scene   = CreatePanel(layout.panels, "Scene",   IconID::Invalid, [this] { ScenePanel(); });
const ID details = CreatePanel(layout.panels, "Details", IconID::Invalid, [this] { DetailsPanel(); });
const ID log     = CreatePanel(layout.panels, "Log",     IconID::Invalid, [this] { Text("Nothing logged yet."); });
layout.root = MakeSplit(DockSplitMode::LeftPinned, 220,
    MakeLeaf({ scene }),
    MakeSplit(DockSplitMode::BottomPinned, 150, MakeLeaf({ details }), MakeLeaf({ log })));

// Each frame:
if (BeginWindow(windowID, "Docking", theme.mainWindowStyleRef, LayoutOverride::FixedSize(900, 600).Centered()))
{
    BeginDockingContainer(dockID, &layout, LayoutOverride::Auto());
    EndDockingContainer();
    EmitFloatingDockWindows();
}
EndWindow();
```


## Toolbars

### BeginToolbar / EndToolbar

A wrapping row (or column) in which default-style controls use their toolbar variants:
`toolbar-button`, `toolbar-toggle`, `toolbar-textbox`, `toolbar-number-field`,
`toolbar-menu-trigger`.

```cpp
bool BeginToolbar(bool horizontal = true);
void EndToolbar();
```

### RenderToolbarDock

A free function (`controls_toolbar_dock.h`) that lays out toolbars on the four sides of
some content; their items can be dragged between sides or torn off into floating
windows. The app owns the item lists and moves items in the callbacks.

```cpp
void InitializeToolbarDockState(ToolbarDockState& state, GUIApplication& app);
void ReleaseToolbarDockState(ToolbarDockState& state, GUIApplication& app);
void RenderToolbarDock(GUIApplication& app, ToolbarDockState& state,
                       const std::vector<ToolbarItem> (&itemsPerSide)[kToolbarSideCount],
                       const ToolbarDockCallbacks& callbacks, std::function<void()> renderContent);
```

A `ToolbarItem` has an `id`, a `debugName` and a `renderBody` that emits its controls
(wrapped in `BeginToolbar` by the dock). `ToolbarDockCallbacks` has `onReorder`,
`onCrossSideDrop` and `onTearOff`. Torn-off toolbars are shown with
`EmitFloatingToolbarWindows(floatingItems, &options)`. Classes: `toolbar-side-horz`,
`toolbar-side-vert`, `toolbar-item-horz`, `toolbar-item-vert`, `toolbar-thumb-horz`,
`toolbar-thumb-vert`, `toolbar-floating-window`.

---

[← Tree and grid views](trees-and-grids.md) · [Controls](../controls.md) · [Node view →](node-view.md)
