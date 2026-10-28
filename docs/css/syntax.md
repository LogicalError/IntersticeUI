# Syntax

## Rules and selectors

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
  File order is the only priority (there is no specificity), except for the
  [child position](#child-position) rules, which always win over the class's other rules.
- There are no combinators: `.a .b`, `.a > .b` and the like are reported as errors.

## Pseudo-classes and states

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

## Child position

`:first-child`, `:last-child` and `:only-child` style an element by where it is among its
parent's children:

```css
.segment             { border-radius: 0; }
.segment:first-child { border-radius: 4px 0 0 4px; }
.segment:last-child  { border-radius: 0 4px 4px 0; }
.segment:only-child  { border-radius: 4px; }
```

- An only child is both the first and the last, so it takes the `:first-child` and
  `:last-child` rules, then the `:only-child` ones. `:first-child:last-child` is the same
  as `:only-child`.
- These rules apply on top of all of the class's other rules, wherever those are in the
  files, and on top of `*`.
- They take the box properties only: backgrounds, borders (`border-*` and
  `-tachyon-border-dash-*`), `box-shadow`, `margin`, `padding`, `gap`, `row-gap`,
  `column-gap` and `cursor`. Any other property is an error. The element's size and text are
  worked out before its parent's last child is known.
- They combine with states (`.a:first-child:hover`), but not with `:focus-visible`, and need
  a class (`*:first-child` is an error).
- Every child counts, hidden ones too.

## Comments

`/* block */` and `// to the end of the line` are both comments.

## \@font-face

```css
@font-face { font-family: "Inter"; src: url("../fonts/Inter/Inter-Regular.ttf"); }
@font-face { font-family: "Inter"; src: url("../fonts/Inter/Inter-Bold.ttf"); }
```

- Descriptors: `font-family` (quoted, or unquoted words) and `src` (`url("path")` or
  `"path"`, optionally followed by `format(...)`, which is ignored). One source per rule.
- Faces with the same family name form one family; weight and style come from the font files.
- Relative paths are relative to the palette file's folder (`themes/`), for every style file.
- Use the family with `font-family: "Inter";`.

## Custom properties

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

---

[← How a theme loads](loading.md) · [CSS and theming](../css.md) · [Values →](values.md)
