// Scrollbar emission for VirtualTable under different sizing regimes.
//
// Production gates each bar on TableState's overflow flags, which a
// POST-LAYOUT sampler on the section body sets (contentSize vs innerRect).
// The harness skips per-element post-layout, so this fixture fires the
// registered entries manually after each frame - the same loop production
// runs in gui_application.cpp after Layout(). That makes the real emission
// path observable headless: frame N samples, frame N+1 emits the bars.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_core/elements/gui_element_storage.h>
#include <gui_core/graphics/gui_custom_draw_context.h>

#include <vector>

namespace tachyon
{
    using GUITest::HeadlessGUIApp;

    namespace
    {
        // SettingsPanelState is protected on GUIApplication (the demo reaches
        // it via inheritance); expose it the same way for the fixture.
        class SettingsHostApp : public HeadlessGUIApp
        {
        public:
            using SettingsState = GUIApplication::SettingsPanelState;
            using GUIApplication::RenderSettingsPanel;
            SettingsState settings;
        };

        class TableScrollbarTest : public ::testing::Test
        {
        protected:
            SettingsHostApp app;
            tachyon::TableState state;
            std::vector<tachyon::VirtualTableColumn> cols;

            void SetUp() override
            {
                ASSERT_TRUE(app.Initialize());
                cols.resize(2);
                cols[0].column = { .header = "Name", .initialWidth = 120, .minWidth = 40, .maxWidth = 400 };
                cols[0].generateCell = [this](int r) { (void)r; app.Cell("name"); };
                cols[1].column = { .header = "Value", .initialWidth = 120, .minWidth = 40, .maxWidth = 400 };
                cols[1].generateCell = [this](int r) { (void)r; app.Cell("value"); };
            }
            void TearDown() override { app.Cleanup(); }

            // Mirror production's post-layout pass (the harness skips it).
            void FirePostLayout()
            {
                for (const auto& entry : app.elementStorage.GetPostLayoutEntries())
                {
                    const auto* er = app.elementStorage.GetElementRectPtr(entry.ownerRef);
                    if (!er) continue;
                    PostLayoutContext plCtx{ app.elementStorage, entry.ownerRef,
                                             er->rect, er->innerRect, er->contentTransform };
                    entry.callback(plCtx);
                }
            }

            void Frame(std::function<void()> build)
            {
                app.Frame(std::move(build));
                FirePostLayout();
            }

            // Frozen-panel style: explicit table size.
            void BuildFixedSize()
            {
                app.BeginRootWindow();
                app.VirtualTable(app.idManager.AllocateDynamicID("tbl"), state,
                                 std::span<tachyon::VirtualTableColumn>(cols),
                                 /*rows*/200, 0, app.theme.tableStyleRef,
                                 tachyon::LayoutOverride{}.Width(500).Height(300));
                app.EndRootWindow();
            }

            // Shortcuts-page style: FlexGrow table inside a bounded
            // (flex-basis:0) chain that fills a fixed-height pane.
            void BuildFlexGrow()
            {
                app.BeginRootWindow();
                app.BeginArea(app.idManager.AllocateDynamicID("pane"),
                              app.theme.settingsPageStyleRef,
                              tachyon::LayoutOverride{}.Size(700, 300).NoGrow().NoShrink());
                app.BeginHorizontal(app.idManager.AllocateDynamicID("fill"),
                                    app.theme.flexFillRowStyleRef,
                                    tachyon::LayoutOverride::Auto().FlexGrow(1));
                app.VirtualTable(app.idManager.AllocateDynamicID("tbl"), state,
                                 std::span<tachyon::VirtualTableColumn>(cols),
                                 /*rows*/200, 0, app.theme.tableStyleRef,
                                 tachyon::LayoutOverride::Auto().FlexGrow(1));
                app.EndHorizontal();
                app.EndArea();
                app.EndRootWindow();
            }

            tachyon::IntRect BarRect() const { return app.GetElementRect(state.internal.vertScrollbarID); }

