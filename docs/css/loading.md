# How a theme loads

A theme is a palette file followed by one or more style files:

| File | Contents |
| --- | --- |
| `themes/dark.css`, `themes/light.css` | The palette: only `:root` custom properties (colors, shadows) |
| `themes/styles.css` | The library's rules: shared dimensions in `:root`, `@font-face`, one class per control part |
| `themes/demo.css` | The demo app's own classes (only the demo loads it) |

The files are joined into one text in that order and parsed once. `var()` is resolved at
parse time against the variables defined so far, so a palette must come before the files
that use it. The same holds inside a file: a rule only sees variables defined above it, and
redefining a variable later does not change rules that already used it.

The application picks the files:

| Call (`GUIApplication`) | What it does |
| --- | --- |
| `SetResourceRoot(dir)` | Folder that relative theme paths (and shaders, fonts) load from. Empty = the working directory. Call before `Initialize`. |
| `SetStyleFiles(files)` | The style files loaded after the palette, in order. Default: `{ "styles.css" }`. Relative paths are next to the palette file. Call before `Initialize`, or follow with `RequestReloadTheme`. |
| `RequestReloadTheme(path, fontScale)` | Load another palette and/or font scale at the start of the next frame. Before `Initialize`, it picks the starting palette. A no-op when nothing changed. |
| `GetThemePath()`, `GetGlobalFontScale()` | The current palette path (default `themes/dark.css`) and font scale. |
| `ThemeReloadResult()` | How the last reload went. On an error (a file that can't be opened) the previous theme stays. |
| `OnStylesReloaded()` | Virtual, called after each reload. Re-fetch or re-register your `StyleRef`s here. |

An application that adds its own classes keeps the library's file first:

```cpp
const std::string_view styleFiles[] = { "styles.css", "my_app.css" };
SetStyleFiles(styleFiles);
```

Switching palettes or text size at runtime (from `examples/custom_theme/custom_theme.cpp`):

```cpp
Button("Light", [this] { RequestReloadTheme("themes/light.css", GetGlobalFontScale()); });
Button("Larger text", [this] { RequestReloadTheme(GetThemePath(), GetGlobalFontScale() * 1.1f); });
```

The font scale multiplies every CSS `font-size`. The theme also reloads when the UI scale
changes (`SetUIScale`) or the main window moves to a monitor with another display scale.

Parse errors don't stop loading. Each one is logged as `file:line:column: message`, the bad
declaration or rule is skipped and parsing goes on.

---

[CSS and theming](../css.md) · [Syntax →](syntax.md)
