# Controls reference

Every control and container is a member function of `interstice::GUIApplication`,
called from `OnGUI()` each frame. This page lists them by family, with their main
signatures, what they return, the callback they take and the CSS class they use by
default. How IDs, Begin / End pairs and return values work in general is in
[overview.md](overview.md); how to style the classes named here is in [css.md](css.md).

The snippets run inside `OnGUI()` of a `GUIApplication` subclass, with
`using namespace interstice;`. The programs in `examples/` show the same calls in
context.

## Common conventions

**Parameters.** Controls take `(id, content or value, ..., callback, style, constraints)`,
and most parts are optional. Each family has overloads:

- with an `ID id` first, for an ID you keep yourself;
- without it: the ID comes from the call site (`std::source_location`), so
  `Button("Save", onSave)` needs no ID at all. See [IDs](overview.md#ids) for when an ID
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
[Return values and callbacks](overview.md#return-values-and-callbacks).

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
`Selected`, `Indeterminate`, `DockingContainer`. See [Scopes](overview.md#scopes).

```cpp
if (auto menu = Menu("File"))
    MenuItem("Open", [this] { Open(); });
```

**Begin / End.** `BeginWindow`, `BeginMenu`, `BeginContextMenu`, `BeginTooltip` and
`BeginCached` return whether to build the content, and their End is called either way.
The other Begin functions always return `true`. See [Begin and End](overview.md#begin-and-end).

## Windows and layout containers

### BeginWindow / EndWindow

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
    Text("Hello from IntersticeUI!");
    EndVertical();
}
EndWindow();
```

`bool CloseRequested(ID windowID)` says whether the user asked to close a window with
`WindowConfirmClose` since last asked; the window stays until the app stops declaring it.

### Ghost windows

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

### BeginHorizontal / BeginVertical and the Wrap variants

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

### BeginArea / EndArea

A grouping container for any content. The default class `collection` is a bordered,
wrapping flex box with `overflow: auto`.

```cpp
bool BeginArea(ID id, StyleRef style, LayoutOverride&& constraints = {});
void EndArea();
```

### Scroll bars

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

### BeginCached / EndCached

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
are cached already (see [Docking](#docking)).

```cpp
if (BeginCached(listID, dataVersion))
{
    for (const Item& item : items)
        Text(item.name);
}
EndCached();
```

### Label groups

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

### Button and toggle groups

`Button`s inside a button group, or `Toggle`s and `Field(bool)` inside a toggle group,
are packed together with shared borders. A single item keeps its plain style.

```cpp
bool BeginButtonGroup(LayoutOverride&& constraints = {});
void EndButtonGroup();
bool BeginToggleGroup(LayoutOverride&& constraints = {});
void EndToggleGroup();
```

Classes: `button-group` with `first-button`, `mid-button`, `last-button`;
`toggle-group` with `first-toggle`, `mid-toggle`, `last-toggle`.

```cpp
BeginToggleGroup(LayoutOverride{}.NoGrow());
Toggle(bold, "B");
Toggle(italic, "I");
Toggle(underline, "U");
EndToggleGroup();
```

## Text and images

### Text

Static text.

```cpp
void Text(ID id, std::string_view text, StyleRef style, LayoutOverride&& constraints = {});
```

Default class: `static-text`.

### Label

A label for the control after it. Inside a label group it takes the group's width.

```cpp
void Label(ID id, std::string_view text, StyleRef style, LayoutOverride&& constraints = {});
void Label(ID id, std::string_view text, std::string_view caption, LayoutOverride&& constraints = {});
```

The second form puts the label at the top of a tall row (such as a list), with a caption
under it, for example `Label("Items", "3 items")`. Default class: `label`; the caption
form uses `group-label-top`, `group-label-title` and `group-label-caption`.

### DragLabel

A label that changes the value of the numeric field after it when dragged sideways.

```cpp
void DragLabel(ID id, std::string_view text, StyleRef style, LayoutOverride&& constraints = {});
```

Default class: `drag-label`.

```cpp
DragLabel("Float");
Field(floatValue);
```

### SelectableText

Read-only rich text with links, mouse selection and Ctrl+C. The text is a span of
`ContentRun`s (text or icon runs; a run with `isLink` and a non-zero `contentId` is a
link). `onLinkActivate` gets a clicked link's `contentId`. Use a stable ID so the
selection survives across frames.

```cpp
void SelectableText(ID id, const std::span<ContentRun>& runs,
                    std::function<void(intptr_t contentId)> onLinkActivate,
                    StyleRef style, LayoutOverride&& constraints = {});
```

There is no default style; pass one.

### Image

Draws an image registered with `graphicsContext->RegisterImage`, stretched over the
element. With `sourceSize` (the image's pixel size) it keeps the aspect ratio.

```cpp
void Image(ID id, ImageIndex image, StyleRef style, LayoutOverride&& constraints = {});
void Image(ID id, ImageIndex image, uint2 sourceSize, StyleRef style, LayoutOverride&& constraints = {});
```

Default style: `theme.imageStyleRef`, built in code (CSS can't express the texture
fill).

## Buttons and toggles

### Button

```cpp
bool Button(ID id, const GUIContent& content, std::function<void()> onClick, StyleRef style,
            LayoutOverride&& constraints = {});
```

Returns `true` in the frame after a click; `onClick` runs when the click is handled.
Default class: `button` (`toolbar-button` inside a toolbar).

```cpp
Button("Click me", [this] { ++clicks; });
```

### CommandButton

An icon button for a command in `commandManager`. Its icon and callback come from the
command; the tooltip shows the command's description and shortcut, e.g. "Undo (Ctrl+Z)".

```cpp
bool CommandButton(CommandID command, StyleRef style, LayoutOverride&& constraints = {});
```

Default class: `button`.

### Toggle

A button that stays pressed while on.

```cpp
bool Toggle(ID id, bool value, const GUIContent& content, std::function<void(bool)> onToggle, StyleRef style,
            LayoutOverride&& constraints = {});
bool Toggle(ID id, bool& value, const GUIContent& content, StyleRef style, LayoutOverride&& constraints = {});
```

The `bool&` form toggles `value` itself. `onToggle` gets the new value. Returns `true`
in the frame after a click. Default class: `toggle` (`toolbar-toggle` in a toolbar).

```cpp
Toggle(bold, "B");
Toggle(mode == Mode::Row, "Row", [this](bool) { mode = Mode::Row; });
```

`Field(bool& value, std::string_view text)` is the same toggle.

### Switch

An on/off switch (track and knob) with an optional label after it.

```cpp
bool Switch(ID id, bool value, std::string_view text, std::function<void(bool)> onToggle, StyleRef style,
            LayoutOverride&& constraints = {});
bool Switch(ID id, bool& value, std::string_view text = {}, LayoutOverride&& constraints = {});
```

Classes: `switch`, `switch-track`, `switch-knob`, `switch-label`.

```cpp
Switch(enabled, enabled ? "On" : "Off");
```

### Checkbox

A box with a tick, and its label. There is only the form with an ID and a callback.

```cpp
bool Checkbox(ID id, bool value, std::string_view text, std::function<void(bool)> onToggle,
              LayoutOverride&& constraints = {});
```

Classes: `checkbox`, `checkbox-box`, `checkbox-label`.

```cpp
Checkbox(gridCheckID, showGrid, "Show grid", [this](bool on) { showGrid = on; });
```

## Fields

`Field` is overloaded on the value type: text, bool, numbers, vectors, lists and
dictionaries. All return `true` when the value changed.

### Text fields

```cpp
bool Field(ID id, std::string& value, TextEditBehavior textEditBehavior, StyleRef style,
           LayoutOverride&& constraints = {});
```

`TextEditBehavior` flags combine with `|`:

| Flag | Effect |
| --- | --- |
| `ContinuousUpdate` (default) | Update `value` on every keystroke, not only on commit |
| `Multiline` | Allow newlines instead of committing on Enter |
| `AcceptTabs` | Tab inserts a tab instead of moving focus |
| `ReadOnly` | Selectable and copyable, not editable |
| `None` | Update on commit (Enter or focus loss) |

Default class: `textbox` (`toolbar-textbox` in a toolbar).

```cpp
Field(name);
Field(notes, TextEditBehavior::ContinuousUpdate | TextEditBehavior::Multiline);
```

### SearchField

A text field with a leading icon inside its border.

```cpp
bool SearchField(ID id, std::string& value, IconID icon,
                 TextEditBehavior textEditBehavior = TextEditBehavior::ContinuousUpdate,
                 LayoutOverride&& constraints = {});
```

Classes: `search-textbox`, `search-textbox-icon`, `search-textbox-field`.

### Numeric fields

```cpp
template<typename T, typename ComponentT = typename TypeDimensions<T>::ComponentType>
bool Field(ID id, T& value, TextEditBehavior textEditBehavior, StyleRef style,
           NumericFieldConfig<ComponentT> config = {}, LayoutOverride&& constraints = {});
```

Shorter overloads leave out the behavior, the style or the config. For `float`,
`double` and the 8- to 64-bit signed and unsigned integers, it is an editable number
that also changes by dragging. `NumericFieldConfig` sets `minVal`, `maxVal` and
`stepPerPixel` (the change per dragged pixel). A NaN shows in a warning color. Default
class: `number-field` (`toolbar-number-field` in a toolbar).

```cpp
Field(floatValue);
Field(intValue, NumericFieldConfig<int>{ .minVal = 0, .maxVal = 10 });
```

### Vector fields

The same `Field` on a vector type shows one number per component, each after a colored
axis label that drags it: `int2`-`int4`, `uint2`-`uint4`, `short2`-`short4`,
`ushort2`-`ushort4`, `byte2`-`byte4`, `float2`-`float4` (X, Y, Z, W), and `color4b`,
`color4f` (R, G, B, A). The config applies to each component.

```cpp
DragLabel("Position");
Field(position);   // float3
```

Classes: `vector-component` for the numbers, `axis-label-x`, `axis-label-y`,
`axis-label-z`, `axis-label-w` for the labels.

### TypedField

A number field with its type after the value, e.g. "0.5  f32".

```cpp
template<typename T> bool TypedField(ID id, T& value, LayoutOverride&& constraints = {});
```

Classes: `typed-field`, `typed-field-input`, `typed-field-type`.

### Lists

A reorderable list over a `std::vector<T>`: each row has a grip, its index, the field
(numbers with their type) and a remove button, with "Add item" after the rows. `T` is
any type `Field` takes. `groupID` must be stable so the reorder state survives.

```cpp
template<typename T>
void Field(ID groupID, std::vector<T>& items, StyleRef style, LayoutOverride layoutOverride = {});
template<typename T>
void Field(ID groupID, std::vector<T>& items, LayoutOverride layoutOverride = {});
```

```cpp
Label("Names", std::to_string(names.size()) + " items");
Field(namesListID, names, LayoutOverride{}.FlexGrow(1));
```

Classes: `collection-area` around it, `collection-list`, `collection-row`,
`collection-index`, `collection-remove`, `collection-add`.

### Dictionaries

A key / value list with `std::string` keys: a header with the types, a row per entry
(key, value, remove) and "Add entry". Over a vector of pairs the rows keep their order;
over an `unordered_map` the order can change when the map rehashes, and a renamed key is
committed on Enter or focus loss (a rename onto an existing key is ignored).

```cpp
template<typename V>
void Field(std::vector<std::pair<std::string, V>>& items, StyleRef style, LayoutOverride layoutOverride = {});
template<typename V>
void Field(std::unordered_map<std::string, V>& items, StyleRef style, LayoutOverride layoutOverride = {});
```

Both also have a form without the style. Classes: `collection-area`,
`collection-header`, `collection-header-cell`, `collection-header-caption`,
`collection-header-type`, `collection-entry`, `collection-value`, `collection-remove`,
`collection-add-entry`.

## Sliders

### SliderField

A filled bar with the value on it (and the optional text). Dragging the bar changes the
value; a click without dragging turns it into a numeric field for typing. The value is
clamped to `[minVal, maxVal]`; the `int` form snaps to whole numbers.

```cpp
bool SliderField(ID id, float& value, float minVal, float maxVal, std::string_view text,
                 LayoutOverride&& constraints = {});
bool SliderField(ID id, int& value, int minVal, int maxVal, std::string_view text,
                 LayoutOverride&& constraints = {});
```

Classes: `slider-field-track`, `slider-field-bg`, `slider-field-fill`,
`slider-field-text`, `slider-field-value`, `slider-field-edge`.

```cpp
SliderField(volume, 0.0f, 100.0f, "Volume");
```

### Slider

A groove and a knob, without value text; drag only, integer valued.

```cpp
bool Slider(ID id, int& value, int minVal, int maxVal, LayoutOverride&& constraints = {});
```

Classes: `slider`, `slider-groove`, `slider-fill`, `slider-knob`.

## Color and gradient fields

### ColorField

A swatch, the color as hex and its alpha in percent. Clicking the swatch opens the color
picker; OK keeps the color, Cancel and the close button restore the original.
`allowHDR` allows values above 1.

```cpp
bool ColorField(ID id, color4f& value, bool allowHDR = false, LayoutOverride&& constraints = {});
bool ColorField(ID id, color4b& value, LayoutOverride&& constraints = {});
```

Returns `true` when the value changed. Classes: `color-field`, `color-swatch`,
`color-hex`, `color-alpha`; the picker uses `color-picker` and the `color-picker-*`
classes.

```cpp
DragLabel("Color");
ColorField(color);
```

### GradientField

A gradient swatch; clicking it opens the gradient editor, with the same OK / Cancel
behavior.

```cpp
bool GradientField(ID id, Gradient& value, LayoutOverride&& constraints = {});
```

Classes: `gradient-swatch`; the editor uses `gradient-editor` and the
`gradient-editor-*` classes.

## Menus

### BeginMenu / EndMenu

Outside a menu, `BeginMenu` is a dropdown button that opens a menu (this is also the
dropdown / combo box). Inside a menu it is a submenu item. Inside a menu bar it is a bar
trigger. Returns whether the menu is open; call `EndMenu` either way.

```cpp
bool BeginMenu(ID id, std::string_view text, StyleRef buttonStyle, StyleRef menuStyle, StyleRef itemStyle,
               LayoutOverride&& constraints = {});
void EndMenu();
```

Classes: `menu-trigger` for the button (`toolbar-menu-trigger` in a toolbar), `menu`
for the popup, `menu-item-row` for its items.

```cpp
if (BeginMenu(menuID, "Choose...", LayoutOverride{}.NoGrow().NoShrink()))
{
    for (const char* item : { "Apple", "Banana", "Cherry" })
        MenuItem(item, [this, item] { choice = item; });
}
EndMenu();
```

To show the current choice with a check mark, wrap its item in `BeginSelected()` /
`EndSelected()`.

### MenuItem

```cpp
bool MenuItem(ID id, const GUIContent& content, std::string_view shortcut, std::function<void()> onClick,
              StyleRef style, LayoutOverride&& constraints = {});
bool MenuItem(const GUIContent& content, std::function<void()> onClick = nullptr, LayoutOverride&& constraints = {});
bool MenuItem(const GUIContent& content, std::string_view shortcut, std::function<void()> onClick,
              LayoutOverride&& constraints = {});
bool MenuItem(CommandID command);
```

An optional icon, the label and an optional shortcut text. The icon column is reserved
when any item in the menu has an icon or is selected. Returns `true` when chosen; the
menu then closes. The `CommandID` form takes its icon, label, shortcut and callback from
the command. Classes: `menu-item-row`, `menu-item-icon`, `menu-item-label`,
`menu-item-shortcut`, `menu-item-arrow` (submenu arrow).

```cpp
MenuItem("Copy", "Ctrl+C", [this] { Copy(); });
```

### Other menu parts

```cpp
bool DestructiveMenuItem(ID id, const GUIContent& content, std::string_view shortcut,
                         std::function<void()> onClick, LayoutOverride&& constraints = {});
void MenuSeparator();
void MenuHeader(std::string_view text);
void MenuFilter();
```

- `DestructiveMenuItem`: an item that destroys something (Delete, Discard), in the error
  color (`menu-item-label-destructive`, `menu-item-icon-destructive`).
- `MenuSeparator`: a divider (`menu-separator`).
- `MenuHeader`: a section name between items, lined up with the labels (`menu-header`).
- `MenuFilter`: a filter field for a big menu, placed first. It takes the keyboard when
  the menu opens; while it holds text, items that don't contain it are left out.

### Menu bar and title bar

`BeginMenuBar` is a row of `BeginMenu` triggers; Left / Right move between triggers and
Down enters the dropdown. `BeginTitleBar` is a menu bar the window is dragged by:
whatever in it isn't a control is the caption, and a double-click maximizes. In a window
with `ElementFlags::WindowCustomTitleBar`, `EndTitleBar` adds the minimize, maximize and
close buttons.

```cpp
bool BeginMenuBar(ID id, StyleRef style, LayoutOverride&& constraints = {});
void EndMenuBar();
bool BeginTitleBar(ID id, StyleRef style, LayoutOverride&& constraints = {});
void EndTitleBar();
WindowHitArea TitleBarHitTest(ID windowID, int2 screenPos) const;
```

Classes: `menu-bar`, `title-bar`, `title-bar-button`, `title-bar-close-button`.

```cpp
BeginMenuBar();
if (BeginMenu("File"))
{
    MenuItem("Open", "Ctrl+O", [this] { Open(); });
    MenuSeparator();
    MenuItem("Quit", [this] { applicationQuit = true; });
}
EndMenu();
EndMenuBar();
```

### Context menus

A menu that opens on a right-click of the target element, below it. Without a target it
attaches to the element registered last.

```cpp
bool BeginContextMenu(ID targetID);
bool BeginContextMenu();
void EndContextMenu();
void OpenContextMenu(ID targetID, int2 screenPos);
```

`OpenContextMenu` opens a target's menu at a screen point, for something that isn't an
element; `BeginContextMenu(targetID)` then builds it. Class: `menu`.

```cpp
Button("Click me", [this] { ++clicks; });
if (BeginContextMenu())
    MenuItem("Reset counter", [this] { clicks = 0; });
EndContextMenu();
```

## Tooltips

```cpp
void SetTooltip(ID targetID, std::string_view text);
void SetTooltip(ID targetID, IconID icon, std::string_view text);
void SetTooltip(std::string_view text);           // the element registered last
bool BeginTooltip(ID targetID);
bool BeginTooltip();                              // the element registered last
void EndTooltip();
```

`SetTooltip` shows a text (and icon) on hover. `BeginTooltip` builds any content; it
returns `true` while the tooltip shows, and `EndTooltip` is called either way. A
`GUIContent` with a tooltip sets it too. Classes: `tooltip`, `tooltip-text`.

```cpp
Button(gridIcon, [this] { showGrid = !showGrid; });
SetTooltip(showGrid ? "Grid on" : "Grid off");
```

## Message dialogs

### MessageDialog

A modal question with verb buttons, centered. Call it every frame while
asking; it returns the index of the chosen button once, in the frame after, else -1.
Esc and the close button choose `cancelButton`; Alt and a button's first letter choose
that button.

```cpp
int MessageDialog(ID id, const MessageDialogDesc& desc);
```

`MessageDialogDesc` fields: `icon` (`MessageIcon::Warning`, `Quit`, `Question`, `Info`,
`Error`), `title` (a question), `message`, `buttons` (a span of `MessageButton`:
`label`, `kind` = `Secondary`, `Primary` or `Danger`, `alignLeft`), `defaultButton`,
`cancelButton` (-1: the default one), `items` (affected items with an optional color dot,
note and checkbox), `dontAskAgain` (a `bool*`; shows the checkbox when set) and
`details` (collapsible technical text).

Classes: `message-box` and the `message-box-*` classes; the buttons use
`dialog-button`, `dialog-button-primary`, `dialog-button-danger`.

```cpp
static constexpr MessageButton kButtons[] = {
    { .label = "Cancel" },
    { .label = "Quit", .kind = MessageButtonKind::Danger },
};
const int choice = MessageDialog(quitDialogID, MessageDialogDesc{
    .icon = MessageIcon::Quit, .title = "Quit?", .message = "Unsaved edits will be lost.",
    .buttons = kButtons, .defaultButton = 0, .cancelButton = 0 });
if (choice == 1) applicationQuit = true;
if (choice >= 0) asking = false;
```

The color picker and gradient editor are the library's other dialogs; they open from
`ColorField` and `GradientField`.

## Lists and reorderable groups

### BeginListView / ListItem

A list of selectable items. `selectedIndex` is the selected item's index (Up / Down move
it when the list has focus); `itemCount` bounds the keyboard movement. With an
`ItemDragDropConfig` whose `payloadType` is not 0, items get a drag handle and become
drag sources and drop targets.

```cpp
bool BeginListView(ID id, int* selectedIndex, int itemCount, ItemDragDropConfig* dragDrop, StyleRef style,
                   LayoutOverride&& constraints = {});
void EndListView();
bool ListItem(ID id, const GUIContent& content, StyleRef style, LayoutOverride&& constraints = {});
int  FindListItemIndex(ID id) const;
```

`ListItem` returns `true` in the frame after a click. `FindListItemIndex` maps an item ID
to its index in end-of-frame handlers (drag and drop). Classes: `list-view`,
`list-item`; with drag and drop `list-item-row`, `list-item-body`,
`list-item-drag-knob-vert`.

```cpp
BeginListView(&selected, int(names.size()));
for (const std::string& name : names)
    ListItem(name);
EndListView();
```

### BeginReorderableGroup / BeginReorderableItem

A container whose items can be dragged into a new order. `onReorder(fromIndex, toIndex)`
fires on drop; the app moves its data. `isWrapped` lets the items wrap onto more lines.
`onDragOut(index, screenPos)` fires when an item is released outside the group, and
`dragVisualizer(index, screenPos)` runs each frame while one is dragged outside it
(typically to draw a ghost window).

```cpp
bool BeginReorderableGroup(ID id, ReorderDirection direction, bool isWrapped,
                           std::function<void(int fromIndex, int toIndex)> onReorder, StyleRef style,
                           LayoutOverride&& constraints = {},
                           std::function<void(int, int2)> onDragOut = nullptr,
                           std::function<void(int, int2)> dragVisualizer = nullptr);
bool BeginReorderableGroup(ID id, ReorderDirection direction, std::function<void(int fromIndex, int toIndex)> onReorder,
                           LayoutOverride&& constraints = {});
void EndReorderableGroup();
bool BeginReorderableItem(ID id, StyleRef style, LayoutOverride&& constraints = {});
void EndReorderableItem();
void EmitDragHandle();
```

`EmitDragHandle` adds a grip inside an item; clicks on it reach the item. Classes:
`horzReorderableGroup`, `vertReorderableGroup` (`horzReorderableGroupWrap`,
`vertReorderableGroupWrap` when wrapped), `reorderableItem`, `reorder-drop-slot`,
`reorder-lifted`.

```cpp
BeginReorderableGroup(groupID, ReorderDirection::Vertical, [this](int from, int to)
{
    std::string moved = std::move(items[from]);
    items.erase(items.begin() + from);
    items.insert(items.begin() + to, std::move(moved));
});
for (const std::string& item : items)
{
    BeginReorderableItem();
    EmitDragHandle();
    Text(item);
    EndReorderableItem();
}
EndReorderableGroup();
```

## Tables

Tables keep their column widths, selection and sort state in a `TableState` that the app
owns. Columns are `VirtualTableColumn`s, described each frame; reorder the array to
reorder the columns.

`VirtualTableColumn` fields: `column` (a `TableColumn`), `generateCell` (for
`VirtualTable`), `sortAscending(rowA, rowB)` (required for sorting a `VirtualTable`),
`sectionId` and `sectionSizing` (columns with the same section scroll together; see
"frozen columns" below).

`TableColumn` fields: `header`, `initialWidth`, `minWidth`, `maxWidth` (CSS pixels),
`sortable`, `locked` (can't be dragged or passed), `sizing` (`ColumnSizing::Resizable`,
`Fixed` or `Stretch`), `interactiveContent` (keep cell widgets clickable in row mode),
`columnBackground` and `cellStyle`.

`TableState` options: `selectionMode` (`TableSelectionMode::Row` or `Cell`),
`selectedRowID`, `selectedColumn`, `multiSelect`, `rowDragDrop`, `sortColumn`,
`sortDescending`, `followTail` (stick to the bottom as rows are added), `selectionBar`,
`rowStyle`, `cellStyle`, `showHeaders`, `columnSeparators`, `onReorderColumns` (headers
become drag-to-reorder when set) and `onRowDoubleClick`.

Classes: `table` (`table-fill` fills its parent), `table-header`, `table-row`,
`table-row-alt`, `table-cell`, `table-cell-number`, `table-sizer`, `table-separator`.

### BeginTable / BeginRow / Cell

Rows and cells emitted directly. Cells advance to the next column; `EndTable` lays them
out.

```cpp
bool BeginTable(ID id, VirtualTableColumn* columns, size_t columnCount, TableState& state, StyleRef style,
                LayoutOverride&& constraints = {});
template <size_t N>
bool BeginTable(ID id, VirtualTableColumn (&columns)[N], TableState& state, LayoutOverride&& constraints = {});
void EndTable();
bool BeginRow();
void EndRow();
void Cell(std::string_view text);
void Cell(std::function<void()> emit);
```

`Cell(emit)` builds custom content in the cell; it runs during `EndTable`, so its
captures must live until then.

```cpp
VirtualTableColumn columns[] = {
    { .column = { .header = "Name", .initialWidth = 160 } },
    { .column = { .header = "Size" } },
};
if (BeginTable(tableID, columns, tableState, LayoutOverride{}.Size(400, 200)))
{
    for (const File& file : files)
    {
        BeginRow();
        Cell(file.name);
        Cell(file.sizeText);
        EndRow();
    }
    EndTable();
}
```

### VirtualTable

A table that builds only the visible rows, so it handles very many. Each column's
`generateCell(rowIndex)` emits that column's cell, usually with `Cell` or `TreeCell`.
With `sortAscending` set on the columns, clicking a header sorts; the table keeps the
order as a permutation and the data is not moved.

```cpp
void VirtualTable(ID id, TableState& state, std::span<VirtualTableColumn> columns, int totalRowCount,
                  CssPx rowHeight = 0, StyleRef style = {}, LayoutOverride&& constraints = {});
```

`rowHeight` is a floor; 0 uses the cell style's height. Give it a bounded height:
sized to all of its rows it would be past the layout range.

```cpp
std::vector<VirtualTableColumn> columns = {
    { .column = { .header = "Name", .initialWidth = 160 },
      .generateCell  = [this](int row) { Cell(rows[row].name); },
      .sortAscending = [this](int a, int b) { return rows[a].name < rows[b].name; } },
};
VirtualTable(tableID, tableState, columns, int(rows.size()), 0, theme.tableFillStyleRef,
             LayoutOverride::Auto().FlexGrow(1).MinHeight(0));
```

Frozen columns: consecutive columns with the same `sectionId` form a section, side by
side with the others. The sections scroll vertically together and horizontally on their
own; column reorder stays within a section. A section with `SectionSizing::FitContent`
is exactly as wide as its columns and has no horizontal scroll bar.

### TreeCell

A tree-style cell: indent, expand arrow, optional icon and text. Arrow clicks toggle
`*isOpen` without selecting; other clicks select the row.

```cpp
void TreeCell(std::string_view text, int depth, bool* isOpen, IconID icon = IconID::Invalid,
              uint32_t ancestorLineMask = 0, bool isLastSibling = true,
              std::string* renameBuffer = nullptr, bool reserveExpander = true);
```

`isOpen` is null for leaves. `renameBuffer` enables in-place rename (F2,
Ctrl+Shift+Click or a slow second click). `ancestorLineMask` and `isLastSibling` draw the
guide lines; `RebuildTreeViewIndex` computes them. Classes: `tree-table-cell`,
`tree-table-arrow`, `tree-table-icon`, `tree-table-text`, `tree-table-text-edit`.

## Tree views

### VirtualTreeView

A single-column, virtualized tree over flat rows that the app owns: a `VirtualTable`
with one `TreeCell` column. A non-empty `header` shows a header row.

```cpp
void VirtualTreeView(ID id, std::vector<TreeViewRow>& rows, std::vector<int>& visibleIdx, TableState& state,
                     StyleRef style = {}, LayoutOverride&& constraints = {}, std::string_view header = {});
static void RebuildTreeViewIndex(std::vector<TreeViewRow>& rows);
static void WalkTreeViewVisibility(const std::vector<TreeViewRow>& rows, std::vector<int>& visibleIdx);
ID TreeDropTargetRow() const;
```

`TreeViewRow` fields: `id`, `name`, `depth`, `isOpen`, `icon`, `renameBuffer`; the rest
is derived by `RebuildTreeViewIndex`, which you call after adding, removing or moving
rows. Rows are in depth-first order. `visibleIdx` is filled each frame with the flat
indices of the visible rows. Selection, scrolling and drag and drop live on the
`TableState`: the selected row's flat index is
`visibleIdx[state.selectedRowID.value.subRangeParts.index]`. Default class: `tree-view`;
rows use `list-row`.

```cpp
RebuildTreeViewIndex(rows);   // after structural changes
VirtualTreeView(treeID, rows, visibleIdx, treeState, {}, LayoutOverride{}.FlexGrow(1));
```

For a tree with more columns, use `VirtualTable` with `TreeCell` in the first column.

## Grid views

### BeginGridView / GridViewItem

A 2D grid of items, like a file explorer. `selectedIndex` takes a single selection;
`MultiSelectState` takes several (Ctrl and Shift clicks); `ItemDragDropConfig` makes
items draggable.

```cpp
bool BeginGridView(ID id, int* selectedIndex, StyleRef style, MultiSelectState* multiSelect = nullptr,
                   ItemDragDropConfig* dragDrop = nullptr, LayoutOverride&& constraints = {});
bool BeginGridView(ID id, int* selectedIndex = nullptr, MultiSelectState* multiSelect = nullptr,
                   ItemDragDropConfig* dragDrop = nullptr, LayoutOverride&& constraints = {});
void EndGridView();
bool GridViewItem(ID id, const GUIContent& content, ElementState extraState = ElementState::None,
                  std::string* renameBuffer = nullptr, StyleRef itemStyle = {}, LayoutOverride&& constraints = {});
bool IsRenaming() const;
```

`renameBuffer` enables in-place rename (F2, Ctrl+Shift+Click or a slow second click;
Enter commits, Escape or focus loss cancels). `IsRenaming` says whether a rename editor
is open. `GridViewItem` returns `true` in the frame after a click. Classes:
`grid-view`, `grid-view-item`, `grid-view-list-item`, `grid-view-rename-*`.

### VirtualGridView

A grid that builds only the visible rows. `generateItem(index, itemID)` must emit
exactly one cell, usually `GridViewItem(itemID, ...)`; `itemID` is a stable ranged
sub-ID used for selection and drag payloads. Cells are `cellSize` pixels.

```cpp
void VirtualGridView(ID id, GridViewVirtualState& vstate, int itemCount, int2 cellSize,
                     const std::function<void(int index, ID itemID)>& generateItem,
                     int* selectedIndex = nullptr, MultiSelectState* multiSelect = nullptr,
                     ItemDragDropConfig* dragDrop = nullptr, StyleRef scrollStyle = {}, StyleRef rowStyle = {},
                     LayoutOverride&& constraints = {});
```

The column count comes from last frame's viewport. `vstate.cellStretchedW` is the cell
width that makes the columns exactly fill it. Classes: `grid-view-scroll`,
`grid-view-row`.

```cpp
VirtualGridView(gridID, gridState, int(files.size()), int2{ styleManager.Px(96), styleManager.Px(96) },
                [this](int i, ID itemID) { GridViewItem(itemID, { files[i].icon, files[i].name }); },
                &selectedFile);
```

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

## Other controls

### Breadcrumb

A path such as Graphs > Terrain material; the last segment is the current one. Clicking
another segment calls `onSelect(index)` and returns `true` that frame.

```cpp
bool Breadcrumb(ID id, std::span<const std::string> segments, std::function<void(int)> onSelect, StyleRef style,
                LayoutOverride&& constraints = {});
```

Classes: `breadcrumb`, `breadcrumb-segment`, `breadcrumb-current`,
`breadcrumb-separator`.

### Status bar

A strip of short status texts, usually along a window's bottom edge. Give one
`StatusText` `FlexGrow(1)` to push the texts after it to the right.

```cpp
bool BeginStatusBar(ID id, StyleRef style, LayoutOverride&& constraints = {});
void EndStatusBar();
void StatusText(ID id, std::string_view text, LayoutOverride&& constraints = {});
```

Classes: `status-bar`, `status-text`.

```cpp
BeginStatusBar();
StatusText("Ready", LayoutOverride::Auto().FlexGrow(1));
StatusText(frameTimeText);
EndStatusBar();
```

### CommandSearch

A search field that finds and runs `commandManager`'s commands. Focused, it lists the
commands whose description contains the query; Up / Down move, Enter or a click runs
one, Escape leaves. `state` must outlive the frame. Returns `true` when a command ran
since the last call. `FocusCommandSearch(state)` puts the keyboard in it next frame
(bind it to a shortcut such as Ctrl+K).

```cpp
bool CommandSearch(ID id, CommandSearchState& state, LayoutOverride&& constraints = {});
static void FocusCommandSearch(CommandSearchState& state);
```

Classes: `command-search`, `command-search-hint`, `command-search-key`,
`command-search-list`, `command-search-current-row`.

### Settings panel

A two-pane settings panel: a page tree, a sizer and the selected page's content. It
renders into the current container. Add pages with stable IDs; each page's `render`
builds its content.

```cpp
void RenderSettingsPanel(SettingsPanelState& state);
ID   SettingsPanelState::AddPage(ID id, std::string_view name, std::function<void()> render);
```

Classes: `settings-page`, `settings-page-selection`.

## Node view

### RenderNodeView

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

## Custom drawing

Any element can draw with its own callback at render time. Register an element with
`RegisterElement`, then attach the callback with `elementStorage.AddCustomDraw`:

```cpp
ElementRef RegisterElement(Element&& element, LayoutOverride&& constraints);
ElementRef RegisterElement(Element&& element, std::string_view text, LayoutOverride&& constraints);

// ElementStorage:
ElementStorageResult AddCustomDraw(const ElementRef& ref, CustomDrawCallback callback);
ElementStorageResult AddCustomDrawAfter(const ElementRef& ref, CustomDrawCallback callback);
ElementStorageResult AddPostLayout(const ElementRef& ref, PostLayoutCallback callback);
```

`AddCustomDraw` runs before the element's children are drawn; `AddCustomDrawAfter`
runs after its whole subtree (call it after the element's children are closed).
`AddPostLayout` runs after layout and before rendering, with the final geometry and no
drawing, for state that depends on it.

The callback gets a `CustomDrawContext`. By default it draws in the element's local
space: (0, 0) is the top-left of its padding box, and pan / zoom of its ancestors applies.
`SetSpace(DrawSpace::Screen)` switches to absolute layout pixels, and
`ScreenToCurrent(screenPos)` converts a cursor position into the current space.

| Method | Draws or returns |
| --- | --- |
| `ElementRect()`, `ElementInnerRect()` | The element's border box and padding box, in the current space |
| `GetElementRect(ref)`, `GetInnerRect(ref)` | Another element's rects |
| `RenderBox(rect, boxStyle, state, ...)` | A styled box |
| `RenderQuad(rect, colorStyle, cornerRadius, ...)` | A filled rectangle |
| `RenderLine(p0, p1, lineStyle)` | A line (also a four-point form) |
| `RenderPolyline(points, closed, lineStyle, cornerRadius)` | A polyline |
| `RenderCircle`, `RenderDisc`, `RenderDot` | Circle outline, filled disc, dot |
| `RenderTriangle`, `RenderFilledPolygon` | Filled triangle, convex polygon |
| `RenderText(textId, rect)` | Text registered with `textSystem->RegisterText` |

Colors are style indices (for example `theme.lineWhiteStyle`, or one registered with
`styleManager.RegisterColorStyle`); `LineStyle` has `colorStyleIndex`, `lineWidth` and
dash settings.

```cpp
const ElementRef ref = RegisterElement(Element{ .id = canvasID, .styleRef = theme.GetStyle("my-canvas") },
                                       LayoutOverride::FixedSize(200, 100));
VERIFY(elementStorage.AddCustomDraw(ref, [this](CustomDrawContext& ctx)
{
    const IntRect r = ctx.ElementInnerRect();
    const LineStyle line{ .colorStyleIndex = theme.lineWhiteStyle, .lineWidth = styleManager.Px(2.0f) };
    ctx.RenderLine(r.position, r.position + r.size, line);
}));
```

The callback runs at render time, after `OnGUI` returns, so its captures must outlive the
frame.

## Default CSS classes

The class each control uses when no style is passed. Parts of a control (a switch's
knob, a table's header) have their own classes, listed with the control above.

| Control | Class |
| --- | --- |
| `BeginWindow` | `window` |
| `BeginGhostWindow`, `AddGhostIcon` | `ghost-window`, `ghost-icon` |
| `BeginHorizontal`, `BeginVertical` and Wrap variants | none (plain flex box built in code) |
| `BeginArea`, `BeginCached` | `collection` |
| `BeginButtonGroup`, `BeginToggleGroup` | `button-group`, `toggle-group` |
| `Text` | `static-text` |
| `Label`, `DragLabel` | `label`, `drag-label` (`group-label`, `group-drag-label` in a resizable label group) |
| `Image` | none (built in code) |
| `Button`, `CommandButton` | `button` |
| `Toggle`, `Field(bool&)` | `toggle` |
| `Switch` | `switch` |
| `Checkbox` | `checkbox` |
| `Field(std::string&)` | `textbox` |
| `SearchField` | `search-textbox` |
| `Field` (numbers) | `number-field` |
| `Field` (vectors) | `vector-component`, `axis-label-x` / `-y` / `-z` / `-w` |
| `TypedField` | `typed-field` |
| `Field` (lists, dictionaries) | `collection-area` |
| `SliderField` | `slider-field-track` |
| `Slider` | `slider` |
| `ColorField` | `color-field` (picker: `color-picker`) |
| `GradientField` | `gradient-swatch` (editor: `gradient-editor`) |
| `BeginMenu` | `menu-trigger`, popup `menu` |
| `MenuItem` | `menu-item-row` |
| `MenuSeparator`, `MenuHeader` | `menu-separator`, `menu-header` |
| `BeginMenuBar`, `BeginTitleBar` | `menu-bar`, `title-bar` |
| `BeginContextMenu` | `menu` |
| `SetTooltip`, `BeginTooltip` | `tooltip` (text: `tooltip-text`) |
| `MessageDialog` | `message-box` |
| `BeginListView`, `ListItem` | `list-view`, `list-item` |
| `BeginReorderableGroup` | `horzReorderableGroup`, `vertReorderableGroup` (`...Wrap` when wrapped) |
| `BeginReorderableItem` | `reorderableItem` |
| `BeginTable`, `VirtualTable` | `table` (cells: `table-cell`) |
| `VirtualTreeView` | `tree-view` |
| `BeginGridView`, `GridViewItem` | `grid-view`, `grid-view-item` |
| `VirtualGridView` | `grid-view-scroll`, `grid-view-row` |
| `BeginTabBar`, `Tab` | `tab-bar`, `tab` |
| `BeginDockingContainer` | `dock-container` |
| `BeginToolbar` | controls inside use `toolbar-*` variants |
| `Breadcrumb` | `breadcrumb` |
| `BeginStatusBar`, `StatusText` | `status-bar`, `status-text` |
| `CommandSearch` | `command-search` |
| `RenderSettingsPanel` | `settings-page` |
| `RenderNodeView` | `node-view` |

The classes are defined in `workdirectory/themes/styles.css`, with colors from the
palette in `dark.css` or `light.css`. See [css.md](css.md) for how to change them or add
your own.
