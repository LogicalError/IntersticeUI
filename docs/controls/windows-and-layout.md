# Windows and layout

## BeginWindow / EndWindow

An OS window. A window lays out its children in wrapping rows; put a `BeginVertical`
inside to stack them.

```cpp
bool BeginWindow(ID windowLayoutID, std::string_view title, StyleRef styleRef, LayoutOverride&& constraints,
                 ElementFlags flags = ElementFlags::WindowUserControlled | ElementFlags::WindowIsCentered,
                 int monitorIndex = -1);
void EndWindow();
```

Shorter overloads leave out the title, the style or the constraints. The ID has to be
stable (`idManager.CreateStaticID()`). Returns `false` once the user closed the window;
`EndWindow` is called either way. Default class: `window` (`theme.mainWindowStyleRef`
is `main-window`).

Useful flags: `WindowUserControlled` (move and resize), `WindowIsCentered`,
`WindowNoCaption`, `WindowCustomTitleBar` (draw your own with `BeginTitleBar`),
`WindowConfirmClose` (closing asks the app first, see `CloseRequested`),
`WindowIsModal`, `WindowRenderToImage`.

```cpp
if (BeginWindow(windowID, "Hello", LayoutOverride{}.Size(360, 160)))
{
    BeginVertical(LayoutOverride{}.FlexGrow(1));
    Text("Hello from TachyonUI!");
    EndVertical();
}
EndWindow();
```

`bool CloseRequested(ID windowID)` says whether the user asked to close a window with
`WindowConfirmClose` since last asked; the window stays until the app stops declaring it.

## Ghost windows

Borderless, transparent windows for drag previews. `BeginCursorGhostWindow` places the
window at the cursor, offset so it isn't under the OS cursor.

```cpp
bool BeginGhostWindow(ID windowLayoutID, int2 position, int2 size, StyleRef styleRef);
bool BeginCursorGhostWindow(ID windowLayoutID, int2 cursorPos);
void AddGhostIcon(IconID icon);
void EndGhostWindow();
```

Other overloads leave out the size (sized to content) or the style. Default class:
`ghost-window`; `AddGhostIcon` uses `ghost-icon`.

## BeginHorizontal / BeginVertical and the Wrap variants

Undecorated flex containers: a row, a column, and rows or columns that wrap.

```cpp
bool BeginHorizontal(ID id, StyleRef style, LayoutOverride&& constraints = {});
bool BeginVertical(ID id, StyleRef style, LayoutOverride&& constraints = {});
bool BeginHorizontalWrap(ID id, StyleRef style, LayoutOverride&& constraints = {});
bool BeginVerticalWrap(ID id, StyleRef style, LayoutOverride&& constraints = {});
void EndHorizontal();   // EndVertical, EndHorizontalWrap, EndVerticalWrap
```

Default style: a plain flex box built in code (`theme.emptyHorzStyleRef`,
`emptyVertStyleRef`, `emptyHorzWrapStyleRef`, `emptyVertWrapStyleRef`), not a CSS
class.

```cpp
BeginHorizontal();
Button("Greet", [this] { greeting = "Hello, " + name + "!"; });
Button("Clear", [this] { greeting.clear(); });
EndHorizontal();
```

## BeginArea / EndArea

A grouping container for any content. The default class `collection` is a bordered,
wrapping flex box with `overflow: auto`.

```cpp
bool BeginArea(ID id, StyleRef style, LayoutOverride&& constraints = {});
void EndArea();
```

## Scroll bars

A container scrolls when its style has `overflow: auto` and its size is bounded. A
flex child is bounded by a fixed size, or by `flex-basis: 0` with `flex-grow` all the
way down from a bounded ancestor (the `flex-fill-row` and `flex-fill-col` classes,
`theme.flexFillRowStyleRef` / `flexFillColStyleRef`); otherwise it grows to its
content and has nothing to scroll. Call these from inside the container; the bar
overlays it and paints nothing without overflow.

```cpp
void VerticalScrollbar(ID containerID);
void HorizontalScrollbar(ID containerID);
void ScrollbarOverlay(ID containerID, int2 lastInnerSize = int2{0, 0},
                      ID vertScrollBarID = ID::Invalid(), ID horzScrollBarID = ID::Invalid(),
                      bool emitVert = true, bool emitHorz = true);
```

`ScrollbarOverlay` draws both bars and a corner cap. Classes: `scrollbar-vertical`,
`scrollbar-vertical-thumb`, `scrollbar-horizontal`, `scrollbar-horizontal-thumb`.

## BeginCached / EndCached

A grouping container (like `BeginArea`) whose rendered output is kept. When nothing in
it changed, `BeginCached` returns `false`, the body is skipped and the kept output is
drawn. It rebuilds on a size change, when the cursor or keyboard focus is in it, when
`signature` changes, or after `Invalidate(id)`.

```cpp
bool BeginCached(ID id, StyleRef style, uint64_t signature = 0, LayoutOverride&& constraints = {});
void EndCached();
void Invalidate(ID id);          // safe from event handlers
void InvalidatePanel(ID panelId);
void SetDrawCacheEnabled(bool enabled);
```

The ID must be stable (static, or salted). Default class: `collection`. Docked panels
are cached already (see [Docking](tabs-docking-toolbars.md#docking)).

The kept output stays on the GPU: once it is there, drawing it again, moved or not, uploads
nothing. Output not drawn for 240 frames is dropped and built again when it's next shown.

```cpp
if (BeginCached(listID, dataVersion))
{
    for (const Item& item : items)
        Text(item.name);
}
EndCached();
```

## Label groups

`Label` and `DragLabel` inside a label group share one width, so label / control pairs
line up as rows. Dragging a label's right edge resizes them all. Widths are CSS pixels.

```cpp
bool BeginLabelGroup(ID id, CssPx defaultWidth = 120, CssPx minWidth = 40, CssPx maxWidth = 500,
                     bool resizable = true);
void EndLabelGroup();
```

```cpp
BeginLabelGroup(140);
Label("Name");
Field(name);
DragLabel("Scale");
Field(scale);
EndLabelGroup();
```

In a resizable group, labels use `group-label` and drag labels `group-drag-label`.

## Button and toggle groups

`Button`s inside a button group, or `Toggle`s and `Field(bool)` inside a toggle group,
are packed together with shared borders. A single item keeps its plain style.

```cpp
bool BeginButtonGroup(LayoutOverride&& constraints = {});
void EndButtonGroup();
bool BeginToggleGroup(LayoutOverride&& constraints = {});
void EndToggleGroup();
```

Classes: `button-group` with `group-button`; `toggle-group` with `group-toggle`. A
single item uses `button` or `toggle`. The group's ends can be styled with
`.group-button:first-child` and `:last-child` (see [Syntax](../css/syntax.md#child-position)).

```cpp
BeginToggleGroup(LayoutOverride{}.NoGrow());
Toggle(bold, "B");
Toggle(italic, "I");
Toggle(underline, "U");
EndToggleGroup();
```

---

[← Common conventions](conventions.md) · [Controls](../controls.md) · [Text and images →](text-and-images.md)
