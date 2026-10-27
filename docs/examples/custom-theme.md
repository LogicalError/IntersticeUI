# Custom theme

An app with its own CSS classes next to the library's, that switches between the dark
and light palettes and changes the font size at runtime.
Full source: [examples/custom_theme/custom_theme.cpp](https://github.com/LogicalError/TachyonUI/blob/main/examples/custom_theme/custom_theme.cpp)
and [examples/custom_theme/custom_theme.css](https://github.com/LogicalError/TachyonUI/blob/main/examples/custom_theme/custom_theme.css).

## Adding a style file

`SetStyleFiles` lists the CSS rule files, loaded in order after the palette. Keep the
library's `styles.css` first, then add your own; call it before `Initialize`:

```cpp
// The library's rules first, then the app's (relative paths are next to the palette).
const std::string_view styleFiles[] = { "styles.css", TACHYON_EXAMPLE_DIR "/custom_theme.css" };
SetStyleFiles(styleFiles);
if (tachyon::Result<void> result = GUIApplication::Initialize(options); !result)
    return result;
```

## The app's classes

The app's file loads after the palette (`themes/dark.css` or `light.css`), so it can use
the palette's variables, and follows the active palette when it changes:

```css
.banner {
    padding: 12px;
    margin: 4px;
    border-width: 2px;
    border-radius: 8px;
    border-color: var(--accent);
    background-color: var(--accent-soft);
    color: var(--text-1);
    font-size: 20px;
    text-align: center;
}
```

A class can set its own states, as `.warning-button` does:

```css
.warning-button:hover  { background-color: #7a5018; }
.warning-button:active { background-color: #a06a20; }
```

## Using the classes

`theme.GetStyle` finds a class by name. Look it up in `OnGUI` rather than keeping it: a
theme reload registers the styles again.

```cpp
// Look the classes up every frame: a theme reload registers new styles.
Text("My themed app", theme.GetStyle("banner"));
```

```cpp
Button("Careful!", [] {}, theme.GetStyle("warning-button"));
```

## Switching palettes and font size

`RequestReloadTheme` takes a palette file and a font scale. It is safe to call from a
callback: the reload happens at the start of the next frame.

```cpp
BeginHorizontal();
Button("Dark",  [this] { RequestReloadTheme("themes/dark.css",  GetGlobalFontScale()); });
Button("Light", [this] { RequestReloadTheme("themes/light.css", GetGlobalFontScale()); });
EndHorizontal();

BeginHorizontal();
Button("Smaller text", [this] { RequestReloadTheme(GetThemePath(), GetGlobalFontScale() * 0.9f); });
Button("Larger text",  [this] { RequestReloadTheme(GetThemePath(), GetGlobalFontScale() * 1.1f); });
EndHorizontal();
```

If a reload fails, the previous theme stays and `ThemeReloadResult()` says why. The
[CSS and theming](../css.md) guide lists every selector and property the parser accepts.

---

[← Docking](docking.md) · [Examples](../examples.md) · [Host device →](host-device.md)
