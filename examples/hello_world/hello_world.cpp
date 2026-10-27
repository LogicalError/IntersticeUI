// Hello world: one window with a text field and a few buttons.
#include <gui_controls/gui_application.h>

#include <cstdio>

#include <string>

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

    // Called every frame: describe the whole UI, the library keeps the state.
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

private:
    tachyon::ID windowID = tachyon::ID::Invalid();
    std::string name     = "world";
    std::string greeting;
};

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
