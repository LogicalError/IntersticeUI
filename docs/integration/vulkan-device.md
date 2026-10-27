# Using your own Vulkan device

By default the GUI creates and owns its own `VkInstance` and `VkDevice`. A host that
already owns a device hands its handles to the GUI so both render on the same device.
`examples/host_device/host_device.cpp` is a complete program.

## Device requirements

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

## Handing over the device

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

## Showing a host-rendered image

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

---

[← Adding the library to a build](build.md) · [Integration](../integration.md) · [Your own window and input →](window-and-input.md)
