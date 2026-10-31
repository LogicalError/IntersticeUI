// A disabled control is drawn faded as a whole, on a real device: its pixels lie between
// the window behind it and the same control enabled. A control in a disabled container
// fades once, not once for each.

#include <gtest/gtest.h>

#include <gui_controls/gui_application_internal.h>

#include <cstdlib>
#include <vector>

namespace
{
    using namespace tachyon;

    class DisabledApp : public GUIApplicationInternals
    {
    public:
        ID windowID = ID::Invalid();
        ID enabledID = ID::Invalid(), disabledID = ID::Invalid(), nestedID = ID::Invalid();
        IntRect enabledRect{}, disabledRect{}, nestedRect{}, windowRect{};
        int frame = 0;

        void OnGUI() override
        {
            if (BeginWindow(windowID, "Disabled", theme.windowStyleRef, LayoutOverride{}.Size(360, 120),
                            ElementFlags::WindowRenderToImage))
            {
                BeginHorizontal(LayoutOverride{}.FlexGrow(1));
                Button(enabledID, "Enabled", nullptr, LayoutOverride{}.Size(100, 40));
                BeginDisabled();
                Button(disabledID, "Disabled", nullptr, LayoutOverride{}.Size(100, 40));
                BeginVertical(LayoutOverride{}.Size(110, 60));
                Button(nestedID, "Nested", nullptr, LayoutOverride{}.Size(100, 40));
                EndVertical();
                EndDisabled();
                EndHorizontal();
            }
            EndWindow();
            if (++frame >= 4)
                applicationQuit = true;
        }

        bool Start()
        {
            if (!Initialize())
                return false;
            windowID   = idManager.CreateStaticID();
            enabledID  = idManager.CreateStaticID();
            disabledID = idManager.CreateStaticID();
            nestedID   = idManager.CreateStaticID();
            m_frame.OnPostLayout([this](FrameContext&)
            {
                auto rectOf = [this](ID id) {
                    const ElementRef ref = elementStorage.FindElementRefByID(id);
                    return ref.IsValid() ? elementStorage.GetElementRectPtr(ref)->rect : IntRect{};
                };
                enabledRect  = rectOf(enabledID);
                disabledRect = rectOf(disabledID);
                nestedRect   = rectOf(nestedID);
                windowRect   = rectOf(windowID);
            });
            return true;
        }
    };

    struct Rgb { int r, g, b; };

    TEST(GpuDisabledOpacity, ADisabledControlIsDrawnFaded)
    {
        DisabledApp app;
        if (!app.Start())
        {
            app.Cleanup();
            GTEST_SKIP() << "no Vulkan device available";
        }
        app.transitionManager.SetEnabled(false);
        ASSERT_TRUE(app.Run());

        const IntRect rect = app.windowManager->GetWindowRect(app.windowID);
        std::vector<uint8_t> pixels(size_t(rect.w) * rect.h * 4);
        ASSERT_TRUE(app.ReadWindowPixels(app.windowID, pixels));
        // Window pixels from layout rects: the window's own rect is its origin. BGRA.
        auto at = [&](int x, int y) {
            x -= app.windowRect.x; y -= app.windowRect.y;
            const size_t o = (size_t(y) * rect.w + size_t(x)) * 4;
            return Rgb{ pixels[o + 2], pixels[o + 1], pixels[o] };
        };
        // A point of a button's background: inside its border, clear of its centred text.
        auto background = [&](IntRect r) { return at(r.x + 4, r.y + r.h / 2); };

        const Rgb window   = at(app.windowRect.x + app.windowRect.w - 4, app.windowRect.y + app.windowRect.h - 4);
        const Rgb enabled  = background(app.enabledRect);
        const Rgb disabled = background(app.disabledRect);
        const Rgb nested   = background(app.nestedRect);
        auto sum = [](Rgb c) { return c.r + c.g + c.b; };
        ASSERT_GT(std::abs(sum(enabled) - sum(window)), 9) << "test setup: a button stands out from the window";

        // Between the window and the enabled button, and nearer the window (40%).
        const int span = sum(enabled) - sum(window);
        const float share = float(sum(disabled) - sum(window)) / float(span);
        EXPECT_GT(share, 0.1f);
        EXPECT_LT(share, 0.8f);

        EXPECT_NEAR(sum(nested), sum(disabled), 3) << "inside a disabled container it fades once";
        app.Cleanup();
    }
}
