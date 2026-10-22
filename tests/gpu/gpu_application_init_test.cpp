// GUIApplication::Initialize on a real device: a fresh app works without the
// setup steps the demo used to do by hand (#40).

#include <gtest/gtest.h>

#include <gui_controls/gui_application.h>

namespace
{
    class EmptyApp : public GUI::GUIApplication
    {
    public:
        void OnGUI() override {}
    };

    TEST(GpuApplicationInit, InitializeSetsUpAWorkingApp)
    {
        EmptyApp app;
        if (!app.Initialize())
        {
            app.Cleanup();
            GTEST_SKIP() << "no Vulkan device or window system available";
        }
        EXPECT_GT(app.styleManager.LayoutStyleCount(), 0u) << "the theme is loaded";
        EXPECT_TRUE(static_cast<bool>(app.onGui)) << "windows repaint during an OS resize";
        EXPECT_TRUE(app.inputManager->PopGlobalHandler()) << "command shortcuts are routed";
        app.Cleanup();
    }

    TEST(GpuApplicationInit, OptionsTurnTheDefaultsOff)
    {
        EmptyApp app;
        GUI::GUIApplication::InitializeOptions options;
        options.loadStyles             = false;
        options.commandShortcuts       = false;
        options.repaintWhileResizing   = false;
        options.closeMenusOnWindowMove = false;
        if (!app.Initialize(options))
        {
            app.Cleanup();
            GTEST_SKIP() << "no Vulkan device or window system available";
        }
        EXPECT_EQ(app.styleManager.LayoutStyleCount(), 0u);
        EXPECT_FALSE(static_cast<bool>(app.onGui));
        EXPECT_FALSE(app.inputManager->PopGlobalHandler());
        app.Cleanup();
    }
}
