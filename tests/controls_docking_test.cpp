// Control-layer tests for the panel docking system (BeginDockingContainer /
// EndDockingContainer in controls_docking.cpp) driven through the headless
// harness: which panels draw, the enable/disable lifecycle, split layout and
// the sizer drag. (dock_node_test.cpp covers the pure tree operations.)

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_controls/gui_dock_node.h>
#include <gui_core/elements/gui_element_storage.h>

namespace GUI
{
    using GUITest::HeadlessGUIApp;

    namespace
    {
        class DockingTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;
            GUI::DockingLayout layout;
            GUI::ID containerID{};
            GUI::ID panelA{}, panelB{}, panelC{};
            int drawsA = 0, drawsB = 0, drawsC = 0;
            int enables[3] = {}, disables[3] = {};

            void SetUp() override
            {
                app.Initialize();
                containerID = app.idManager.CreateStaticID();
                panelA = app.CreatePanel(layout.panels, "A", IconID::Invalid,
                                         [this] { drawsA++; }, {},
                                         [this] { enables[0]++; }, [this] { disables[0]++; });
                panelB = app.CreatePanel(layout.panels, "B", IconID::Invalid,
                                         [this] { drawsB++; }, {},
                                         [this] { enables[1]++; }, [this] { disables[1]++; });
                panelC = app.CreatePanel(layout.panels, "C", IconID::Invalid,
                                         [this] { drawsC++; }, {},
                                         [this] { enables[2]++; }, [this] { disables[2]++; });

                // [A] | [B, C] — left pane pinned at 250px.
                layout.root = MakeSplit(DockSplitMode::LeftPinned, 250,
                                        MakeLeaf({ panelA }),
                                        MakeLeaf({ panelB, panelC }));
            }

            void TearDown() override { app.Cleanup(); }

            void EmitDock()
            {
                app.BeginRootWindow();
                app.BeginDockingContainer(containerID, &layout,
                                          GUI::LayoutOverride{}.Size(800, 600));
                app.EndDockingContainer();
                app.EndRootWindow();
            }

            void Frame() { app.Frame([&] { EmitDock(); }); }

            // The content element of a leaf, as registered during construction.
            GUI::IntRect ContentRect(const DockNode* leaf) const
            {
                return app.GetElementRect(leaf->lastContentID);
            }
        };

        TEST_F(DockingTest, VisiblePanelsDrawHiddenTabsDoNot)
        {
            Frame();
            EXPECT_EQ(drawsA, 1) << "left leaf's only panel draws";
            EXPECT_EQ(drawsB, 1) << "right leaf's selected tab draws";
            EXPECT_EQ(drawsC, 0) << "the hidden tab must not draw";
        }

        TEST_F(DockingTest, EnableFiresOncePerVisibilityTransition)
        {
            Frame();
            Frame();
            EXPECT_EQ(enables[0], 1);
            EXPECT_EQ(enables[1], 1);
            EXPECT_EQ(enables[2], 0) << "hidden tab is not enabled";
            EXPECT_EQ(disables[0], 0);
        }

        TEST_F(DockingTest, SplitPinsFirstPaneAndStacksHorizontally)
        {
            Frame();
            const auto a = ContentRect(layout.root->first.get());
            const auto b = ContentRect(layout.root->second.get());
            ASSERT_GT(a.w, 0);
            ASSERT_GT(b.w, 0);
            EXPECT_LE(a.x + a.w, b.x) << "left pane sits left of the right pane";
            // Pinned at 250 minus leaf/content chrome; right pane absorbs the rest.
            EXPECT_NEAR(a.w, 250, 60);
            EXPECT_GT(b.w, a.w) << "the unpinned pane absorbs the remaining width";
        }

        TEST_F(DockingTest, SelectingTabSwitchesPanelAndFiresLifecycle)
        {
            Frame();
            ASSERT_EQ(drawsB, 1);
            ASSERT_EQ(drawsC, 0);

            // Switch the right leaf to its second tab (panel C).
            layout.root->second->selectedTab = 1;
            Frame();

            EXPECT_EQ(drawsC, 1) << "newly selected tab draws";
            EXPECT_EQ(drawsB, 1) << "deselected tab stops drawing";
            EXPECT_EQ(enables[2], 1)  << "panel C transitions hidden → visible";
            EXPECT_EQ(disables[1], 1) << "panel B transitions visible → hidden";
        }

        TEST_F(DockingTest, SizerDragResizesPinnedPane)
        {
            Frame();
            const auto a0 = ContentRect(layout.root->first.get());
            const auto b0 = ContentRect(layout.root->second.get());
            ASSERT_GT(a0.w, 0);
            ASSERT_GT(b0.w, 0);

            // The 4px sizer sits in the gap between the panes; grab its middle.
            const int sizerX = (a0.x + a0.w + b0.x) / 2;
            const GUI::int2 from{ sizerX, a0.y + a0.h / 2 };
            const GUI::int2 to  { sizerX + 80, from.y };
            const int before = layout.root->splitSize;

            app.InjectMouseMove(from);
            app.InjectMouseDown(from);
            Frame();
            app.InjectMouseMove(to, GUI::MouseButtons::Left);
            Frame();
            app.InjectMouseUp(to);
            Frame();

            EXPECT_EQ(layout.root->splitSize, before + 80)
                << "dragging the split sizer right grows the pinned pane";
        }

