# Rendering the UI into a host image

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

## Copying into the host's image

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

## Reading the pixels back

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

## Input for render-to-image windows

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

---

[← Your own loop](frame-loop.md) · [Integration](../integration.md) · [Allocation hooks and headless backends →](allocation-and-headless.md)
