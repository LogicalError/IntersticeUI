# The application and the frame

## The application

An application derives from `tachyon::GUIApplication` and implements `OnGUI`:

```cpp
#include <gui_controls/gui_application.h>

class MyApp : public tachyon::GUIApplication
{
public:
    tachyon::Result<void> Initialize(const InitializeOptions& options = {}) override
    {
        if (tachyon::Result<void> result = GUIApplication::Initialize(options); !result)
            return result;
        windowID = idManager.CreateStaticID();
        return {};
    }

    void OnGUI() override
    {
        if (BeginWindow(windowID, "My app", tachyon::LayoutOverride{}.Size(360, 160)))
        {
            Text("Hello");
            Button("Quit", [this] { applicationQuit = true; });
        }
        EndWindow();
    }

private:
    tachyon::ID windowID = tachyon::ID::Invalid();
};

int main()
{
    MyApp app;
    tachyon::Result<void> result = app.Initialize();
    if (result)
        result = app.Run();   // until the last window closes
    app.Cleanup();
    return result ? 0 : 1;
}
```

The program needs no files next to it: the shaders, the default themes and the fonts are
built into the library. `SetResourceRoot` (for your own themes and fonts),
`SetGraphicsBackend`, `SetWindowBackend`, `SetClipboard` and `SetStyleFiles` are optional
and called before `Initialize`. `Cleanup` releases everything
`Initialize` made.


## The frame

Each frame runs these steps, in this order:

1. **Build.** The library calls `OnGUI`. Each control call adds an element to this
   frame's tree; nothing is drawn yet. A control reads its state (hovered, focused,
   pressed, checked) from what the library kept since the last frame.
2. **Layout.** The tree is laid out with the styles from the CSS files and the
   `LayoutOverride` passed to each call. OS windows are created, moved and resized to
   match their window elements.
3. **Input.** The input that arrived since the last frame is hit-tested against the new
   layout and sent to the elements. Event handlers and control callbacks run here,
   after `OnGUI` has returned.
4. **Render.** The tree is drawn into each window.

So a click is seen in two places: the button's callback runs in the input step of the
frame that received the click, and the UI built in the next frame sees the result.

Because callbacks run after `OnGUI` returns, what they capture must outlive the frame.
Members of the application, user-owned objects and copies are fine; references to
locals of `OnGUI` are not.

```cpp
// Fine: captures `this` and a copy of the index.
Button("Select", [this, i] { selected = i; });

// Wrong: `count` is a local of OnGUI and is gone when the callback runs.
int count = 0;
Button("Add", [&count] { ++count; });
```

### Run and the split frame calls

`Run()` loops until `applicationQuit` is set or the last window closes:

```cpp
while (app.UpdateFrame())
{
    if (!app.RecordFrame()) break;   // build, layout, input, render; submitted to the GPU
    // ... the host's own work ...
    app.PresentFrame();              // may wait for vsync
}
```

`UpdateFrame` pumps OS events and input and returns false once the app quits.
`RecordFrame` runs the frame described above and submits every window; it returns an
error only when a lost GPU device can't be made again (with a host's device).
`PresentFrame` presents the OS windows. A host with its own main loop calls these
three itself; see [Integration](../integration/frame-loop.md).

---

[Overview](../overview.md) · [IDs →](ids.md)
