// Robustness (#87): the API used the wrong way is reported (logged once) and the app
// carries on: the frame finishes and the next, correct frame lays out as usual. Nothing
// crashes, asserts or leaves state behind.

#include <gtest/gtest.h>

#include "headless_gui_app.h"
#include "misuse_reports.h"

#include <cstdio>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <limits>
#include <string>
#include <vector>

namespace
{
    using namespace tachyon;
    using GUITest::HeadlessGUIApp;
    using GUITest::MisuseReports;

    struct MisuseCase
    {
        const char*                         name;
        std::function<void(HeadlessGUIApp&)> misuse;   // runs inside a frame's root window
    };

    std::string CaseName(const ::testing::TestParamInfo<MisuseCase>& info) { return info.param.name; }

    // A correct frame after the misuse: a button lays out, and construction is balanced.
    void ExpectTheNextFrameWorks(HeadlessGUIApp& app)
    {
        const ID buttonID = app.idManager.CreateStaticID();
        for (int i = 0; i < 2; i++)
            app.Frame([&] {
                app.BeginRootWindow();
                app.Button(buttonID, "OK", [] {});
                app.EndRootWindow();
            });
        EXPECT_GT(app.GetElementRect(buttonID).w, 0) << "the next frame didn't lay out";
        EXPECT_TRUE(app.guiStateManager.Construction().IsBalanced());
        EXPECT_TRUE(app.menuManager.IsBalanced());
    }

