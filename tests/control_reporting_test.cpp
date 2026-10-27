// A control reports a change twice (#51): input is handled after the frame it arrived
// in, where the callback runs; the frame built after that returns true, then false again.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <vector>

namespace
{
    using namespace tachyon;
    using GUITest::HeadlessGUIApp;

    class ControlReportingTest : public ::testing::Test
    {
    protected:
        HeadlessGUIApp app;
        void SetUp() override { ASSERT_TRUE(app.Initialize()); }
        void TearDown() override { app.Cleanup(); }
    };

    // What one frame saw: had the callback run before the control was built, and did the
    // control return true.
    struct Seen { bool callbackBefore; bool returned; };

    TEST_F(ControlReportingTest, AButtonsCallbackRunsBeforeTheFrameThatReturnsTrue)
    {
        const ID buttonID = app.idManager.CreateStaticID();
        int  clicks = 0;
        std::vector<Seen> frames;
        auto draw = [&] {
            app.BeginRootWindow();
            const int before = clicks;
            const bool returned = app.Button(buttonID, "OK", [&] { ++clicks; });
            frames.push_back({ before > 0, returned });
            app.EndRootWindow();
        };
        app.Frame(draw);
        app.ClickAt(app.CenterOf(buttonID));
        app.Frame(draw);   // the click arrives; handled after this frame
        app.Frame(draw);
        app.Frame(draw);

        ASSERT_EQ(frames.size(), 4u);
        EXPECT_EQ(clicks, 1);
        EXPECT_FALSE(frames[1].callbackBefore) << "the frame the click arrived in is built first";
        EXPECT_FALSE(frames[1].returned);
        EXPECT_TRUE(frames[2].callbackBefore)  << "the callback ran after it";
        EXPECT_TRUE(frames[2].returned)        << "the next frame returns true";
        EXPECT_FALSE(frames[3].returned)       << "and the one after doesn't";
    }

    TEST_F(ControlReportingTest, AToggleReportsTheSameWay)
    {
        const ID toggleID = app.idManager.CreateStaticID();
        bool on = false;
        int  calls = 0;
        std::vector<Seen> frames;
        auto draw = [&] {
            app.BeginRootWindow();
            const int before = calls;
            const bool returned = app.Toggle(toggleID, on, "On", [&](bool v) { on = v; ++calls; });
            frames.push_back({ before > 0, returned });
            app.EndRootWindow();
        };
        app.Frame(draw);
        app.ClickAt(app.CenterOf(toggleID));
        app.Frame(draw);
        app.Frame(draw);
        app.Frame(draw);

        ASSERT_EQ(frames.size(), 4u);
        EXPECT_TRUE(on);
        EXPECT_FALSE(frames[1].returned);
        EXPECT_TRUE(frames[2].callbackBefore);
        EXPECT_TRUE(frames[2].returned);
        EXPECT_FALSE(frames[3].returned);
    }
}
