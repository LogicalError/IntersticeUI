# Your own loop

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

## Errors and device loss

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

---

[← Your own window and input](window-and-input.md) · [Integration](../integration.md) · [Rendering the UI into a host image →](render-to-image.md)
