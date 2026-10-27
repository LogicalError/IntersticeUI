# Integrating TachyonUI

How to put TachyonUI into a program that has its own build, Vulkan device, windows
or main loop, such as a game engine hosting it as its editor. Each part is optional: a
program can take the defaults for everything and only call `Initialize` and `Run`, as
in the README's hello world.

The frame model (what `OnGUI` is, when it runs, how IDs stay stable) is described in
[overview.md](overview.md).

## Adding the library to a build

The library is two static libraries, `gui_core` and `gui_controls`. A program links
`gui_controls`, which brings `gui_core` with it. Both ways below give the same target
names.

### As a subproject

```cmake
add_subdirectory(external/TachyonUI)
target_link_libraries(my_app PRIVATE TachyonUI::gui_controls)
```

As a subproject only the libraries are built: the demo, examples, tests and install
rules are off by default. Nothing is written to the TachyonUI source tree; the
compiled shaders and copies of the fonts and themes go into the build tree, in
`<TachyonUI build dir>/resources`.

Options, set before `add_subdirectory`:

| Option | Default | |
| --- | --- | --- |
| `TACHYON_BUILD_SHOWCASE` | on when top-level | The showcase application (`examples/showcase`) |
| `TACHYON_BUILD_EXAMPLES` | on when top-level | The programs in `examples/` |
| `TACHYON_BUILD_TESTS` | on when top-level | The unit and GPU tests |
| `TACHYON_INSTALL` | on when top-level | The install rules and the CMake package |
| `TACHYON_INSTALL_THIRD_PARTY` | on | Bundle the third-party libraries into the install |
| `TACHYON_SDL` | on | The SDL window backend and clipboard; off builds without SDL |
| `TACHYON_SYSTEM_SDL` | off | Use the host's SDL3 |
| `TACHYON_SYSTEM_HARFBUZZ` | off | Use the host's HarfBuzz |
| `TACHYON_SYSTEM_VMA` | off | Use the host's VulkanMemoryAllocator |
| `TACHYON_VMA_IMPLEMENTATION` | on | Compile VMA's implementation into `gui_core`; off when the host links its own |
| `TACHYON_FALLBACK_FONTS` | off | Copy (and install) the Noto fallback fonts with the resources |

The `TACHYON_SYSTEM_*` options take a target the host already defined
(`SDL3::SDL3`, `harfbuzz::harfbuzz`, `GPUOpen::VulkanMemoryAllocator`), or else one
`find_package` finds:

```cmake
find_package(SDL3 CONFIG REQUIRED)
set(TACHYON_SYSTEM_SDL      ON)
set(TACHYON_SYSTEM_HARFBUZZ ON)
add_subdirectory(external/TachyonUI)
```

On Windows, SDL3 is a DLL that must be next to the executable. The consumers in
`tests/` copy it after each build:

```cmake
add_custom_command(TARGET my_app POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          $<TARGET_RUNTIME_DLLS:my_app> $<TARGET_FILE_DIR:my_app>
  COMMAND_EXPAND_LISTS)
```

`tests/subproject_consumer` is a minimal host; with `CONSUMER_HOST_DEPENDENCIES=ON` it
brings its own SDL3, HarfBuzz and VMA.

### As an installed package

`cmake --install` produces a self-contained package: both libraries, their public
headers, the runtime files, the bundled third-party libraries, the license files and
a CMake package configuration. Debug and Release install side by side into the same
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

### Resources

A GUI loads these files at runtime:

| Folder | Contents |
| --- | --- |
| `shaders_compiled/` | The compiled shaders |
| `fonts/Inter/`, `fonts/JetBrainsMono/` | The built-in fonts |
| `fonts/Noto-otf/` | Optional fallback fonts, loaded with `InitializeOptions::fallbackFonts` |
| `themes/` | `dark.css`, `light.css` (palettes) and `styles.css` (the rules) |
| `icons/` | The demo's SVG icons; an app may use them but doesn't need them |

Where they are:

| Build | Folder |
| --- | --- |
| TachyonUI built on its own | `workdirectory/` in the source tree |
| As a subproject | `resources/` in TachyonUI's build directory |
| Installed package | `share/TachyonUI/resources` in the prefix |

