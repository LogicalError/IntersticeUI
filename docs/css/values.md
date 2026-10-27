# Values

## Numbers and units

Lengths are CSS pixels. The `px` suffix is optional (`4` and `4px` are the same). A number may
start with its decimal point (`.5`). There are no other length units; `em` works only in
`line-height`. Box-model lengths (margin, padding, border width, radius) are whole numbers
from 0 to 65535, so a negative margin becomes 0.

## Colors

| Form | Notes |
| --- | --- |
| `#rgb`, `#rgba`, `#rrggbb`, `#rrggbbaa` | |
| `rgb(r, g, b)` | 0-255. Commas are optional |
| `rgba(r, g, b, a)` | `a` from 0 to 1, or above 1 as 0-255 |
| Named colors | `transparent white black red green lime blue yellow cyan aqua magenta fuchsia orange purple gray grey silver navy teal maroon olive coral salmon gold pink brown indigo violet crimson darkgray darkgrey lightgray lightgrey` |
| `var(--name)` | A color variable |

## Gradients

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

---

[← Syntax](syntax.md) · [CSS and theming](../css.md) · [Properties →](properties.md)
