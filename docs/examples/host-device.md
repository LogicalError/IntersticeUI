# Host device

A "host" application, think of a game engine, creates its own Vulkan instance and device,
hands them to the GUI, renders into an image of its own every frame, and the GUI shows
that image in a window.
Full source: [examples/host_device/host_device.cpp](https://github.com/LogicalError/TachyonUI/blob/main/examples/host_device/host_device.cpp).

The example uses vk-bootstrap to create the device in a few lines. A real host has its own
code for that; what matters is what it enables and hands over.

## A device the GUI can use

The host enables everything the GUI needs, which
`VulkanDeviceContext::GetDeviceRequirements()` lists. Its instance also needs the
extensions SDL uses to create window surfaces:

```cpp
// Enable everything the GUI needs on the device.
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

## Handing it over

The host describes its handles in an `ExternalDevice`. Both sides submit to the same
queue, so the host also passes the mutex it locks around its own submissions:

```cpp
tachyon::vulkan::VulkanDeviceContext::ExternalDevice external;
external.instance            = instance.instance;
external.physicalDevice      = physicalDevice.physical_device;
external.device              = device.device;
external.graphicsQueue       = queue;
external.graphicsQueueFamily = queueFamily;
external.queueMutex          = &queueMutex;
```

The app gives the Vulkan backend that device before `Initialize`:

```cpp
auto backend = std::make_unique<tachyon::vulkan::VulkanGraphicsContext>();
if (this->host.CreateDevice())
    backend->SetExternalDevice(this->host.Describe());
else
    std::printf("no Vulkan 1.3 device with the features the GUI needs\n");
vk = backend.get();
SetGraphicsBackend(std::move(backend));
```

## Showing the host's image

After `Initialize`, the host's image view and sampler are registered once. The GUI
samples them; the host keeps owning them:

```cpp
// The GUI samples the host's view and sampler; the host keeps owning them.
hostImage = vk->RegisterExternalImage(host.view, host.sampler);
```

Each frame, the host submits its rendering while the UI is built, before the GUI
submits its own, and `Image` shows the result:

```cpp
void OnGUI() override
{
    // The host submits its frame while the UI is built, before the GUI submits.
    const float t = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
    host.Render(0.5f + 0.5f * std::sin(t), 0.5f + 0.5f * std::sin(t + 2.1f), 0.5f + 0.5f * std::sin(t + 4.2f));

    if (BeginWindow(windowID, "Host device", tachyon::LayoutOverride{}.Size(320, 340)))
    {
        BeginVertical(tachyon::LayoutOverride{}.FlexGrow(1));
        Text("Rendered by the host:");
        Image(hostImage, tachyon::uint2{ Host::kImageSize.width, Host::kImageSize.height });
        EndVertical();
    }
    EndWindow();
}
```

`Host::Render` follows the GUI's synchronization contract: it waits for the GUI's
fragment-stage reads before writing the image, and leaves it in
`SHADER_READ_ONLY_OPTIMAL`, readable by the fragment stage.

## Shutting down

`Cleanup` destroys only what the GUI created; the host's device survives it, and the
host destroys it afterwards:

```cpp
app.Cleanup();   // the GUI destroys only what it created; the device stays
```

[Using your own Vulkan device](../integration/vulkan-device.md) has the full requirements,
the ownership rules and the synchronization contract.

---

[← Custom theme](custom-theme.md) · [Examples](../examples.md) · [Showcase →](showcase.md)