            // A table whose columns (sum 500) far exceed the host width, using
            // the given outer style. The container is fixed at 300 wide.
            void BuildWideColumns(tachyon::StyleRef tableStyle)
            {
                std::vector<tachyon::VirtualTableColumn> wide(2);
                wide[0].column = { .header = "A", .initialWidth = 250, .minWidth = 200, .maxWidth = 400 };
                wide[0].generateCell = [this](int r){ (void)r; app.Cell("aaaa"); };
                wide[1].column = { .header = "B", .initialWidth = 250, .minWidth = 200, .maxWidth = 400 };
                wide[1].generateCell = [this](int r){ (void)r; app.Cell("bbbb"); };
                // Keep them alive for the frame; static so the sizer pointers
                // (if any) stay valid like the demo's persistent columns.
                static std::vector<tachyon::VirtualTableColumn> held;
                held = std::move(wide);

                app.BeginRootWindow();
                app.BeginArea(app.idManager.AllocateDynamicID("wpane"),
                              app.theme.flexFillRowStyleRef,
                              tachyon::LayoutOverride{}.Size(300, 300).NoGrow().NoShrink());
                app.VirtualTable(app.idManager.AllocateDynamicID("wtbl"), state,
                                 std::span<tachyon::VirtualTableColumn>(held),
                                 /*rows*/200, 0, tableStyle,
                                 tachyon::LayoutOverride{}.FlexGrow(1));
                app.EndArea();
                app.EndRootWindow();
            }
        };

        TEST_F(TableScrollbarTest, FixedSizeTableEmitsVerticalScrollbar)
        {
            Frame([&] { BuildFixedSize(); });   // frame 1: sampler runs
            Frame([&] { BuildFixedSize(); });   // frame 2: overflow flags consumed -> bar emitted
            Frame([&] { BuildFixedSize(); });   // frame 3: bar laid out with sampled viewport

            ASSERT_TRUE(app.HasElement(state.internal.vertScrollbarID))
                << "200 rows in a 300px table must emit the vertical scrollbar";
            const auto r = BarRect();
            EXPECT_GT(r.h, 50) << "bar should span ~the body height, got " << r.h;
        }

        TEST_F(TableScrollbarTest, FlexGrowTableEmitsVerticalScrollbar)
        {
            Frame([&] { BuildFlexGrow(); });
            Frame([&] { BuildFlexGrow(); });
            Frame([&] { BuildFlexGrow(); });

            ASSERT_TRUE(app.HasElement(state.internal.vertScrollbarID))
                << "200 rows in a 300px flex-grow pane must emit the vertical scrollbar";
            const auto r = BarRect();
            EXPECT_GT(r.h, 50) << "bar should span ~the body height, got " << r.h;
        }

        // The page tree's width is CSS pixels: its default stays 200 at any UI scale.
        TEST(SettingsPanelTest, TreeWidthIsInCssPixels)
        {
            SettingsHostApp app;
            app.SetUIScale(2.0f);
            ASSERT_TRUE(app.Initialize());
            app.settings.AddPage(app.idManager.CreateStaticID(), "Page", [] {});
            app.Frame([&] {
                app.BeginRootWindow();
                app.RenderSettingsPanel(app.settings);
                app.EndRootWindow();
            });
            EXPECT_EQ(app.settings.treeWidth, 200);
            app.Cleanup();
        }

