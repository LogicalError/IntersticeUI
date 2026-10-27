# 2. Buttons

## Panels as classes

Each panel is a small class: the values it shows, and a `Draw` that builds its controls.
Every control is a member function of `tachyon::GUIApplication`, so `Draw` takes the app.
In `my_app.h`, after `#include <gui_controls/gui_application.h>`,
`#include <gui_controls/gui_dock_node.h>` and `#include <string>`:

```cpp
namespace my_app
{
```

<!-- snippet: examples/tutorial/my_app.h [panels] -->
```cpp
// A count, and buttons that change it.
class CounterPanel
{
public:
    void Draw(tachyon::GUIApplication& ui);

    int count = 0;
};

// A few fields, and a greeting made from them.
class GreetingPanel
{
public:
    void Draw(tachyon::GUIApplication& ui);

    std::string name    = "World";
    int         times   = 1;
    bool        excited = false;
};
```

This step draws the counter panel; the next one draws the greeting panel.

## The counter panel

In `my_app.cpp`:

```cpp
#include "my_app.h"

namespace my_app
{
    void CounterPanel::Draw(tachyon::GUIApplication& ui)
    {
```

<!-- snippet: examples/tutorial/my_app.cpp [counter] -->
```cpp
ui.Text("Count: " + std::to_string(count));

ui.BeginHorizontal();
ui.Button("Add one", [this] { ++count; });
ui.Button("Reset",   [this] { count = 0; });
ui.EndHorizontal();
```

and close `Draw` with `}`. What this shows:

- **The whole panel, every frame.** `Draw` runs each frame and describes what the panel
  shows now; the text always reads the current count.
- **No IDs to manage.** Each control takes its ID from its call site, the file and line
  of the call, which stays the same from frame to frame.
- **`BeginHorizontal` / `EndHorizontal`** put the buttons in a row. Every `Begin*` has its
  `End*`, called whether or not the `Begin*` returned true.
- **A button's callback** runs when the button is clicked, after the controls are built,
  so it can change the panel's values freely; the next frame shows the result.

`[this]` captures the panel. That's safe because the panel lives as long as the app,
which owns it (step 4).

---

[← 1. Set up the project](setup.md) · [Tutorial](../tutorial.md) · [3. Fields →](fields.md)
