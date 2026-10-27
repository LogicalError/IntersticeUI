# Overview

TachyonUI is an immediate-mode GUI library: the application describes its whole UI
in code every frame, and the library keeps the state (focus, scroll positions, open
menus, window rects), lays the UI out with flexbox and grid, handles input and draws it.

This guide explains the ideas the rest of the API builds on. The other guides:

- [Controls](controls.md): the controls and containers
- [CSS and theming](css.md): styles and themes
- [Integration](integration.md): hosting the library in an existing application

Everything is in the namespace `tachyon`. The examples in these pages are written inside a
class derived from `GUIApplication`, as in the [README](https://github.com/LogicalError/TachyonUI#readme).

This guide has these pages:

| Page | What it covers |
| --- | --- |
| [The application and the frame](overview/application.md) | `GUIApplication`, `OnGUI`, and what happens in one frame. |
| [IDs](overview/ids.md) | Call-site, static and ranged IDs, parent scopes, and when an ID must be stable. |
| [Begin and End](overview/begin-end.md) | Pairing `Begin*` with `End*`, scope objects, and what misuse does. |
| [Return values, content and units](overview/values.md) | What controls return, callbacks, `GUIContent`, and CSS px versus pixels. |
| [Errors and saved state](overview/errors-and-state.md) | `Result<T>`, error codes, and saving window and dock layouts. |

<!--! Page tree for the API reference.
\subpage md_docs_2overview_2application
\subpage md_docs_2overview_2ids
\subpage md_docs_2overview_2begin-end
\subpage md_docs_2overview_2values
\subpage md_docs_2overview_2errors-and-state
-->
