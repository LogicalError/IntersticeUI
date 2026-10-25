// Controls overview: the common controls, laid out as label/value rows.
#include <gui_controls/gui_application.h>

#include <cstdio>

#include <string>

class ControlsApp : public interstice::GUIApplication
{
public:
    interstice::Result<void> Initialize(const InitializeOptions& options = {}) override
    {
        if (interstice::Result<void> result = GUIApplication::Initialize(options); !result)
            return result;
        windowID = idManager.CreateStaticID();
        menuID   = idManager.CreateStaticID();
        return {};
    }

    void OnGUI() override
    {
        // The scope ends the window when it leaves, open or not.
        auto window = Window(windowID, "Controls", interstice::LayoutOverride{}.Size(480, 560));
        if (!window)
            return;
        BeginVertical(interstice::LayoutOverride{}.FlexGrow(1));

        // A label group lines up Label + control pairs as rows with a shared,
        // resizable label column. DragLabel also drags the value it labels.
        BeginLabelGroup(140);

        Label("Button");
        Button("Click me", [this] { ++clicks; });

        Label("Clicks");
        Text(std::to_string(clicks));

        Label("Checkbox");
        Field(enabled, enabled ? "On" : "Off");

        Label("Toggles");
        BeginHorizontal();
        Toggle(bold, "B");
        Toggle(italic, "I");
        Toggle(underline, "U");
        EndHorizontal();

        Label("Text");
        Field(text);

        Label("Multiline");
        Field(notes, interstice::TextEditBehavior::ContinuousUpdate | interstice::TextEditBehavior::Multiline);

        DragLabel("Float");
        Field(floatValue);

        DragLabel("Int [0, 10]");
        Field(intValue, interstice::NumericFieldConfig<int>{ .minVal = 0, .maxVal = 10 });

        DragLabel("Vector");
        Field(position);

        Label("Slider");
        SliderField(volume, 0.0f, 100.0f, "Volume");

        Label("Color");
        ColorField(color);

        Label("Menu");
        if (BeginMenu(menuID, "Choose...", interstice::LayoutOverride{}.NoGrow().NoShrink()))
        {
            for (const char* item : { "Apple", "Banana", "Cherry" })
                MenuItem(item, [this, item] { choice = item; });
        }
        EndMenu();

        Label("Chosen");
        Text(choice);

        EndLabelGroup();

        // Tabs select which content to show; the content is up to the app.
        BeginTabBar(idManager.AllocateDynamicID("tabs"), &selectedTab, nullptr);
        Tab("First");
        Tab("Second");
        EndTabBar();
        Text(selectedTab == 0 ? "Content of the first tab" : "Content of the second tab");

        EndVertical();
    }

private:
    interstice::ID     windowID = interstice::ID::Invalid();
    interstice::ID     menuID   = interstice::ID::Invalid();
    int         clicks   = 0;
    bool        enabled  = true;
    bool        bold = false, italic = false, underline = false;
    std::string text     = "Edit me";
    std::string notes    = "Line one\nLine two";
    float       floatValue = 1.5f;
    int         intValue   = 5;
    interstice::float3 position { 1.0f, 2.0f, 3.0f };
    float       volume   = 75.0f;
    interstice::color4f color   { 0.2f, 0.6f, 1.0f, 1.0f };
    std::string choice   = "(none)";
    int         selectedTab = 0;
};

int main()
{
    ControlsApp app;
    app.SetResourceRoot(INTERSTICE_RESOURCE_DIR);
    interstice::Result<void> result = app.Initialize();
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
