// Switch: an on/off track with a knob, and an optional label after it.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

namespace
{
    using namespace tachyon;
    using GUITest::HeadlessGUIApp;

    class SwitchTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ASSERT_TRUE(app.Initialize());
            switchID = app.idManager.CreateStaticID();
        }

        bool Emit(bool& value, std::string_view text = {})
        {
            bool changed = false;
            app.Frame([&] {
                app.BeginRootWindow();
                changed = app.Switch(switchID, value, text);
                app.EndRootWindow();
            });
            return changed;
        }

        HeadlessGUIApp app;
        ID switchID{};
    };

    // The track is 28 x 16, the control 24 high so it lines up with fields.
    TEST_F(SwitchTest, HasTheSpecSizes)
    {
        bool on = false;
        Emit(on);
        const IntRect control = app.GetElementRect(switchID);
        EXPECT_EQ(control.h, app.styleManager.Px(24));
        const auto ref = app.elementStorage.FindElementRefByID(switchID);
        ASSERT_TRUE(ref.IsValid());
        const auto children = app.elementStorage.GetChildrenSpan(ref);
        ASSERT_EQ(children.size(), 1u) << "no label without text";
        const IntRect track = app.GetElementRect(children[0].id);
        EXPECT_EQ(track.w, app.styleManager.Px(28));
        EXPECT_EQ(track.h, app.styleManager.Px(16));
    }

    TEST_F(SwitchTest, AClickTurnsItOnAndOff)
    {
        bool on = false;
        Emit(on, "Off");
        EXPECT_EQ(app.elementStorage.GetChildrenSpan(app.elementStorage.FindElementRefByID(switchID)).size(), 2u)
            << "the label follows the track";

        app.ClickAt(app.CenterOf(switchID));
        Emit(on, "Off");
        EXPECT_TRUE(on);
        EXPECT_TRUE(Emit(on, "On")) << "returns true the frame it changes";   // the click registers as of this frame

        app.ClickAt(app.CenterOf(switchID));
        Emit(on, "On");
        EXPECT_FALSE(on);
    }
}
