# CSS and theming

TachyonUI styles come from CSS files. The parser reads a subset of CSS plus a few
`-tachyon-*` properties, and turns each class into a `tachyon::Style`. These pages list
exactly what it accepts. For the library's layout and controls, see the [Overview](overview.md)
and [Controls](controls.md).

This guide has these pages:

| Page | What it covers |
| --- | --- |
| [How a theme loads](css/loading.md) | Which files load, in what order, and how palettes and reloads work. |
| [Syntax](css/syntax.md) | Rules, selectors, pseudo-classes and states, comments, `@font-face` and custom properties. |
| [Values](css/values.md) | Numbers and units, colors and gradients. |
| [Properties](css/properties.md) | Every supported property: box model, borders, colors, text, flex, grid, sizing, icons, and what isn't supported. |
| [Using styles from C++](css/from-cpp.md) | Theme classes, styles built in code, and the UI scale. |

<!--! Page tree for the API reference.
\subpage md_docs_2css_2loading
\subpage md_docs_2css_2syntax
\subpage md_docs_2css_2values
\subpage md_docs_2css_2properties
\subpage md_docs_2css_2from-cpp
-->
