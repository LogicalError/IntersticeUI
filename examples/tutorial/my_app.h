// The tutorial's app: two docked panels, one with buttons and one with fields.
// docs/tutorial.md walks through it; its code blocks are the [regions] marked below and in
// my_app.cpp and main.cpp, and tests/tutorial_snippets_test.cpp keeps the two the same.
#pragma once

#include <gui_controls/gui_application.h>
#include <gui_controls/gui_dock_node.h>

#include <string>

namespace my_app
{
    // [panels]
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
    // [panels]

    // [app]
    class MyApp : public tachyon::GUIApplication
    {
    public:
        tachyon::Result<void> Initialize(const InitializeOptions& options = {}) override;
        void OnGUI() override;

        CounterPanel  counter;
        GreetingPanel greeting;

    private:
        tachyon::ID            m_windowID = tachyon::ID::Invalid();
        tachyon::ID            m_dockID   = tachyon::ID::Invalid();
        tachyon::DockingLayout m_layout;
    };
    // [app]
}
