# CSS and theming

TachyonUI styles come from CSS files. The parser reads a subset of CSS plus a few
`-tachyon-*` properties, and turns each class into an `tachyon::Style`. This page lists
exactly what it accepts. For the library's layout and controls, see [overview.md](overview.md)
and [controls.md](controls.md).

## How a theme loads

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

Switching palettes or text size at runtime (from `examples/custom_theme.cpp`):

```cpp
Button("Light", [this] { RequestReloadTheme("themes/light.css", GetGlobalFontScale()); });
Button("Larger text", [this] { RequestReloadTheme(GetThemePath(), GetGlobalFontScale() * 1.1f); });
```

The font scale multiplies every CSS `font-size`. The theme also reloads when the UI scale
changes (`SetUIScale`) or the main window moves to a monitor with another display scale.

Parse errors don't stop loading. Each one is logged as `file:line:column: message`, the bad
declaration or rule is skipped and parsing goes on.

## Syntax

### Rules and selectors

```css
.button { padding: 2px 8px; }            /* a class */
.button, .toggle { cursor: pointer; }    /* a selector list shares one block */
* { font-family: "Inter"; }              /* the base of every style */
```

- A selector is a class name. The leading `.` is optional (`button { }` is the class
  `button`). Class names are case-sensitive; property names and keywords are not.
- `*` sets the base: it applies to every class defined so far and is the starting point
  of every class defined after it. Class rules override it. Put it before the rules it
  should be the base of. A library style whose class no file defines gets the base.
- A class can appear in any number of rules; later declarations overwrite earlier ones.
  File order is the only priority (there is no specificity).
- There are no combinators: `.a .b`, `.a > .b` and the like are reported as errors.

### Pseudo-classes and states

Each style has a slot per element state. A rule writes to every slot whose state bits
include the selector's bits:

| Selector | State |
| --- | --- |
| (none) | Every state, including disabled |
| `:hover` | Under the pointer |
| `:active` | Being pressed |
| `:focus` | Has keyboard focus |
| `:selected` or `::highlight(selected)` | Selected or checked (a selected row, a toggle that is on) |
| `::selection` | Selected text in the element (its `background-color` is the highlight) |
| `:disabled` | Disabled. Exclusive: it can't combine with other states |
| `:not(:hover)`, `:not(:active)`, `:not(:focus)`, `:not(:selected)` | Slots without that bit |
| `:focus-visible` | The focus ring, shown while focus came from the keyboard. Only `box-shadow`, see below |
| `:normal` | Same as no pseudo-class |

States combine: `.a:hover` also applies while hovered and pressed, and
`.a:focus::selection` applies to selected text in a focused element. Because a rule covers
every slot that has its bits, write the general rule first and the specific ones after:

```css
.text-style        { background-color: var(--bg-control); border-color: var(--line-strong); }
.text-style:hover  { background-color: var(--bg-active); }
.text-style:active { border-color: var(--accent); background-color: var(--bg-pressed); }
.text-style:focus  { border-color: var(--accent); background-color: var(--bg-control); }
.text-style::highlight(selected) { color: var(--text-1); background-color: var(--accent-soft); }

.textbox::selection { color: var(--text-1); background-color: var(--selection); }
.textbox:not(:focus)::selection { background-color: var(--selection-inactive); }
```

A `:disabled` rule changes only what it declares; the rest comes from the class's base rule.
`::selection` must have two colons (`:selection` is an error).

Only per-state properties (marked "state" in the tables below) differ between slots. A
layout property such as `padding` or `border-style` written in a `:hover` rule changes the
whole style, for every state.

### Comments

`/* block */` and `// to the end of the line` are both comments.

### @font-face

```css
@font-face { font-family: "Inter"; src: url("../fonts/Inter/Inter-Regular.ttf"); }
@font-face { font-family: "Inter"; src: url("../fonts/Inter/Inter-Bold.ttf"); }
```

- Descriptors: `font-family` (quoted, or unquoted words) and `src` (`url("path")` or
  `"path"`, optionally followed by `format(...)`, which is ignored). One source per rule.
- Faces with the same family name form one family; weight and style come from the font files.
- Relative paths are relative to the palette file's folder (`themes/`), for every style file.
- Use the family with `font-family: "Inter";`.

### Custom properties

Custom properties are defined in `:root` blocks (any number, in any file) and used with
`var(--name)`:

