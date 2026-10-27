// Docking: a few panels in a dock layout. Drag a tab to move a panel, onto
// another leaf to dock it there, or outside the window to float it.
#include <gui_controls/gui_application.h>

#include <cstdio>
#include <gui_controls/gui_dock_node.h>

#include <string>

class DockingApp : public tachyon::GUIApplication
{
public:
    tachyon::Result<void> Initialize(const InitializeOptions& options = {}) override
    {
        if (tachyon::Result<void> result = GUIApplication::Initialize(options); !result)
            return result;
        windowID = idManager.CreateStaticID();
        dockID   = idManager.CreateStaticID();

        // Panels are created once; each draws its content in its callback.
        const tachyon::IconID noIcon = tachyon::IconID::Invalid;
        const tachyon::ID scene   = CreatePanel(layout.panels, "Scene",   noIcon, [this] { ScenePanel(); });
        const tachyon::ID details = CreatePanel(layout.panels, "Details", noIcon, [this] { DetailsPanel(); });
        const tachyon::ID log     = CreatePanel(layout.panels, "Log",     noIcon, [this] { Text("Nothing logged yet."); });
        const tachyon::ID help    = CreatePanel(layout.panels, "Help",    noIcon, [this] { Text("Drag the tabs around."); });

        // Left column of 220 px, the rest split over a 150 px bottom row.
        using tachyon::DockSplitMode;
        layout.root = tachyon::MakeSplit(DockSplitMode::LeftPinned, 220,
            tachyon::MakeLeaf({ scene }),
            tachyon::MakeSplit(DockSplitMode::BottomPinned, 150,
                tachyon::MakeLeaf({ details, help }),
                tachyon::MakeLeaf({ log })));
        return {};
    }

    void OnGUI() override
    {
        if (BeginWindow(windowID, "Docking", theme.mainWindowStyleRef, tachyon::LayoutOverride::FixedSize(900, 600).Centered()))
        {
            BeginDockingContainer(dockID, &layout, tachyon::LayoutOverride::Auto());
            EndDockingContainer();
            // Panels dragged out of the window get their own OS windows, owned by this one.
            EmitFloatingDockWindows();
        }
        EndWindow();
    }

private:
    void ScenePanel()
    {
        for (int i = 0; i < 4; ++i)
            Button("Object " + std::to_string(i), [this, i] { selected = i; }, tachyon::LayoutOverride{}.NoGrow());
    }

    void DetailsPanel()
    {
        Text("Selected object " + std::to_string(selected));
        BeginLabelGroup();
        DragLabel("Scale");
        Field(scale);
        EndLabelGroup();
    }

    tachyon::ID            windowID = tachyon::ID::Invalid();
    tachyon::ID            dockID   = tachyon::ID::Invalid();
    tachyon::DockingLayout layout;
    int                selected = 0;
    float              scale    = 1.0f;
};

int main()
{
    DockingApp app;
    app.SetResourceRoot(TACHYON_RESOURCE_DIR);
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
