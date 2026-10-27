# Buttons and toggles

## Button

```cpp
bool Button(ID id, const GUIContent& content, std::function<void()> onClick, StyleRef style,
            LayoutOverride&& constraints = {});
```

Returns `true` in the frame after a click; `onClick` runs when the click is handled.
Default class: `button` (`toolbar-button` inside a toolbar).

```cpp
Button("Click me", [this] { ++clicks; });
```

## CommandButton

An icon button for a command in `commandManager`. Its icon and callback come from the
command; the tooltip shows the command's description and shortcut, e.g. "Undo (Ctrl+Z)".

```cpp
bool CommandButton(CommandID command, StyleRef style, LayoutOverride&& constraints = {});
```

Default class: `button`.

## Toggle

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

## Switch

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

## Checkbox

A box with a tick, and its label. There is only the form with an ID and a callback.

```cpp
bool Checkbox(ID id, bool value, std::string_view text, std::function<void(bool)> onToggle,
              LayoutOverride&& constraints = {});
```

Classes: `checkbox`, `checkbox-box`, `checkbox-label`.

```cpp
Checkbox(gridCheckID, showGrid, "Show grid", [this](bool on) { showGrid = on; });
```

---

[← Text and images](text-and-images.md) · [Controls](../controls.md) · [Fields →](fields.md)