```css
:root {
    --accent: #6B9BFF;                          /* a color */
    --bg-band: linear-gradient(90deg, #000, #333); /* a gradient */
    --radius-md: 4px;                           /* one number */
    --radius-md-first: 4px 0px 0px 4px;         /* 2 to 4 numbers, for shorthands */
    --focus-ring: 0 0 0 3px #6B9BFF4D;          /* a box-shadow (numbers then a color) */
    --shadow-control: none;                     /* no box-shadow */
}
```

| Value | Can be used in |
| --- | --- |
| A color or gradient (any color form below, or `var()` of an earlier color variable) | Color properties, gradient stops, `box-shadow` colors |
| One number (`px` or `em` suffix ignored) | Any property that takes `var()` and one number |
| 2 to 4 numbers | `margin`, `padding`, `border-width`, `border-radius` (each value fills one slot) |
| A box-shadow list, or `none` | `box-shadow` |

Numeric variables are stored as whole numbers from 0 to 65535: a fraction is cut off and
a negative value becomes 0. Using an undefined variable, or one of the wrong kind, is an error.

## Values

### Numbers and units

Lengths are CSS pixels. The `px` suffix is optional (`4` and `4px` are the same). A number may
start with its decimal point (`.5`). There are no other length units; `em` works only in
`line-height`. Box-model lengths (margin, padding, border width, radius) are whole numbers
from 0 to 65535, so a negative margin becomes 0.

### Colors

| Form | Notes |
| --- | --- |
| `#rgb`, `#rgba`, `#rrggbb`, `#rrggbbaa` | |
| `rgb(r, g, b)` | 0-255. Commas are optional |
| `rgba(r, g, b, a)` | `a` from 0 to 1, or above 1 as 0-255 |
| Named colors | `transparent white black red green lime blue yellow cyan aqua magenta fuchsia orange purple gray grey silver navy teal maroon olive coral salmon gold pink brown indigo violet crimson darkgray darkgrey lightgray lightgrey` |
| `var(--name)` | A color variable |

### Gradients

Background, border, text and icon colors (and color variables) also take gradients:

```css
background-color: linear-gradient(90deg, #202A3D, #6B9BFF 60%, #E8EAEF);
background-color: radial-gradient(var(--accent), transparent);
```

- `linear-gradient([angle,] stop, stop, ...)`: the angle is a number with `deg`, `grad`,
  `rad` or `turn`, or a bare number in degrees. Default 0.
- `radial-gradient(stop, stop, ...)`: same form.
- A stop is a color with an optional position in percent. Stops without a position are
  spread evenly; the first defaults to 0% and the last to 100%. At most 8 stops.
- `to right` and other keyword directions, shapes and sizes are not supported.

## Properties

"State" means the property can differ per state (`:hover`, `:focus`, ...). The others are
layout and apply to the style as a whole.

### Box model

| Property | Values |
| --- | --- |
| `margin`, `padding` | 1 to 4 lengths (top, right, bottom, left, as in CSS) or `var()` |
| `margin-top`, `-right`, `-bottom`, `-left` | length or `var()` |
| `padding-top`, `-right`, `-bottom`, `-left` | length or `var()` |

### Borders and radius

| Property | Values | |
| --- | --- | --- |
| `border-width` | 1 to 4 lengths or `var()` | |
| `border-top-width`, `-right-`, `-bottom-`, `-left-width` | length or `var()` | |
| `border-radius` | 1 to 4 lengths or `var()` (top-left, top-right, bottom-right, bottom-left; 2 values = TL/BR and TR/BL; 3 values = TL, TR/BL, BR) | |
| `border-top-left-radius`, `border-top-right-radius`, `border-bottom-right-radius`, `border-bottom-left-radius` | length or `var()` | |
| `border-color` | color or gradient (all four edges) | state |
| `border-top-color`, `-right-`, `-bottom-`, `-left-color` | color or gradient | state |
| `border-style` | `solid`, `dashed` (6px dashes), `dotted` (2px dashes) | |
| `-tachyon-border-dash-size` | dash length, 0 = solid | |
| `-tachyon-border-dash-offset` | where the dash pattern starts | |

There is no `border` shorthand; set width, color and style separately.

### Colors and backgrounds

| Property | Values | |
| --- | --- | --- |
| `background-color` | color or gradient | state |
| `color` | color or gradient: the text color | state |
| `-tachyon-icon-color` | color or gradient: the icon color. Unset = the text color | state |

### Box shadow and focus ring

