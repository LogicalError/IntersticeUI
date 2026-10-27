// The Begin / End contract (#50): every Begin* has its End*, called whether or not the
// Begin returned true. The scopes (Window(), Menu(), ...) end their container when they
// leave.

#include <gtest/gtest.h>

#include "headless_gui_app.h"
#include "misuse_reports.h"

#include <utility>

namespace
{
    using namespace tachyon;
    using GUITest::HeadlessGUIApp;
    using GUITest::MisuseReports;

    class BeginEndTest : public ::testing::Test
    {
    protected:
        HeadlessGUIApp app;
        void SetUp() override { ASSERT_TRUE(app.Initialize()); }
        void TearDown() override { app.Cleanup(); }

        void InRoot(const std::function<void()>& content)
        {
            app.Frame([&] {
                app.BeginRootWindow();
                content();
                app.EndRootWindow();
            });
        }
    };

    // A closed menu, ended every frame, then opened and closed: nothing is reported.
    TEST_F(BeginEndTest, AMenuEndedOpenOrClosedIsNotReported)
    {
        const ID menuID = app.idManager.CreateStaticID();
        const ID itemID = app.idManager.CreateStaticID();
        bool built = false;
        auto draw = [&] {
            built = app.BeginMenu(menuID, "File");
            if (built)
                app.MenuItem(itemID, "Open");
            app.EndMenu();
        };
        MisuseReports reports;
        InRoot(draw);
        InRoot(draw);
        EXPECT_FALSE(built);

        app.ClickAt(app.CenterOf(menuID));
        InRoot(draw);
        InRoot(draw);
        EXPECT_TRUE(built);
        EXPECT_TRUE(app.HasElement(itemID));

        app.ClickAt(app.CenterOf(menuID));
        InRoot(draw);
        InRoot(draw);
        EXPECT_FALSE(built);
        EXPECT_EQ(reports.Count(), 0u) << reports.All();
    }

    // A tooltip and a context menu that aren't showing, and a skipped (cached) scope.
    TEST_F(BeginEndTest, SkippedContainersEndedAreNotReported)
    {
        const ID buttonID = app.idManager.CreateStaticID();
        const ID cachedID = app.idManager.CreateStaticID();
        int bodyRuns = 0;
        app.SetDrawCacheEnabled(true);
        app.InjectMouseMove({ 1900, 1000 });
        MisuseReports reports;
        for (int i = 0; i < 3; i++)
            InRoot([&] {
                app.Button(buttonID, "B", [] {});
                if (app.BeginTooltip(buttonID))
                    app.Label("tip");
                app.EndTooltip();
                if (app.BeginContextMenu(buttonID))
                    app.MenuItem("Item", nullptr);
                app.EndContextMenu();
                if (app.BeginCached(cachedID, app.theme.emptyVertStyleRef, 0, LayoutOverride{}.Width(200).Height(100)))
                {
                    bodyRuns++;
                    app.Label("cached");
                }
                app.EndCached();
            });
        EXPECT_EQ(bodyRuns, 1);   // later frames hit the cache
        EXPECT_EQ(reports.Count(), 0u) << reports.All();
    }

    // An End without its Begin is reported and ignored.
    TEST_F(BeginEndTest, AnEndWithoutItsBeginIsReported)
    {
        MisuseReports reports;
        InRoot([&] {
            app.EndMenu();
            app.EndTooltip();
            app.EndCached();
        });
        EXPECT_TRUE(reports.Contains("EndMenu without BeginMenu")) << reports.All();
        EXPECT_TRUE(reports.Contains("EndTooltip without BeginTooltip")) << reports.All();
        EXPECT_TRUE(reports.Contains("EndCached without BeginCached")) << reports.All();
    }

    // A Begin left without its End (here a closed menu, the old way) is reported at the
    // end of the frame, and the next frame starts clean.
    TEST_F(BeginEndTest, ABeginWithoutItsEndIsReportedAtTheEndOfTheFrame)
    {
        const ID menuID = app.idManager.CreateStaticID();
        MisuseReports reports;
        InRoot([&] {
            if (app.BeginMenu(menuID, "File"))
                app.EndMenu();
        });
        EXPECT_TRUE(reports.Contains("without its End")) << reports.All();

        MisuseReports next;
        InRoot([&] {
            app.BeginMenu(menuID, "File");
            app.EndMenu();
        });
        EXPECT_EQ(next.Count(), 0u) << next.All();
    }

    // The scopes end their containers: the content nests, nothing is reported.
    TEST_F(BeginEndTest, AScopeEndsItsContainer)
    {
        const ID rowID    = app.idManager.CreateStaticID();
        const ID buttonID = app.idManager.CreateStaticID();
        const ID afterID  = app.idManager.CreateStaticID();
        const ID menuID   = app.idManager.CreateStaticID();
        MisuseReports reports;
        for (int i = 0; i < 2; i++)
            InRoot([&] {
                if (auto row = app.Horizontal(rowID))
                    app.Button(buttonID, "In", [] {});
                if (auto menu = app.Menu(menuID, "File"))
                    app.MenuItem("Open", nullptr);
                app.Button(afterID, "After", [] {});
            });
        EXPECT_EQ(reports.Count(), 0u) << reports.All();
        ASSERT_TRUE(app.HasElement(buttonID));
        ASSERT_TRUE(app.HasElement(afterID));
        const IntRect row = app.GetElementRect(rowID);
        const IntRect in  = app.GetElementRect(buttonID);
        EXPECT_TRUE(in.x >= row.x && in.x + in.w <= row.x + row.w) << "the button is in the row";
        const IntRect after = app.GetElementRect(afterID);
        EXPECT_TRUE(after.x >= row.x + row.w || after.y >= row.y + row.h) << "the next button is after the row, not in it";
    }

    // End() ends the container early, once: leaving the scope doesn't end it again, nor
    // does the scope it was moved from.
    TEST_F(BeginEndTest, AScopeEndsOnce)
    {
        const ID menuID = app.idManager.CreateStaticID();
        MisuseReports reports;
        InRoot([&] {
            auto menu  = app.Menu(menuID, "File");
            auto moved = std::move(menu);
            moved.End();
            moved.End();
        });
        EXPECT_EQ(reports.Count(), 0u) << reports.All();
    }
}
