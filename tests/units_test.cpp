// Units (#52): sizes in the API are CSS px, scaled by the UI scale like the stylesheet's;
// Pixels{} sizes, int2 and rects are device pixels, taken as they are.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

namespace
{
    using namespace interstice;
    using GUITest::HeadlessGUIApp;

    class UnitsTest : public ::testing::Test
    {
    protected:
        HeadlessGUIApp app;
        void SetUp() override
        {
            app.SetUIScale(1.5f);
            ASSERT_TRUE(app.Initialize());
        }
        void TearDown() override { app.Cleanup(); }
    };

    TEST_F(UnitsTest, ACssSizeScalesAndAPixelSizeDoesnt)
    {
        const ID cssID    = app.idManager.CreateStaticID();
        const ID pixelsID = app.idManager.CreateStaticID();
        const ID pairID   = app.idManager.CreateStaticID();
        app.Frame([&] {
            app.BeginRootWindow();
            app.BeginVertical(cssID,    app.theme.emptyVertStyleRef, LayoutOverride{}.Width(100).Height(40).NoGrow().NoShrink());
            app.EndVertical();
            app.BeginVertical(pixelsID, app.theme.emptyVertStyleRef, LayoutOverride{}.Width(Pixels{ 100 }).Height(Pixels{ 40 }).NoGrow().NoShrink());
            app.EndVertical();
            app.BeginVertical(pairID,   app.theme.emptyVertStyleRef, LayoutOverride::FixedSize(int2{ 100, 40 }).NoGrow().NoShrink());
            app.EndVertical();
            app.EndRootWindow();
        });
        EXPECT_EQ(app.GetElementRect(cssID).w, 150);
        EXPECT_EQ(app.GetElementRect(cssID).h, 60);
        EXPECT_EQ(app.GetElementRect(pixelsID).w, 100);
        EXPECT_EQ(app.GetElementRect(pixelsID).h, 40);
        EXPECT_EQ(app.GetElementRect(pairID).w, 100) << "an int2 size is pixels";
    }

    // Min and max are scaled like the size.
    TEST_F(UnitsTest, CssMinAndMaxScale)
    {
        const ID boxID = app.idManager.CreateStaticID();
        app.Frame([&] {
            app.BeginRootWindow();
            app.BeginVertical(boxID, app.theme.emptyVertStyleRef, LayoutOverride{}.MinWidth(100).MaxWidth(100).Height(10).NoGrow().NoShrink());
            app.EndVertical();
            app.EndRootWindow();
        });
        EXPECT_EQ(app.GetElementRect(boxID).w, 150);
    }

    // A label group's widths are CSS px.
    TEST_F(UnitsTest, ALabelGroupWidthIsCss)
    {
        const ID labelID = app.idManager.CreateStaticID();
        for (int i = 0; i < 2; i++)
            app.Frame([&] {
                app.BeginRootWindow();
                app.BeginLabelGroup(120, 40, 300);
                app.Label(labelID, "Name");
                app.EndLabelGroup();
                app.EndRootWindow();
            });
        EXPECT_EQ(app.GetElementRect(labelID).w, 180);
    }
}
