# Menus

## BeginMenu / EndMenu

Outside a menu, `BeginMenu` is a dropdown button that opens a menu. Inside a menu it is a
submenu item. Inside a menu bar it is a bar trigger. Returns whether the menu is open;
call `EndMenu` either way. To pick one value from a list, use
[`Dropdown`](fields.md#dropdown) instead.

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

## MenuItem

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

## Other menu parts

```cpp
bool DestructiveMenuItem(ID id, const GUIContent& content, std::string_view shortcut,
                         std::function<void()> onClick, StyleRef style, LayoutOverride&& constraints = {});
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

## Menu bar and title bar

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

## Context menus

A menu that opens below the target element on a right-click on it, or on Shift+F10 or the
Menu key while it has the focus. Without a target it attaches to the control just drawn
(as `SetTooltip` does); a control made of several parts opens it from any of them. When
several targets match (a control in a table row that has a menu too), the first
`BeginContextMenu` of the frame wins: the control's, declared right after it.

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

---

[← Color and gradient fields](color-and-gradient.md) · [Controls](../controls.md) · [Tooltips and message dialogs →](tooltips-and-dialogs.md)
