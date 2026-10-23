// The full GUIApplication on NullGraphicsContext: no GPU, the rest of the
// library (window backend, text, layout, input) runs as usual.
#include <gtest/gtest.h>

#include <gui_controls/gui_application_internal.h>
#include <gui_core/graphics/null/gui_null_graphics_context.h>

#include <functional>
#include <memory>

namespace
{
    // A render-to-image window with a button, so no OS window appears.
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
            if (BeginWindow(windowID, "Null", interstice::LayoutOverride{}.Size(240, 120), theme.windowStyleRef,
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
            SetGraphicsBackend(std::make_unique<interstice::NullGraphicsContext>());
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

    TEST(NullGraphicsBackend, RunsTheApplicationWithoutAGPU)
    {
        NullBackendApp app;
        ASSERT_TRUE(app.Start());
        EXPECT_EQ(app.graphicsContext->GetAPI(), interstice::GraphicsAPI::None);
        app.Run();
        EXPECT_EQ(app.frame, app.frames);
        EXPECT_NE(app.windowManager->GetWindowHandle(app.windowID), nullptr) << "the window exists";
        EXPECT_GT(app.buttonCenter.x, 0) << "the button was laid out";
        app.Cleanup();
        EXPECT_EQ(app.graphicsContext, nullptr) << "Cleanup releases the backend";
    }

    TEST(NullGraphicsBackend, InjectedClickReachesAButton)
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
        app.Run();
        EXPECT_EQ(app.clicks, 1);
        app.Cleanup();
    }
}
