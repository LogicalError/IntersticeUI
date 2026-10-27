#include "my_app.h"

namespace my_app
{
    void CounterPanel::Draw(tachyon::GUIApplication& ui)
    {
        // [counter]
        ui.Text("Count: " + std::to_string(count));

        ui.BeginHorizontal();
        ui.Button("Add one", [this] { ++count; });
        ui.Button("Reset",   [this] { count = 0; });
        ui.EndHorizontal();
        // [counter]
    }

    void GreetingPanel::Draw(tachyon::GUIApplication& ui)
    {
        // [greeting-fields]
        ui.BeginLabelGroup();

        ui.Label("Name");
        ui.Field(name);

        ui.Label("Times");
        ui.Field(times, tachyon::NumericFieldConfig<int>{ .minVal = 1, .maxVal = 5 });

        ui.Label("Excited");
        ui.Field(excited, excited ? "Yes" : "No");

        ui.EndLabelGroup();
        // [greeting-fields]

        // [greeting-text]
        const std::string text = "Hello, " + name + (excited ? "!" : ".");
        for (int i = 0; i < times; ++i)
            ui.Text(text);
        // [greeting-text]
    }

    // [app-initialize]
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
    // [app-initialize]

    // [app-ongui]
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
    // [app-ongui]
}
