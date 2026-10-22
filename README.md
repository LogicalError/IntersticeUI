# IntersticeUI

A C++20 GUI library, used as the UI layer of an editor. It is a retained-mode
element tree with flexbox and grid layout, docking, multiple OS windows, a
CSS-like theming system, and a toolkit of controls (tables, trees, text editing,
node graphs, menus, and more).

The UI draws through a graphics backend and gets windows and input from a window
backend, so it isn't tied to one graphics API or platform. The current backends
are Vulkan and SDL3.

The library is two static libraries:

| Library | Contents |
| --- | --- |
| `gui_core` | Windowing, rendering backends, input, layout, text, styles, element storage |
| `gui_controls` | `GUIApplication`, controls, docking, menus, tooltips, transitions |

`demo` is a testbed application. It is not part of the library.

## Build

Windows x64, MSVC, CMake 3.22 or newer, and the Vulkan SDK (for the Vulkan backend).

```text
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

The first configure builds the third-party libraries (SDL3, skribidi, the
Vulkan backend's VMA and vk-bootstrap, and their dependencies) into
`build/third_party/install`. Later builds reuse them.

Tests:

```text
build\bin\Debug\gui_core_tests.exe              # unit tests, no GPU
cd workdirectory
..\bin\Debug\gui_gpu_tests.exe                  # real-device tests, skip without a GPU
```

## Use as a package

`cmake --install` produces a self-contained package: both libraries, their
headers, runtime resources (themes, fonts, icons, compiled shaders), the
bundled third-party libraries, and a CMake package configuration.

```text
cmake --install build --config Debug   --prefix <prefix>
cmake --install build --config Release --prefix <prefix>
```

Debug and Release install side by side into the same prefix. A consumer then
needs no IntersticeUI source or build tree:

```cmake
find_package(IntersticeUI 0.1 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE IntersticeUI::gui_controls)
# IntersticeUI_RESOURCE_DIR points at themes, fonts, icons and shaders_compiled.
```

`tests/package_consumer` is a minimal consumer that checks this against an
installed prefix. The consumer must use the same toolchain as the package.
Only the Debug and Release configurations are installed; other configurations
need `CMAKE_MAP_IMPORTED_CONFIG_<CONFIG>` in the consumer. Set
`-DINTERSTICE_INSTALL_THIRD_PARTY=OFF` to skip bundling the third-party libraries.

## Vulkan backend

When a host application also renders with Vulkan, it can share its device and
images with the GUI.

### Sharing the host's device

By default the GUI creates and owns its own `VkInstance` and `VkDevice`. A host
application that already owns a device, such as a game engine hosting this GUI
as its editor, can hand its handles to the GUI so both render on the same
device:

```cpp
GUI::GUIApplication app;
app.configureGraphicsContext = [&](GUI::GraphicsContext& context)
{
    auto& vk = static_cast<Vulkan::VulkanGraphicsContext&>(context);
    Vulkan::VulkanDeviceContext::ExternalDevice external;
    external.instance            = hostInstance;
    external.physicalDevice      = hostPhysicalDevice;
    external.device              = hostDevice;
    external.graphicsQueue       = hostGraphicsQueue;
    external.graphicsQueueFamily = hostGraphicsQueueFamily;
    external.queueMutex          = &hostQueueMutex;   // optional, see below
    vk.deviceContext.SetExternalDevice(external);
};
app.Initialize();
```

Requirements on the host device:

- `Vulkan::VulkanDeviceContext::GetDeviceRequirements()` lists the API version,
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

A host can render into an image it owns and show it in a GUI element, for example
a 3D viewport:

```cpp
auto& vk = static_cast<Vulkan::VulkanGraphicsContext&>(*graphicsContext);
GUI::ImageIndex index = vk.RegisterExternalImage(view, sampler);   // once
vk.UpdateExternalImage(index, newView, sampler);                   // after a resize

GUI::Element canvas{};
canvas.styleRef = textureStyle;        // a style whose background is ColorType::StretchedTexture
canvas.backgroundImage = index;
```

Only color styles of type `StretchedTexture` sample the image given to an element
or primitive. The view and sampler stay owned by the host and must outlive every
GUI frame that references the index.

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

## Status

Under active development. Known issues:

- Vulkan backend: validation errors are reported by the running editor; the
  descriptor related ones are not yet investigated.
- Vulkan backend: the Khronos validation layer shipped with Vulkan SDK 1.4.341.1
  crashes (a null read on one of its worker threads) in tests that present to a
  window. This is
  not a GUI defect: it reproduces on the unmodified repository and goes away with
  the 1.4.309 layer, with which all GPU tests pass, including synchronization
  validation. Point the loader at an older layer to work around it, for example
  `set VK_LAYER_PATH=C:\VulkanSDK\1.4.309.0\Bin`. A debug build of an
  application that enables validation layers is affected the same way. Crashes in
  the GPU tests print a symbolized stack trace (`tests/gpu/gpu_crash_trace.cpp`),
  and `INTERSTICE_GPU_TEST_NO_SYNC_VALIDATION=1` runs them with core validation only.
- Docking layouts can be saved and restored (`SerializeDockTree` / `ParseDockTree` in
  `gui_controls/gui_dock_node.h`); the application chooses where to store them. Floating
  windows are not part of the saved tree.
- Text editor undo and redo (Ctrl+Z, Ctrl+Shift+Z) exist; whether they behave correctly across
  focus changes and with application-level undo has not been checked.

## License

IntersticeUI is licensed under the MIT license; see [LICENSE](LICENSE). The
bundled third-party code, fonts and icons keep their own licenses, listed in
[THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt).
