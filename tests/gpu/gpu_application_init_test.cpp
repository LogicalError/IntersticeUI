// GUIApplication::Initialize on a real device: a fresh app works without the
// setup steps the demo used to do by hand (#40).

#include <gtest/gtest.h>

#include <gui_controls/gui_application.h>
#include <gui_controls/controls_toolbar_dock.h>
#include <gui_core/memory/gui_tracking_allocator.h>

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_stdinc.h>

#include <filesystem>
#include <functional>
#include <vector>

namespace
{
    class EmptyApp : public interstice::GUIApplication
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
        interstice::GUIApplication::InitializeOptions options;
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

    class OneWindowApp : public interstice::GUIApplication
    {
    public:
        interstice::ID windowID = interstice::ID::Invalid();

        void OnGUI() override
        {
            if (BeginWindow(windowID, "Allocations", interstice::LayoutOverride{}.Size(200, 100)))
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
        interstice::TrackingAllocator tracker;
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

    // A render-to-image window with a button. Run() drives real frames (OnFrame
    // only builds); `script` runs at the start of each frame's OnGUI.
    class ImageWindowApp : public interstice::GUIApplication
    {
    public:
        interstice::ID windowID = interstice::ID::Invalid();
        interstice::ID buttonID = interstice::ID::Invalid();
        int     clicks   = 0;
        int     frame    = 0;
        int     frames   = 3;
        interstice::int2 buttonCenter{ 0, 0 };   // after last frame's layout
        std::function<void(int)> script;

        void OnGUI() override
        {
            if (script) script(frame);
            if (BeginWindow(windowID, "Image", interstice::LayoutOverride{}.Size(240, 120), theme.windowStyleRef,
                            interstice::ElementFlags::WindowRenderToImage))
            {
                BeginVertical(interstice::LayoutOverride{}.FlexGrow(1));
                Button(buttonID, "Click", [this] { ++clicks; });
                EndVertical();
                EndWindow();
            }
            if (++frame >= frames)
                applicationQuit = true;
        }

        bool Start()
        {
            if (!Initialize())
                return false;
            windowID = idManager.CreateStaticID();
            buttonID = idManager.CreateStaticID();
            m_frame.OnPostLayout([this](interstice::FrameContext&)
            {
                const interstice::ElementRef ref = elementStorage.FindElementRefByID(buttonID);
                if (!ref.IsValid()) return;
                const interstice::IntRect r = elementStorage.GetElementRectPtr(ref)->rect;
                buttonCenter = { r.x + r.w / 2, r.y + r.h / 2 };
            });
            return true;
        }
    };

    // A render-to-image window has no OS window, renders, and can be read back.
    TEST(GpuApplicationInit, RenderToImageWindowDrawsWithoutAnOSWindow)
    {
        ImageWindowApp app;
        if (!app.Start())
        {
            app.Cleanup();
            GTEST_SKIP() << "no Vulkan device available";
        }
        app.Run();
        const interstice::OSWindowHandle handle = app.windowManager->GetWindowHandle(app.windowID);
        ASSERT_NE(handle, nullptr) << "the window exists";
        EXPECT_EQ(app.windowBackend->GetNativeHandle(handle), nullptr) << "no OS window behind it";

        const interstice::IntRect rect = app.windowManager->GetWindowRect(app.windowID);
        ASSERT_GT(rect.w, 0);
        std::vector<uint8_t> pixels(size_t(rect.w) * rect.h * 4);
        ASSERT_TRUE(app.ReadWindowPixels(app.windowID, pixels));
        const size_t center = (size_t(rect.h / 2) * rect.w + size_t(rect.w / 2)) * 4;
        EXPECT_EQ(pixels[center + 3], 255) << "the window's background covers its image";
        app.Cleanup();
    }

    // Input injected into a render-to-image window reaches its controls.
    TEST(GpuApplicationInit, InjectedClickReachesAButtonInARenderToImageWindow)
    {
        ImageWindowApp app;
        if (!app.Start())
        {
            app.Cleanup();
            GTEST_SKIP() << "no Vulkan device available";
        }
        app.frames = 7;
        app.script = [&](int frame)
        {
            const interstice::int2 center = app.buttonCenter;
            switch (frame)
            {
                case 2: app.InjectMouseMove(app.windowID, center); break;
                case 3: app.InjectMouseButton(app.windowID, center, interstice::MouseButtons::Left, true); break;
                case 4: app.InjectMouseButton(app.windowID, center, interstice::MouseButtons::Left, false); break;
                case 5: app.InjectMouseLeave(app.windowID); break;
                default: break;
            }
        };
        app.Run();
        EXPECT_EQ(app.clicks, 1);
        app.Cleanup();
    }

    class FloatingToolbarApp : public interstice::GUIApplication
    {
    public:
        std::vector<interstice::FloatingToolbarItem> floating;
        interstice::FloatingToolbarOptions           options;
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
        const interstice::ID windowID = app.idManager.CreateStaticID();
        const interstice::ID buttonID = app.idManager.CreateStaticID();
        interstice::FloatingToolbarItem fw;
        fw.windowID        = windowID;
        fw.spawnPosition   = { 200, 200 };
        fw.homeSide        = interstice::ToolbarSide::Left;
        fw.item.id         = buttonID;
        fw.item.renderBody = [&] { app.Button(buttonID, "X", [] {}); };
        app.floating.push_back(std::move(fw));

        int             dockBacks  = 0;
        interstice::ToolbarSide dockedSide = interstice::ToolbarSide::Top;
        app.options.onDockBack = [&](interstice::ID id, interstice::ToolbarSide side)
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
        EXPECT_EQ(dockedSide, interstice::ToolbarSide::Left);
        EXPECT_TRUE(app.floating.empty());
        app.idManager.ReleaseStaticID(buttonID);
        app.Cleanup();
    }
}
