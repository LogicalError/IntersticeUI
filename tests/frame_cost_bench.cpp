// Frame cost: builds, lays out and finishes a UI of a few thousand controls headless, and
// prints the mean CPU time per frame. Disabled by default; run with
//   gui_core_tests --gtest_also_run_disabled_tests --gtest_filter=*FrameCost*

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
    using namespace tachyon;
    using GUITest::HeadlessGUIApp;

    TEST(FrameCostBench, DISABLED_AThousandRowsOfControls)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        std::vector<std::string> fields(200, "text");
        bool flag = false;

        auto draw = [&] {
            app.BeginRootWindow({ 1920, 1080 });
            for (int row = 0; row < 200; row++)
            {
                app.BeginHorizontal();
                app.Label("Name");
                app.Field(fields[size_t(row)]);
                app.Button("Apply", [] {});
                app.Toggle(flag, "On", [&](bool v) { flag = v; });
                app.Text("A line of static text");
                app.EndHorizontal();
            }
            app.EndRootWindow();
        };
        for (int i = 0; i < 20; i++) app.Frame(draw);   // warm up

        constexpr int kFrames = 200;
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < kFrames; i++) app.Frame(draw);
        const auto t1 = std::chrono::steady_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count() / kFrames;
        std::printf("FRAMECOST %.3f ms/frame\n", ms);
        app.Cleanup();
    }
}
