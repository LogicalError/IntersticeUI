// Integration tests for BeginTabBar / Tab / EndTabBar: selection and the
// right-click "Close Panel" menu (reorder is tested separately).

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_core/gui_id.h>
#include <gui_core/gui_id_manager.h>
#include <gui_core/state/gui_state_manager.h>

namespace interstice
{
    using GUITest::HeadlessGUIApp;

    namespace
    {

        class ControlsTabsTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;
            ID barID{};
            void SetUp() override
            {
                app.Initialize();
                barID = app.idManager.CreateStaticID();
            }
            void TearDown() override { app.Cleanup(); }

            // The item rect is wider than its auto-sized body, so click near the left
            // edge to land inside the body.
            void ClickTabBody(ID tabID, MouseButtons button = MouseButtons::Left)
            {
                const auto r = app.GetElementRect(tabID);
                app.ClickAt({ r.x + 6, r.y + r.h / 2 }, button);
            }

            // Render N tabs with stable IDs; returns them in order.
            std::vector<ID> EmitTabBar(int& selectedIndex, int n,
                                       std::function<void(int)> onClose = nullptr)
            {
                std::vector<ID> tabIDs;
                tabIDs.reserve(n);
                for (int i = 0; i < n; ++i)
                {
                    ID id{};
                    id.value.dynamicParts.mode = IDMode::Static;
                    id.value.dynamicParts.hashValue = 1000 + i;
                    tabIDs.push_back(id);
                }
                app.Frame([&] {
                    app.BeginRootWindow();
                    if (app.BeginTabBar(barID, &selectedIndex, /*onReorder*/ nullptr,
                                        onClose, /*onDragOut*/ nullptr,
                                        /*dragVisualizer*/ nullptr,
                                        StyleRef{}, LayoutOverride{}.Size(600, 40)))
                    {
                        // Auto width so the item shrinks to its body.
                        for (int i = 0; i < n; ++i)
                            app.Tab(tabIDs[i], std::string_view("Tab"), IconID::Invalid,
                                    LayoutOverride::Auto());
                        app.EndTabBar();
                    }
                    app.EndRootWindow();
                });
                return tabIDs;
            }

            // First element with style `style` across all windows, popup menus included.
            ID FindFirstByStyle(StyleRef style)
            {
                for (uint32_t depth = 0; depth < 64; ++depth)
                    for (uint32_t idx = 0;
                         app.elementStorage.IsValidRef(ElementRef{ depth, idx }); ++idx)
                    {
                        const Element* e = app.elementStorage.GetElementPtr(ElementRef{ depth, idx });
                        if (e && e->styleRef == style) return e->id;
                    }
                return ID::Invalid();
            }

            // Hover then right-click the tab to open its context menu; returns the open
            // menu's "Close Panel" item ID (or Invalid). The hover must settle a frame
            // first, as BeginContextMenu matches the prior frame's hovered element.
            ID OpenTabContextMenu(ID tabID, int& sel, int n, std::function<void(int)> onClose)
            {
                const auto r = app.GetElementRect(tabID);
                const int2 pos{ r.x + 6, r.y + r.h / 2 };
                app.InjectMouseMove(pos);
                EmitTabBar(sel, n, onClose);
                app.InjectMouseDown(pos, MouseButtons::Right);
                app.InjectMouseUp(pos, MouseButtons::Right);
                EmitTabBar(sel, n, onClose);
                return FindFirstByStyle(app.theme.menuItemRowStyleRef);
            }
        };

        TEST_F(ControlsTabsTest, TabBarRendersAllTabs)
        {
            int sel = 0;
            auto ids = EmitTabBar(sel, 3);
            for (const auto& id : ids)
                EXPECT_TRUE(app.HasElement(id));
        }

        TEST_F(ControlsTabsTest, ClickingDifferentTabUpdatesSelectedIndex)
        {
            int sel = 0;
            auto ids = EmitTabBar(sel, 3);
            ASSERT_EQ(sel, 0);

            ClickTabBody(ids[2]);
            EmitTabBar(sel, 3);

            EXPECT_EQ(sel, 2);
        }

        TEST_F(ControlsTabsTest, ClickingAlreadySelectedTabIsHarmless)
        {
            int sel = 1;
            auto ids = EmitTabBar(sel, 3);
            ASSERT_EQ(sel, 1);

            ClickTabBody(ids[1]);
            EmitTabBar(sel, 3);

            EXPECT_EQ(sel, 1);
        }

        TEST_F(ControlsTabsTest, RightClickingTabDoesNotChangeSelection)
        {
            int sel = 0;
            auto ids = EmitTabBar(sel, 3);

            ClickTabBody(ids[2], MouseButtons::Right);
            EmitTabBar(sel, 3);

            EXPECT_EQ(sel, 0);
        }

        TEST_F(ControlsTabsTest, OnCloseInvokedFromContextMenu)
        {
            int sel = 0;
            int closedIndex = -1;
            auto onClose = [&](int idx) { closedIndex = idx; };

            auto ids = EmitTabBar(sel, 3, onClose);
            ASSERT_TRUE(app.HasElement(ids[1]));

            const ID itemID = OpenTabContextMenu(ids[1], sel, 3, onClose);
            ASSERT_TRUE(app.menuManager.IsOpen()) << "right-click should open the tab context menu";
            ASSERT_NE(itemID, ID::Invalid()) << "Close Panel menu item not found";

            app.ClickAt(app.CenterOf(itemID));
            EmitTabBar(sel, 3, onClose);

            EXPECT_EQ(closedIndex, 1);
        }