    void RunMisuse(const MisuseCase& c)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        MisuseReports reports;
        app.Frame([&] {
            app.BeginRootWindow();
            c.misuse(app);
            app.EndRootWindow();
        });
        EXPECT_GE(reports.Count(), 1u) << c.name << " wasn't reported";
        ExpectTheNextFrameWorks(app);
        app.Cleanup();
    }

    // ---- An End without its Begin ----

    class EndWithoutBeginTest : public ::testing::TestWithParam<MisuseCase> {};

    TEST_P(EndWithoutBeginTest, IsReportedAndIgnored) { RunMisuse(GetParam()); }

    INSTANTIATE_TEST_SUITE_P(Misuse, EndWithoutBeginTest, ::testing::Values(
        MisuseCase{ "EndArea",             [](HeadlessGUIApp& a) { a.EndArea(); } },
        MisuseCase{ "EndHorizontal",       [](HeadlessGUIApp& a) { a.EndHorizontal(); } },
        MisuseCase{ "EndHorizontalWrap",   [](HeadlessGUIApp& a) { a.EndHorizontalWrap(); } },
        MisuseCase{ "EndVertical",         [](HeadlessGUIApp& a) { a.EndVertical(); } },
        MisuseCase{ "EndVerticalWrap",     [](HeadlessGUIApp& a) { a.EndVerticalWrap(); } },
        MisuseCase{ "EndCached",           [](HeadlessGUIApp& a) { a.EndCached(); } },
        MisuseCase{ "EndMenu",             [](HeadlessGUIApp& a) { a.EndMenu(); } },
        MisuseCase{ "EndMenuBar",          [](HeadlessGUIApp& a) { a.EndMenuBar(); } },
        MisuseCase{ "EndTitleBar",         [](HeadlessGUIApp& a) { a.EndTitleBar(); } },
        MisuseCase{ "EndTooltip",          [](HeadlessGUIApp& a) { a.EndTooltip(); } },
        MisuseCase{ "EndContextMenu",      [](HeadlessGUIApp& a) { a.EndContextMenu(); } },
        MisuseCase{ "EndLabelGroup",       [](HeadlessGUIApp& a) { a.EndLabelGroup(); } },
        MisuseCase{ "EndButtonGroup",      [](HeadlessGUIApp& a) { a.EndButtonGroup(); } },
        MisuseCase{ "EndToggleGroup",      [](HeadlessGUIApp& a) { a.EndToggleGroup(); } },
        MisuseCase{ "EndToolbar",          [](HeadlessGUIApp& a) { a.EndToolbar(); } },
        MisuseCase{ "EndReorderableGroup", [](HeadlessGUIApp& a) { a.EndReorderableGroup(); } },
        MisuseCase{ "EndReorderableItem",  [](HeadlessGUIApp& a) { a.EndReorderableItem(); } },
        MisuseCase{ "EndListView",         [](HeadlessGUIApp& a) { a.EndListView(); } },
        MisuseCase{ "EndGridView",         [](HeadlessGUIApp& a) { a.EndGridView(); } },
        MisuseCase{ "EndTable",            [](HeadlessGUIApp& a) { a.EndTable(); } },
        MisuseCase{ "EndRow",              [](HeadlessGUIApp& a) { a.EndRow(); } },
        MisuseCase{ "EndTabBar",           [](HeadlessGUIApp& a) { a.EndTabBar(); } },
        MisuseCase{ "EndStatusBar",        [](HeadlessGUIApp& a) { a.EndStatusBar(); } },
        MisuseCase{ "EndDockingContainer", [](HeadlessGUIApp& a) { a.EndDockingContainer(); } },
        MisuseCase{ "EndWindow",           [](HeadlessGUIApp& a) { a.EndWindow(); } },
        MisuseCase{ "EndGhostWindow",      [](HeadlessGUIApp& a) { a.EndGhostWindow(); } },
        MisuseCase{ "EndDisabled",         [](HeadlessGUIApp& a) { a.EndDisabled(); } },
        MisuseCase{ "EndSelected",         [](HeadlessGUIApp& a) { a.EndSelected(); } },
        MisuseCase{ "EndIndeterminate",    [](HeadlessGUIApp& a) { a.EndIndeterminate(); } },
        MisuseCase{ "PopParent",           [](HeadlessGUIApp& a) { (void)a.idManager.PopParent(); } }
    ), CaseName);

    // ---- A Begin left open at the end of the frame ----

    class BeginLeftOpenTest : public ::testing::TestWithParam<MisuseCase> {};

    TEST_P(BeginLeftOpenTest, IsReportedAndClosed) { RunMisuse(GetParam()); }

    int g_selected = 0;
    TableState g_tableState;
    VirtualTableColumn g_columns[1];
    DockingLayout g_dockingLayout;

    INSTANTIATE_TEST_SUITE_P(Misuse, BeginLeftOpenTest, ::testing::Values(
        MisuseCase{ "BeginArea",             [](HeadlessGUIApp& a) { a.BeginArea(a.idManager.CreateStaticID()); } },
        MisuseCase{ "BeginHorizontal",       [](HeadlessGUIApp& a) { a.BeginHorizontal(a.idManager.CreateStaticID()); } },
        MisuseCase{ "BeginHorizontalWrap",   [](HeadlessGUIApp& a) { a.BeginHorizontalWrap(a.idManager.CreateStaticID()); } },
        MisuseCase{ "BeginVertical",         [](HeadlessGUIApp& a) { a.BeginVertical(a.idManager.CreateStaticID()); } },
        MisuseCase{ "BeginVerticalWrap",     [](HeadlessGUIApp& a) { a.BeginVerticalWrap(a.idManager.CreateStaticID()); } },
        MisuseCase{ "BeginCached",           [](HeadlessGUIApp& a) { a.BeginCached(a.idManager.CreateStaticID()); } },
        MisuseCase{ "BeginMenuBar",          [](HeadlessGUIApp& a) { a.BeginMenuBar(a.idManager.CreateStaticID()); } },
        MisuseCase{ "BeginTitleBar",         [](HeadlessGUIApp& a) { a.BeginTitleBar(a.idManager.CreateStaticID()); } },
        MisuseCase{ "BeginStatusBar",        [](HeadlessGUIApp& a) { a.BeginStatusBar(a.idManager.CreateStaticID()); } },
        MisuseCase{ "BeginLabelGroup",       [](HeadlessGUIApp& a) { a.BeginLabelGroup(a.idManager.CreateStaticID()); } },
        MisuseCase{ "BeginButtonGroup",      [](HeadlessGUIApp& a) { a.BeginButtonGroup(); } },
        MisuseCase{ "BeginToggleGroup",      [](HeadlessGUIApp& a) { a.BeginToggleGroup(); } },
        MisuseCase{ "BeginToolbar",          [](HeadlessGUIApp& a) { a.BeginToolbar(); } },
        MisuseCase{ "BeginReorderableGroup", [](HeadlessGUIApp& a) { a.BeginReorderableGroup(a.idManager.CreateStaticID(), ReorderDirection::Vertical, [](int, int) {}); } },
        MisuseCase{ "BeginListView",         [](HeadlessGUIApp& a) { a.BeginListView(a.idManager.CreateStaticID(), &g_selected, 0); } },
        MisuseCase{ "BeginGridView",         [](HeadlessGUIApp& a) { a.BeginGridView(a.idManager.CreateStaticID(), &g_selected); } },
        MisuseCase{ "BeginTable",            [](HeadlessGUIApp& a) { a.BeginTable(a.idManager.CreateStaticID(), g_columns, g_tableState); } },
        MisuseCase{ "BeginTabBar",           [](HeadlessGUIApp& a) { a.BeginTabBar(a.idManager.CreateStaticID(), &g_selected, [](int, int) {}); } },
        MisuseCase{ "BeginDockingContainer", [](HeadlessGUIApp& a) { a.BeginDockingContainer(a.idManager.CreateStaticID(), &g_dockingLayout); } },
        MisuseCase{ "BeginDisabled",         [](HeadlessGUIApp& a) { a.BeginDisabled(); } },
        MisuseCase{ "BeginSelected",         [](HeadlessGUIApp& a) { a.BeginSelected(); } },
        MisuseCase{ "BeginIndeterminate",    [](HeadlessGUIApp& a) { a.BeginIndeterminate(); } },
        MisuseCase{ "PushParent",            [](HeadlessGUIApp& a) { (void)a.idManager.PushParent(a.idManager.CreateStaticID()); } }
    ), CaseName);

    // A menu that is open, left without its EndMenu.
    TEST(MisuseTest, AnOpenMenuLeftWithoutEndMenu)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        const ID menuID = app.idManager.CreateStaticID();
        const ID itemID = app.idManager.CreateStaticID();
        bool leak = false;
        auto draw = [&] {
            app.BeginRootWindow();
            if (app.BeginMenu(menuID, "File"))
            {
                app.MenuItem(itemID, "Open");
            }
            if (!leak)
                app.EndMenu();
            app.EndRootWindow();
        };
        app.Frame(draw);
        app.ClickAt(app.CenterOf(menuID));
        app.Frame(draw);
        app.Frame(draw);
        ASSERT_TRUE(app.menuManager.IsOpen());

        MisuseReports reports;
        leak = true;
        app.Frame(draw);
        EXPECT_GE(reports.Count(), 1u);
        ExpectTheNextFrameWorks(app);
        app.Cleanup();
    }

    // A window declared and left without its EndWindow.
    TEST(MisuseTest, AWindowLeftWithoutEndWindow)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        const ID windowID = app.idManager.CreateStaticID();
        MisuseReports reports;
        app.Frame([&] {
            if (app.BeginWindow(windowID, "Leaky"))
                app.Label("inside");   // and no EndWindow
        });
        EXPECT_GE(reports.Count(), 1u);
        ExpectTheNextFrameWorks(app);
        app.Cleanup();
    }

    // ---- Controls in the wrong place, and bad IDs ----

    // A control emitted outside any frame: reported, nothing built, the next frame works.
    TEST(MisuseTest, AControlOutsideAFrame)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        MisuseReports reports;
        app.Button(app.idManager.CreateStaticID(), "Nowhere", [] {});
        EXPECT_GE(reports.Count(), 1u);
        ExpectTheNextFrameWorks(app);
        app.Cleanup();
    }

    TEST(MisuseTest, AControlInAFrameButOutsideAWindow)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        MisuseReports reports;
        app.Frame([&] { app.Button(app.idManager.CreateStaticID(), "Nowhere", [] {}); });
        EXPECT_GE(reports.Count(), 1u);
        ExpectTheNextFrameWorks(app);
        app.Cleanup();
    }

    INSTANTIATE_TEST_SUITE_P(MisuseIDs, BeginLeftOpenTest, ::testing::Values(
        MisuseCase{ "TheSameIDTwice", [](HeadlessGUIApp& a) {
            const ID id = a.idManager.CreateStaticID();
            a.Button(id, "One", [] {});
            a.Button(id, "Two", [] {}); } },
        MisuseCase{ "AnInvalidID", [](HeadlessGUIApp& a) { a.Button(ID::Invalid(), "Invalid", [] {}); } },
        MisuseCase{ "AnInvalidStyle", [](HeadlessGUIApp& a) { a.Label(a.idManager.CreateStaticID(), "Unstyled", StyleRef::Invalid()); } }
    ), CaseName);

    // A released static ID used again: it's just an unknown ID now, not a crash.
    TEST(MisuseTest, AReleasedIDUsedAgain)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        const ID id = app.idManager.CreateStaticID();
        auto draw = [&] { app.BeginRootWindow(); app.Button(id, "Stale", [] {}); app.EndRootWindow(); };
        app.Frame(draw);
        ASSERT_TRUE(app.idManager.ReleaseStaticID(id));
        app.Frame(draw);
        app.ClickAt({ 20, 20 });
        app.Frame(draw);
        ExpectTheNextFrameWorks(app);
        app.Cleanup();
    }

    // ---- Styles and themes ----

    TEST(MisuseTest, AnUnknownCssClassGivesAnInvalidStyle)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        MisuseReports reports;
        const StyleRef style = app.theme.GetStyle("no-such-class");
        EXPECT_EQ(style, StyleRef::Invalid());
        app.Frame([&] {
            app.BeginRootWindow();
            app.Label(app.idManager.CreateStaticID(), "Unknown class", style);
            app.EndRootWindow();
        });
        ExpectTheNextFrameWorks(app);
        app.Cleanup();
    }

    // A theme that is missing or can't be parsed: reported, the current theme stays.
    TEST(MisuseTest, AMissingThemeKeepsTheCurrentOne)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        MisuseReports reports;
        app.RequestReloadTheme("themes/no-such-theme.css", 1.0f);
        ExpectTheNextFrameWorks(app);
        EXPECT_GE(reports.Count(), 1u);
        app.Cleanup();
    }

    TEST(MisuseTest, AMalformedThemeKeepsTheCurrentOne)
    {
        const char* path = "misuse-malformed.css";
        {
            std::ofstream out(path, std::ios::binary);
            out << ".button { color: ; padding: 4px 4px 4px 4px 4px; } }}} @@@ { .x { background-color: rgb(1,2";
        }
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        MisuseReports reports;
        app.RequestReloadTheme(path, 1.0f);
        ExpectTheNextFrameWorks(app);
        std::remove(path);
        app.Cleanup();
    }

    TEST(MisuseTest, AThemeReloadRequestedMidFrame)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        app.Frame([&] {
            app.BeginRootWindow();
            app.RequestReloadTheme("themes/light.css", 1.25f);
            app.Button(app.idManager.CreateStaticID(), "During", [] {});
            app.EndRootWindow();
        });
        ExpectTheNextFrameWorks(app);
        app.Cleanup();
    }

    TEST(MisuseTest, AMissingIconFile)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        const IconID icon = app.textSystem->RegisterIconFromSVGFile("icons/no-such-icon.svg");
        app.Frame([&] {
            app.BeginRootWindow();
            app.Button(app.idManager.CreateStaticID(), icon, [] {});
            app.EndRootWindow();
        });
        ExpectTheNextFrameWorks(app);
        app.Cleanup();
    }

    // ---- Values ----

    class OddValueTest : public ::testing::TestWithParam<MisuseCase> {};

    // Odd but possible values: no report needed, just no crash, and the frame works.
    TEST_P(OddValueTest, LaysOut)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        for (int i = 0; i < 2; i++)
            app.Frame([&] {
                app.BeginRootWindow();
                GetParam().misuse(app);
                app.EndRootWindow();
            });
        ExpectTheNextFrameWorks(app);
        app.Cleanup();
    }

    float g_float = 0.0f;
    int   g_int = 0;
    std::string g_string;

    INSTANTIATE_TEST_SUITE_P(Misuse, OddValueTest, ::testing::Values(
        MisuseCase{ "NaNField",           [](HeadlessGUIApp& a) { g_float = std::numeric_limits<float>::quiet_NaN(); a.Field(g_float); } },
        MisuseCase{ "InfinityField",      [](HeadlessGUIApp& a) { g_float = std::numeric_limits<float>::infinity(); a.Field(g_float); } },
        MisuseCase{ "NaNSlider",          [](HeadlessGUIApp& a) { g_float = std::numeric_limits<float>::quiet_NaN(); a.SliderField(a.idManager.CreateStaticID(), g_float, 0.0f, 1.0f, "NaN"); } },
        MisuseCase{ "SliderMinAboveMax",  [](HeadlessGUIApp& a) { g_float = 0.5f; a.SliderField(a.idManager.CreateStaticID(), g_float, 1.0f, 0.0f, "Backwards"); } },
        MisuseCase{ "SliderEmptyRange",   [](HeadlessGUIApp& a) { g_int = 3; a.Slider(a.idManager.CreateStaticID(), g_int, 5, 5); } },
        MisuseCase{ "IntSliderOutOfRange",[](HeadlessGUIApp& a) { g_int = 1000; a.Slider(a.idManager.CreateStaticID(), g_int, 0, 10); } },
        MisuseCase{ "EmptyLabel",         [](HeadlessGUIApp& a) { a.Label(a.idManager.CreateStaticID(), ""); } },
        MisuseCase{ "HugeLabel",          [](HeadlessGUIApp& a) { a.Label(a.idManager.CreateStaticID(), std::string(200000, 'x')); } },
        MisuseCase{ "InvalidUtf8Label",   [](HeadlessGUIApp& a) { a.Label(a.idManager.CreateStaticID(), "\xff\xfe\xc3 \xe2\x82 \xf0\x9f"); } },
        MisuseCase{ "InvalidUtf8Field",   [](HeadlessGUIApp& a) { g_string = "\xc3\x28\xa0\xa1"; a.Field(a.idManager.CreateStaticID(), g_string); } },
        MisuseCase{ "NegativeSize",       [](HeadlessGUIApp& a) { a.Button(a.idManager.CreateStaticID(), "Negative", [] {}, LayoutOverride{}.Size(-50, -20)); } },
        MisuseCase{ "ZeroSize",           [](HeadlessGUIApp& a) { a.Button(a.idManager.CreateStaticID(), "Zero", [] {}, LayoutOverride{}.Size(0, 0)); } },
        MisuseCase{ "HugeSize",           [](HeadlessGUIApp& a) { a.Button(a.idManager.CreateStaticID(), "Huge", [] {}, LayoutOverride{}.Size(1000000, 1000000)); } },
        MisuseCase{ "ListPastSixteenBits",[](HeadlessGUIApp& a) {
            static int selected = 0;
            if (a.BeginListView(a.idManager.CreateStaticID(), &selected, 70000))
            {
                for (int i = 0; i < 70000; i++)
                    a.ListItem(std::string_view("row"));
                a.EndListView();
            } } }
    ), CaseName);

    // ---- Callbacks ----

    TEST(MisuseTest, ANullCallback)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        const ID buttonID = app.idManager.CreateStaticID();
        auto draw = [&] { app.BeginRootWindow(); app.Button(buttonID, "Nothing", nullptr); app.EndRootWindow(); };
        app.Frame(draw);
        app.ClickAt(app.CenterOf(buttonID));
        app.Frame(draw);
        ExpectTheNextFrameWorks(app);
        app.Cleanup();
    }

    // A callback that clears the items it is called from.
    TEST(MisuseTest, ACallbackThatClearsItsOwnList)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        std::vector<std::string> items = { "a", "b", "c" };
        std::vector<ID> ids = { app.idManager.CreateStaticID(), app.idManager.CreateStaticID(), app.idManager.CreateStaticID() };
        auto draw = [&] {
            app.BeginRootWindow();
            for (size_t i = 0; i < items.size(); i++)
                app.Button(ids[i], items[i], [&] { items.clear(); });
            app.EndRootWindow();
        };
        app.Frame(draw);
        app.ClickAt(app.CenterOf(ids[1]));
        app.Frame(draw);
        EXPECT_TRUE(items.empty());
        ExpectTheNextFrameWorks(app);
        app.Cleanup();
    }

    // ---- Lifecycle ----
    //
    // Initialize / Cleanup misuse is tested on a real app (GpuApplicationInit.*): the
    // harness replaces both.

    // Input for an element that is gone by the time it arrives.
    TEST(MisuseTest, InputForAnElementThatIsGone)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        const ID buttonID = app.idManager.CreateStaticID();
        bool show = true;
        int clicks = 0;
        auto draw = [&] {
            app.BeginRootWindow();
            if (show)
                app.Button(buttonID, "Gone", [&] { clicks++; });
            app.EndRootWindow();
        };
        app.Frame(draw);
        const int2 at = app.CenterOf(buttonID);
        app.InjectMouseMove(at);
        app.InjectMouseDown(at);
        show = false;
        app.Frame(draw);
        app.InjectMouseUp(at);
        app.Frame(draw);
        EXPECT_EQ(clicks, 0);
        ExpectTheNextFrameWorks(app);
        app.Cleanup();
    }

    // A window that closes itself from its own code, then isn't declared again.
    TEST(MisuseTest, AWindowClosedFromItsOwnCode)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        const ID windowID = app.idManager.CreateStaticID();
        bool open = true, closeNow = false;
        auto draw = [&] {
            if (!open)
                return;
            if (app.BeginWindow(windowID, "Closes itself", LayoutOverride{}.Size(300, 200)))
            {
                app.Label("inside");
                if (closeNow)
                {
                    (void)app.windowManager->RequestUserClose(windowID);
                    open = false;
                }
            }
            app.EndWindow();
        };
        app.Frame(draw);
        app.Frame(draw);
        closeNow = true;
        app.Frame(draw);
        app.Frame(draw);
        EXPECT_FALSE(open);
        EXPECT_TRUE(app.windowManager->WasUserClosed(windowID));
        ExpectTheNextFrameWorks(app);
        app.Cleanup();
    }

    // A callback that throws: the exception reaches the caller of the frame, and the next
    // frame still works.
    TEST(MisuseTest, ACallbackThatThrows)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        const ID buttonID = app.idManager.CreateStaticID();
        auto draw = [&] {
            app.BeginRootWindow();
            app.Button(buttonID, "Throws", [] { throw std::runtime_error("from a callback"); });
            app.EndRootWindow();
        };
        app.Frame(draw);
        app.ClickAt(app.CenterOf(buttonID));
        bool thrown = false;
        try { app.Frame(draw); }
        catch (const std::runtime_error&) { thrown = true; }
        EXPECT_TRUE(thrown);
        ExpectTheNextFrameWorks(app);
        app.Cleanup();
    }

    // =====================================================================
    // Round two
    // =====================================================================

    // ---- Parts of a container used outside it ----

    INSTANTIATE_TEST_SUITE_P(MisuseParts, EndWithoutBeginTest, ::testing::Values(
        MisuseCase{ "MenuItemOutsideAMenu",        [](HeadlessGUIApp& a) { a.MenuItem(a.idManager.CreateStaticID(), "Stray"); } },
        MisuseCase{ "ListItemOutsideAList",        [](HeadlessGUIApp& a) { a.ListItem(a.idManager.CreateStaticID(), std::string_view("Stray"), LayoutOverride{}); } },
        MisuseCase{ "TabOutsideATabBar",           [](HeadlessGUIApp& a) { a.Tab(a.idManager.CreateStaticID(), "Stray"); } },
        MisuseCase{ "TreeCellOutsideATable",       [](HeadlessGUIApp& a) { bool open = true; a.TreeCell("Stray", 0, &open); } },
        MisuseCase{ "CellOutsideATable",           [](HeadlessGUIApp& a) { a.Cell("Stray"); } },
        MisuseCase{ "ReorderableItemOutsideAGroup",[](HeadlessGUIApp& a) { a.BeginReorderableItem(a.idManager.CreateStaticID()); a.EndReorderableItem(); } }
    ), CaseName);

    // ---- Scale and odd data ----

    std::vector<VirtualTableColumn> g_bigTableColumns;
    TableState g_bigTableState;
    NodeView g_badNodeView;

    INSTANTIATE_TEST_SUITE_P(MisuseScale, OddValueTest, ::testing::Values(
        MisuseCase{ "DeepNesting", [](HeadlessGUIApp& a) {
            for (int i = 0; i < 3000; i++) a.BeginVertical();
            a.Label(a.idManager.CreateStaticID(), "deep");
            for (int i = 0; i < 3000; i++) a.EndVertical(); } },
        MisuseCase{ "FortyThousandLabels", [](HeadlessGUIApp& a) {
            for (int i = 0; i < 40000; i++) a.Label(std::string_view("row")); } },
        MisuseCase{ "ATableOfAHundredThousandRows", [](HeadlessGUIApp& a) {
            if (g_bigTableColumns.empty())
            {
                g_bigTableColumns.resize(2);
                g_bigTableColumns[0].column.header = "A";
                g_bigTableColumns[1].column.header = "B";
                for (auto& c : g_bigTableColumns) c.generateCell = [&a](int row) { a.Cell(std::to_string(row)); };
            }
            a.VirtualTable(a.idManager.CreateStaticID(), g_bigTableState, g_bigTableColumns, 100000, 0, {}, LayoutOverride{}.Size(400, 300)); } },
        MisuseCase{ "ATableWithNoColumns", [](HeadlessGUIApp& a) {
            static TableState state;
            a.VirtualTable(a.idManager.CreateStaticID(), state, std::span<VirtualTableColumn>{}, 10, 0, {}, LayoutOverride{}.Size(400, 300)); } },
        MisuseCase{ "ATableWithNegativeRows", [](HeadlessGUIApp& a) {
            static TableState state;
            static VirtualTableColumn column[1];
            a.VirtualTable(a.idManager.CreateStaticID(), state, column, -5, -10, {}, LayoutOverride{}.Size(400, 300)); } },
        MisuseCase{ "NodeWiresToNodesThatDontExist", [](HeadlessGUIApp& a) {
            if (g_badNodeView.nodes.empty())
            {
                g_badNodeView.nodes.resize(2);
                g_badNodeView.nodes[0].stableID = a.idManager.CreateStaticID();
                g_badNodeView.nodes[0].properties.resize(2);
                g_badNodeView.nodes[0].properties[0].input = PropertyEndpoint{ 99, 0 };   // no node 99
                g_badNodeView.nodes[0].properties[1].input = PropertyEndpoint{ 1, 7 };    // node 1 has no property 7
                g_badNodeView.nodes[1].stableID = a.idManager.CreateStaticID();
                g_badNodeView.nodes[1].pos = { 2000000000, -2000000000 };
                g_badNodeView.nodes[1].properties.resize(1);
                g_badNodeView.nodes[1].properties[0].input = PropertyEndpoint{ 1, 0 };    // wired to itself
                g_badNodeView.ordering = { 5, -1, 0 };                                     // bad z-order
            }
            a.RenderNodeView(g_badNodeView); } },
        MisuseCase{ "AnEmptyNodeView", [](HeadlessGUIApp& a) { static NodeView view; a.RenderNodeView(view); } },
        MisuseCase{ "ATooltipForNoElement", [](HeadlessGUIApp& a) { a.SetTooltip(ID::Invalid(), "for nothing"); } }
    ), CaseName);

    // The same window declared twice in one frame.
    TEST(MisuseTest, TheSameWindowTwiceInAFrame)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        const ID windowID = app.idManager.CreateStaticID();
        for (int i = 0; i < 3; i++)
            app.Frame([&] {
                for (int w = 0; w < 2; w++)
                {
                    if (app.BeginWindow(windowID, "Twice", LayoutOverride{}.Size(200, 100)))
                        app.Label("inside");
                    app.EndWindow();
                }
            });
        ExpectTheNextFrameWorks(app);
        app.Cleanup();
    }

    // ---- Re-entry, and things that vanish mid-gesture ----

    // A frame run from inside a callback of the frame: reported, ignored.
    TEST(MisuseTest, AFrameRunFromInsideACallback)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        const ID buttonID = app.idManager.CreateStaticID();
        std::function<void()> draw;
        draw = [&] {
            app.BeginRootWindow();
            app.Button(buttonID, "Re-enter", [&] { app.Frame(draw); });
            app.EndRootWindow();
        };
        app.Frame(draw);
        MisuseReports reports;
        app.ClickAt(app.CenterOf(buttonID));
        app.Frame(draw);
        EXPECT_GE(reports.Count(), 1u);
        ExpectTheNextFrameWorks(app);
        app.Cleanup();
    }

    // A field that stops being declared while it has the focus and is being typed into.
    TEST(MisuseTest, AFieldRemovedWhileTyping)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        const ID fieldID = app.idManager.CreateStaticID();
        std::string text = "abc";
        bool show = true;
        auto draw = [&] {
            app.BeginRootWindow();
            if (show) app.Field(fieldID, text);
            app.EndRootWindow();
        };
        app.Frame(draw);
        app.ClickAt(app.CenterOf(fieldID));
        app.Frame(draw);
        app.InjectText("de");
        show = false;
        app.Frame(draw);
        app.InjectText("fg");
        app.InjectKey(KeyCode::Backspace, KeyAction::KeyDown);
        app.Frame(draw);
        app.Frame(draw);
        show = true;
        app.Frame(draw);
        app.Frame(draw);
        ExpectTheNextFrameWorks(app);
        app.Cleanup();
    }

    // A reorder drag whose group disappears before the release.
    TEST(MisuseTest, ADragWhoseGroupDisappears)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        const ID groupID = app.idManager.CreateStaticID();
        const ID itemIDs[3] = { app.idManager.CreateStaticID(), app.idManager.CreateStaticID(), app.idManager.CreateStaticID() };
        bool show = true;
        int reorders = 0;
        auto draw = [&] {
            app.BeginRootWindow();
            if (show && app.BeginReorderableGroup(groupID, ReorderDirection::Vertical, [&](int, int) { reorders++; }))
            {
                for (const ID id : itemIDs)
                {
                    app.BeginReorderableItem(id, LayoutOverride{}.Size(100, 20));
                    app.Label("item");
                    app.EndReorderableItem();
                }
                app.EndReorderableGroup();
            }
            app.EndRootWindow();
        };
        app.Frame(draw);
        const int2 from = app.CenterOf(itemIDs[0]);
        app.InjectMouseMove(from);
        app.InjectMouseDown(from);
        app.Frame(draw);
        app.InjectMouseMove({ from.x, from.y + 30 }, MouseButtons::Left);
        app.Frame(draw);
        show = false;
        app.InjectMouseMove({ from.x, from.y + 50 }, MouseButtons::Left);
        app.Frame(draw);
        app.InjectMouseUp({ from.x, from.y + 50 });
        app.Frame(draw);
        EXPECT_EQ(reorders, 0) << "a drop into a group that is gone reorders nothing";
        show = true;
        ExpectTheNextFrameWorks(app);
        app.Cleanup();
    }

    // A node dragged while the nodes are removed from under it.
    TEST(MisuseTest, NodesRemovedDuringANodeDrag)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        NodeView view;
        view.nodes.resize(2);
        for (int i = 0; i < 2; i++)
        {
            view.nodes[size_t(i)].stableID = app.idManager.CreateStaticID();
            view.nodes[size_t(i)].title = "N";
            view.nodes[size_t(i)].pos = { 100 + 300 * i, 100 };
            view.nodes[size_t(i)].properties.resize(1);
            view.nodes[size_t(i)].properties[0].output = true;
        }
        int edits = 0;
        view.onEdit = [&](const NodeViewEdit&) { edits++; };
        auto draw = [&] {
            app.BeginRootWindow({ 1920, 1080 }, ElementFlags::None, app.theme.mainWindowStyleRef);
            app.RenderNodeView(view);
            app.EndRootWindow();
        };
        app.Frame(draw);
        app.Frame(draw);
        const IntRect n = app.GetElementRect(view.nodes[1].stableID);
        const int2 from{ n.x + n.w / 2, n.y + app.theme.nodeGridSize };
        app.InjectMouseMove(from);
        app.InjectMouseDown(from);
        app.Frame(draw);
        app.InjectMouseMove({ from.x + 40, from.y + 40 }, MouseButtons::Left);
        app.Frame(draw);
        view.nodes.clear();
        app.InjectMouseMove({ from.x + 80, from.y + 80 }, MouseButtons::Left);
        app.Frame(draw);
        app.InjectMouseUp({ from.x + 80, from.y + 80 });
        app.Frame(draw);
        app.Frame(draw);
        ExpectTheNextFrameWorks(app);
        app.Cleanup();
    }

    // Each kind is reported once, not every frame it happens.
    TEST(MisuseTest, ARepeatedMistakeIsReportedOnce)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        MisuseReports reports;
        for (int i = 0; i < 5; i++)
            app.Frame([&] {
                app.BeginRootWindow();
                app.EndDisabled();
                app.EndRootWindow();
            });
        EXPECT_EQ(reports.Count(), 1u) << reports.All();
        app.Cleanup();
    }
}
