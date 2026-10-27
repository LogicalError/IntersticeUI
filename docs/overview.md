# TachyonUI overview

TachyonUI is an immediate-mode GUI library: the application describes its whole UI
in code every frame, and the library keeps the state (focus, scroll positions, open
menus, window rects), lays the UI out with flexbox and grid, handles input and draws it.

This guide explains the ideas the rest of the API builds on. The other guides:

- [controls.md](controls.md): the controls and containers
- [css.md](css.md): styles and themes
- [integration.md](integration.md): hosting the library in an existing application

Everything is in the namespace `tachyon`. The examples below are written inside a
class derived from `GUIApplication`, as in the [README](https://github.com/LogicalError/TachyonUI#readme).

## The application

An application derives from `tachyon::GUIApplication` and implements `OnGUI`:

```cpp
#include <gui_controls/gui_application.h>

class MyApp : public tachyon::GUIApplication
{
public:
    tachyon::Result<void> Initialize(const InitializeOptions& options = {}) override
    {
        if (tachyon::Result<void> result = GUIApplication::Initialize(options); !result)
            return result;
        windowID = idManager.CreateStaticID();
        return {};
    }

    void OnGUI() override
    {
        if (BeginWindow(windowID, "My app", tachyon::LayoutOverride{}.Size(360, 160)))
        {
            Text("Hello");
            Button("Quit", [this] { applicationQuit = true; });
        }
        EndWindow();
    }

private:
    tachyon::ID windowID = tachyon::ID::Invalid();
};

int main()
{
    MyApp app;
    app.SetResourceRoot(TACHYON_RESOURCE_DIR);   // shaders, fonts and themes
    tachyon::Result<void> result = app.Initialize();
    if (result)
        result = app.Run();   // until the last window closes
    app.Cleanup();
    return result ? 0 : 1;
}
```

`SetResourceRoot`, `SetGraphicsBackend`, `SetWindowBackend`, `SetClipboard` and
`SetStyleFiles` are called before `Initialize`. `Cleanup` releases everything
`Initialize` made.

## The frame

Each frame runs these steps, in this order:

1. **Build.** The library calls `OnGUI`. Each control call adds an element to this
   frame's tree; nothing is drawn yet. A control reads its state (hovered, focused,
   pressed, checked) from what the library kept since the last frame.
2. **Layout.** The tree is laid out with the styles from the CSS files and the
   `LayoutOverride` passed to each call. OS windows are created, moved and resized to
   match their window elements.
3. **Input.** The input that arrived since the last frame is hit-tested against the new
   layout and sent to the elements. Event handlers and control callbacks run here,
   after `OnGUI` has returned.
4. **Render.** The tree is drawn into each window.

So a click is seen in two places: the button's callback runs in the input step of the
frame that received the click, and the UI built in the next frame sees the result.

Because callbacks run after `OnGUI` returns, what they capture must outlive the frame.
Members of the application, user-owned objects and copies are fine; references to
locals of `OnGUI` are not.

```cpp
// Fine: captures `this` and a copy of the index.
Button("Select", [this, i] { selected = i; });

// Wrong: `count` is a local of OnGUI and is gone when the callback runs.
int count = 0;
Button("Add", [&count] { ++count; });
```

### Run and the split frame calls

`Run()` loops until `applicationQuit` is set or the last window closes:

```cpp
while (app.UpdateFrame())
{
    if (!app.RecordFrame()) break;   // build, layout, input, render; submitted to the GPU
    // ... the host's own work ...
    app.PresentFrame();              // may wait for vsync
}
```

`UpdateFrame` pumps OS events and input and returns false once the app quits.
`RecordFrame` runs the frame described above and submits every window; it returns an
error only when a lost GPU device can't be made again (with a host's device).
`PresentFrame` presents the OS windows. A host with its own main loop calls these
three itself; see [integration.md](integration.md).

## IDs

Every element has an `tachyon::ID`. The library keys its retained state by it:
which element is hovered or focused, the text being edited, scroll offsets, window
rects. An element that gets a different ID next frame loses that state.

There are three kinds, all made by `idManager` (a member of `GUIApplication`).

### Call-site IDs (dynamic)

Most controls have overloads without an ID. Those take the ID from their call site:
the file and line of the call, through a defaulted `std::source_location` parameter.

```cpp
Label("Name");
Field(name);                  // ID from this line
Button("OK", onOK);           // ID from this line
```

These IDs are stable as long as the code doesn't change shape:

- A control that appears or disappears doesn't change the IDs of the controls
  after it. Each call site has its own counter.
- The same call site repeated in a frame (a loop) is numbered: the first call gets
  one ID, the second another, and so on.

To make a call-site ID yourself, for a control that only takes an explicit ID, call
`idManager.AllocateDynamicID()`. It has three forms:

```cpp
tachyon::ID a = idManager.AllocateDynamicID();             // this call site
tachyon::ID b = idManager.AllocateDynamicID("tabs");       // a string hint
tachyon::ID c = idManager.AllocateDynamicID(__FILE__, __LINE__);
```

```cpp
BeginTabBar(idManager.AllocateDynamicID("tabs"), &selectedTab, nullptr);
Tab("First");
Tab("Second");
EndTabBar();
```

Dynamic IDs are made fresh every frame, so they can only be made inside the frame
(in `OnGUI` or code it calls). Outside it the call is reported and returns
`ID::Invalid()`.

### Static IDs

A static ID is made once and stays the same until it is released. Make it outside the
frame, typically in `Initialize`, and store it:

```cpp
windowID = idManager.CreateStaticID();
// ...
idManager.ReleaseStaticID(windowID);   // when the thing it names is gone for good
```

`IsStaticIDValid(id)` tells whether the ID is still the current one in its slot; a
released ID stays invalid even if its slot is reused.

### Ranged IDs

A ranged ID stands for a block of IDs, one per item of a list. `GetSubID` gives the
ID of item `index`:

```cpp
const uint32_t count = uint32_t(std::max<size_t>(1, files.size()));   // the size must be at least 1
const tachyon::ID rows = idManager.AllocateRangedID(count, "files");
for (size_t i = 0; i < files.size(); ++i)
    Button(idManager.GetSubID(rows, uint32_t(i)), files[i], [this, i] { Open(i); });
```

Like dynamic IDs, ranged IDs are made inside the frame, from the call site or a
string hint. A sub-ID depends on the range and the index, not on the size, so items
keep their IDs while the list grows or shrinks at the end.

### Parent scopes

Dynamic and ranged IDs are combined with the ID on top of the parent stack. Push a
parent to give a group of calls its own scope:

```cpp
if (idManager.PushParent(panelID))
{
    Field(value);   // its ID is scoped under panelID
    (void)idManager.PopParent();
}
```

`BeginWindow` pushes the window's ID and `EndWindow` pops it, so the IDs inside a
window are scoped by that window. Containers whose content may be skipped or built
for some rows only do the same: `BeginCached`, docked panels, virtual tables and
virtual grid views. Plain layout containers (`BeginVertical`, `BeginHorizontal`,
`BeginArea`) don't push a parent.

`PushParent` returns false, and pushes nothing, for an invalid ID or outside the
frame; pop only when it returned true. A parent left on the stack at the end of the
frame is reported and cleared.

### When an ID has to be stable

Call-site IDs are enough for most controls. Use your own ID when:

- **It is a window.** `BeginWindow` takes an explicit ID; use a static ID. The window's
  OS window, position and size are tied to it.
- **Its state must survive beyond what the call site gives.** For example a control
  that is drawn from different places, or a panel whose content is created and
  destroyed. The panels made by `CreatePanel` get static IDs.
- **It is in a list whose items move.** Call-site and ranged IDs follow the position:
  if items are reordered, inserted or removed in the middle, the state (focus, an edit
  in progress) stays at the position instead of moving with the item. Give each item a
  static ID when it is created and push it as the parent while drawing the item:

```cpp
for (Item& item : items)   // item.id = idManager.CreateStaticID() when the item is made
{
    if (idManager.PushParent(item.id))
    {
        Field(item.name);
        (void)idManager.PopParent();
    }
}
```

- **It is drawn by a helper function called from several places.** The controls
  inside the helper share call sites, so they are numbered in call order; if an earlier
  call is skipped, the later ones are renumbered. Push a parent per call.

A control called with `ID::Invalid()` is reported; it is drawn but takes no input and
keeps no state.

IDs are runtime values; they are not meant to be saved. Saved UI state uses string
keys instead (see below).

## Begin and End

Containers are pairs of calls: `BeginWindow` / `EndWindow`, `BeginMenu` / `EndMenu`,
`BeginVertical` / `EndVertical`, and so on. The rule is the same for all of them:

**Every `Begin*` has its `End*`, called whether or not the `Begin*` returned true.**
The result only says whether to build the content.

```cpp
if (BeginMenu(menuID, "File"))
{
    MenuItem("Open", [this] { OpenFile(); });
    MenuItem("Quit", [this] { applicationQuit = true; });
}
EndMenu();   // also when the menu is closed
```

A closed menu, a tooltip that isn't showing or a window the user closed returns false;
layout containers such as `BeginVertical` always return true.

### Scopes

Each pair also has a scope object that calls the `End*` when it leaves. It takes the
same arguments as the `Begin*` and converts to the `Begin*`'s result:

```cpp
if (auto menu = Menu(menuID, "File"))
    MenuItem("Quit", [this] { applicationQuit = true; });

auto window = Window(windowID, "Controls", tachyon::LayoutOverride{}.Size(480, 560));
if (!window)
    return;   // the scope ends the window, open or not
```

The scopes are named after the pair without `Begin`: `Window`, `Menu`, `MenuBar`,
`ContextMenu`, `Tooltip`, `Horizontal`, `Vertical`, `LabelGroup`, `Table`, `Row`,
`ListView`, `TabBar`, `Disabled` and the others in `gui_application.h`. `End()` ends
the container early; the scope then doesn't end it again.

### Misuse

Misuse is reported, not fatal. The library logs an error and carries on:

- An `End*` without its `Begin*` is reported and ignored.
- A `Begin*` left without its `End*` is reported at the end of the frame and closed,
  and the next frame starts clean.
- Calls in the wrong place, such as a `MenuItem` outside an open menu, are reported
  and ignored.

Each message is logged once, as the same mistake usually repeats every frame.

## Return values and callbacks

Controls report a change in two ways, on purpose:

- The **callback** runs in the input step of the frame that received the input.
- The **return value** is true in the next frame's `OnGUI`.

Use whichever fits the code:

```cpp
// A callback.
Button("Greet", [this] { greeting = "Hello, " + name + "!"; });

// The return value.
if (Button("Reset", nullptr))
    ResetSettings();
```

Controls that edit a value take it by reference and write it themselves; the return
value says it changed:

```cpp
Field(name);                    // std::string
Field(enabled, "Enabled");      // bool, with its label
if (Field(scale))               // float
    ApplyScale(scale);
```

See [controls.md](controls.md) for each control's overloads.

## GUIContent

A control shows a `GUIContent`: a text, an icon or both, and an optional tooltip. A
string or an icon converts to one, so these all work:

```cpp
Button("Save", onSave);
Button(saveIcon, onSave);
Button({ saveIcon, "Save", "Write the file to disk" }, onSave);
Button({ "Save", "Write the file to disk" }, onSave);   // text and tooltip
```

`GUIContent` refers to its strings (`std::string_view`); they only need to live for
the call.

## Units

Sizes passed to the API are CSS px. The library scales them by the UI scale, the same
way it scales the sizes in the stylesheet. Positions, rects, and sizes the library
measured or computed are whole device pixels.

```cpp
tachyon::LayoutOverride{}.Width(200)                            // 200 CSS px: 300 pixels at 150%
tachyon::LayoutOverride{}.Width(tachyon::Pixels{ measured })  // already pixels, used as is
```

A plain `int` converts to `CssPx`; `Pixels` must be written out. `int2` and `IntRect`
are always pixels.

The UI scale is the main window's display scale times the zoom set with
`SetUIScale(scale)` (1 follows the OS). `styleManager.Px(n)` converts CSS px to pixels
for code that needs pixels, such as custom drawing.

## Errors

Calls that can fail return a `Result<T>`: the value, or an `Error` with an
`ErrorCode` and a message. It converts to true when it holds the value. `Result<void>`
is success or an error.

```cpp
if (tachyon::Result<void> result = app.Initialize(); !result)
    std::fprintf(stderr, "%s\n", result.GetError().message.c_str());
```

`Result<T>` has `Value()`, `operator*`, `operator->` and `ValueOr(fallback)`.
The error codes are `Failed`, `InvalidArgument`, `NotFound`, `ParseFailed`,
`Unsupported`, `OutOfMemory` and `DeviceLost`.

`Initialize`, `Run`, `RecordFrame`, `ReadWindowPixels` and `RestoreUIState` return
one; `ThemeReloadResult()` tells how the last `RequestReloadTheme` went.

## Saved UI state

The library can save the state of the UI as JSON text, which the application stores
wherever it likes. Name what to keep once, by keys that stay the same between runs;
the named objects must outlive the naming:

```cpp
PersistWindow("main", mainWindowID);         // position, size, mode
PersistDocking("dock", dockingLayout);       // panels by type, else title
PersistTable("assets", tableState, columns); // column widths and order
PersistValue("zoom", zoom);                  // an app setting (bool, int, float, string)
```

`PersistToolbars` keeps the toolbars. Then:

```cpp
std::string json = app.SaveUIState();                        // e.g. after Run()

if (tachyon::Result<void> restored = RestoreUIState(json); !restored)
    std::fprintf(stderr, "UI state not restored: %s\n", restored.GetError().message.c_str());
```

`RestoreUIState` applies windows when they are next declared and everything else at
once. Keys, panels and columns it doesn't know are skipped. The demo calls the
`Persist*` functions and `RestoreUIState` at the end of `Initialize`, and
`SaveUIState` after `Run()` returns.
