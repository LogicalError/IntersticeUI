# Properties

"State" means the property can differ per state (`:hover`, `:focus`, ...). The others are
layout and apply to the style as a whole.

## Box model

| Property | Values |
| --- | --- |
| `margin`, `padding` | 1 to 4 lengths (top, right, bottom, left, as in CSS) or `var()` |
| `margin-top`, `-right`, `-bottom`, `-left` | length or `var()` |
| `padding-top`, `-right`, `-bottom`, `-left` | length or `var()` |

## Borders and radius

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

## Colors and backgrounds

| Property | Values | |
| --- | --- | --- |
| `background-color` | color or gradient | state |
| `color` | color or gradient: the text color | state |
| `-tachyon-icon-color` | color or gradient: the icon color. Unset = the text color | state |

## Box shadow and focus ring

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

## Text and fonts

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

## Text decoration

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

## Flex

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

## Grid

| Property | Values |
| --- | --- |
| `display: grid` | makes the box a grid container |
| `column-gap`, `row-gap` | length or `var()` |
| `justify-items` | `stretch` (default: items fill their cell), or a `justify-content` value to place items in the cell |
| `grid-column`, `grid-row` | `<start>`, `auto`, `span <n>`, or `<start> / span <n>` |
| `grid-column-start`, `grid-row-start` | number or `auto` |
| `-tachyon-grid-column-track`, `-tachyon-grid-row-track` | `auto`, `<n>px` (or a bare number), `<n>fr`. Set on an item: the size of the column or row it sits in. Items in the same track merge (fr wins over auto, auto over px; equal kinds take the larger value) |

There are no `grid-template-*` properties.

## Sizing

| Property | Values |
| --- | --- |
| `width`, `height` | `auto` or a length |
| `min-width`, `max-width`, `min-height`, `max-height` | a length |

Sizes don't take `var()` or percentages. Code can override them per element with a
`LayoutOverride` (see [Controls](../controls.md)).

## Display, overflow and visibility

| Property | Values |
| --- | --- |
| `display` | `flex`, `grid`, `none` (not drawn, takes no space) |
| `overflow` | `auto` (clip, scroll when needed), `hidden` (clip), `visible`; sets both axes |
| `overflow-x`, `overflow-y` | same values, one axis |
| `visibility` | `visible`, `collapsed` (not drawn, takes no space) |
| `-tachyon-clamp-scroll` | `true`/`1` (default), `false`/`0`: let the content scroll past its edges |

## Cursor

| Property | Values |
| --- | --- |
| `cursor` | `default`, `text`, `wait`, `crosshair`, `pointer`, `col-resize`, `row-resize`, `ew-resize`, `ns-resize`, `nwse-resize`, `nesw-resize`, `move`, `not-allowed`, `none` |

## Icons

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

## Defaults that differ from browsers

A style starts with `display: flex`, `flex-direction: row`, `align-items: flex-start`,
`flex-grow: 0`, `flex-shrink: 0`, `flex-basis: auto`, `overflow: auto`, no border, no padding,
no margin and no colors. Nothing is inherited from the parent element: a class sets what it
needs, or gets it from `*`.

## Not supported

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

---

[← Values](values.md) · [CSS and theming](../css.md) · [Using styles from C++ →](from-cpp.md)
