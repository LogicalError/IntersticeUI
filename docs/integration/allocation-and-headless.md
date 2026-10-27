# Allocation hooks and headless backends

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

---

[← Rendering the UI into a host image](render-to-image.md) · [Integration](../integration.md)
