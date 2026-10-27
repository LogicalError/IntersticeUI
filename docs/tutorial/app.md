# 4. The app

## The app class

The app derives from `tachyon::GUIApplication`, owns the panels, and keeps the window and
the dock layout. At the end of `my_app.h`:

<!-- snippet: examples/tutorial/my_app.h [app] -->
```cpp
class MyApp : public tachyon::GUIApplication
{
public:
    tachyon::Result<void> Initialize(const InitializeOptions& options = {}) override;
    void OnGUI() override;

    CounterPanel  counter;
    GreetingPanel greeting;

private:
    tachyon::ID            m_windowID = tachyon::ID::Invalid();
    tachyon::ID            m_dockID   = tachyon::ID::Invalid();
    tachyon::DockingLayout m_layout;
};
```

and close the namespace with `}`.

## Setting up once

`Initialize` runs once, after the library has started. In `my_app.cpp`:

<!-- snippet: examples/tutorial/my_app.cpp [app-initialize] -->
```cpp
tachyon::Result<void> MyApp::Initialize(const InitializeOptions& options)
{
    if (tachyon::Result<void> result = GUIApplication::Initialize(options); !result)
        return result;

    // The window and the dock layout keep their state between frames, so they get
    // IDs that stay the same: static IDs, made once.
    m_windowID = idManager.CreateStaticID();
    m_dockID   = idManager.CreateStaticID();

    const tachyon::IconID noIcon = tachyon::IconID::Invalid;
    const tachyon::ID left  = CreatePanel(m_layout.panels, "Counter",  noIcon, [this] { counter.Draw(*this); });
    const tachyon::ID right = CreatePanel(m_layout.panels, "Greeting", noIcon, [this] { greeting.Draw(*this); });
    m_layout.root = tachyon::MakeSplit(tachyon::DockSplitMode::LeftPinned, 240,
        tachyon::MakeLeaf({ left }),
        tachyon::MakeLeaf({ right }));
    return {};
}
```

- **Static IDs** are the exception to "no IDs": a window and a dock layout keep their
  position, size and arrangement by ID, so they need one that doesn't depend on where the
  code is.
- **`CreatePanel`** registers a dockable panel with a title and the callback that draws
  it; the callback calls the panel's `Draw`.
- **The dock tree** puts the counter in a 240 px column on the left and the greeting in
  the rest. The user can drag the tabs to rearrange them, or out of the window to float a
  panel in its own window.

## Every frame

<!-- snippet: examples/tutorial/my_app.cpp [app-ongui] -->
```cpp
void MyApp::OnGUI()
{
    if (BeginWindow(m_windowID, "My app", theme.mainWindowStyleRef,
                    tachyon::LayoutOverride::FixedSize(640, 360).Centered()))
    {
        BeginDockingContainer(m_dockID, &m_layout, tachyon::LayoutOverride::Auto());
        EndDockingContainer();
        EmitFloatingDockWindows();
    }
    EndWindow();
}
```

`OnGUI` runs every frame. The docking container draws the visible panels, each through
its callback; `EmitFloatingDockWindows` draws the panels the user floated. Close the
namespace with `}`.

## main

In `main.cpp`, after `#include "my_app.h"` and `#include <cstdio>`:

<!-- snippet: examples/tutorial/main.cpp [main] -->
```cpp
int main()
{
    my_app::MyApp app;
    tachyon::Result<void> result = app.Initialize();
    if (result)
        result = app.Run();   // until the last window closes
    app.Cleanup();
    if (!result)
    {
        std::fprintf(stderr, "%s\n", result.GetError().message.c_str());
        return 1;
    }
    return 0;
}
```

`Initialize` and `Run` return a `Result<void>` that says why when they fail, for example
on a GPU without the features the library needs.

## Run it

Build as in [step 1](setup.md), then run `build\Debug\my_app.exe` on Windows, or
`build/Debug/my_app` on Linux and macOS. It runs from any folder.

## Where next

- [Controls](../controls.md): every control, with its options and its CSS class.
- [CSS and theming](../css.md): your own style file, and switching between dark and light.
- [Overview](../overview.md): how IDs, Begin / End and the frame work in more depth.
- [Integration](../integration.md): your own Vulkan device, window backend or main loop.

---

[← 3. Fields](fields.md) · [Tutorial](../tutorial.md)
