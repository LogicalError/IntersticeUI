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
- Color and gradient swatches show keyboard focus with an accent border, like the fields next to them.
- Tab bars no longer wrap: the tabs that don't fit are listed by a "+N" button after the others, and the selected tab is always shown.