| Property | Values | |
| --- | --- | --- |
| `box-shadow` | `none`, or 1 or 2 layers separated by commas | state |

A layer is `<x> <y> [<blur> [<spread>]]` and a color, before or after the lengths, or a
`var()` holding shadows. The first layer draws on top.

```css
.node                      { box-shadow: var(--shadow-node); }
.node::highlight(selected) { box-shadow: var(--selected-ring), var(--shadow-node); }
```

Limits: at most 2 layers, the color is required, blur can't be negative, and `inset` is
only allowed in `:focus-visible`.

`:focus-visible` takes exactly one `box-shadow` layer and nothing else, and can't be
combined with other states. It sets the focus ring, drawn over the state's own shadows
while focus came from the keyboard. An inset ring is an outline inside the box and must
have the form `inset 0 0 0 <width> <color>`:

```css
.button:focus-visible { box-shadow: var(--focus-ring); }              /* 0 0 0 3px #6B9BFF4D */
.menu-item:focus-visible { box-shadow: inset 0 0 0 1px var(--accent); }
```

### Text and fonts

| Property | Values | |
| --- | --- | --- |
| `font-family` | comma list of family names (quoted or not) and generics: `default`, `emoji`, `sans-serif`, `serif`, `monospace`, `math`. The first name is the font; the first generic is used while that font isn't registered. A quoted generic is a name | state |
| `font-size` | number or `var()` | state |
| `font-weight` | `normal`, `thin`, `extra-light`/`ultralight`, `light`, `medium`, `demi-bold`/`semi-bold`/`semibold`, `bold`, `extra-bold`/`ultrabold`, `black`/`heavy`, `extra-black`/`ultra-black`, or a number (rounded up to the next hundred; above 900 is extra-black) | state |
| `font-style` | `normal`, `italic`, `oblique` | state |
| `font-stretch` | `normal`, `ultra-condensed`, `extra-condensed`, `condensed`, `semi-condensed`, `semi-expanded`, `expanded`, `extra-expanded`, `ultra-expanded` | state |
| `line-height` | `normal`, a bare number (times the font size), `<n>em` (same), `<n>px` | state |
| `letter-spacing`, `word-spacing` | number or `var()` | state |
| `-tachyon-language` | a BCP 47 tag (`ja`, `"zh-Hans"`) for shaping and font fallback, or `none` | state |
| `text-align` | `start`, `end`, `center`, `left`, `right`, `top`, `bottom` | |
| `-tachyon-vertical-text-align` | `start`/`top`, `center`/`middle`, `end`/`bottom`: where the text (and icon) block sits vertically in the box | |
| `vertical-align`, `dominant-baseline`, `-tachyon-text-baseline` | `auto`/`baseline`/`alphabetic`, `ideographic`, `central`, `hanging`, `mathematical`, `middle`, `text-top`, `text-bottom`: the glyph baseline, not the block position | |
| `text-wrap` | `nowrap`, `wrap`, `word-char` (wrap at words, break a too-long word between characters) | |
| `text-overflow` | `clip`, `ellipsis` | |
| `direction` | `auto`, `initial` (same as auto), `ltr`, `rtl` | |

### Text decoration

There are four lines: `underline`, `overline`, `throughline` and `bottomline`. Each has four
properties, all per state; a line is drawn when its thickness is above 0.

| Property | Values | |
| --- | --- | --- |
| `text-<line>-color` | color (not a gradient) | state |
| `text-<line>-thickness` | number or `var()` | state |
| `text-<line>-offset` | number or `var()` | state |
| `text-<line>-style` | `solid`, `double`, `dotted`, `dashed`, `wavy` | state |

For example `text-underline-color: var(--text-1); text-underline-offset: 2px;`. There is
no `text-decoration` shorthand.

### Flex

Every box is a flex container unless it is `display: grid`.

| Property | Values |
| --- | --- |
| `flex-direction` | `row`, `row-reverse`, `column`, `column-reverse` |
| `flex-wrap` | `nowrap`, `wrap`, `wrap-reverse` |
| `justify-content` | `flex-start`/`start`, `flex-end`/`end`, `center`, `space-between`, `space-around`, `space-evenly` (flex and grid) |
| `align-items` | `flex-start`/`start`, `flex-end`/`end`, `center`, `stretch` (flex and grid) |
| `align-content` | `flex-start`/`start`, `flex-end`/`end`, `center`, `stretch`, `space-between`, `space-around`, `space-evenly` (between wrapped lines) |
| `align-self` | `auto`, `flex-start`/`start`, `flex-end`/`end`, `center`, `stretch` |
| `gap` | length or `var()`: space between flex items |
| `flex-grow`, `flex-shrink` | whole number or `var()` (a fraction is cut off) |
| `flex-basis` | `auto`, length or `var()` |
| `flex` | `none` (0 0 auto), `auto` (1 1 auto), or `<grow> [<shrink> [<basis> \| auto]]`; with one number, shrink is 1 and basis 0 |
| `break-after` | `auto`, `always`/`right` (line break after this item), `left` (line break before it), `avoid` (keep it on the line with the next item) |

