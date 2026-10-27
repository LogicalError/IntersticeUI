# Hello world

One window with a text field and three buttons: the smallest complete TachyonUI program.
Full source: [examples/hello_world/hello_world.cpp](https://github.com/LogicalError/TachyonUI/blob/main/examples/hello_world/hello_world.cpp).

## The application class

An application derives from `tachyon::GUIApplication`. It sets up what lasts between
frames in `Initialize`, after the base class has created the window manager, the text
system and the theme:

```cpp
class HelloApp : public tachyon::GUIApplication
{
public:
    tachyon::Result<void> Initialize(const InitializeOptions& options = {}) override
    {
        if (tachyon::Result<void> result = GUIApplication::Initialize(options); !result)
            return result;
        // Windows need IDs that stay the same from frame to frame.
        windowID = idManager.CreateStaticID();
        return {};
    }
```

Most controls take their ID from their call site, so they need none. A window is
different: its position and size are kept by ID, so it gets a static ID, made once.
[IDs](../overview/ids.md) explains the kinds.

## Describing the UI

`OnGUI` runs every frame and describes the whole UI. There is no tree of widget objects
to keep in sync: the app's own members (`name`, `greeting`) are the state, and the
library keeps the rest (focus, the text cursor, the window's rect).

```cpp
void OnGUI() override
{
    if (BeginWindow(windowID, "Hello", tachyon::LayoutOverride{}.Size(360, 160)))
    {
        // A window lays its children out in wrapping rows; stack them instead.
        BeginVertical(tachyon::LayoutOverride{}.FlexGrow(1));
        Text("Hello from TachyonUI!");

        BeginLabelGroup();
        Label("Name");
        Field(name);
        EndLabelGroup();

        BeginHorizontal();
        Button("Greet",  [this] { greeting = "Hello, " + name + "!"; });
        Button("Clear",  [this] { greeting.clear(); });
        Button("Quit",   [this] { applicationQuit = true; });
        EndHorizontal();

        Text(greeting);
        EndVertical();
    }
    EndWindow();
}
```

Things to notice:

- `EndWindow` is called whether or not `BeginWindow` returned true; the result only says
  whether to build the content. Every `Begin*` works this way ([Begin and End](../overview/begin-end.md)).
- `Field(name)` edits the string in place. Its type picks the editor: a `bool` gets a
  toggle, a `float` a number field, a `float3` three of them.
- A button's callback runs after the frame, when input is handled, so it may change
  anything the next frame reads.
- Sizes such as `Size(360, 160)` are CSS px, scaled with the UI.

## The program

`main` points the app at its resources, then runs it until the last window closes:

```cpp
int main()
{
    HelloApp app;
    app.SetResourceRoot(TACHYON_RESOURCE_DIR);   // shaders, fonts and themes
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

`Initialize` and `Run` return a `Result<void>` that carries the reason when they fail,
for example a GPU without the features the library needs.
[Errors and saved state](../overview/errors-and-state.md) has the details.

---

[Examples](../examples.md) · [Controls →](controls.md)
