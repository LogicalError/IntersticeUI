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
- CMake package: `find_package(IntersticeUI)` with `IntersticeUI::gui_core` and `IntersticeUI::gui_controls`; Windows x64 with MSVC.
