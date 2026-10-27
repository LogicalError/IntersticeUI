# Return values, content and units

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

See [Controls](../controls.md) for each control's overloads.


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

---

[← Begin and End](begin-end.md) · [Overview](../overview.md) · [Errors and saved state →](errors-and-state.md)
