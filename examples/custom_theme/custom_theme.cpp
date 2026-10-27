// Custom theme: the app adds its own CSS file next to the library's styles,
// uses its classes by name, and switches between the dark and light palettes.
#include <gui_controls/gui_application.h>

#include <cstdio>

#include <string_view>

class ThemeApp : public tachyon::GUIApplication
{
public:
    tachyon::Result<void> Initialize(const InitializeOptions& options = {}) override
    {
        // The library's rules first, then the app's (relative paths are next to the palette).
        const std::string_view styleFiles[] = { "styles.css", TACHYON_EXAMPLE_DIR "/custom_theme.css" };
        SetStyleFiles(styleFiles);
        if (tachyon::Result<void> result = GUIApplication::Initialize(options); !result)
            return result;
        windowID = idManager.CreateStaticID();
        return {};
    }

    void OnGUI() override
    {
        // The scope ends the window when it leaves, open or not.
        auto window = Window(windowID, "Custom theme", tachyon::LayoutOverride{}.Size(400, 220));
        if (!window)
            return;
        BeginVertical(tachyon::LayoutOverride{}.FlexGrow(1));

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
    }

private:
    tachyon::ID windowID = tachyon::ID::Invalid();
};

int main()
{
    ThemeApp app;
    tachyon::Result<void> result = app.Initialize();
    if (result)
        result = app.Run();
    app.Cleanup();
    if (!result)
    {
        std::fprintf(stderr, "%s\n", result.GetError().message.c_str());
        return 1;
    }
    return 0;
}
