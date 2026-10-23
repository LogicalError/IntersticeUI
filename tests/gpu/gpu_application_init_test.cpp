// GUIApplication::Initialize on a real device: a fresh app works without the
// setup steps the demo used to do by hand (#40).

#include <gtest/gtest.h>

#include <gui_controls/gui_application.h>
#include <gui_controls/controls_toolbar_dock.h>
#include <gui_core/memory/gui_tracking_allocator.h>

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_stdinc.h>

#include <filesystem>

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

    // Shaders, fonts and the theme load from the resource root, not the working directory.
    TEST(GpuApplicationInit, ResourceRootLoadsFromAnyWorkingDirectory)
    {
        const std::filesystem::path resources = std::filesystem::current_path();
        {
            EmptyApp probe;
            const bool available = probe.Initialize();
            probe.Cleanup();
            if (!available)
                GTEST_SKIP() << "no Vulkan device or window system available";
        }

        std::filesystem::current_path(std::filesystem::temp_directory_path());
        EmptyApp app;
        app.SetResourceRoot(resources.string());
        const bool initialized = app.Initialize();
        std::filesystem::current_path(resources);

        EXPECT_TRUE(initialized);
        EXPECT_GT(app.styleManager.LayoutStyleCount(), 0u) << "the theme is loaded";
        app.Cleanup();
    }

    class OneWindowApp : public GUI::GUIApplication
    {
    public:
        GUI::ID windowID = GUI::ID::Invalid();

        void OnGUI() override
        {
            if (BeginWindow(windowID, "Allocations", GUI::LayoutOverride{}.Size(200, 100)))
            {
                Label("Counting allocations");
                EndWindow();
            }
        }
    };

    // Everything allocated through the host's hooks (Vulkan objects, vk-bootstrap,
    // VMA, SDL) is freed again by Cleanup.
    TEST(GpuApplicationInit, AllocationHooksSeeEverythingFreedByCleanup)
    {
        GUI::TrackingAllocator tracker;
        // SDL's Windows hotplug thread can leave allocations behind across SDL_Quit;
        // then SDL rightly stays on its own allocator. Only a fresh SDL is routed.
        const bool sdlRoutable = SDL_WasInit(0) == 0 && SDL_GetNumAllocations() == 0;
        {
            OneWindowApp app;
            app.SetAllocationHooks(tracker.Hooks());
            if (!app.Initialize())
            {
                app.Cleanup();
                GTEST_SKIP() << "no Vulkan device or window system available";
            }
            SDL_malloc_func current = nullptr, original = nullptr;
            SDL_GetMemoryFunctions(&current, nullptr, nullptr, nullptr);
            SDL_GetOriginalMemoryFunctions(&original, nullptr, nullptr, nullptr);
            if (sdlRoutable)
                EXPECT_NE(current, original) << "SDL allocates through the hooks";

            app.windowID = app.idManager.CreateStaticID();
            for (int i = 0; i < 3; ++i)
                app.OnFrame();
            EXPECT_GT(tracker.LiveAllocations(), 0u);
            app.Cleanup();
        }
        EXPECT_GT(tracker.TotalAllocations(), 0u);
        EXPECT_EQ(tracker.LiveAllocations(), 0u) << tracker.LiveBytes() << " bytes still live";
        tracker.ReportLeaks();
    }

    class FloatingToolbarApp : public GUI::GUIApplication
    {
    public:
        std::vector<GUI::FloatingToolbarItem> floating;
        GUI::FloatingToolbarOptions           options;
        bool                                  closeWindow = false;

        void OnGUI() override
        {
            if (closeWindow && !floating.empty())
                windowManager->RequestClose(floating.front().windowID);   // as the OS does on Alt+F4
            closeWindow = false;
            EmitFloatingToolbarWindows(floating, &options);
        }
    };

    // Closing a floating toolbar's window docks the item back on its home side
    // instead of losing it.
    TEST(GpuApplicationInit, ClosingAFloatingToolbarWindowDocksItBack)
    {
        FloatingToolbarApp app;
        if (!app.Initialize())
        {
            app.Cleanup();
            GTEST_SKIP() << "no Vulkan device or window system available";
        }
        const GUI::ID windowID = app.idManager.CreateStaticID();
        const GUI::ID buttonID = app.idManager.CreateStaticID();
        GUI::FloatingToolbarItem fw;
        fw.windowID        = windowID;
        fw.spawnPosition   = { 200, 200 };
        fw.homeSide        = GUI::ToolbarSide::Left;
        fw.item.id         = buttonID;
        fw.item.renderBody = [&] { app.Button(buttonID, "X", [] {}); };
        app.floating.push_back(std::move(fw));

        int             dockBacks  = 0;
        GUI::ToolbarSide dockedSide = GUI::ToolbarSide::Top;
        app.options.onDockBack = [&](GUI::ID id, GUI::ToolbarSide side)
        {
            ++dockBacks;
            dockedSide = side;
            app.idManager.ReleaseStaticID(id);
            app.floating.clear();
        };

        app.OnFrame();
        app.OnFrame();
        ASSERT_EQ(app.floating.size(), 1u) << "the window is open";
        app.closeWindow = true;
        app.OnFrame();

        EXPECT_EQ(dockBacks, 1);
        EXPECT_EQ(dockedSide, GUI::ToolbarSide::Left);
        EXPECT_TRUE(app.floating.empty());
        app.idManager.ReleaseStaticID(buttonID);
        app.Cleanup();
    }
}
