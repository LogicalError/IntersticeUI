// Custom theme: the app adds its own CSS file next to the library's styles,
// uses its classes by name, and switches between the dark and light palettes.
#include <gui_controls/gui_application.h>

#include <string_view>

class ThemeApp : public interstice::GUIApplication
{
public:
    bool Initialize(const InitializeOptions& options = {}) override
    {
        // The library's rules first, then the app's (relative paths are next to the palette).
        const std::string_view styleFiles[] = { "styles.css", INTERSTICE_EXAMPLES_DIR "/custom_theme.css" };
        SetStyleFiles(styleFiles);
        if (!GUIApplication::Initialize(options))
            return false;
        windowID = idManager.CreateStaticID();
        return true;
    }

    void OnGUI() override
    {
        if (!BeginWindow(windowID, "Custom theme", interstice::LayoutOverride{}.Size(400, 220)))
            return;
        BeginVertical(interstice::LayoutOverride{}.FlexGrow(1));

        // Look the classes up every frame: a theme reload registers new styles.
        Text("My themed app", theme.GetStyle("banner"));

        BeginHorizontal();
        Button("Dark",  [this] { RequestReloadTheme("themes/dark.css",  GetGlobalFontScale()); });
        Button("Light", [this] { RequestReloadTheme("themes/light.css", GetGlobalFontScale()); });
        EndHorizontal();

        BeginHorizontal();
        Button("Smaller text", [this] { RequestReloadTheme(GetThemePath(), GetGlobalFontScale() * 0.9f); });
        Button("Larger text",  [this] { RequestReloadTheme(GetThemePath(), GetGlobalFontScale() * 1.1f); });
        EndHorizontal();

        Button("Careful!", [] {}, theme.GetStyle("warning-button"));
        EndVertical();
        EndWindow();
    }

private:
    interstice::ID windowID = interstice::ID::Invalid();
};

int main()
{
    ThemeApp app;
    app.SetResourceRoot(INTERSTICE_RESOURCE_DIR);
    if (app.Initialize())
        app.Run();
    app.Cleanup();
    return 0;
}
