# TachyonUI

[![CI](https://github.com/LogicalError/TachyonUI/actions/workflows/ci.yml/badge.svg)](https://github.com/LogicalError/TachyonUI/actions/workflows/ci.yml)
[![Docs](https://github.com/LogicalError/TachyonUI/actions/workflows/docs.yml/badge.svg)](https://logicalerror.github.io/TachyonUI/)

A C++20 GUI library for tools and editors. Build the UI each frame in code; the
library keeps the state, lays it out with flexbox and grid, and draws it.

- Docking panels, multiple OS windows, menus, tooltips and drag and drop
- Controls: buttons, text and numeric fields, sliders, color and gradient
  editors, tables, trees, list and grid views, node graphs
- Styles from CSS files, with dark and light themes, UI scaling and font scaling
- Text shaping, bidirectional text and font fallback for most scripts

![The demo application](docs/screenshot.png)

The UI draws through a graphics backend and gets windows and input from a window
backend, so it isn't tied to one graphics API or platform. The current backends
are Vulkan and SDL3. A host passes its own with `SetGraphicsBackend`,
`SetWindowBackend` and `SetClipboard`; the CMake option `TACHYON_SDL=OFF`
builds without SDL, and then the host must pass a window backend.

The library is two static libraries:

| Library | Contents |
| --- | --- |
| `gui_core` | Windowing, rendering backends, input, layout, text, styles, element storage |
| `gui_controls` | `GUIApplication`, controls, docking, menus, tooltips, transitions |

The guides in [docs](docs/README.md) explain the frame model and IDs, every control,
the CSS the themes use, and integrating the library into an engine. The
[API reference](https://logicalerror.github.io/TachyonUI/) has them together with the headers' `///` comments.

## Requirements

- Windows x64 with Visual Studio 2026 (MSVC) and CMake 4.2 or newer; Visual Studio 2022
  with CMake 3.22 or newer also works
- Linux x64 with GCC 13 or Clang 17 or newer, Ninja and CMake 3.22 or newer, and the
  X11 and Wayland development packages for SDL3 (see the Linux job in
  `.github/workflows/ci.yml` for the Ubuntu package list)
- macOS (untested: CI builds it with Apple Clang against MoltenVK from Homebrew and runs
  the headless tests, but it has never run on a Mac)
- The [Vulkan SDK](https://vulkan.lunarg.com/): headers, loader and
  `glslangValidator` for the shaders; the GPU tests also use its validation layers
- Git with [Git LFS](https://git-lfs.com/): the fonts are stored in LFS, and a
  clone without it only has pointer files, so the fonts fail to load
- Network access on the first configure
- At runtime, a GPU driver with Vulkan 1.3, `VK_KHR_swapchain` and the features
  listed by `tachyon::vulkan::VulkanDeviceContext::GetDeviceRequirements()` (dynamic
  rendering, synchronization2, descriptor indexing, buffer device address, 8- and
  16-bit storage, among others)

## Build

```text
git clone --recursive https://github.com/LogicalError/TachyonUI.git
cd TachyonUI
cmake -S . -B build -G "Visual Studio 18 2026" -A x64   # or "Visual Studio 17 2022"
cmake --build build --config Debug
```

On Linux and macOS:

```text
cmake -S . -B build -G "Ninja Multi-Config"
cmake --build build --config Debug
```

In an existing clone without the submodules, run
`git submodule update --init --recursive`; configure stops with that hint when
they are missing.

The first configure builds the third-party libraries once into
`build/third_party/install`; later builds reuse them. Executables land in
`bin/<config>`.

| Option | Default | |
| --- | --- | --- |
| `TACHYON_BUILD_SHOWCASE` | on when top-level | The showcase application (`examples/showcase`) |
| `TACHYON_BUILD_EXAMPLES` | on when top-level | The programs in `examples/` |
| `TACHYON_BUILD_TESTS` | on when top-level | The unit and GPU tests |
| `TACHYON_INSTALL` | on when top-level | The install rules and the CMake package |
| `TACHYON_INSTALL_THIRD_PARTY` | on | Bundle the third-party libraries into the install |
| `TACHYON_SDL` | on | The SDL window backend; off builds without SDL |
| `TACHYON_SYSTEM_SDL`, `_HARFBUZZ`, `_VMA` | off | Use the host's copy of that library |

As a subproject (`add_subdirectory`), only the libraries are built by default.

## Run the showcase, examples and tests

`showcase` (in `examples/showcase`) is a testbed that shows most of the library; it is
not part of it. It
loads its files relative to the working directory:

```text
cd workdirectory
..\bin\Debug\showcase.exe           rem --ui-scale 1.5 zooms the UI
```

`examples/` has small programs that each show one thing and run from any folder:

| Example | Shows |
| --- | --- |
| `hello_world` | A window with a text field and buttons |
| `controls` | The common controls, laid out as label/value rows |
| `docking` | Panels in a dock layout |
| `custom_theme` | An app's own CSS file, dark/light palettes, font scaling |
| `host_device` | Hosting on the app's own Vulkan device and showing an image it renders |

The tests find `workdirectory` themselves:

```text
bin\Debug\gui_core_tests.exe        rem unit tests, no GPU needed
bin\Debug\gui_gpu_tests.exe         rem real-device tests, skipped without a GPU
ctest --test-dir build -C Debug     rem both, one process per test
```

## Hello world

```cpp
#include <gui_controls/gui_application.h>

#include <string>

class HelloApp : public tachyon::GUIApplication
{
public:
    tachyon::Result<void> Initialize(const InitializeOptions& options = {}) override
    {
        if (tachyon::Result<void> result = GUIApplication::Initialize(options); !result)
            return result;
        windowID = idManager.CreateStaticID();   // windows need stable IDs
        return {};
    }

    // Called every frame: describe the whole UI; the library keeps the state.
    void OnGUI() override
    {
        if (BeginWindow(windowID, "Hello", tachyon::LayoutOverride{}.Size(360, 160)))
        {
            BeginVertical(tachyon::LayoutOverride{}.FlexGrow(1));
            BeginLabelGroup();
            Label("Name");
            Field(name);
            EndLabelGroup();
            Button("Greet", [this] { greeting = "Hello, " + name + "!"; });
            Text(greeting);
            EndVertical();
        }
        EndWindow();
    }

private:
    tachyon::ID     windowID = tachyon::ID::Invalid();
    std::string name     = "world";
    std::string greeting;
};

int main()
{
    HelloApp app;
    app.SetResourceRoot("path/to/resources");   // see below
    tachyon::Result<void> result = app.Initialize();
    if (result)
        result = app.Run();   // until the last window closes
    app.Cleanup();
}
```

Every `Begin*` has its `End*`, called whether or not the `Begin*` returned true; the result
only says whether to build the content. Each pair also has a scope that ends it when it
leaves, taking the same arguments:

```cpp
if (auto menu = Menu("File"))
    MenuItem("Quit", [this] { applicationQuit = true; });
```

A control shows a `GUIContent`: a text, an icon or both, and an optional tooltip. A string or
an icon converts to one, so `Button("Save", onSave)` and `Button(saveIcon, onSave)` work as is:

```cpp
Button({ saveIcon, "Save", "Write the file to disk" }, onSave);
```

Sizes are CSS px, scaled by the UI scale like the stylesheet's; `Pixels{}` marks a size that
is already in device pixels (a measured one), and positions and rects are pixels:

```cpp
LayoutOverride{}.Width(200)                  // 300 pixels at 150%
LayoutOverride{}.Width(tachyon::Pixels{ measured.w })
```

Calls that can fail return a `Result<T>` with the reason (`Initialize`, `Run`, `ReadWindowPixels`,
`FindStyle`, `ThemeReloadResult`). A lost GPU device is made again before the next frame; with a
host's device, `RecordFrame` reports it instead.

A host with its own main loop runs the frame itself; `Run()` loops over the same calls:

```cpp
while (app.UpdateFrame())
{
    if (!app.RecordFrame()) break;   // render-to-image windows are drawn now
    // ... the host's own work ...
    app.PresentFrame();              // may wait for vsync
}
```

## Runtime files

An application ships these files, in this layout:

| Folder | Contents |
| --- | --- |
| `shaders_compiled/` | The compiled shaders (built from `workdirectory/shaders`) |
| `fonts/Inter/`, `fonts/JetBrainsMono/` | The built-in fonts: Inter for text, JetBrains Mono for monospace |
| `fonts/Noto-otf/` | Optional fallback fonts for other scripts, emoji and symbols, loaded with `InitializeOptions::fallbackFonts`; ship only the ones you need |
| `themes/` | `dark.css` and `light.css` (palettes) and `styles.css` (the rules) |

`GUIApplication::SetResourceRoot` sets the folder they are loaded from; without it
they load relative to the working directory. In the source tree they are in
`workdirectory/`; an installed package has them in `share/TachyonUI/resources`.
`icons/` holds the demo's SVG icons, which an app may use but doesn't need.
`SDL3.dll` must be next to the executable; the build copies it there.

An application adds its own styles with `SetStyleFiles`, which loads more CSS
files after `styles.css`, and finds their classes with `theme.GetStyle("name")`.
`examples/custom_theme/custom_theme.cpp` shows both. `RequestReloadTheme` switches palettes
or the font scale at runtime.

## Use as a package

`cmake --install` produces a self-contained package: both libraries, their
headers, the runtime files, the bundled third-party libraries, the license
files and a CMake package configuration.

```text
cmake --install build --config Debug   --prefix <prefix>
cmake --install build --config Release --prefix <prefix>
```

Debug and Release install side by side into the same prefix. A consumer then
needs no TachyonUI source or build tree:

```cmake
find_package(TachyonUI 0.1 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE TachyonUI::gui_controls)
```

The package defines `TACHYON_RESOURCE_DIR` for code that links it, so
`app.SetResourceRoot(TACHYON_RESOURCE_DIR)` loads the installed runtime files;
CMake code can use `TachyonUI_RESOURCE_DIR`. `tests/package_consumer` is a
minimal consumer that checks this against an installed prefix.

The consumer must use the same toolchain as the package. Only the Debug and
Release configurations are installed; other configurations need
`CMAKE_MAP_IMPORTED_CONFIG_<CONFIG>` in the consumer.

## Use as a subproject

`add_subdirectory(<TachyonUI>)` works too, with the same target names and
`TACHYON_RESOURCE_DIR`. Nothing is written to the TachyonUI source tree:
the compiled shaders and copies of the fonts and themes go into the build tree.
The Noto fallback fonts are copied (and installed) only with `TACHYON_FALLBACK_FONTS`.
`TACHYON_SYSTEM_SDL`, `TACHYON_SYSTEM_HARFBUZZ` and `TACHYON_SYSTEM_VMA`
use the host's copy of that library instead of the bundled one, either a target
the host already defined or one `find_package` finds. `tests/subproject_consumer`
is a minimal host.

## Vulkan backend

When a host application also renders with Vulkan, it can share its device and
images with the GUI. `examples/host_device/host_device.cpp` does both.

### Sharing the host's device

By default the GUI creates and owns its own `VkInstance` and `VkDevice`. A host
application that already owns a device, such as a game engine hosting this GUI
as its editor, can hand its handles to the GUI so both render on the same
device:

```cpp
auto backend = std::make_unique<tachyon::vulkan::VulkanGraphicsContext>();
tachyon::vulkan::VulkanDeviceContext::ExternalDevice external;
external.instance            = hostInstance;
external.physicalDevice      = hostPhysicalDevice;
external.device              = hostDevice;
external.graphicsQueue       = hostGraphicsQueue;
external.graphicsQueueFamily = hostGraphicsQueueFamily;
external.queueMutex          = &hostQueueMutex;   // optional, see below
backend->SetExternalDevice(external);

auto* vk = backend.get();   // for RegisterExternalImage below
app.SetGraphicsBackend(std::move(backend));
if (tachyon::Result<void> result = app.Initialize(); !result)
    std::fprintf(stderr, "%s\n", result.GetError().message.c_str());   // e.g. a missing device feature
```

If the host's device is lost, `RecordFrame` (and `Run`) returns an error with
`ErrorCode::DeviceLost`. The GUI can't recreate a device it doesn't own, so the host makes
a new device and a new app.

`SetGraphicsBackend` hands the app the graphics backend to use; without it,
`Initialize` creates a `VulkanGraphicsContext`. Another backend implements
`tachyon::GraphicsContext`; `NullGraphicsContext` draws nothing and needs no GPU.

Requirements on the host device:

- `tachyon::vulkan::VulkanDeviceContext::GetDeviceRequirements()` lists the API version,
  features, and device extensions the GUI needs. Enable all of them when
  creating the device. `CheckPhysicalDeviceSupport` reports what a physical
  device lacks. The GUI checks this when it adopts the device, and fails
  initialization with a message if the device cannot run it. It cannot see what
  the host actually enabled at device creation.
- The instance must enable the extensions the window backend needs to create
  surfaces (for SDL, `SDL_Vulkan_GetInstanceExtensions`).
- The queue must support graphics and presentation to the GUI's windows.

Ownership: the GUI destroys only what it created. A host-provided instance,
device, and allocator belong to the host and survive `Cleanup`. If the host
passes no `VmaAllocator`, the GUI creates and owns its own.

Threading: a Vulkan queue needs external synchronization. The GUI locks
`queueMutex` around every `vkQueueSubmit2` and `vkQueuePresentKHR` it issues. A
host that submits to the same queue from other threads must pass the mutex it
locks around its own submissions. Note that presentation can block until vsync
while the lock is held; a host with a second graphics queue should give the GUI
its own queue to avoid that.

### Showing a host-rendered image

A host can render into an image it owns and show it in the UI, for example a
3D viewport:

```cpp
tachyon::ImageIndex index = vk->RegisterExternalImage(view, sampler);   // once
vk->UpdateExternalImage(index, newView, sampler);                   // after a resize

Image(index, tachyon::uint2{ width, height });   // in OnGUI
```

`Image` uses `theme.imageStyleRef`. A custom element or style shows the image only
when its background color style is of type `ColorType::StretchedTexture`. The view
and sampler stay owned by the host and must outlive every GUI frame that
references the index.

Synchronization contract (all on the one queue given to `SetExternalDevice`):

1. By the end of the command buffer it submits for the frame, the host leaves the
   image in `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL` with its writes visible to
   the fragment stage (destination stage `FRAGMENT_SHADER`, access `SHADER_SAMPLED_READ`).
2. Before writing the image again, the host transitions it away with a source stage
   of `FRAGMENT_SHADER`, so the write waits for the GUI's earlier reads.
3. The host submits during the UI build phase (`OnGUI`), before the GUI prepares and
   submits the frame. Submission order on one queue makes both barriers effective
   across command buffers.

After a resize the host must make sure no in-flight GUI frame still samples the old
view; the simplest way is `vkDeviceWaitIdle`. `tests/gpu/gpu_external_device_test.cpp`
exercises the contract under synchronization validation.

### Rendering the UI into an image

A window opened with `ElementFlags::WindowRenderToImage` has no OS window: it lays
out and draws like any other window, into an image instead of onto the screen.
Use it to put the UI inside an engine viewport, composite it into the host's
frame, or render it without anything appearing on screen, as the GPU tests do.

```cpp
if (BeginWindow(windowID, "Panel", theme.windowStyleRef, tachyon::LayoutOverride{}.Size(400, 300),
                tachyon::ElementFlags::WindowRenderToImage))
{
    // controls as usual
}
EndWindow();

// Each frame of the window is copied into the host's image (may be set before the window exists):
vk->SetWindowImageTarget(windowID, hostImage, VkExtent2D{ 400, 300 });

// Or read the last frame back (BGRA8):
if (tachyon::Result<void> read = app.ReadWindowPixels(windowID, pixels); !read)
    std::fprintf(stderr, "%s\n", read.GetError().message.c_str());
```

The window gets no input from the OS, so the host passes it in, with positions in
the window's pixels: `InjectMouseMove`, `InjectMouseButton`, `InjectMouseScroll`,
`InjectMouseLeave`, `InjectKey`, `InjectText` and `InjectFocus` on `GUIApplication`.

Synchronization contract for the target image (on the queue given to
`SetExternalDevice`):

1. The image needs `TRANSFER_DST` usage. The GUI's frame moves it from
   `SHADER_READ_ONLY_OPTIMAL` (`UNDEFINED` the first time) to `TRANSFER_DST_OPTIMAL`
   after the host's fragment-stage reads, copies the window into it (scaled to the
   given extent), and leaves it in `SHADER_READ_ONLY_OPTIMAL`, visible to the
   fragment stage.
2. The host reads it in work submitted after the GUI's frame and leaves it in
   `SHADER_READ_ONLY_OPTIMAL`.

Draw commands can't yet be recorded straight into the host's command buffer; that
needs the host to drive the frame loop (#55).

## Dependencies

Install yourself: the Vulkan SDK (on Linux, the distribution's Vulkan and glslang packages
also do), a compiler, CMake, Git and Git LFS
(see [Requirements](#requirements)).

Handled by the build:

| Dependency | How |
| --- | --- |
| SDL3, skribidi, googletest, VMA, vk-bootstrap, HarfBuzz, SheenBidi, libunibreak, budouxc | Git submodules; configure needs no network |
| nanosvg | Included in the repository |

## Status

Version 0.1, a preview under active development. Windows x64 and Linux x64 with
Vulkan are built and tested in CI; macOS builds in CI but is
untested on a Mac.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for reporting problems and sending changes, and
[SECURITY.md](SECURITY.md) for reporting a vulnerability.

## Support

If TachyonUI is useful to you, you can support its development through
[GitHub Sponsors](https://github.com/sponsors/LogicalError) or
[Patreon](https://www.patreon.com/c/logicalerror).

## License

TachyonUI is licensed under the MIT license; see [LICENSE](LICENSE). The
bundled third-party code, fonts and icons keep their own licenses, listed in
[THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt).
