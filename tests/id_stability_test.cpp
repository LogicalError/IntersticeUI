// Stable IDs (#48): a dynamic ID comes from its call site, so a widget appearing or
// disappearing elsewhere doesn't renumber the widgets after it. Only repeats of the
// same call site (a loop) are numbered.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <set>

namespace
{
    using namespace tachyon;
    using GUITest::HeadlessGUIApp;

    class IDStabilityTest : public ::testing::Test
    {
    protected:
        HeadlessGUIApp app;
        void SetUp() override { ASSERT_TRUE(app.Initialize()); }
        void TearDown() override { app.Cleanup(); }
    };

    // An ID-less button keeps its ID when a label appears before it.
    TEST_F(IDStabilityTest, AWidgetAppearingBeforeAnotherDoesntRenumberIt)
    {
        auto buttonIDWith = [&](bool warning) {
            ID id = ID::Invalid();
            app.Frame([&] {
                app.BeginRootWindow();
                if (warning)
                    app.Label("Name taken");
                app.Button("OK", [] {});
                id = app.elementStorage.GetLastAddedID();
                app.EndRootWindow();
            });
            return id;
        };
        const ID without = buttonIDWith(false);
        EXPECT_NE(without, ID::Invalid());
        EXPECT_EQ(buttonIDWith(true), without);
    }

    // The same call site repeated in a loop still gets one ID per repeat.
    TEST_F(IDStabilityTest, RepeatsOfOneCallSiteGetTheirOwnIDs)
    {
        std::set<uint64_t> ids;
        app.Frame([&] {
            app.BeginRootWindow();
            for (int i = 0; i < 3; i++)
            {
                app.Button("Item", [] {});
                ids.insert(app.elementStorage.GetLastAddedID().value.integer);
            }
            app.EndRootWindow();
        });
        EXPECT_EQ(ids.size(), 3u);
    }

    // Two ID-less buttons on different lines get different IDs (not one shared header line).
    TEST_F(IDStabilityTest, IDLessOverloadsHashTheirCaller)
    {
        ID a = ID::Invalid(), b = ID::Invalid();
        app.Frame([&] {
            app.BeginRootWindow();
            app.Button("A", [] {});
            a = app.elementStorage.GetLastAddedID();
            app.Button("B", [] {});
            b = app.elementStorage.GetLastAddedID();
            app.EndRootWindow();
        });
        EXPECT_NE(a, b);
    }
}
