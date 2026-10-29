// GUIApplication::Initialize on a real device: a fresh app works without setup
// steps of its own.

#include <gtest/gtest.h>

#include <gui_controls/gui_application_internal.h>
#include <gui_core/graphics/vulkan/vk_graphics_context.h>
#include <gui_core/graphics/gui_primitive_renderer.h>
#include <gui_controls/controls_toolbar_dock.h>
#include <gui_controls/subsystems/gui_ui_cache_subsystem.h>
#include <gui_core/memory/gui_tracking_allocator.h>

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <functional>
#include <thread>
#include <vector>

namespace
{
    class EmptyApp : public tachyon::GUIApplicationInternals
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

    // Lifecycle misuse (#87): Cleanup without Initialize (or twice) has nothing to free, and
    // Initialize on an initialized app is reported and ignored.
    TEST(GpuApplicationInit, CleanupWithoutInitializeIsHarmless)
    {
        EmptyApp app;
        app.Cleanup();
        app.Cleanup();
        SUCCEED();
    }

    TEST(GpuApplicationInit, InitializeTwiceIsIgnored)
    {
        EmptyApp app;
        if (!app.Initialize())
        {
            app.Cleanup();
            GTEST_SKIP() << "no Vulkan device or window system available";
        }
        auto* const textSystem = app.textSystem;
        EXPECT_TRUE(app.Initialize()) << "the app is initialized";
        EXPECT_EQ(app.textSystem, textSystem) << "nothing was made again (or leaked)";
        app.Cleanup();
        app.Cleanup();
    }

    TEST(GpuApplicationInit, OptionsTurnTheDefaultsOff)
    {
        EmptyApp app;
        tachyon::GUIApplication::InitializeOptions options;
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
            const bool available = probe.Initialize().HasValue();
            probe.Cleanup();
            if (!available)
                GTEST_SKIP() << "no Vulkan device or window system available";
        }

        std::filesystem::current_path(std::filesystem::temp_directory_path());
        EmptyApp app;
        app.SetResourceRoot(resources.string());
        const bool initialized = app.Initialize().HasValue();
        std::filesystem::current_path(resources);

