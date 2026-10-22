// Docking: a few panels in a dock layout. Drag a tab to move a panel, onto
// another leaf to dock it there, or outside the window to float it.
#include <gui_controls/gui_application.h>
#include <gui_controls/gui_dock_node.h>

#include <string>

class DockingApp : public GUI::GUIApplication
{
public:
    bool Initialize(const InitializeOptions& options = {}) override
    {
        if (!GUIApplication::Initialize(options))
            return false;
        windowID = idManager.CreateStaticID();
        dockID   = idManager.CreateStaticID();

        // Panels are created once; each draws its content in its callback.
        const GUI::IconID noIcon = GUI::IconID::Invalid;
        const GUI::ID scene   = CreatePanel(layout.panels, "Scene",   noIcon, [this] { ScenePanel(); });
        const GUI::ID details = CreatePanel(layout.panels, "Details", noIcon, [this] { DetailsPanel(); });
        const GUI::ID log     = CreatePanel(layout.panels, "Log",     noIcon, [this] { Text("Nothing logged yet."); });
        const GUI::ID help    = CreatePanel(layout.panels, "Help",    noIcon, [this] { Text("Drag the tabs around."); });

        // Left column of 220 px, the rest split over a 150 px bottom row.
        using GUI::DockSplitMode;
        layout.root = GUI::MakeSplit(DockSplitMode::LeftPinned, 220,
            GUI::MakeLeaf({ scene }),
            GUI::MakeSplit(DockSplitMode::BottomPinned, 150,
                GUI::MakeLeaf({ details, help }),
                GUI::MakeLeaf({ log })));
        return true;
    }

    void OnGUI() override
    {
        if (BeginWindow(windowID, "Docking", GUI::LayoutOverride::FixedSize(900, 600).Centered(), theme.mainWindowStyleRef))
        {
            BeginDockingContainer(dockID, &layout, GUI::LayoutOverride::Auto());
            EndDockingContainer();
            // Panels dragged out of the window get their own OS windows, owned by this one.
            EmitFloatingDockWindows();
            EndWindow();
        }
    }

private:
    void ScenePanel()
    {
        for (int i = 0; i < 4; ++i)
            Button("Object " + std::to_string(i), [this, i] { selected = i; }, GUI::LayoutOverride{}.NoGrow());
    }

    void DetailsPanel()
    {
        Text("Selected object " + std::to_string(selected));
        BeginLabelGroup();
        DragLabel("Scale");
        Field(scale);
        EndLabelGroup();
    }

    GUI::ID            windowID = GUI::ID::Invalid();
    GUI::ID            dockID   = GUI::ID::Invalid();
    GUI::DockingLayout layout;
    int                selected = 0;
    float              scale    = 1.0f;
};

int main()
{
    DockingApp app;
    app.SetResourceRoot(INTERSTICE_RESOURCE_DIR);
    if (app.Initialize())
        app.Run();
    app.Cleanup();
    return 0;
}
