# Showcase

An editor-style application with most of the library in its panels: the one in the
README's screenshot. It is a testbed for the library's features rather than a tidy
example, so read it for how a larger app fits together, and the smaller examples for how
to use one part.
Source: [examples/showcase](https://github.com/LogicalError/TachyonUI/tree/main/examples/showcase).

## Running it

It runs from any folder, double-click included:

```text
bin\Debug\showcase.exe                  rem Windows
bin/Debug/showcase                      # Linux and macOS
```

Unlike the small examples it has files of its own, in `workdirectory/`: `showcase.css`, its
icons and the optional Noto fallback fonts. It finds them with
`SetResourceRoot(TACHYON_RESOURCE_DIR)`, the folder the build defines for them.

Command-line options:

| Option | Does |
| --- | --- |
| `--ui-scale <factor>` | Zooms the UI on top of the OS display scale, for example `1.5`. |
| `--theme <palette>` | Starts with another palette, for example `themes/light.css`. |
| `--panel <title>` | Brings that panel to the front of its tab group (repeatable). |
| `--float <title>` | Starts with that panel in its own window (repeatable). |

It saves its window and dock layout to `workdirectory/ui_state.json` when it quits, and
restores it on the next start.

## Where to look

| File | What it shows |
| --- | --- |
| `application.h`, `application.cpp` | Setup: style files, icons, commands and shortcuts, the dock layout, toolbars and saved UI state. |
| `panel_hierarchy.cpp` | A scene tree: tree cells in a virtual table. |
| `panel_inspector.cpp` | Fields, menus and context menus as an inspector. |
| `panel_files.cpp` | A file browser: a folder tree and a grid of files, renaming, drag and drop. |
| `panel_console.cpp` | A log in a virtual table, with level filters. |
| `panel_node_view.cpp` | The node graph editor, with undo. |
| `panel_table.cpp`, `panel_split_scroll.cpp`, `panel_tree_table.cpp` | Tables: sizing, a 50,000-row virtual table, frozen columns and tree rows. |
| `panel_profiler_tree.cpp` | The profiler's frame tree, with custom-drawn bars. |
| `panel_settings.cpp` | The settings panel, including a shortcuts page. |
| `panel_reorderable.cpp` | Drag-to-reorder lists, tabs and wrapped groups. |
| `panel_view.cpp` | A viewport with docked toolbars. |
| `panel_misc.cpp`, `panel_box_test.cpp` | Custom drawing, images, icon placement and box layout. |

Its own CSS classes are in `workdirectory/themes/showcase.css`, loaded after the library's
`styles.css` as the [Custom theme](custom-theme.md) example shows.

---

[← Host device](host-device.md) · [Examples](../examples.md)
