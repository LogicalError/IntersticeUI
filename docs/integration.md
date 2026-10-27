# Integration

How to put TachyonUI into a program that has its own build, Vulkan device, windows
or main loop, such as a game engine hosting it as its editor. Each part is optional: a
program can take the defaults for everything and only call `Initialize` and `Run`, as
in the README's hello world.

The frame model (what `OnGUI` is, when it runs, how IDs stay stable) is described in
the [Overview](overview.md).

This guide has these pages:

| Page | What it covers |
| --- | --- |
| [Adding the library to a build](integration/build.md) | As a subproject or an installed package, and where the resources come from. |
| [Using your own Vulkan device](integration/vulkan-device.md) | Device requirements, handing over the device, and showing a host-rendered image. |
| [Your own window and input](integration/window-and-input.md) | Writing a window backend, or running SDL's event loop yourself. |
| [Your own loop](integration/frame-loop.md) | Driving the frame from the host's main loop, errors and device loss. |
| [Rendering the UI into a host image](integration/render-to-image.md) | Windows without an OS window: copying into the host's image, reading pixels back, injecting input. |
| [Allocation hooks and headless backends](integration/allocation-and-headless.md) | Routing allocations through your allocator, and running without a GPU or window. |

<!--! Page tree for the API reference.
\subpage md_docs_2integration_2build
\subpage md_docs_2integration_2vulkan-device
\subpage md_docs_2integration_2window-and-input
\subpage md_docs_2integration_2frame-loop
\subpage md_docs_2integration_2render-to-image
\subpage md_docs_2integration_2allocation-and-headless
-->