        // Full settings-panel path: the REAL RenderSettingsPanel hosting a
        // page shaped like the demo's Shortcuts page (title, search row,
        // category-tree split, flex-grow table). Mirrors panel_settings.cpp.
        TEST_F(TableScrollbarTest, ShortcutsShapedSettingsPageEmitsVerticalScrollbar)
        {
            auto& sp = app.settings;
            std::vector<tachyon::TreeViewRow> catRows;
            std::vector<int>           catVisible;
            tachyon::TableState catTable;
            std::string searchText;
            int  catWidth = 160;

            sp.AddPage(app.idManager.CreateStaticID(), "Shortcuts", [&]
            {
                app.Text(app.idManager.AllocateDynamicID("title"), "Shortcuts",
                         app.theme.textStyleRef);

                app.BeginHorizontal(app.idManager.AllocateDynamicID("searchRow"),
                                    app.theme.searchRowStyleRef,
                                    tachyon::LayoutOverride::Auto().NoShrink());
                app.BeginLabelGroup(140, 40, 500, /*resizable*/false);
                app.Label("Search");
                app.Field(searchText, TextEditBehavior::ContinuousUpdate);
                app.EndLabelGroup();
                app.EndHorizontal();

                app.BeginHorizontal(app.idManager.AllocateDynamicID("split"),
                                    app.theme.flexFillRowStyleRef,
                                    tachyon::LayoutOverride::Auto().FlexGrow(1));

                catRows.clear();
                tachyon::TreeViewRow r{};
                r.id   = app.idManager.AllocateDynamicID("cat");
                r.name = "All";
                catRows.push_back(std::move(r));
                app.RebuildTreeViewIndex(catRows);
                app.VirtualTreeView(app.idManager.AllocateDynamicID("catTree"),
                                    catRows, catVisible, catTable,
                                    app.theme.treeViewStyleRef,
                                    tachyon::LayoutOverride::Auto().FixedWidth(CssPx(catWidth)).NoGrow());

                tachyon::Element sizer{};
                sizer.id       = app.idManager.AllocateDynamicID("catSizer");
                sizer.styleRef = app.theme.dockSizerHorzStyleRef;
                sizer.state    = app.guiStateManager.GetCurrentState(sizer.id);
                sizer.eventHandler = tachyon::MakeHorizontalSizerHandler(
                    app.guiStateManager, catWidth, 100, 400);
                auto sRes = app.elementStorage.Add(std::move(sizer),
                                                   tachyon::LayoutOverride::FixedWidth(4));
                VERIFY(sRes.status);
                VERIFY(app.windowRegistry.Add(sRes.ref));

                app.BeginHorizontal(app.idManager.AllocateDynamicID("gridCol"),
                                    app.theme.flexFillRowStyleRef,
                                    tachyon::LayoutOverride::Auto().FlexGrow(1));
                app.VirtualTable(app.idManager.AllocateDynamicID("tbl"), state,
                                 std::span<tachyon::VirtualTableColumn>(cols),
                                 /*rows*/200, 0, app.theme.tableStyleRef,
                                 tachyon::LayoutOverride::Auto().FlexGrow(1));
                app.EndHorizontal();

                app.EndHorizontal(); // split
            });

            auto build = [&]
            {
                app.BeginRootWindow();
                // Faithful dock hosting: a fixed-size leaf stand-in, then the
                // UNSIZED dock-content (flex-grow:1, no explicit size - it gets
                // its height from the leaf via flex-shrink), exactly as
                // EmitDockLeaf wraps a panel's onPanelGUI.
                app.BeginArea(app.idManager.AllocateDynamicID("leaf"),
                              app.theme.flexFillRowStyleRef,
                              tachyon::LayoutOverride{}.Size(900, 400).NoGrow().NoShrink());
                app.BeginArea(app.idManager.AllocateDynamicID("dockContent"),
                              app.theme.dockContentStyleRef,
                              tachyon::LayoutOverride{}.FlexGrow(1));
                app.RenderSettingsPanel(sp);
                app.EndArea(); // dockContent
                app.EndArea(); // leaf
                app.EndRootWindow();
            };

            Frame(build);
            Frame(build);
            Frame(build);

            ASSERT_TRUE(app.HasElement(state.internal.vertScrollbarID))
                << "the shortcuts-shaped page must emit the table's vertical scrollbar";
            const auto bar = BarRect();
            EXPECT_GT(bar.h, 50) << "bar should span ~the body height, got " << bar.h;
            // The bar must also sit INSIDE the visible pane (an off-screen or
            // clipped-away bar is as good as missing).
            EXPECT_LT(bar.y, 400) << "bar lies below the visible pane";
            EXPECT_LT(bar.x, 900) << "bar lies right of the visible pane";
        }

        // With tableFillStyleRef the table bounds to its container (flex-basis:0):
        // over-wide columns scroll inside a table that doesn't grow, and the
        // vertical bar stays inside the visible pane.
        TEST_F(TableScrollbarTest, OverflowingColumnsKeepVerticalBarInBounds)
        {
            Frame([&] { BuildWideColumns(app.theme.tableFillStyleRef); });
            Frame([&] { BuildWideColumns(app.theme.tableFillStyleRef); });
            Frame([&] { BuildWideColumns(app.theme.tableFillStyleRef); });

            ASSERT_TRUE(app.HasElement(state.internal.vertScrollbarID));
            const auto bar  = BarRect();
            const auto body = app.GetElementRect(state.SectionBodyID(0));
            EXPECT_GT(bar.h, 50);
            // The body bounds to the ~300px pane, not the 500px column content.
            EXPECT_LT(body.w, 350)
                << "body width " << body.w << " — the table grew to its column "
                   "content (500px) instead of bounding to the pane.";
            // And the V bar sits at the body's right edge, inside it.
            EXPECT_LE(bar.x + bar.w, body.x + body.w + 2)
                << "V scrollbar extends past the bounded body's right edge.";
        }

    } // namespace
}
