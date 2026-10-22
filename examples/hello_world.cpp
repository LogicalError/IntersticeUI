// Hello world: one window with a text field and a few buttons.
#include <gui_controls/gui_application.h>

#include <string>

class HelloApp : public GUI::GUIApplication
{
public:
    bool Initialize(const InitializeOptions& options = {}) override
    {
        if (!GUIApplication::Initialize(options))
            return false;
        // Windows need IDs that stay the same from frame to frame.
        windowID = idManager.CreateStaticID();
        return true;
    }

    // Called every frame: describe the whole UI, the library keeps the state.
    void OnGUI() override
    {
        if (BeginWindow(windowID, "Hello", GUI::LayoutOverride{}.Size(360, 160)))
        {
            // A window lays its children out in wrapping rows; stack them instead.
            BeginVertical(GUI::LayoutOverride{}.FlexGrow(1));
            Text("Hello from IntersticeUI!");

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
            EndWindow();
        }
    }

private:
    GUI::ID     windowID = GUI::ID::Invalid();
    std::string name     = "world";
    std::string greeting;
};

int main()
{
    HelloApp app;
    app.SetResourceRoot(INTERSTICE_RESOURCE_DIR);   // shaders, fonts and themes
    if (app.Initialize())
        app.Run();   // until the last window closes
    app.Cleanup();
    return 0;
}
