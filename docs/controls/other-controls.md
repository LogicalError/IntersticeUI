# Other controls

## Breadcrumb

A path such as Graphs > Terrain material; the last segment is the current one. Clicking
another segment calls `onSelect(index)` and returns `true` that frame.

```cpp
bool Breadcrumb(ID id, std::span<const std::string> segments, std::function<void(int)> onSelect, StyleRef style,
                LayoutOverride&& constraints = {});
```

Classes: `breadcrumb`, `breadcrumb-segment`, `breadcrumb-current`,
`breadcrumb-separator`.

## Status bar

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

## CommandSearch

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

## Settings panel

A two-pane settings panel: a page tree, a sizer and the selected page's content. It
renders into the current container. Add pages with stable IDs; each page's `render`
builds its content.

```cpp
void RenderSettingsPanel(SettingsPanelState& state);
ID   SettingsPanelState::AddPage(ID id, std::string_view name, std::function<void()> render);
```

Classes: `settings-page`, `settings-page-selection`.

---

[← Node view](node-view.md) · [Controls](../controls.md) · [Custom drawing →](custom-drawing.md)
