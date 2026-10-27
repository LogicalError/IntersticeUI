# Node view

## RenderNodeView

A graph editor: nodes with property rows, sockets and wires, pan and zoom, marquee
selection, a minimap, zoom controls and an optional breadcrumb. All state lives in the
`NodeView` the app owns, so several views can coexist.

```cpp
void RenderNodeView(NodeView& view);
void NodeText(std::string_view text);
```

A `NodeModel` has a `stableID` (assign once, e.g. `idManager.CreateStaticID()`), a
`title`, an optional `category` and `categoryColor`, a `pos` in pixels and its
`properties`. A `NodeProperty` has an `input` socket (set to `PropertyEndpoint{}` for an
unwired input; it holds the source output when wired), an `output` socket flag, a
`renderBody` that builds the row with any controls, a `socketColor` and a `rowSpan`.
`NodeText` is row text aligned by the row's sockets: input left, output right, both
centered.

`NodeView` callbacks: `canConnect(view, output, input)` refuses wires, `onEdit` gets
each finished edit as a `NodeViewEdit` (for undo; `ApplyNodeViewEdit` in
`gui_node_view_helpers.h` applies or reverts one), `onBreadcrumb(index)` handles the
breadcrumb. `showZoomControls` and `showMinimap` turn the overlays on and off.

Classes: `node-view`, `node`, `node-header`, `node-title`, `node-category`,
`node-row`, `node-socket`, `node-view-overlay`, `node-view-minimap`.

```cpp
NodeModel mix;
mix.stableID = idManager.CreateStaticID();
mix.title    = "Mix";
mix.properties.push_back(NodeProperty{ .input = PropertyEndpoint{}, .renderBody = [this] { NodeText("Strength"); } });
mix.properties.push_back(NodeProperty{ .output = true, .renderBody = [this] { Label("Gain"); Field(gain); } });
view.nodes.push_back(std::move(mix));

// Each frame, inside a sized container:
RenderNodeView(view);
```

---

[← Tabs, docking and toolbars](tabs-docking-toolbars.md) · [Controls](../controls.md) · [Other controls →](other-controls.md)
