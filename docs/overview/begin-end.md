# Begin and End

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

## Scopes

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

## Misuse

Misuse is reported, not fatal. The library logs an error and carries on:

- An `End*` without its `Begin*` is reported and ignored.
- A `Begin*` left without its `End*` is reported at the end of the frame and closed,
  and the next frame starts clean.
- Calls in the wrong place, such as a `MenuItem` outside an open menu, are reported
  and ignored.

Each message is logged once, as the same mistake usually repeats every frame.

---

[← IDs](ids.md) · [Overview](../overview.md) · [Return values, content and units →](values.md)
