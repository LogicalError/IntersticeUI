# Using styles from C++

## Classes from the theme

`theme.GetStyle("class")` returns the `StyleRef` registered for a class, from any loaded
file. Pass it to a control:

```cpp
Text("My themed app", theme.GetStyle("banner"));
Button("Careful!", [] {}, theme.GetStyle("warning-button"));
```

A class no file defines gives an empty `StyleRef` and logs a warning once;
`theme.FindStyle("class")` returns a `Result<StyleRef>` with an error instead.
`theme.GetColor("--accent", fallback)` reads a palette color as `color4f`.

The library's own controls use styles the theme registers by name (for example
`theme.labelStyleRef`, `theme.windowStyleRef`, `theme.regularControls.button`). Changing the
matching class (`.label`, `.window`, `.button`) in a style file restyles them.

A reload clears all registered styles, so `StyleRef`s from before it are stale. Look classes
up each frame, or store them again in `OnStylesReloaded`.

## Styles built in code

A style can also be filled in code and registered with the `StyleManager`:

```cpp
tachyon::Style s;
s.padding      = tachyon::BoxEdges(4, 8);
s.borderWidths = tachyon::BoxEdges(1);
s.borderRadius = tachyon::BoxCorners(4);
s.states[tachyon::Style::kNormal].backgroundColor = tachyon::ColorStyle::Create(tachyon::color4b{ 30, 33, 40, 255 });
s.states[tachyon::Style::kHover].backgroundColor  = tachyon::ColorStyle::Create(tachyon::color4b{ 35, 39, 47, 255 });
tachyon::StyleRef ref = styleManager.Register(s);
```

Each of the 33 state slots in `Style::states` is set separately: the bits are `kActive` (1),
`kHover` (2), `kFocus` (4), `kSelected` (8) and `kSelection` (16), combined, plus `kDisabled`
(32) on its own. Register such styles again in `OnStylesReloaded`. A code style doesn't get
the font scale.

## UI scale

Styles are written in CSS pixels and stored in device pixels. `StyleManager::Register` scales
a style by the UI scale, which is the main window's display scale times `SetUIScale`:

- Font sizes, letter and word spacing and pixel line heights scale exactly.
- Everything else (margins, padding, sizes, gaps, radii, shadows, icon sizes, fixed grid
  tracks) is rounded to whole pixels. A border or text decoration that is set stays at least
  1 pixel. `auto` sizes stay `auto`.

`Register(style, tachyon::StyleUnits::Pixels)` registers a style that is already in device
pixels, and `styleManager.ToPixels(style)` gives the scaled copy. For sizes in code,
`styleManager.Px(n)` converts CSS pixels to whole device pixels (a non-zero size stays at
least 1).

---

[← Properties](properties.md) · [CSS and theming](../css.md)