### Grid

| Property | Values |
| --- | --- |
| `display: grid` | makes the box a grid container |
| `column-gap`, `row-gap` | length or `var()` |
| `justify-items` | `stretch` (default: items fill their cell), or a `justify-content` value to place items in the cell |
| `grid-column`, `grid-row` | `<start>`, `auto`, `span <n>`, or `<start> / span <n>` |
| `grid-column-start`, `grid-row-start` | number or `auto` |
| `-tachyon-grid-column-track`, `-tachyon-grid-row-track` | `auto`, `<n>px` (or a bare number), `<n>fr`. Set on an item: the size of the column or row it sits in. Items in the same track merge (fr wins over auto, auto over px; equal kinds take the larger value) |

There are no `grid-template-*` properties.

### Sizing

| Property | Values |
| --- | --- |
| `width`, `height` | `auto` or a length |
| `min-width`, `max-width`, `min-height`, `max-height` | a length |

Sizes don't take `var()` or percentages. Code can override them per element with a
`LayoutOverride` (see [controls.md](controls.md)).

### Display, overflow and visibility

| Property | Values |
| --- | --- |
| `display` | `flex`, `grid`, `none` (not drawn, takes no space) |
| `overflow` | `auto` (clip, scroll when needed), `hidden` (clip), `visible`; sets both axes |
| `overflow-x`, `overflow-y` | same values, one axis |
| `visibility` | `visible`, `collapsed` (not drawn, takes no space) |
| `-tachyon-clamp-scroll` | `true`/`1` (default), `false`/`0`: let the content scroll past its edges |

### Cursor

| Property | Values |
| --- | --- |
| `cursor` | `default`, `text`, `wait`, `crosshair`, `pointer`, `col-resize`, `row-resize`, `ew-resize`, `ns-resize`, `nwse-resize`, `nesw-resize`, `move`, `not-allowed`, `none` |

### Icons

For controls that show an icon next to their text.

| Property | Values | |
| --- | --- | --- |
| `-tachyon-icon-side` | `left` (default), `right`, `top`, `bottom` | |
| `-tachyon-icon-text-gap` | length or `var()`: space between icon and text | |
| `-tachyon-icon-size` | length or `var()`; 0 = the icon's own size | |
| `-tachyon-icon-offset-y` | length (may be negative) or `var()`: moves the icon up | |
| `-tachyon-icon-color` | color or gradient, see above | state |

```css
.icon-label {
    -tachyon-icon-side: left;
    -tachyon-icon-text-gap: 6px;
    -tachyon-icon-size: 16px;
    -tachyon-vertical-text-align: middle;
}
```

### Defaults that differ from browsers

A style starts with `display: flex`, `flex-direction: row`, `align-items: flex-start`,
`flex-grow: 0`, `flex-shrink: 0`, `flex-basis: auto`, `overflow: auto`, no border, no padding,
no margin and no colors. Nothing is inherited from the parent element: a class sets what it
needs, or gets it from `*`.

### Not supported

Compared with browser CSS, these are not implemented and are errors or ignored:

- Selectors: combinators, type, id and attribute selectors, `:first-child` and other
  structural pseudo-classes, `::before`/`::after`.
- At-rules other than `@font-face` (`@media`, `@import`, `@keyframes`, ...).
- `!important`, `inherit`, `initial` (except `direction: initial`), `unset`.
- Units other than px (`%`, `rem`, `vw`, ...; `em` only in `line-height`), `calc()`,
  `hsl()`, keyword gradient directions.
- `position`, `top`/`left`/..., `z-index`, `opacity`, `transform`, `transition`,
  `animation`, `outline`, `background-image`, the `border`, `background`, `font` and
  `text-decoration` shorthands, `grid-template-*`.

## Using styles from C++

### Classes from the theme

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

### Styles built in code

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

### UI scale

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
