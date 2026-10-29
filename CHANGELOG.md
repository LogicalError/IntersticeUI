# Changelog

Notable changes per release. Releases are tagged `vX.Y.Z`; the version lives in
`CMakeLists.txt` and is available in code from `gui_core/gui_version.h`.

## [Unreleased] — 0.1.0

First public preview. The API is not stable yet.

- Immediate-mode style GUI library for C++20 on Vulkan and SDL3 (`gui_core`, `gui_controls`).
- CSS-like styling with a dark and a light theme; text shaping, bidi and font fallback through skribidi.
- Flex and grid layout, docking with saved layouts, tables, tree views, grid views, node view, color and gradient pickers, menus, tooltips and toolbars.
- UI scaling that follows the display, with stored sizes in CSS pixels.
- Retained render cache for panels that haven't changed.
- CMake package: `find_package(TachyonUI)` with `TachyonUI::gui_core` and `TachyonUI::gui_controls`; Windows x64 with MSVC.
- No files or DLLs to ship: the shaders, the default themes and the fonts are built into the library (`TACHYON_EMBED_RESOURCES`), and the bundled SDL3 is linked statically. `SetResourceRoot` is only needed for an app's own themes and fonts; a file there replaces the built-in one of the same name.
- `GraphicsContext::SetShaderDirectory` is gone; `VulkanGraphicsContext::SetShaderDirectory` loads shaders from disk instead of the built-in ones. The package no longer installs `shaders_compiled/`.
- `TACHYON_SDL_TARGET`: the SDL target the libraries link (`SDL3::SDL3-static`, or the host's `SDL3::SDL3`), for a host that calls SDL too.
- `build.bat` / `build.sh` and `CMakePresets.json`; every example runs from any folder.
- A step-by-step tutorial (`docs/tutorial.md`) whose code is the `examples/tutorial` app, kept in sync by tests.
- `Dropdown`: pick one of a list of options; the list opens on the current option.
- `AutocompleteField`: a text field that lists the suggestions matching what's typed.
- Keyboard focus: Tab reaches every control (not disabled ones, which also ignore keys) and every control shows it has the focus; [Writing a control](docs/controls/writing-controls.md) lists what each control must do.
- Saved windows remember their monitor: they come back on it, or at the same place on a monitor of the same size, or on the primary one inside its work area when it's gone.
- Node view: keyboard focus moving to a node out of view pans the view to it.
- `SetTooltip`, `BeginTooltip` and `BeginContextMenu` without a target go on the whole control just drawn, not its last part; a color field's, breadcrumb's or vector field's tooltip and context menu work over each of its parts.
- Context menus open from the keyboard (Shift+F10, the Menu key); tabs close on Ctrl+W / Ctrl+F4; Alt + an arrow moves list rows, tabs and table columns; table headers sort with Space / Enter; the color picker's square and strips take the arrow keys; the node view moves, selects, collapses, pans and zooms from the keyboard.
- Escape closes the color picker and gradient editor as Cancel. The message dialog lets Tab reach all its controls, and takes Esc and its Alt mnemonics from any of them.
- Every control has the four forms (an ID or the call site, a style or the theme's): style overloads for the fields, sliders, dropdowns, menu items, status text, command search and message dialog; call-site forms for the containers, tables and tree, grid and list views; `Checkbox` with a `bool&`; list and dictionary fields take an ID like the others. A control's parts get IDs under its own.
- Sliders, slider fields, a number field's label drag, color, gradient and dropdown fields report a change (return value and `IsModified`) like the other fields; a clicked tab takes the focus; menu items' parts are no longer Tab stops.
- Color and gradient swatches show keyboard focus with an accent border, like the fields next to them.
- Rendering: circles are drawn exactly round, as a primitive of their own (they were four Bezier curves); dots are one triangle each, clipped per pixel; box-shaped primitives are drawn from an index buffer, four vertices each. `SDFGradient` runs from a shape's edge to its deepest point for every primitive.
- A cached scope's output (`BeginCached`, docked panels) stays on the GPU: drawing it again, moved or not, uploads nothing.
- Opaque boxes hide what was drawn under them on the GPU: the depth buffer now holds the draw order, and fragments an opaque box covers are skipped.
- Uploads (each frame's primitives, cached scopes, images) are copied on a thread of their own while the frame's commands are recorded.
- Each window's frame is timed on the GPU (`VulkanWindowContext::LastGpuFrameSeconds`).
- Links in rich text take their cursor from `LinkStyle::cursor`, and `theme.linkStyle` from the theme's `.link` class (color, hover color, cursor). Links in padded or vertically centred text are found where they are drawn.
- CSS `text-shadow`: up to two shadows or glows under text, its decorations and its icon, blurred from the glyph atlas' distance field (now wider, so a blur has room).
- CSS `:first-child`, `:last-child` and `:only-child` for the box properties (backgrounds, borders, shadows, margin, padding, gap, cursor). Button and toggle group items use one class, `group-button` / `group-toggle`, in place of `first-`, `mid-` and `last-button` / `-toggle`; style the ends with `:first-child` and `:last-child`.
- Tab bars no longer wrap: the tabs that don't fit are listed by a "+N" button after the others, and the selected tab is always shown.