        // Split sizes are CSS pixels: at UI scale 2 the 250 split lays out 500 px,
        // and an 80 px drag moves it by 40.
        TEST_F(DockingTest, SplitSizeIsInCssPixels)
        {
            app.SetUIScale(2.0f);
            Frame();
            Frame();
            const DockNode* pinned = layout.root->first.get();
            EXPECT_EQ(app.GetElementRect(pinned->nodeID).w, 500);

            const auto a0 = ContentRect(layout.root->first.get());
            const auto b0 = ContentRect(layout.root->second.get());
            const int sizerX = (a0.x + a0.w + b0.x) / 2;
            const GUI::int2 from{ sizerX, a0.y + a0.h / 2 };
            const GUI::int2 to  { sizerX + 80, from.y };
            app.InjectMouseMove(from);
            app.InjectMouseDown(from);
            Frame();
            app.InjectMouseMove(to, GUI::MouseButtons::Left);
            Frame();
            app.InjectMouseUp(to);
            Frame();

            EXPECT_EQ(layout.root->splitSize, 290);
            EXPECT_EQ(app.GetElementRect(pinned->nodeID).w, 580);
        }

        TEST_F(DockingTest, SingleLeafFillsContainer)
        {
            layout.root = MakeLeaf({ panelA });
            Frame();
            const auto a = ContentRect(layout.root.get());
            ASSERT_GT(a.w, 0);
            // Content spans most of the 800x600 container (minus chrome/tab bar).
            EXPECT_GT(a.w, 700);
            EXPECT_GT(a.h, 450);
        }

        // ---- Floating dock windows ----

        // Static IDs a dock tree holds (node + tab-bar IDs, assigned on emit).
        static size_t CountTreeStaticIDs(const DockNode* n)
        {
            if (!n) return 0;
            size_t c = 0;
            if (n->nodeID   != ID::Invalid() && n->nodeID.GetMode()   == IDMode::Static) ++c;
            if (n->tabBarID != ID::Invalid() && n->tabBarID.GetMode() == IDMode::Static) ++c;
            if (!n->isLeaf) c += CountTreeStaticIDs(n->first.get()) + CountTreeStaticIDs(n->second.get());
            return c;
        }

        // A floating window that empties (its last tab dragged out) goes away and
        // releases every static ID it held, its dock tree's included.
        TEST_F(DockingTest, EmptiedFloatingWindowReleasesItsTreeIDs)
        {
            auto fw = std::make_unique<FloatingDockWindow>();
            fw->windowID = app.idManager.CreateStaticID();
            const ID floating = app.CreatePanel(fw->layout.panels, "F", IconID::Invalid, [] {}, {}, {}, {});
            fw->layout.root = MakeLeaf({ floating });
            FloatingDockWindow* window = fw.get();
            app.m_floatingDockWindows.push_back(std::move(fw));

            auto frame = [&] { app.Frame([&] { EmitDock(); app.EmitFloatingDockWindows(); }); };
            frame();
            frame();
            const size_t treeIDs = CountTreeStaticIDs(window->layout.root.get());
            ASSERT_GT(treeIDs, 0u) << "test setup: the floating tree was emitted";
            const size_t before = app.idManager.GetStaticIDCount();

            window->layout.root->panelIds.clear();   // last tab dragged out
            frame();

            ASSERT_TRUE(app.m_floatingDockWindows.empty());
            EXPECT_EQ(app.idManager.GetStaticIDCount(), before - 1 - treeIDs);
        }

        // Closing a floating window closes its panels: each gets onDisable, the
        // window goes away, and all its IDs are released.
        TEST_F(DockingTest, ClosingAFloatingWindowClosesItsPanels)
        {
            int enabled = 0, disabled = 0;
            auto fw = std::make_unique<FloatingDockWindow>();
            fw->windowID = app.idManager.CreateStaticID();
            const ID floating = app.CreatePanel(fw->layout.panels, "F", IconID::Invalid, [] {}, {},
                                                [&] { enabled++; }, [&] { disabled++; });
            fw->layout.root = MakeLeaf({ floating });
            FloatingDockWindow* window = fw.get();
            app.m_floatingDockWindows.push_back(std::move(fw));

            auto frame = [&] { app.Frame([&] { EmitDock(); app.EmitFloatingDockWindows(); }); };
            frame();
            frame();
            ASSERT_EQ(enabled, 1) << "test setup: the floating panel is shown";
            const size_t treeIDs = CountTreeStaticIDs(window->layout.root.get());
            const size_t before  = app.idManager.GetStaticIDCount();

            // A user close (the OS reports the window gone) ends here; the
            // headless harness has no OS windows, so call it directly.
            app.CloseFloatingDockWindow(0);
            frame();

            EXPECT_TRUE(app.m_floatingDockWindows.empty());
            EXPECT_EQ(disabled, 1) << "the panel is closed (onDisable), like closing its tab";
            EXPECT_EQ(app.idManager.GetStaticIDCount(), before - 1 - treeIDs);
        }

        // Closing a docked tab closes its panel for good: onDisable fires and the
        // Panel (and its state) is gone; a new panel can be created later.
        TEST_F(DockingTest, ClosingADockedTabDestroysItsPanel)
        {
            Frame();
            ASSERT_EQ(enables[1], 1) << "test setup: B is the visible tab";
            DockNode* right = layout.root->second.get();

            app.CloseDockedPanel(layout, *right, 0);   // close B
            Frame();

            EXPECT_EQ(disables[1], 1);
            EXPECT_EQ(layout.panels.count(panelB), 0u) << "the closed panel must not be kept";
            EXPECT_EQ(right->panelIds, (std::vector<ID>{ panelC }));
        }

    } // namespace
}
