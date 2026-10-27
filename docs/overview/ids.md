# IDs

Every element has a `tachyon::ID`. The library keys its retained state by it:
which element is hovered or focused, the text being edited, scroll offsets, window
rects. An element that gets a different ID next frame loses that state.

There are three kinds, all made by `idManager` (a member of `GUIApplication`).

## Call-site IDs (dynamic)

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

## Static IDs

A static ID is made once and stays the same until it is released. Make it outside the
frame, typically in `Initialize`, and store it:

```cpp
windowID = idManager.CreateStaticID();
// ...
idManager.ReleaseStaticID(windowID);   // when the thing it names is gone for good
```

`IsStaticIDValid(id)` tells whether the ID is still the current one in its slot; a
released ID stays invalid even if its slot is reused.

## Ranged IDs

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

## Parent scopes

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

## When an ID has to be stable

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

---

[← The application and the frame](application.md) · [Overview](../overview.md) · [Begin and End →](begin-end.md)
