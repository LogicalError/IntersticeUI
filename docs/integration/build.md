# Adding the library to a build

The library is two static libraries, `gui_core` and `gui_controls`. A program links
`gui_controls`, which brings `gui_core` with it. Both ways below give the same target
names.

## As a subproject

```cmake
add_subdirectory(external/TachyonUI)
target_link_libraries(my_app PRIVATE TachyonUI::gui_controls)
```

As a subproject only the libraries are built: the showcase, examples, tests and install
rules are off by default. Nothing is written to the TachyonUI source tree; copies of the
optional resources (themes, fonts, icons) go into the build tree, in
`<TachyonUI build dir>/resources`.

Options, set before `add_subdirectory`:

| Option | Default | |
| --- | --- | --- |
| `TACHYON_BUILD_SHOWCASE` | on when top-level | The showcase application (`examples/showcase`) |
| `TACHYON_BUILD_EXAMPLES` | on when top-level | The small examples in `examples/` |
| `TACHYON_BUILD_TESTS` | on when top-level | The unit and GPU tests |
| `TACHYON_INSTALL` | on when top-level | The install rules and the CMake package |
| `TACHYON_INSTALL_THIRD_PARTY` | on | Bundle the third-party libraries into the install |
| `TACHYON_SDL` | on | The SDL window backend and clipboard; off builds without SDL |
| `TACHYON_SYSTEM_SDL` | off | Use the host's SDL3 |
| `TACHYON_SYSTEM_HARFBUZZ` | off | Use the host's HarfBuzz |
| `TACHYON_SYSTEM_VMA` | off | Use the host's VulkanMemoryAllocator |
| `TACHYON_VMA_IMPLEMENTATION` | on | Compile VMA's implementation into `gui_core`; off when the host links its own |
| `TACHYON_FALLBACK_FONTS` | off | Copy (and install) the Noto fallback fonts with the resources |
| `TACHYON_EMBED_RESOURCES` | on | Build the default themes and fonts into `gui_core` (about 3 MB). Off loads them from the resource root only, so every program, the examples included, must call `SetResourceRoot` with a folder that holds `themes/` and `fonts/` |

The `TACHYON_SYSTEM_*` options take a target the host already defined
(`SDL3::SDL3`, `harfbuzz::harfbuzz`, `GPUOpen::VulkanMemoryAllocator`), or else one
`find_package` finds:

```cmake
find_package(SDL3 CONFIG REQUIRED)
set(TACHYON_SYSTEM_SDL      ON)
set(TACHYON_SYSTEM_HARFBUZZ ON)
add_subdirectory(external/TachyonUI)
```

The bundled SDL3 is a static library, so a program has no SDL DLL to ship. A host's own
SDL3 may be a DLL; on Windows it must then be next to the executable, which a post-build
step can copy:

```cmake
add_custom_command(TARGET my_app POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          $<TARGET_RUNTIME_DLLS:my_app> $<TARGET_FILE_DIR:my_app>
  COMMAND_EXPAND_LISTS)
```

`tests/subproject_consumer` is a minimal host; with `CONSUMER_HOST_DEPENDENCIES=ON` it
brings its own SDL3, HarfBuzz and VMA.

## As an installed package

`cmake --install` produces a self-contained package: both libraries, their public
headers, the optional resources, the bundled third-party libraries, the license files
and a CMake package configuration. Debug and Release install side by side into the same
prefix:

```text
cmake --install build --config Debug   --prefix <prefix>
cmake --install build --config Release --prefix <prefix>
```

A consumer then needs no TachyonUI source or build tree:

```cmake
find_package(TachyonUI 0.1 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE TachyonUI::gui_controls)
```

The package finds its dependencies (Vulkan, SDL3, HarfBuzz and the other bundled
libraries) in the same prefix. The consumer must use the same toolchain as the
package. Only Debug and Release are installed; other configurations need
`CMAKE_MAP_IMPORTED_CONFIG_<CONFIG>` in the consumer. `tests/package_consumer` is a
minimal consumer.

## Resources

A program needs no files: linking `gui_controls` is enough. The library has these built in:

- the Vulkan backend's shaders (always);
- the default themes, `dark.css`, `light.css` and `styles.css`, and the Inter and
  JetBrains Mono fonts (unless built with `TACHYON_EMBED_RESOURCES` off).

Files on disk are optional. `GUIApplication::SetResourceRoot(folder)` sets where they load
from, before `Initialize`; relative theme and font paths resolve against it. A file there
replaces the built-in file of the same name, so a changed `themes/styles.css` takes over,
and anything else is your own:

| Folder | Contents |
| --- | --- |
| `themes/` | Your own palettes and style files (see `SetStyleFiles`) |
| `fonts/` | Fonts your style files name in `@font-face` |
| `fonts/Noto-otf/` | Optional fallback fonts for other scripts, emoji and symbols, loaded with `InitializeOptions::fallbackFonts` |
| `icons/` | The showcase's SVG icons; an app may use them but doesn't need them |

Linking `gui_controls` defines `TACHYON_RESOURCE_DIR` as the folder with copies of these
for that build or package:

| Build | Folder |
| --- | --- |
| TachyonUI built on its own | `workdirectory/` in the source tree |
| As a subproject | `resources/` in TachyonUI's build directory |
| Installed package | `share/TachyonUI/resources` in the prefix |

CMake code that uses the package gets the same folder as `TachyonUI_RESOURCE_DIR`, for
example to copy the fallback fonts next to a shipped program, which then sets the root to
wherever it put them.

A custom graphics backend brings its own shaders: the library only hands it the
primitives to draw (see `PrimitiveSpec` in `gui_core/graphics/gui_render_primitives.h`;
the GLSL in `workdirectory/shaders` is the Vulkan backend's version).

---

[Integration](../integration.md) · [Using your own Vulkan device →](vulkan-device.md)