        TEST_F(ControlsTabsTest, ClosingTabFromContextMenuKeepsSelectedIndex)
        {
            // The framework only invokes the callback; selectedIndex is untouched.
            int sel = 0;
            int closedIndex = -1;
            auto onClose = [&](int idx) { closedIndex = idx; };

            auto ids = EmitTabBar(sel, 3, onClose);
            ASSERT_EQ(sel, 0);

            const ID itemID = OpenTabContextMenu(ids[2], sel, 3, onClose);
            ASSERT_NE(itemID, ID::Invalid());

            app.ClickAt(app.CenterOf(itemID));
            EmitTabBar(sel, 3, onClose);

            EXPECT_EQ(closedIndex, 2);
            EXPECT_EQ(sel, 0);
        }

        TEST_F(ControlsTabsTest, NoCloseButtonWhenOnCloseIsNull)
        {
            // Without onClose there is no close button: a click near the right edge
            // selects the tab.
            int sel = 0;
            auto ids = EmitTabBar(sel, 3, /*onClose*/ nullptr);

            const auto rect = app.GetElementRect(ids[2]);
            const int2 rightEdgePos{ rect.x + rect.w - 6, rect.y + rect.h / 2 };
            app.ClickAt(rightEdgePos);
            EmitTabBar(sel, 3, nullptr);

            EXPECT_EQ(sel, 2);
        }

        // The element's stored Selected flag (GetCurrentState only reflects
        // construction-time Push/PopSelected).
        static bool TabHasSelectedFlag(HeadlessGUIApp& app, ID id)
        {
            auto ref = app.elementStorage.FindElementRefByID(id);
            if (!ref.IsValid()) return false;
            const auto& el = app.elementStorage.GetElementUnsafe(ref);
            return (el.state & ElementState::Selected) != ElementState::None;
        }

        TEST_F(ControlsTabsTest, SelectedTabCarriesSelectedStateOnElement)
        {
            int sel = 1;
            auto ids = EmitTabBar(sel, 3);

            EXPECT_TRUE(TabHasSelectedFlag(app, ids[1]));
            EXPECT_FALSE(TabHasSelectedFlag(app, ids[0]));
            EXPECT_FALSE(TabHasSelectedFlag(app, ids[2]));
        }

        TEST_F(ControlsTabsTest, SwitchingSelectionUpdatesSelectedState)
        {
            int sel = 0;
            auto ids = EmitTabBar(sel, 3);
            EXPECT_TRUE(TabHasSelectedFlag(app, ids[0]));

            ClickTabBody(ids[2]);
            EmitTabBar(sel, 3);
            EmitTabBar(sel, 3); // settle

            EXPECT_EQ(sel, 2);
            EXPECT_FALSE(TabHasSelectedFlag(app, ids[0]));
            EXPECT_TRUE(TabHasSelectedFlag(app, ids[2]));
        }

        // A tab given an explicit width wider than its content is clickable
        // across its whole width, not just over its content.
        TEST_F(ControlsTabsTest, ClickInRightHalfOfWideTabSelectsIt)
        {
            int sel = 0;
            // Force a wide tab - text content "Tab" is ~24 px, so 200 px
            // leaves a large right-side dead zone if the body doesn't grow.
            ID barID2 = app.idManager.CreateStaticID();
            std::vector<ID> tabIDs;
            for (int i = 0; i < 3; ++i)
            {
                ID id{};
                id.value.dynamicParts.mode = IDMode::Static;
                id.value.dynamicParts.hashValue = 7000 + i;
                tabIDs.push_back(id);
            }
            auto draw = [&] {
                app.BeginRootWindow();
                if (app.BeginTabBar(barID2, &sel, nullptr, nullptr, nullptr, nullptr,
                                    StyleRef{}, LayoutOverride{}.Size(900, 40)))
                {
                    for (int i = 0; i < 3; ++i)
                        app.Tab(tabIDs[i], "Tab", IconID::Invalid,
                                LayoutOverride{}.Width(200));
                    app.EndTabBar();
                }
                app.EndRootWindow();
            };
            app.Frame(draw);

            const auto r2 = app.GetElementRect(tabIDs[2]);
            // Click in the right portion of the tab - visually inside the
            // 200px-wide tab but past the auto-sized body's content width.
            const int2 clickPos{ r2.x + r2.w - 20, r2.y + r2.h / 2 };
            app.ClickAt(clickPos);
            app.Frame(draw);

            EXPECT_EQ(sel, 2)
                << "Click in the visible right portion of a Width(200) tab should "
                   "still select it. The tab body is currently Auto-sized and does "
                   "not fill the parent reorderable item.";
        }

        TEST_F(ControlsTabsTest, NullSelectedIndexPointerHandledGracefully)
        {
            // A null selectedIndex (stateless tab bar) must not crash.
            app.Frame([&] {
                app.BeginRootWindow();
                if (app.BeginTabBar(barID, /*selectedIndex*/ nullptr,
                                    nullptr, nullptr, nullptr, nullptr,
                                    StyleRef{}, LayoutOverride{}.Size(400, 40)))
                {
                    ID id{};
                    id.value.dynamicParts.mode = IDMode::Static;
                    id.value.dynamicParts.hashValue = 5000;
                    app.Tab(id, "Solo", IconID::Invalid, LayoutOverride{}.Width(80));
                    app.EndTabBar();
                }
                app.EndRootWindow();
            });
            SUCCEED();
        }

    } // namespace
} // namespace interstice