        EXPECT_TRUE(initialized);
        EXPECT_GT(app.styleManager.LayoutStyleCount(), 0u) << "the theme is loaded";
        app.Cleanup();
    }

    class OneWindowApp : public tachyon::GUIApplicationInternals
    {
    public:
        tachyon::ID windowID = tachyon::ID::Invalid();

        void OnGUI() override
        {
            if (BeginWindow(windowID, "Allocations", tachyon::LayoutOverride{}.Size(200, 100)))
            {
                Label("Counting allocations");
            }
            EndWindow();
        }
    };

    // Everything allocated through the host's hooks (Vulkan objects, vk-bootstrap,
    // VMA, SDL) is freed again by Cleanup.
    TEST(GpuApplicationInit, AllocationHooksSeeEverythingFreedByCleanup)
    {
        tachyon::TrackingAllocator tracker;
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
            {
                EXPECT_NE(current, original) << "SDL allocates through the hooks";
            }

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
    class ImageWindowApp : public tachyon::GUIApplicationInternals
    {
    public:
        tachyon::ID windowID = tachyon::ID::Invalid();
        tachyon::ID buttonID = tachyon::ID::Invalid();
        int     clicks   = 0;
        int     frame    = 0;
        int     frames   = 3;
        tachyon::int2 buttonCenter{ 0, 0 };   // after last frame's layout
        std::function<void(int)> script;

        void OnGUI() override
        {
            if (script) script(frame);
            if (BeginWindow(windowID, "Image", theme.windowStyleRef, tachyon::LayoutOverride{}.Size(240, 120),
                            tachyon::ElementFlags::WindowRenderToImage))
            {
                BeginVertical(tachyon::LayoutOverride{}.FlexGrow(1));
                Button(buttonID, "Click", [this] { ++clicks; });
                EndVertical();
            }
            EndWindow();
            if (++frame >= frames)
                applicationQuit = true;
        }

        bool Start()
        {
            if (!Initialize())
                return false;
            windowID = idManager.CreateStaticID();
            buttonID = idManager.CreateStaticID();
            m_frame.OnPostLayout([this](tachyon::FrameContext&)
            {
                const tachyon::ElementRef ref = elementStorage.FindElementRefByID(buttonID);
                if (!ref.IsValid()) return;
                const tachyon::IntRect r = elementStorage.GetElementRectPtr(ref)->rect;
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
        EXPECT_TRUE(app.Run());
        const tachyon::OSWindowHandle handle = app.windowManager->GetWindowHandle(app.windowID);
        ASSERT_NE(handle, nullptr) << "the window exists";
        EXPECT_EQ(app.windowBackend->GetNativeHandle(handle), nullptr) << "no OS window behind it";

        const tachyon::IntRect rect = app.windowManager->GetWindowRect(app.windowID);
        ASSERT_GT(rect.w, 0);
        std::vector<uint8_t> pixels(size_t(rect.w) * rect.h * 4);
        ASSERT_TRUE(app.ReadWindowPixels(app.windowID, pixels));
        const size_t center = (size_t(rect.h / 2) * rect.w + size_t(rect.w / 2)) * 4;
        EXPECT_EQ(pixels[center + 3], 255) << "the window's background covers its image";
        app.Cleanup();
    }

    // A host loop drives the frame through the split calls (#55): a render-to-image window
    // is drawn once RecordFrame returns, before any present.
    TEST(GpuApplicationInit, AHostLoopDrivesTheFrameCalls)
    {
        ImageWindowApp app;
        if (!app.Start())
        {
            app.Cleanup();
            GTEST_SKIP() << "no Vulkan device available";
        }
        app.frames = 1000;   // the host decides when to stop
        int hostFrames = 0;
        std::vector<uint8_t> pixels;
        bool drawnBeforePresent = false;
        while (hostFrames < 3 && app.UpdateFrame())
        {
            ASSERT_TRUE(app.RecordFrame());
            if (hostFrames == 2)
            {
                const tachyon::IntRect rect = app.windowManager->GetWindowRect(app.windowID);
                pixels.resize(size_t(rect.w) * rect.h * 4);
                drawnBeforePresent = rect.w > 0 && app.ReadWindowPixels(app.windowID, pixels)
                                  && pixels[(size_t(rect.h / 2) * rect.w + size_t(rect.w / 2)) * 4 + 3] == 255;
            }
            app.PresentFrame();
            ++hostFrames;
        }
        EXPECT_EQ(hostFrames, 3);
        EXPECT_EQ(app.frame, 3) << "one build per host frame";
        EXPECT_TRUE(drawnBeforePresent);

        app.applicationQuit = true;
        EXPECT_FALSE(app.UpdateFrame()) << "false once the app quits";
        app.Cleanup();
    }

    // A render-to-image window showing a red image of the app's.
    class DeviceLossApp : public tachyon::GUIApplicationInternals
    {
    public:
        tachyon::ID         windowID = tachyon::ID::Invalid();
        tachyon::Image*     image    = nullptr;
        tachyon::ImageIndex red      = tachyon::ImageIndex::Invalid;

        void OnGUI() override
        {
            if (BeginWindow(windowID, "Image", theme.windowStyleRef, tachyon::LayoutOverride{}.Size(240, 120),
                            tachyon::ElementFlags::WindowRenderToImage))
                Image(red, tachyon::LayoutOverride{}.Size(tachyon::Pixels{ 1000 }, tachyon::Pixels{ 1000 }));
            EndWindow();
        }

        bool Start()
        {
            if (!Initialize())
                return false;
            windowID = idManager.CreateStaticID();
            std::vector<uint8_t> pixels(4 * 4 * 4);
            for (size_t i = 0; i < pixels.size(); i += 4) { pixels[i] = 255; pixels[i + 3] = 255; }   // RGBA red
            image = graphicsContext->CreateImage(pixels.data(), tachyon::uint2{ 4, 4 }, tachyon::ImageFormat::R8G8B8A8,
                                                 tachyon::Sampler::LinearClampToEdge);
            red = graphicsContext->RegisterImage(image);
            return image != nullptr && red != tachyon::ImageIndex::Invalid;
        }

        // The window's center pixel is red (BGRA8).
        bool CenterIsRed()
        {
            const tachyon::IntRect rect = windowManager->GetWindowRect(windowID);
            if (rect.w <= 0) return false;
            std::vector<uint8_t> pixels(size_t(rect.w) * rect.h * 4);
            if (!ReadWindowPixels(windowID, pixels)) return false;
            const size_t c = (size_t(rect.h / 2) * rect.w + size_t(rect.w / 2)) * 4;
            return pixels[c + 2] > 200 && pixels[c + 1] < 50 && pixels[c] < 50;
        }

        bool Frames(int count)
        {
            for (int i = 0; i < count; i++)
            {
                if (!UpdateFrame() || !RecordFrame()) return false;
                PresentFrame();
            }
            return true;
        }
    };

    // A lost device is made again before the next frame (#47): the app's image keeps its
    // index and pixels, and the window draws on.
    TEST(GpuApplicationInit, ALostDeviceIsMadeAgainAndTheUIDrawsOn)
    {
        DeviceLossApp app;
        if (!app.Start())
        {
            app.Cleanup();
            GTEST_SKIP() << "no Vulkan device available";
        }
        ASSERT_TRUE(app.Frames(3));
        ASSERT_TRUE(app.CenterIsRed()) << "the image draws before the loss";

        auto* vulkan = static_cast<tachyon::vulkan::VulkanGraphicsContext*>(app.graphicsContext);
        vulkan->SimulateDeviceLoss();
        EXPECT_TRUE(app.graphicsContext->DeviceLost());
        ASSERT_TRUE(app.Frames(3)) << "the frames go on";
        EXPECT_FALSE(app.graphicsContext->DeviceLost());
        EXPECT_TRUE(app.CenterIsRed()) << "the image draws from its index on the new device";

        app.graphicsContext->DestroyImageAtEnd(app.image, tachyon::DeviceScope::Application);
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
            const tachyon::int2 center = app.buttonCenter;
            switch (frame)
            {
                case 2: app.InjectMouseMove(app.windowID, center); break;
                case 3: app.InjectMouseButton(app.windowID, center, tachyon::MouseButtons::Left, true); break;
                case 4: app.InjectMouseButton(app.windowID, center, tachyon::MouseButtons::Left, false); break;
                case 5: app.InjectMouseLeave(app.windowID); break;
                default: break;
            }
        };
        EXPECT_TRUE(app.Run());
        EXPECT_EQ(app.clicks, 1);
        app.Cleanup();
    }

    // An element whose subtree ends in a cached area: its after-draw fires on every
    // rendered frame, also when the area is replayed from the cache instead of walked.
    class AfterDrawOverCacheApp : public tachyon::GUIApplicationInternals
    {
    public:
        tachyon::ID windowID = tachyon::ID::Invalid();
        tachyon::ID outerID  = tachyon::ID::Invalid();
        tachyon::ID cachedID = tachyon::ID::Invalid();
        int frame = 0, frames = 20;
        int afterDrawsThisFrame = 0;
        int replayFrames = 0, replayFramesWithAfterDraw = 0;

        void OnGUI() override
        {
            // Give the cache time to settle and capture (it waits for transitions).
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            if (BeginWindow(windowID, "Image", theme.windowStyleRef, tachyon::LayoutOverride{}.Size(240, 120),
                            tachyon::ElementFlags::WindowRenderToImage))
            {
                BeginArea(outerID, tachyon::LayoutOverride{}.FlexGrow(1));
                (void)elementStorage.AddCustomDrawAfter(elementStorage.FindElementRefByID(outerID),
                    [this](tachyon::CustomDrawContext&) { ++afterDrawsThisFrame; });
                if (BeginCached(cachedID, 0, tachyon::LayoutOverride{}.FlexGrow(1)))
                {
                    Text("cached content");
                }
                EndCached();
                EndArea();
            }
            EndWindow();
            if (++frame >= frames)
                applicationQuit = true;
        }

        bool Start()
        {
            if (!Initialize())
                return false;
            windowID = idManager.CreateStaticID();
            outerID  = idManager.CreateStaticID();
            cachedID = idManager.CreateStaticID();
            m_frame.OnPreRender([this](tachyon::FrameContext&) { afterDrawsThisFrame = 0; });
            m_frame.OnPostRender([this](tachyon::FrameContext&)
            {
                if (m_uiCache->FrameDrawReplays() == 0) return;
                ++replayFrames;
                if (afterDrawsThisFrame > 0) ++replayFramesWithAfterDraw;
            });
            return true;
        }
    };

    TEST(GpuApplicationInit, AfterDrawsFireOverACachedArea)
    {
        AfterDrawOverCacheApp app;
        if (!app.Start())
        {
            app.Cleanup();
            GTEST_SKIP() << "no Vulkan device available";
        }
        EXPECT_TRUE(app.Run());
        ASSERT_GT(app.replayFrames, 0) << "test setup: the area was replayed from the cache";
        EXPECT_EQ(app.replayFramesWithAfterDraw, app.replayFrames);
        app.Cleanup();
    }

    // The same when the subtree ends in content clipped away (scrolled out of a panel): the
    // culled part is skipped as a whole, and the after-draw used to hang on its last node.
    class AfterDrawOverCulledApp : public tachyon::GUIApplicationInternals
    {
    public:
        tachyon::ID windowID = tachyon::ID::Invalid();
        tachyon::ID outerID  = tachyon::ID::Invalid();
        int frame = 0, frames = 6;
        int afterDrawsThisFrame = 0;
        int renderedFrames = 0, renderedFramesWithAfterDraw = 0;

        void OnGUI() override
        {
            if (BeginWindow(windowID, "Image", theme.windowStyleRef, tachyon::LayoutOverride{}.Size(240, 120),
                            tachyon::ElementFlags::WindowRenderToImage))
            {
                // A clipping area too small for its rows: the last ones (a row with a child
                // at the very end) are culled.
                BeginArea(outerID, tachyon::LayoutOverride{}.Size(200, 60));
                (void)elementStorage.AddCustomDrawAfter(elementStorage.FindElementRefByID(outerID),
                    [this](tachyon::CustomDrawContext&) { ++afterDrawsThisFrame; });
                for (int i = 0; i < 20; ++i)
                    Text("row");
                BeginHorizontal();
                Text("last row");
                EndHorizontal();
                EndArea();
            }
            EndWindow();
            if (++frame >= frames)
                applicationQuit = true;
        }

        bool Start()
        {
            if (!Initialize())
                return false;
            windowID = idManager.CreateStaticID();
            outerID  = idManager.CreateStaticID();
            m_frame.OnPreRender([this](tachyon::FrameContext&) { afterDrawsThisFrame = 0; });
            m_frame.OnPostRender([this](tachyon::FrameContext&)
            {
                ++renderedFrames;
                if (afterDrawsThisFrame > 0) ++renderedFramesWithAfterDraw;
            });
            return true;
        }
    };

    TEST(GpuApplicationInit, AfterDrawsFireOverCulledContent)
    {
        AfterDrawOverCulledApp app;
        if (!app.Start())
        {
            app.Cleanup();
            GTEST_SKIP() << "no Vulkan device available";
        }
        EXPECT_TRUE(app.Run());
        ASSERT_GT(app.renderedFrames, 0);
        EXPECT_EQ(app.renderedFramesWithAfterDraw, app.renderedFrames);
        app.Cleanup();
    }

    class FloatingToolbarApp : public tachyon::GUIApplicationInternals
    {
    public:
        std::vector<tachyon::FloatingToolbarItem> floating;
        tachyon::FloatingToolbarOptions           options;
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
        const tachyon::ID windowID = app.idManager.CreateStaticID();
        const tachyon::ID buttonID = app.idManager.CreateStaticID();
        tachyon::FloatingToolbarItem fw;
        fw.windowID        = windowID;
        fw.spawnPosition   = { 200, 200 };
        fw.homeSide        = tachyon::ToolbarSide::Left;
        fw.item.id         = buttonID;
        fw.item.renderBody = [&] { app.Button(buttonID, "X", [] {}); };
        app.floating.push_back(std::move(fw));

        int             dockBacks  = 0;
        tachyon::ToolbarSide dockedSide = tachyon::ToolbarSide::Top;
        app.options.onDockBack = [&](tachyon::ID id, tachyon::ToolbarSide side)
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
        EXPECT_EQ(dockedSide, tachyon::ToolbarSide::Left);
        EXPECT_TRUE(app.floating.empty());
        app.idManager.ReleaseStaticID(buttonID);
        app.Cleanup();
    }

    // Two boxes in a render-to-image window: one with a green box-shadow spread around
    // it, one with a red focus ring.
    class BoxShadowApp : public tachyon::GUIApplicationInternals
    {
    public:
        tachyon::ID     windowID = tachyon::ID::Invalid();
        tachyon::ID     shadowedID = tachyon::ID::Invalid();
        tachyon::ID     ringedID = tachyon::ID::Invalid();
        tachyon::ID     outlinedID = tachyon::ID::Invalid();
        tachyon::IntRect shadowedRect{}, ringedRect{}, outlinedRect{};

        void OnGUI() override
        {
            tachyon::Style shadowed{}, ringed{};
            for (tachyon::StyleState& state : shadowed.states)
            {
                state.backgroundColor = tachyon::ColorStyle::Create(tachyon::color4b{ 255, 255, 255, 255 });
                state.boxShadow[0]    = tachyon::BoxShadow{ .spread = 6, .color = { 0, 255, 0, 255 } };
            }
            for (tachyon::StyleState& state : ringed.states)
                state.backgroundColor = tachyon::ColorStyle::Create(tachyon::color4b{ 255, 255, 255, 255 });
            ringed.focusRing = tachyon::BoxShadow{ .spread = 6, .color = { 255, 0, 0, 255 } };
            tachyon::Style outlined = ringed;
            outlined.focusRing = tachyon::BoxShadow{ .spread = 2, .color = { 0, 0, 255, 255 }, .inset = true };

            if (BeginWindow(windowID, "Image", theme.windowStyleRef, tachyon::LayoutOverride{}.Size(240, 120),
                            tachyon::ElementFlags::WindowRenderToImage))
            {
                BeginArea(shadowedID, styleManager.Register(shadowed), tachyon::LayoutOverride{}.Size(tachyon::Pixels{ 40 }, tachyon::Pixels{ 40 }).Position(40, 40));
                EndArea();
                BeginArea(ringedID, styleManager.Register(ringed), tachyon::LayoutOverride{}.Size(tachyon::Pixels{ 40 }, tachyon::Pixels{ 40 }).Position(140, 40));
                EndArea();
                BeginArea(outlinedID, styleManager.Register(outlined), tachyon::LayoutOverride{}.Size(tachyon::Pixels{ 40 }, tachyon::Pixels{ 40 }).Position(140, 70));
                EndArea();
            }
            EndWindow();
        }

        bool Start()
        {
            if (!Initialize())
                return false;
            transitionManager.SetEnabled(false);
            windowID   = idManager.CreateStaticID();
            shadowedID = idManager.CreateStaticID();
            ringedID   = idManager.CreateStaticID();
            outlinedID = idManager.CreateStaticID();
            m_frame.OnPostLayout([this](tachyon::FrameContext&)
            {
                auto rectOf = [this](tachyon::ID id) {
                    const tachyon::ElementRef ref = elementStorage.FindElementRefByID(id);
                    return ref.IsValid() ? elementStorage.GetElementRectPtr(ref)->rect : tachyon::IntRect{};
                };
                shadowedRect = rectOf(shadowedID);
                ringedRect   = rectOf(ringedID);
                outlinedRect = rectOf(outlinedID);
            });
            return true;
        }

        bool Frames(int count)
        {
            for (int i = 0; i < count; i++)
            {
                if (!UpdateFrame() || !RecordFrame()) return false;
                PresentFrame();
            }
            return true;
        }

        // BGRA8 pixel of the window's image.
        uint32_t PixelAt(tachyon::int2 p)
        {
            const tachyon::IntRect rect = windowManager->GetWindowRect(windowID);
            std::vector<uint8_t> pixels(size_t(rect.w) * rect.h * 4);
            if (rect.w <= 0 || !ReadWindowPixels(windowID, pixels)) return 0;
            const size_t i = (size_t(p.y) * rect.w + size_t(p.x)) * 4;
            return uint32_t(pixels[i]) | uint32_t(pixels[i + 1]) << 8 | uint32_t(pixels[i + 2]) << 16;
        }
    };

    // A style's box-shadow draws outside its box; its focus ring only while the element
    // has focus and focus is visible (from the keyboard).
    TEST(GpuApplicationInit, BoxShadowsAndTheFocusRingDrawOutsideTheBox)
    {
        BoxShadowApp app;
        if (!app.Start())
        {
            app.Cleanup();
            GTEST_SKIP() << "no Vulkan device available";
        }
        ASSERT_TRUE(app.Frames(3));
        ASSERT_GT(app.shadowedRect.w, 0);
        const tachyon::IntRect s = app.shadowedRect, r = app.ringedRect;
        EXPECT_EQ(app.PixelAt({ s.x - 3, s.y + s.h / 2 }), 0x00FF00u) << "the shadow, spread 6 around the box";
        EXPECT_NE(app.PixelAt({ s.x - 9, s.y + s.h / 2 }), 0x00FF00u) << "past the spread";

        const uint32_t besideRing = app.PixelAt({ r.x - 3, r.y + r.h / 2 });
        EXPECT_NE(besideRing, 0xFF0000u) << "no ring without focus";

        app.guiStateManager.Interaction().SetFocusID(app.ringedID);
        ASSERT_TRUE(app.Frames(2));
        EXPECT_EQ(app.PixelAt({ r.x - 3, r.y + r.h / 2 }), besideRing) << "no ring when focus came from the mouse";

        app.guiStateManager.Interaction().SetFocusVisible(true);
        ASSERT_TRUE(app.Frames(2));
        EXPECT_EQ(app.PixelAt({ r.x - 3, r.y + r.h / 2 }), 0xFF0000u) << "the ring, with focus from the keyboard";

        // An inset ring outlines the inside of the box and leaves the outside alone.
        const tachyon::IntRect o = app.outlinedRect;
        const uint32_t outside = app.PixelAt({ o.x - 3, o.y + o.h / 2 });
        const uint32_t fill    = app.PixelAt({ o.x + o.w / 2, o.y + o.h / 2 });
        app.guiStateManager.Interaction().SetFocusID(app.outlinedID);
        ASSERT_TRUE(app.Frames(2));
        EXPECT_EQ(app.PixelAt({ o.x + 1, o.y + o.h / 2 }), 0x0000FFu) << "the outline, inside the edge";
        EXPECT_EQ(app.PixelAt({ o.x + o.w / 2, o.y + o.h / 2 }), fill) << "the middle keeps its fill";
        EXPECT_EQ(app.PixelAt({ o.x - 3, o.y + o.h / 2 }), outside) << "nothing outside the box";
        app.Cleanup();
    }

    // An "I" drawn only by its green text-shadow (the text itself is transparent) on black,
    // in four boxes: a sharp shadow, the same moved right, a blurred one, and small text
    // blurred further than its distance field reaches. A fifth box holds only an icon.
    class TextShadowApp : public tachyon::GUIApplicationInternals
    {
    public:
        tachyon::ID windowID = tachyon::ID::Invalid();
        tachyon::ID ids[5] {};
        tachyon::IntRect rects[5] {};
        tachyon::IconID dot = tachyon::IconID::Invalid;
        enum { kSharp, kMoved, kBlurred, kWide, kIcon };

        tachyon::StyleRef Shadowed(float fontSize, tachyon::BoxShadow shadow)
        {
            tachyon::Style s{};
            s.textLayoutStyle.horizontalAlignment = tachyon::TextAlign::Center;
            s.textLayoutStyle.verticalAlignment   = tachyon::TextAlign::Center;
            for (tachyon::StyleState& state : s.states)
            {
                state.backgroundColor    = tachyon::ColorStyle::Create(tachyon::color4b{ 0, 0, 0, 255 });
                state.textStyle.fontSize = fontSize;
                state.textStyle.color    = { 0, 0, 0, 0 };
                state.textShadow[0]      = shadow;
            }
            return styleManager.Register(s);
        }

        void OnGUI() override
        {
            constexpr tachyon::color4b kGreen{ 0, 255, 0, 255 };
            const tachyon::StyleRef styles[4] = {
                Shadowed(40.0f, tachyon::BoxShadow{ .color = kGreen }),
                Shadowed(40.0f, tachyon::BoxShadow{ .x = 10, .color = kGreen }),
                Shadowed(40.0f, tachyon::BoxShadow{ .blur = 6, .color = kGreen }),
                Shadowed(14.0f, tachyon::BoxShadow{ .blur = 16, .color = kGreen }),
            };
            if (BeginWindow(windowID, "Image", theme.windowStyleRef, tachyon::LayoutOverride{}.Size(500, 120),
                            tachyon::ElementFlags::WindowRenderToImage))
            {
                for (int i = 0; i < 4; ++i)
                    Text(ids[i], "I", styles[i], tachyon::LayoutOverride{}.Size(tachyon::Pixels{ 80 }, tachyon::Pixels{ 80 }).Position(10 + 95 * i, 20));
                RegisterElement(tachyon::Element{ .id = ids[kIcon], .iconID = dot, .styleRef = styles[kSharp] },
                                tachyon::LayoutOverride{}.Size(tachyon::Pixels{ 80 }, tachyon::Pixels{ 80 }).Position(10 + 95 * kIcon, 20));
            }
            EndWindow();
        }

        bool Start()
        {
            if (!Initialize())
                return false;
            transitionManager.SetEnabled(false);
            windowID = idManager.CreateStaticID();
            for (tachyon::ID& id : ids)
                id = idManager.CreateStaticID();
            static constexpr char kDot[] = R"(<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><circle cx="12" cy="12" r="10" fill="#000"/></svg>)";
            dot = textSystem->RegisterIconFromSVG("text-shadow-dot", kDot, sizeof(kDot) - 1);
            m_frame.OnPostLayout([this](tachyon::FrameContext&)
            {
                for (int i = 0; i < 5; ++i)
                {
                    const tachyon::ElementRef ref = elementStorage.FindElementRefByID(ids[i]);
                    rects[i] = ref.IsValid() ? elementStorage.GetElementRectPtr(ref)->rect : tachyon::IntRect{};
                }
            });
            return true;
        }

        bool Frames(int count)
        {
            for (int i = 0; i < count; i++)
            {
                if (!UpdateFrame() || !RecordFrame()) return false;
                PresentFrame();
            }
            return true;
        }

        // The red, green and blue of the middle row of box `box`, left to right (BGRA8 image).
        struct Row { std::vector<int> r, g, b; };
        Row MiddleRow(int box)
        {
            Row row;
            const tachyon::IntRect window = windowManager->GetWindowRect(windowID);
            std::vector<uint8_t> pixels(size_t(window.w) * window.h * 4);
            if (window.w <= 0 || !ReadWindowPixels(windowID, pixels)) return row;
            const tachyon::IntRect& rect = rects[box];
            const int y = rect.y + rect.h / 2;
            for (int x = rect.x; x < rect.x + rect.w; ++x)
            {
                const size_t i = (size_t(y) * window.w + size_t(x)) * 4;
                row.b.push_back(pixels[i]);
                row.g.push_back(pixels[i + 1]);
                row.r.push_back(pixels[i + 2]);
            }
            return row;
        }
    };

    // The first and last pixel at or over `threshold`; {-1, -1} for none.
    static std::pair<int, int> Extent(const std::vector<int>& v, int threshold)
    {
        int first = -1, last = -1;
        for (int i = 0; i < int(v.size()); ++i)
            if (v[i] >= threshold) { if (first < 0) first = i; last = i; }
        return { first, last };
    }

    // text-shadow draws the glyph's shape in the shadow's color, moved by its offset and
    // blurred by its blur, fading out before the glyph's distance field ends.
    TEST(GpuApplicationInit, TextShadowsDrawTheGlyphsShape)
    {
        TextShadowApp app;
        if (!app.Start())
        {
            app.Cleanup();
            GTEST_SKIP() << "no Vulkan device available";
        }
        ASSERT_TRUE(app.Frames(3));
        ASSERT_EQ(app.rects[TextShadowApp::kSharp].w, 80);

        const auto sharp = app.MiddleRow(TextShadowApp::kSharp);
        ASSERT_EQ(sharp.g.size(), 80u);
        const auto [s0, s1] = Extent(sharp.g, 128);
        ASSERT_GE(s0, 0) << "the shadow of the I's stem";
        EXPECT_LE(s1 - s0, 10) << "just the stem";
        EXPECT_EQ(sharp.g[s0 + (s1 - s0) / 2], 255);
        EXPECT_LT(sharp.g[s0 - 3], 16) << "a sharp edge";
        EXPECT_EQ(Extent(sharp.r, 1).first, -1) << "the shadow's color alone; the text is transparent";
        EXPECT_EQ(Extent(sharp.b, 1).first, -1);

        const auto moved = app.MiddleRow(TextShadowApp::kMoved);
        EXPECT_EQ(Extent(moved.g, 128), std::make_pair(s0 + 10, s1 + 10)) << "moved by its offset";

        const auto blurred = app.MiddleRow(TextShadowApp::kBlurred);
        const auto [b0, b1] = Extent(blurred.g, 8);
        ASSERT_GE(b0, 0);
        EXPECT_LE(b0, s0 - 4) << "spread out by the blur";
        EXPECT_GE(b1, s1 + 4);
        for (int x = b0; x < s0; ++x)
            EXPECT_LE(blurred.g[x], blurred.g[x + 1]) << "darker away from the glyph, at " << x;
        EXPECT_LT(blurred.g[s0 - 12], 4) << "gone well past the blur";

        // Small text holds less distance field than this blur needs: the shadow still fades out
        // from nothing instead of stopping at an edge.
        const auto wide = app.MiddleRow(TextShadowApp::kWide);
        const auto [w0, w1] = Extent(wide.g, 1);
        ASSERT_GT(w0, 0);
        const int peak = int(std::max_element(wide.g.begin(), wide.g.end()) - wide.g.begin());
        EXPECT_GE(peak - w0, 5) << "a wide blur";
        EXPECT_LE(wide.g[w0], 6) << "no edge where the shadow ends";
        EXPECT_LE(wide.g[w1], 6);
        for (int x = w0; x < peak; ++x)
            EXPECT_LE(wide.g[x], wide.g[x + 1]) << "at " << x;

        // An icon on its own has its style's text shadows too.
        ASSERT_NE(app.dot, tachyon::IconID::Invalid);
        const auto icon = app.MiddleRow(TextShadowApp::kIcon);
        const auto [i0, i1] = Extent(icon.g, 128);
        EXPECT_GE(i1 - i0, 20) << "the dot's shadow";
        app.Cleanup();
    }
    // Three areas in a render-to-image window: an opaque red one, an opaque green one over
    // it, and a translucent one. Opaque boxes come with an occluder; the picture is the same.
    class OccluderApp : public tachyon::GUIApplicationInternals
    {
    public:
        tachyon::ID windowID = tachyon::ID::Invalid();
        tachyon::ID ids[3] {};
        tachyon::IntRect rects[3] {};

        tachyon::StyleRef Filled(tachyon::color4b color)
        {
            tachyon::Style s{};
            s.borderWidths = tachyon::BoxEdges(4);
            s.borderRadius = tachyon::BoxCorners(6);
            for (tachyon::StyleState& state : s.states)
            {
                state.backgroundColor = tachyon::ColorStyle::Create(color);
                state.SetBorderColor(tachyon::ColorStyle::Create(tachyon::color4b{ 0, 0, 255, 255 }));
            }
            return styleManager.Register(s);
        }

        void OnGUI() override
        {
            const tachyon::StyleRef styles[3] = { Filled({ 255, 0, 0, 255 }), Filled({ 0, 255, 0, 255 }), Filled({ 255, 255, 255, 128 }) };
            if (BeginWindow(windowID, "Image", theme.windowStyleRef, tachyon::LayoutOverride{}.Size(240, 160),
                            tachyon::ElementFlags::WindowRenderToImage))
            {
                const tachyon::int2 at[3] = { { 20, 20 }, { 40, 40 }, { 140, 20 } };
                for (int i = 0; i < 3; ++i)
                {
                    BeginArea(ids[i], styles[i], tachyon::LayoutOverride{}.Size(tachyon::Pixels{ 80 }, tachyon::Pixels{ 80 }).Position(at[i].x, at[i].y));
                    EndArea();
                }
            }
            EndWindow();
        }

        bool Start()
        {
            if (!Initialize())
                return false;
            transitionManager.SetEnabled(false);
            windowID = idManager.CreateStaticID();
            for (tachyon::ID& id : ids)
                id = idManager.CreateStaticID();
            m_frame.OnPostLayout([this](tachyon::FrameContext&)
            {
                for (int i = 0; i < 3; ++i)
                {
                    const tachyon::ElementRef ref = elementStorage.FindElementRefByID(ids[i]);
                    rects[i] = ref.IsValid() ? elementStorage.GetElementRectPtr(ref)->rect : tachyon::IntRect{};
                }
            });
            return true;
        }

        bool Frames(int count)
        {
            for (int i = 0; i < count; i++)
            {
                if (!UpdateFrame() || !RecordFrame()) return false;
                PresentFrame();
            }
            return true;
        }

        uint32_t PixelAt(tachyon::int2 p)
        {
            const tachyon::IntRect rect = windowManager->GetWindowRect(windowID);
            std::vector<uint8_t> pixels(size_t(rect.w) * rect.h * 4);
            if (rect.w <= 0 || !ReadWindowPixels(windowID, pixels)) return 0;
            const size_t i = (size_t(p.y) * rect.w + size_t(p.x)) * 4;
            return uint32_t(pixels[i]) | uint32_t(pixels[i + 1]) << 8 | uint32_t(pixels[i + 2]) << 16;
        }

        // The occluders the window drew last frame, inside out from their rects.
        std::vector<tachyon::Occluder> Occluders()
        {
            auto* renderer = static_cast<tachyon::PrimitiveRenderer*>(guiRenderer);
            const auto& bytes = renderer->GetTarget(windowID).streams[size_t(tachyon::PrimitiveKind::Occluder)].cpu;
            std::vector<tachyon::Occluder> out(bytes.size() / sizeof(tachyon::Occluder));
            std::memcpy(out.data(), bytes.data(), out.size() * sizeof(tachyon::Occluder));
            return out;
        }
    };

    // An opaque box's middle gets an occluder, inside its border and corners; a translucent
    // box gets none; the picture is drawn as before.
    TEST(GpuApplicationInit, OpaqueBoxesComeWithAnOccluder)
    {
        OccluderApp app;
        if (!app.Start())
        {
            app.Cleanup();
            GTEST_SKIP() << "no Vulkan device available";
        }
        ASSERT_TRUE(app.Frames(3));
        ASSERT_EQ(app.rects[0].w, 80);

        const auto occluders = app.Occluders();
        ASSERT_EQ(occluders.size(), 2u) << "the two opaque areas";
        for (int i = 0; i < 2; ++i)
        {
            const tachyon::IntRect r = app.rects[i];
            const tachyon::Occluder& o = occluders[size_t(i)];
            EXPECT_GE(o.pos.x, r.x + 4 + 6) << "inside the border and the corner radius";
            EXPECT_GE(o.pos.y, r.y + 4 + 6);
            EXPECT_LE(o.pos.x + o.size.x, r.x + r.w - 10);
            EXPECT_LE(o.pos.y + o.size.y, r.y + r.h - 10);
            EXPECT_GT(o.size.x, 50);
        }

        const tachyon::IntRect red = app.rects[0], green = app.rects[1], glass = app.rects[2];
        EXPECT_EQ(app.PixelAt({ red.x + 10, red.y + 10 }), 0xFF0000u) << "the red area outside the green one";
        EXPECT_EQ(app.PixelAt({ green.x + 40, green.y + 40 }), 0x00FF00u) << "the green area over it";
        EXPECT_EQ(app.PixelAt({ green.x + 1, green.y + 40 }), 0x0000FFu) << "the green area's border, over the red";
        EXPECT_NE(app.PixelAt({ glass.x + 40, glass.y + 40 }), 0u) << "the translucent area";
        app.Cleanup();
    }
}