`GUIApplication::SetResourceRoot` sets the folder they load from; without it they load
relative to the working directory. Linking `gui_controls` defines
`TACHYON_RESOURCE_DIR` as the folder of that build or package, so this loads them
from any working directory:

```cpp
app.SetResourceRoot(TACHYON_RESOURCE_DIR);   // before Initialize
```

CMake code that uses the package gets the same folder as `TachyonUI_RESOURCE_DIR`,
for example to copy it next to a shipped program. A shipped program sets the root to
wherever it put the files.

## Using your own Vulkan device

By default the GUI creates and owns its own `VkInstance` and `VkDevice`. A host that
already owns a device hands its handles to the GUI so both render on the same device.
`examples/host_device/host_device.cpp` is a complete program.

### Device requirements

`tachyon::vulkan::VulkanDeviceContext::GetDeviceRequirements()` returns what the
device must enable:

```cpp
struct DeviceRequirements
{
    uint32_t                         apiVersion = VK_API_VERSION_1_3;
    VkPhysicalDeviceFeatures         features{};
    VkPhysicalDeviceVulkan11Features features11{};
    VkPhysicalDeviceVulkan12Features features12{};
    VkPhysicalDeviceVulkan13Features features13{};
    std::vector<const char*>         deviceExtensions;
};
```

The feature structs have no `pNext` chain; the host chains them itself. With
vk-bootstrap, as in the example:

```cpp
auto requirements = tachyon::vulkan::VulkanDeviceContext::GetDeviceRequirements();
auto selected = vkb::PhysicalDeviceSelector{ instance }
    .set_minimum_version(1, 3)
    .set_required_features(requirements.features)
    .set_required_features_11(requirements.features11)
    .set_required_features_12(requirements.features12)
    .set_required_features_13(requirements.features13)
    .add_required_extensions(requirements.deviceExtensions)
    .defer_surface_initialization()
    .select();
```

`VulkanDeviceContext::CheckPhysicalDeviceSupport(physicalDevice, &problems)` reports
what a physical device lacks, one line per missing capability. The GUI checks this
when it adopts the device and fails `Initialize` with a message if the device can't
run it. It can't see what the host actually enabled at device creation.

Besides the device:

- The instance must enable the extensions the window backend needs to create surfaces;
  for SDL, `SDL_Vulkan_GetInstanceExtensions`. That needs SDL running before the host
  creates its instance, so the example calls `SDL_Init(SDL_INIT_VIDEO)` first.
- The queue must support graphics and presentation to the GUI's windows.

### Handing over the device

Set the device on a `VulkanGraphicsContext` and give that to the app, both before
`Initialize`:

```cpp
#include <gui_core/graphics/vulkan/vk_graphics_context.h>

auto backend = std::make_unique<tachyon::vulkan::VulkanGraphicsContext>();
tachyon::vulkan::VulkanDeviceContext::ExternalDevice external;
external.instance            = hostInstance;
external.physicalDevice      = hostPhysicalDevice;
external.device              = hostDevice;
external.graphicsQueue       = hostGraphicsQueue;
external.graphicsQueueFamily = hostGraphicsQueueFamily;
external.allocator           = hostAllocator;     // optional
external.queueMutex          = &hostQueueMutex;   // optional
backend->SetExternalDevice(external);

auto* vk = backend.get();   // for RegisterExternalImage and SetWindowImageTarget
app.SetGraphicsBackend(std::move(backend));
if (tachyon::Result<void> result = app.Initialize(); !result)
    std::fprintf(stderr, "%s\n", result.GetError().message.c_str());   // e.g. a missing device feature
```

`allocator` is optional: without one the GUI creates and owns its own `VmaAllocator`.
A host allocator must be created with `VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT`;
`Initialize` fails without it.

Ownership: the GUI destroys only what it created. The host's instance, device and
allocator survive `Cleanup`; the host destroys them after it.

Threading: a Vulkan queue needs external synchronization. The GUI locks `queueMutex`
around every `vkQueueSubmit2` and `vkQueuePresentKHR` it issues, and around
`vkDeviceWaitIdle`. A host that submits to the same queue from other threads passes
the mutex it locks around its own submissions; without one the GUI uses a private
mutex, which only protects against its own threads. Presentation can block until
vsync while the lock is held; a host with a second graphics queue can give the GUI
its own queue to avoid that.

