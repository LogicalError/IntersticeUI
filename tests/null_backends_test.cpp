// The full GUIApplication on the null graphics and window backends: no GPU and
// no OS windows; text, layout and input run as usual.
#include <gtest/gtest.h>

#include <gui_controls/gui_application_internal.h>
#include <gui_core/graphics/null/gui_null_graphics_context.h>
#include <gui_core/windows/null/gui_null_window_backend.h>
#include <gui_core/windows/gui_window_flags.h>

#include <functional>
#include <memory>

namespace
{
    // A render-to-image window with a button.
    class NullBackendApp : public interstice::GUIApplicationInternals
    {
    public:
        interstice::ID windowID = interstice::ID::Invalid();
        interstice::ID buttonID = interstice::ID::Invalid();
        int     clicks = 0;
        int     frame  = 0;
        int     frames = 3;
        interstice::int2 buttonCenter{ 0, 0 };   // after last frame's layout
        std::function<void(int)> script;

        void OnGUI() override
        {
            if (script) script(frame);
            if (BeginWindow(windowID, "Null", theme.windowStyleRef, interstice::LayoutOverride{}.Size(240, 120),
                            interstice::ElementFlags::WindowRenderToImage))
            {
                BeginVertical(interstice::LayoutOverride{}.FlexGrow(1));
                Button(buttonID, "Click", [this] { ++clicks; });
                EndVertical();
            }
            EndWindow();
            if (++frame >= frames)
                applicationQuit = true;
        }

        bool Start()
        {
            SetGraphicsBackend(std::make_unique<interstice::NullGraphicsContext>());
            SetWindowBackend(std::make_unique<interstice::NullWindowBackend>());
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

    TEST(NullBackends, RunsTheApplicationWithoutAGPU)
    {
        NullBackendApp app;
        ASSERT_TRUE(app.Start());
        EXPECT_EQ(app.graphicsContext->GetAPI(), interstice::GraphicsAPI::None);
        EXPECT_TRUE(app.Run());
        EXPECT_EQ(app.frame, app.frames);
        EXPECT_NE(app.windowManager->GetWindowHandle(app.windowID), nullptr) << "the window exists";
        EXPECT_GT(app.buttonCenter.x, 0) << "the button was laid out";
        app.Cleanup();
        EXPECT_EQ(app.graphicsContext, nullptr) << "Cleanup releases the backend";
    }

    TEST(NullBackends, InjectedClickReachesAButton)
    {
        NullBackendApp app;
        ASSERT_TRUE(app.Start());
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
        EXPECT_TRUE(app.Run());
        EXPECT_EQ(app.clicks, 1);
        app.Cleanup();
    }

    // The host's window backend and clipboard are the ones the app uses.
    TEST(NullBackends, UsesTheHostsWindowBackendAndClipboard)
    {
        NullBackendApp app;
        auto clipboard = std::make_unique<interstice::LocalClipboard>();
        interstice::IClipboard* hostClipboard = clipboard.get();
        app.SetClipboard(std::move(clipboard));
        ASSERT_TRUE(app.Start());
        EXPECT_EQ(app.clipboard.get(), hostClipboard);

        app.clipboard->Set("pasted");
        EXPECT_EQ(app.clipboard->Get(), "pasted");

        // A window that isn't render-to-image is one of the backend's windows.
        const interstice::OSWindowHandle window = app.windowBackend->CreateWindow("os", interstice::int2{ 300, 200 },
                                                                                  interstice::WindowFlags::None, nullptr);
        ASSERT_NE(window, nullptr);
        EXPECT_EQ(app.windowBackend->GetWindowSize(window), (interstice::int2{ 300, 200 }));
        EXPECT_TRUE(app.windowBackend->DestroyWindow(window));
        app.Cleanup();
        EXPECT_EQ(app.clipboard, nullptr) << "Cleanup releases the clipboard";
    }
}
