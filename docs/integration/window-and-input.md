# Your own window and input

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

## Writing a window backend

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

## Running SDL's event loop yourself

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

---

[← Using your own Vulkan device](vulkan-device.md) · [Integration](../integration.md) · [Your own loop →](frame-loop.md)