### Showing a host-rendered image

A host can render into an image it owns and show it in the UI, for example a 3D
viewport:

```cpp
tachyon::ImageIndex index = vk->RegisterExternalImage(view, sampler);   // once
vk->UpdateExternalImage(index, newView, sampler);                          // after a resize

Image(index, tachyon::uint2{ width, height });   // in OnGUI
```

The view and sampler stay owned by the host and must outlive every GUI frame that
references the index. `Image` uses `theme.imageStyleRef`; a custom element or style
shows the image only when its background color style is of type
`ColorType::StretchedTexture`.

Synchronization contract, all on the queue given to `SetExternalDevice`:

1. By the end of the command buffer it submits for the frame, the host leaves the
   image in `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL` with its writes visible to the
   fragment stage (destination stage `FRAGMENT_SHADER`, access `SHADER_SAMPLED_READ`).
2. Before writing the image again, the host transitions it away with a source stage of
   `FRAGMENT_SHADER`, so the write waits for the GUI's earlier reads.
3. The host submits during the UI build (`OnGUI`), before the GUI submits the frame.
   Submission order on one queue makes both barriers effective across command buffers.

After a resize the host makes sure no in-flight GUI frame still samples the old view;
the simplest way is `vkDeviceWaitIdle`. `Host::Render` in `examples/host_device/host_device.cpp`
follows this contract, and `tests/gpu/gpu_external_device_test.cpp` checks it under
synchronization validation.

## Your own window and input

The GUI gets its OS windows and their input from a window backend, an
`tachyon::IWindowBackend` (`gui_core/windows/gui_window_backend.h`). Without one,
`Initialize` creates an `SDLWindowBackend`. A host passes its own before `Initialize`:

```cpp
app.SetWindowBackend(std::make_unique<MyWindowBackend>());
app.SetClipboard(std::make_unique<MyClipboard>());   // optional
```

The clipboard defaults to SDL's, or to a `LocalClipboard` in builds without SDL
(`TACHYON_SDL=OFF`). In those builds `Initialize` fails with
`ErrorCode::Unsupported` unless the host passed a window backend.

### Writing a window backend

A backend creates, moves, shows and destroys OS windows; reports their rects, display
scale and the monitors; and delivers input. All sizes and positions are physical
pixels. The important parts:

- `CreateWindow` returns an `OSWindowHandle` (a `void*` the backend chooses);
  `GetNativeHandle` returns what the graphics backend creates its surface from.
- `PollEvents` runs in `UpdateFrame`, and again in `RecordFrame` after the build. It
  handles the OS's events and calls `cleanup` for windows the OS destroyed.
- Input goes to the `IPlatformInputHandler` given by `AttachInputHandlerToWindow`
  (`gui_core/input/gui_platform_input_handler.h`): `OnMouseMove`, `OnMouseButton`,
  `OnMouseScroll`, `OnKeyboard` and `OnTextComposition`. The ID passed is the window's,
  from the `IWindowContext` attached with `AttachContextToWindow`; positions are screen
  pixels. Typed text arrives as `OnKeyboard` with `KeyAction::KeyChar` and the
  codepoint as the key code.
- The Vulkan backend creates a surface for each window through a surface factory.
  `SDLWindowBackend` installs its own when the app calls `SetGraphicsContext` on it;
  another backend installs one with `VulkanGraphicsContext::SetSurfaceFactory`.
  `VulkanGraphicsContext::Initialize` creates a hidden 64 x 64 window through the
  backend to pick a GPU that can present to it.

`NullWindowBackend` (`gui_core/windows/null/gui_null_window_backend.h`) is the minimal
backend and the one to read first: its windows are records with a rect and no input
arrives.

`Initialize` wraps the host's backend to add render-to-image windows, so
`app.windowBackend` is not the object passed to `SetWindowBackend`.

### Running SDL's event loop yourself

A host that already uses SDL keeps the `SDLWindowBackend` and turns off its event
pumping:

```cpp
#include <gui_core/windows/sdl/gui_window_backend_sdl.h>

tachyon::SDLWindowBackend::Options options;
options.pumpEvents = false;
app.SetWindowBackend(std::make_unique<tachyon::SDLWindowBackend>(options));
```

The backend reads events through SDL event watches, not the event filter, so the
host's filter (`SDL_SetEventFilter`) stays in place. With `pumpEvents` on (the
default), `PollEvents` pumps SDL and empties its event queue. With it off, the GUI's
windows still get their events while the host pumps (`SDL_PollEvent`,
`SDL_PumpEvents`), and the host's events stay in the queue for the host.
`SDL_EVENT_QUIT` is then the host's call: the GUI doesn't close its windows on it.
The host pumps before `UpdateFrame`, so the frame sees that input.

If SDL's video subsystem is already running, the backend uses it and leaves it
running; otherwise it starts SDL and quits it when destroyed.
`tests/gpu/gpu_sdl_host_event_loop_test.cpp` checks both the filter and the queue.

`Options::hooks` routes SDL's memory through allocation hooks (see below). When the app
creates the backend itself it does this; a host that creates it passes the hooks here.

## Your own loop

`Run()` loops over three calls until the app quits. A host with its own main loop calls
them itself, in this order, once per frame:

```cpp
while (app.UpdateFrame())
{
    if (!app.RecordFrame()) break;   // render-to-image windows are drawn now
    // ... the host's own work ...
    app.PresentFrame();              // may wait for vsync
}
```

| Call | Does |
| --- | --- |
| `bool UpdateFrame()` | Starts the frame and pumps OS events and input. False once the app quits. |
| `Result<void> RecordFrame()` | Recovers a lost device if needed, then builds the UI (`OnGUI`, layout, input dispatch, draw recording), uploads what the build asked for (glyphs, styles, new or resized windows), and records and submits every window. An error when a lost device can't be made again. |
| `void PresentFrame()` | Presents the OS windows; may wait for vsync. Nothing to do for render-to-image windows. |

When `RecordFrame` returns, render-to-image windows are drawn and can be read back;
`tests/gpu/gpu_application_init_test.cpp` (`AHostLoopDrivesTheFrameCalls`) checks this.
The host's work between `RecordFrame` and `PresentFrame` can use them.

The app quits when its last main window closes, or when the host sets
`app.applicationQuit = true`; the next `UpdateFrame` then returns false. `Run` clears
`applicationQuit` when it starts. After the loop, call `Cleanup`.

### Errors and device loss

Calls that can fail return a `Result<T>`: true on success, otherwise `GetError()` gives
an `Error` with a `code` (`ErrorCode`) and a `message`. `Initialize`, `Run`,
`RecordFrame` and `ReadWindowPixels` return one.

When the GPU device is lost, `RecordFrame` (and so `Run`) deals with it before the next
frame:

- On a device the GUI created, it makes a new device and new window contexts. Images
  made with `CreateImage` keep their `ImageIndex` and are uploaded again from the pixels
  they were made from. The frames go on.
- On a host's device, the GUI can't make a new device, so `RecordFrame` returns an
  error with `ErrorCode::DeviceLost`. The host makes a new device and a new app.

`VulkanGraphicsContext::SimulateDeviceLoss()` makes the next frame act as if the device
were lost, for testing.

## Rendering the UI into a host image

A window opened with `ElementFlags::WindowRenderToImage` has no OS window: it lays out
and draws like any other window, into an image instead of onto the screen. Use it to
put the UI inside an engine viewport, composite it into the host's frame, or render it
without anything appearing on screen, as the GPU tests do.

```cpp
if (BeginWindow(windowID, "Panel", theme.windowStyleRef, tachyon::LayoutOverride{}.Size(400, 300),
                tachyon::ElementFlags::WindowRenderToImage))
{
    // controls as usual
}
EndWindow();
```

### Copying into the host's image

On a host's device (see above), each frame of the window can be copied into an image
the host owns:

```cpp
vk->SetWindowImageTarget(windowID, hostImage, VkExtent2D{ 400, 300 });
```

It may be set before the window exists; `VK_NULL_HANDLE` stops the copies. The
window's pixels are scaled to the given extent.

