// An app that links the library and does nothing else: no resource root, no files. The
// shaders, themes and fonts it needs are built into the library.

#include <gtest/gtest.h>

#include <gui_controls/gui_application_internal.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
    class ZeroSetupApp : public tachyon::GUIApplicationInternals
    {
    public:
        tachyon::ID windowID = tachyon::ID::Invalid();
        tachyon::ID buttonID = tachyon::ID::Invalid();
        int         frame    = 0;

        void OnGUI() override
        {
            if (BeginWindow(windowID, "Zero setup", theme.windowStyleRef, tachyon::LayoutOverride{}.Size(240, 120),
                            tachyon::ElementFlags::WindowRenderToImage))
            {
                BeginVertical(tachyon::LayoutOverride{}.FlexGrow(1));
                Button(buttonID, "A button with some text", [] {});
                EndVertical();
            }
            EndWindow();
            if (++frame >= 3)
                applicationQuit = true;
        }
    };

    TEST(GpuZeroSetup, AnAppWithNoResourceFilesInitializesAndDraws)
    {
        ZeroSetupApp app;
        app.SetResourceRoot((std::filesystem::temp_directory_path() / "tachyon-no-such-resources").string());
        if (!app.Initialize())
        {
            app.Cleanup();
            GTEST_SKIP() << "no Vulkan device available";
        }
        app.windowID = app.idManager.CreateStaticID();
        app.buttonID = app.idManager.CreateStaticID();
        EXPECT_TRUE(app.theme.FindStyle("button")) << "the built-in theme is loaded";
        ASSERT_TRUE(app.Run()) << "the built-in shaders made the pipelines";

        const tachyon::IntRect rect = app.windowManager->GetWindowRect(app.windowID);
        ASSERT_GT(rect.w, 0);
        std::vector<uint8_t> pixels(size_t(rect.w) * rect.h * 4);
        ASSERT_TRUE(app.ReadWindowPixels(app.windowID, pixels));

        // The window background plus a button with text: more than a handful of colors.
        std::vector<uint32_t> colors;
        for (size_t i = 0; i + 3 < pixels.size() && colors.size() < 16; i += 4)
        {
            const uint32_t c = uint32_t(pixels[i]) | uint32_t(pixels[i + 1]) << 8 | uint32_t(pixels[i + 2]) << 16;
            if (std::find(colors.begin(), colors.end(), c) == colors.end())
                colors.push_back(c);
        }
        EXPECT_GE(colors.size(), 8u) << "the button and its text were drawn";
        app.Cleanup();
    }
}
