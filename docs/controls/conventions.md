# Common conventions

**Parameters.** Controls take `(id, content or value, ..., callback, style, constraints)`,
and most parts are optional. Each family has overloads:

- with an `ID id` first, for an ID you keep yourself;
- without it: the ID comes from the call site (`std::source_location`), so
  `Button("Save", onSave)` needs no ID at all. See [IDs](../overview/ids.md) for when an ID
  has to be stable;
- with a `StyleRef style`, to replace the default CSS class (get one with
  `theme.GetStyle("my-class")`), or without it for the default.

The signatures below show the overload with an ID and a style; the shorter ones leave
those out.

**Content.** Buttons, toggles, tabs, list items and menu items take a `GUIContent`: a
text, an icon, or both, plus an optional tooltip. It converts from a string or an icon:

```cpp
Button("Save", onSave);
Button(saveIcon, onSave);
Button({ saveIcon, "Save", "Write the file to disk" }, onSave);
```

**Results.** A change is reported twice: the callback runs when the input is handled
(after the frame it arrives in), and the control returns `true` in the next frame. Value
controls that take a `T&` write the value themselves. See
[Return values and callbacks](../overview/values.md#return-values-and-callbacks).

**LayoutOverride.** The last parameter, `LayoutOverride&& constraints = {}`, adjusts
size and position for this one call on top of the style. Its builders chain:

```cpp
Button("OK", onOK, LayoutOverride{}.NoGrow().NoShrink());
BeginVertical(LayoutOverride{}.FlexGrow(1));
BeginWindow(windowID, "Tool", LayoutOverride::FixedSize(900, 600).Centered());
```

| Builder | Effect |
| --- | --- |
| `LayoutOverride::Auto()`, `FixedSize(w, h)`, `FixedWidth(w)`, `FixedHeight(h)` | Factories |
| `Size(w, h)`, `Width(w)`, `Height(h)` | Fixed size |
| `MinWidth`, `MinHeight`, `MaxWidth`, `MaxHeight` | Size limits |
| `FlexGrow(n)`, `FlexShrink(n)`, `NoGrow()`, `NoShrink()` | Flex factors |
| `Position(x, y)`, `Centered()`, `Relative(...)`, `Absolute(...)` | Position |
| `Anchored(target, placement, offset)` | Position next to another element |
| `StickyX()`, `StickyY()`, `Sticky()` | Stick while scrolling |
| `BreakBefore()`, `BreakAfter()`, `KeepWithNext()` | Line breaks in wrapping containers |
| `GridColumn(start, span)`, `GridRow(start, span)`, `ColumnTrack`, `RowTrack` | Grid placement |
| `SetIconSide(side)`, `IconTextGap(gap)` | Icon placement |

Sizes are CSS pixels (scaled by the UI scale) when given as numbers or `CssPx`, and
pixels when given as `Pixels{ n }` or an `int2`. The builders are in
`src/gui_core/layout/gui_constraints.h`.

**State scopes.** Controls emitted between these pairs get a shared state:

| Pair | Effect |
| --- | --- |
| `BeginDisabled()` / `EndDisabled()` | Dimmed, no input; menu items can't be chosen |
| `BeginSelected()` / `EndSelected()` | Drawn selected; a menu item shows a check mark |
| `BeginIndeterminate()` / `EndIndeterminate()` | Fields show a dash for mixed values and return `false` |

**Scope objects.** Each Begin / End pair also has a scope object that calls the End
when it leaves, taking the Begin's arguments: `Window`, `GhostWindow`,
`CursorGhostWindow`, `Menu`, `MenuBar`, `ContextMenu`, `Tooltip`, `Cached`,
`Horizontal`, `Vertical`, `HorizontalWrap`, `VerticalWrap`, `Area`, `LabelGroup`,
`ButtonGroup`, `ToggleGroup`, `Table`, `Row`, `ListView`, `GridView`, `TabBar`,
`Toolbar`, `StatusBar`, `TitleBar`, `ReorderableGroup`, `ReorderableItem`, `Disabled`,
`Selected`, `Indeterminate`, `DockingContainer`. See [Scopes](../overview/begin-end.md#scopes).

```cpp
if (auto menu = Menu("File"))
    MenuItem("Open", [this] { Open(); });
```

**Begin / End.** `BeginWindow`, `BeginMenu`, `BeginContextMenu`, `BeginTooltip` and
`BeginCached` return whether to build the content, and their End is called either way.
The other Begin functions always return `true`. See [Begin and End](../overview/begin-end.md).

---

[Controls](../controls.md) · [Windows and layout →](windows-and-layout.md)