Synchronization contract for the target image, on the queue given to
`SetExternalDevice`:

1. The image needs `TRANSFER_DST` usage. The GUI's frame moves it from
   `SHADER_READ_ONLY_OPTIMAL` (`UNDEFINED` the first time) to `TRANSFER_DST_OPTIMAL`
   after the host's fragment-stage reads, copies the window into it, and leaves it in
   `SHADER_READ_ONLY_OPTIMAL`, visible to the fragment stage.
2. The host reads it in work submitted after the GUI's frame (after `RecordFrame`) and
   leaves it in `SHADER_READ_ONLY_OPTIMAL`.

Draw commands can't yet be recorded straight into the host's command buffer.

### Reading the pixels back

`ReadWindowPixels` copies the window's last frame to memory, as BGRA8 with tightly
packed rows. It works on the GUI's own device too, and waits for that frame to finish
on the GPU:

```cpp
const tachyon::IntRect rect = app.windowManager->GetWindowRect(windowID);   // pixels
std::vector<uint8_t> pixels(size_t(rect.w) * rect.h * 4);
if (tachyon::Result<void> read = app.ReadWindowPixels(windowID, pixels); !read)
    std::fprintf(stderr, "%s\n", read.GetError().message.c_str());
```

It fails with `ErrorCode::NotFound` for an unknown window and `ErrorCode::Unsupported`
when the window isn't a render-to-image window, hasn't rendered a frame yet, or the
span is too small.

### Input for render-to-image windows

The window gets no input from the OS, so the host passes it in, with positions in the
window's pixels:

```cpp
app.InjectMouseMove(windowID, pos);
app.InjectMouseButton(windowID, pos, tachyon::MouseButtons::Left, true);    // down
app.InjectMouseButton(windowID, pos, tachyon::MouseButtons::Left, false);   // up
app.InjectMouseScroll(windowID, pos, tachyon::float2{ 0.0f, 1.0f });
app.InjectMouseLeave(windowID);
app.InjectKey(windowID, key, tachyon::KeyAction::KeyDown);
app.InjectText(windowID, "text");   // UTF-8
app.InjectFocus(windowID, false);
```

Each takes an optional `KeyModifier` where it applies. The window is under the pointer
from a mouse event until `InjectMouseLeave`, and has keyboard focus from any injected
input until `InjectFocus(windowID, false)`.

## Allocation hooks

A host that tracks memory gives the library its own memory functions, before
`Initialize`:

```cpp
#include <gui_core/memory/gui_allocation_hooks.h>

tachyon::AllocationHooks hooks;
hooks.allocate   = &MyAllocate;     // void* (size_t size, size_t alignment, void* user)
hooks.reallocate = &MyReallocate;   // void* (void* ptr, size_t size, size_t alignment, void* user)
hooks.free       = &MyFree;         // void  (void* ptr, void* user)
hooks.user       = &myHeap;
app.SetAllocationHooks(hooks);
```

They receive the allocations that can be routed: Vulkan objects, VMA, vk-bootstrap
and SDL. SDL's memory goes through them only when the GUI starts SDL and SDL holds no
memory yet; SDL's memory functions are process-wide. The hooks must outlive the GUI
and be thread-safe, since drivers may call them from their own threads. SDL can keep
a few allocations past `SDL_Quit` and then keeps using the hooks, so they should live
as long as the process.

`tachyon::TrackingAllocator` (`gui_core/memory/gui_tracking_allocator.h`) is a set
of hooks that counts live allocations and reports leaks; `Hooks()` returns them.

## Headless backends

`NullGraphicsContext` (`gui_core/graphics/null/gui_null_graphics_context.h`) draws
nothing and needs no GPU: the UI is built, laid out and recorded as usual, but windows
stay empty. With `NullWindowBackend` there is no OS either, and input comes only from
the `Inject*` calls. Together they run an app with no GPU and no display, for tests:

```cpp
app.SetGraphicsBackend(std::make_unique<tachyon::NullGraphicsContext>());
app.SetWindowBackend(std::make_unique<tachyon::NullWindowBackend>());
```

Both are also the smallest examples of a graphics backend (`tachyon::GraphicsContext`)
and a window backend.
