// Integration tests for BeginTabBar / Tab / EndTabBar: selection and the
// right-click "Close Panel" menu (reorder is tested separately).

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_core/gui_id.h>
#include <gui_core/gui_id_manager.h>
#include <gui_core/state/gui_state_manager.h>

namespace tachyon
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
                ASSERT_TRUE(app.Initialize());
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
                            app.Tab(tabIDs[i], std::string_view("Tab"),
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
                        app.Tab(tabIDs[i], "Tab",
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
                    app.Tab(id, "Solo", LayoutOverride{}.Width(80));
                    app.EndTabBar();
                }
                app.EndRootWindow();
            });
            SUCCEED();
        }

        // Keyboard focus on tabs (#95): Tab reaches the bar at its selected tab, arrows
        // and Home/End move along it, selecting the tab moved to.
        class TabKeyboardTest : public ControlsTabsTest
        {
        protected:
            int selected = 1;
            void Frame() { EmitTabBar(selected, 3); }
            ID Focused() { return app.guiStateManager.Interaction().GetFocusedID(); }
            bool FocusIsOnATab()
            {
                const ElementRef ref = app.elementStorage.FindElementRefByID(Focused());
                return ref.IsValid() && app.elementStorage.GetElementUnsafe(ref).styleRef == app.theme.tabStyleRef;
            }
            void Press(KeyCode key) { app.InjectKey(key); Frame(); Frame(); }
        };

        TEST_F(TabKeyboardTest, TabReachesTheSelectedTab)
        {
            Frame();
            Frame();
            Press(KeyCode::Tab);
            EXPECT_TRUE(FocusIsOnATab());
            EXPECT_EQ(selected, 1) << "reaching the bar selects nothing new";
            EXPECT_TRUE(app.guiStateManager.Interaction().IsFocusVisible());
        }

        TEST_F(TabKeyboardTest, ArrowsMoveAlongTheTabs)
        {
            Frame();
            Frame();
            Press(KeyCode::Tab);
            const ID first = Focused();
            Press(KeyCode::Right);
            EXPECT_EQ(selected, 2);
            EXPECT_TRUE(FocusIsOnATab());
            EXPECT_NE(Focused(), first) << "focus moved with the selection";
            Press(KeyCode::Right);
            EXPECT_EQ(selected, 2) << "no wrapping past the last tab";
            Press(KeyCode::Home);
            EXPECT_EQ(selected, 0);
            Press(KeyCode::Left);
            EXPECT_EQ(selected, 0);
            Press(KeyCode::End);
            EXPECT_EQ(selected, 2);
            Press(KeyCode::Left);
            EXPECT_EQ(selected, 1);
            EXPECT_EQ(Focused(), first) << "the same tab, the same focus";
        }

        // Without focus on the bar, arrows leave its tabs alone.
        TEST_F(TabKeyboardTest, ArrowsDoNothingWithoutFocusOnTheBar)
        {
            Frame();
            Press(KeyCode::Right);
            EXPECT_EQ(selected, 1);
        }

        // A click focuses the tab, so the arrow keys go on from the one clicked.
        TEST_F(TabKeyboardTest, AClickedTabTakesTheFocus)
        {
            selected = 0;
            Frame();
            const std::vector<ID> tabs = EmitTabBar(selected, 3);
            ClickTabBody(tabs[1]);
            Frame();
            Frame();
            EXPECT_EQ(selected, 1);
            EXPECT_TRUE(FocusIsOnATab());
            Press(KeyCode::Right);
            EXPECT_EQ(selected, 2);
        }

        // Ctrl+W and Ctrl+F4 close the focused tab, as "Close Panel" does.
        TEST_F(TabKeyboardTest, CtrlWClosesTheFocusedTab)
        {
            for (const KeyCode key : { KeyCode::KeyW, KeyCode::F4 })
            {
                int closed = -1;
                auto frame = [&] { EmitTabBar(selected, 3, [&](int i) { closed = i; }); };
                frame();
                frame();
                app.InjectKey(KeyCode::Tab);
                frame();
                frame();
                ASSERT_TRUE(FocusIsOnATab());
                app.InjectKey(key, KeyAction::KeyDown, KeyModifier::Control);
                frame();
                EXPECT_EQ(closed, 1) << int(key);
                app.guiStateManager.Interaction().SetFocusID(ID::Invalid());
                frame();
            }
        }

        // Alt+Right / Left moves the focused tab; the focus stays on it.
        TEST_F(TabKeyboardTest, AltArrowsMoveTheFocusedTab)
        {
            std::vector<std::string> names{ "One", "Two", "Three" };
            std::vector<ID> ids;
            for (int i = 0; i < 3; ++i) ids.push_back(app.idManager.CreateStaticID());
            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow();
                    auto onReorder = [&](int from, int to) {
                        std::string n = names[size_t(from)]; names.erase(names.begin() + from); names.insert(names.begin() + to, n);
                        ID id = ids[size_t(from)];          ids.erase(ids.begin() + from);     ids.insert(ids.begin() + to, id);
                        if (selected == from) selected = to;
                    };
                    if (app.BeginTabBar(barID, &selected, onReorder, nullptr, nullptr, nullptr, StyleRef{}, LayoutOverride{}.Size(600, 40)))
                    {
                        for (int i = 0; i < 3; ++i)
                            app.Tab(ids[size_t(i)], std::string_view(names[size_t(i)]), LayoutOverride::Auto());
                        app.EndTabBar();
                    }
                    app.EndRootWindow();
                });
            };
            frame();
            frame();
            app.InjectKey(KeyCode::Tab);
            frame();
            frame();
            ASSERT_TRUE(FocusIsOnATab());
            const ID focusedTab = Focused();
            app.InjectKey(KeyCode::Right, KeyAction::KeyDown, KeyModifier::Alt);
            frame();
            frame();
            EXPECT_EQ(names, (std::vector<std::string>{ "One", "Three", "Two" }));
            EXPECT_EQ(selected, 2) << "Alt+Right moved it rather than selecting the next";
            EXPECT_EQ(Focused(), focusedTab) << "the tab's IDs go with it, so does the focus";
            app.InjectKey(KeyCode::Left, KeyAction::KeyDown, KeyModifier::Alt);
            frame();
            frame();
            EXPECT_EQ(names, (std::vector<std::string>{ "One", "Two", "Three" }));
        }

        // ---- Tabs that don't fit -------------------------------------------------

        // A bar of `count` tabs named "Panel 1".. in a bar `width` wide: the ones that fit are
        // shown, then a "+N" button listing the rest.
        class TabOverflowTest : public ControlsTabsTest
        {
        protected:
            int selected = 0;
            int count    = 12;
            int width    = 600;
            std::vector<ID> tabIDs;
            std::vector<std::string> names;
            TabBarLayout* layout = nullptr;               // the caller's, or the bar keeps its own
            std::function<void(int, int)> onReorder;

            void SetUp() override
            {
                ControlsTabsTest::SetUp();
                app.runPostLayoutCallbacks = true;   // the bar measures its tabs there
                for (int i = 0; i < 20; ++i)
                {
                    tabIDs.push_back(app.idManager.CreateStaticID());
                    names.push_back("Panel " + std::to_string(i + 1));
                }
            }

            void Frame()
            {
                app.Frame([&] {
                    app.BeginRootWindow();
                    if (app.BeginTabBar(barID, &selected, onReorder, nullptr, nullptr, nullptr,
                                        StyleRef{}, LayoutOverride{}.Width(width), layout))
                    {
                        for (int i = 0; i < count; ++i)
                            app.Tab(tabIDs[size_t(i)], std::string_view(names[size_t(i)]), LayoutOverride::Auto());
                        app.EndTabBar();
                    }
                    app.EndRootWindow();
                });
            }

            void Settle() { for (int i = 0; i < 4; ++i) Frame(); }

            std::vector<int> Shown() const
            {
                std::vector<int> shown;
                for (int i = 0; i < count; ++i)
                    if (app.HasElement(tabIDs[size_t(i)]))
                        shown.push_back(i);
                return shown;
            }

            // The overflow button, found by its "+N" text.
            ID Overflow() const
            {
                for (int n = 1; n <= count; ++n)
                    if (const ID id = app.FindByText("+" + std::to_string(n)); id != ID::Invalid())
                        return id;
                return ID::Invalid();
            }
        };

        TEST_F(TabOverflowTest, TabsThatFitHaveNoOverflowButton)
        {
            count = 3;
            Settle();
            EXPECT_EQ(Shown().size(), 3u);
            EXPECT_EQ(Overflow(), ID::Invalid());
        }

        TEST_F(TabOverflowTest, TheFirstTabsThatFitAreShownAndTheButtonCountsTheRest)
        {
            Settle();
            const std::vector<int> shown = Shown();
            ASSERT_GT(shown.size(), 1u);
            ASSERT_LT(shown.size(), size_t(count)) << "test setup: they don't all fit";
            for (size_t i = 0; i < shown.size(); ++i)
                EXPECT_EQ(shown[i], int(i)) << "the first ones, in order";
            const ID overflow = Overflow();
            ASSERT_NE(overflow, ID::Invalid());
            EXPECT_EQ(app.TextOf(overflow), "+" + std::to_string(count - int(shown.size())));
            const IntRect bar = app.GetElementRect(barID);
            const IntRect button = app.GetElementRect(overflow);
            EXPECT_LE(button.x + button.w, bar.x + bar.w) << "the button fits in the bar";
        }

        TEST_F(TabOverflowTest, TheSelectedTabIsAlwaysShown)
        {
            Settle();
            selected = count - 1;
            Settle();
            const std::vector<int> shown = Shown();
            ASSERT_FALSE(shown.empty());
            EXPECT_EQ(shown.back(), count - 1) << "in place of the last ones that fit";
            EXPECT_NE(Overflow(), ID::Invalid());
        }

        TEST_F(TabOverflowTest, PickingFromTheListSelectsAndShowsTheTab)
        {
            Settle();
            const ID overflow = Overflow();
            ASSERT_NE(overflow, ID::Invalid());
            app.ClickAt(app.CenterOf(overflow));
            Settle();
            const ID item = app.FindByText(names[size_t(count - 2)]);
            ASSERT_NE(item, ID::Invalid()) << "the list holds the hidden tabs";
            app.ClickAt(app.CenterOf(item));
            Settle();
            EXPECT_EQ(selected, count - 2);
            EXPECT_TRUE(app.HasElement(tabIDs[size_t(count - 2)]));
        }

        TEST_F(TabOverflowTest, ArrowsMoveOnToTheTabsInTheList)
        {
            Settle();
            const std::vector<int> shown = Shown();
            ASSERT_LT(shown.size(), size_t(count));
            selected = shown.back();
            Settle();
            app.InjectKey(KeyCode::Tab);
            Settle();
            app.InjectKey(KeyCode::Right);
            Settle();
            EXPECT_EQ(selected, shown.back() + 1);
            EXPECT_TRUE(app.HasElement(tabIDs[size_t(selected)])) << "the tab moved to is shown";
        }
        // ---- Tabs on several rows ----

        class TabRowsTest : public TabOverflowTest
        {
        protected:
            TabBarLayout callerLayout = TabBarLayout::Rows;

            // Right-clicks at `pos` and picks `item` from the menu that opens.
            void PickFromMenuAt(int2 pos, std::string_view item)
            {
                app.InjectMouseMove(pos);
                Frame();
                app.InjectMouseDown(pos, MouseButtons::Right);
                app.InjectMouseUp(pos, MouseButtons::Right);
                Frame();
                const ID found = app.FindByText(item);
                ASSERT_NE(found, ID::Invalid()) << "no menu item " << item;
                app.ClickAt(app.CenterOf(found));
                Settle();
            }

            int Rows() const
            {
                std::vector<int32_t> tops;
                for (const int i : Shown())
                {
                    const int32_t y = app.GetElementRect(tabIDs[size_t(i)]).y;
                    if (std::find(tops.begin(), tops.end(), y) == tops.end())
                        tops.push_back(y);
                }
                return int(tops.size());
            }
        };

        TEST_F(TabRowsTest, OnSeveralRowsEveryTabShows)
        {
            layout = &callerLayout;
            Settle();
            EXPECT_EQ(int(Shown().size()), count);
            EXPECT_EQ(Overflow(), ID::Invalid()) << "no +N button";
            EXPECT_GT(Rows(), 1);
            const IntRect bar = app.GetElementRect(barID);
            for (const int i : Shown())
            {
                const IntRect tab = app.GetElementRect(tabIDs[size_t(i)]);
                EXPECT_GE(tab.y, bar.y) << "tab " << i;
                EXPECT_LE(tab.y + tab.h, bar.y + bar.h) << "tab " << i << ": the bar grows to hold every row";
            }
        }

        // A tab's context menu switches its bar between one row and several, and back.
        TEST_F(TabRowsTest, ATabsMenuSwitchesTheLayout)
        {
            Settle();
            ASSERT_LT(int(Shown().size()), count) << "test setup: one row with a +N button";
            const IntRect first = app.GetElementRect(tabIDs[0]);
            PickFromMenuAt({ first.x + 6, first.y + first.h / 2 }, "Multiple Rows");
            EXPECT_EQ(int(Shown().size()), count);
            EXPECT_EQ(Overflow(), ID::Invalid());

            // From a tab on the last row, so the menu opens below the bar: headless, a menu over
            // the tabs doesn't take the click from them.
            const IntRect again = app.GetElementRect(tabIDs[size_t(count - 1)]);
            PickFromMenuAt({ again.x + 6, again.y + again.h / 2 }, "Collapse Rows");
            EXPECT_LT(int(Shown().size()), count);
            EXPECT_NE(Overflow(), ID::Invalid());
            EXPECT_EQ(Rows(), 1);
        }

        // Right-clicking the "+N" button opens the layouts too.
        TEST_F(TabRowsTest, TheOverflowButtonsMenuSwitchesTheLayout)
        {
            Settle();
            const ID overflow = Overflow();
            ASSERT_NE(overflow, ID::Invalid()) << "test setup: one row with a +N button";
            const IntRect button = app.GetElementRect(overflow);
            PickFromMenuAt({ button.x + button.w / 2, button.y + button.h / 2 }, "Multiple Rows");
            EXPECT_EQ(int(Shown().size()), count);
            EXPECT_EQ(Overflow(), ID::Invalid());
        }

        // Right-clicking the strip beside the tabs opens the layouts too; a caller that keeps
        // the layout gets it set.
        TEST_F(TabRowsTest, TheStripsMenuSetsTheCallersLayout)
        {
            callerLayout = TabBarLayout::OneRow;
            layout = &callerLayout;
            count = 2;
            Settle();
            const IntRect bar = app.GetElementRect(barID);
            const IntRect last = app.GetElementRect(tabIDs[1]);
            ASSERT_GT(bar.x + bar.w, last.x + last.w + 20) << "test setup: room beside the tabs";
            PickFromMenuAt({ bar.x + bar.w - 10, bar.y + bar.h / 2 }, "Multiple Rows");
            EXPECT_EQ(callerLayout, TabBarLayout::Rows);
        }

        // Each bar keeps its own layout.
        TEST_F(TabRowsTest, EachBarHasItsOwnLayout)
        {
            const ID otherBar = app.idManager.CreateStaticID();
            std::vector<ID> otherTabs;
            for (int i = 0; i < count; ++i)
                otherTabs.push_back(app.idManager.CreateStaticID());
            int otherSelected = 0;
            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow();
                    for (int bar = 0; bar < 2; ++bar)
                    {
                        const ID id = bar == 0 ? barID : otherBar;
                        if (app.BeginTabBar(id, bar == 0 ? &selected : &otherSelected, nullptr, nullptr, nullptr, nullptr,
                                            StyleRef{}, LayoutOverride{}.Width(width)))
                        {
                            for (int i = 0; i < count; ++i)
                                app.Tab(bar == 0 ? tabIDs[size_t(i)] : otherTabs[size_t(i)], std::string_view(names[size_t(i)]), LayoutOverride::Auto());
                            app.EndTabBar();
                        }
                    }
                    app.EndRootWindow();
                });
            };
            for (int i = 0; i < 4; ++i) frame();
            const IntRect first = app.GetElementRect(tabIDs[0]);
            const int2 pos{ first.x + 6, first.y + first.h / 2 };
            app.InjectMouseMove(pos);
            frame();
            app.InjectMouseDown(pos, MouseButtons::Right);
            app.InjectMouseUp(pos, MouseButtons::Right);
            frame();
            const ID item = app.FindByText("Multiple Rows");
            ASSERT_NE(item, ID::Invalid());
            app.ClickAt(app.CenterOf(item));
            for (int i = 0; i < 4; ++i) frame();

            int shownFirst = 0, shownOther = 0;
            for (int i = 0; i < count; ++i)
            {
                shownFirst += app.HasElement(tabIDs[size_t(i)]) ? 1 : 0;
                shownOther += app.HasElement(otherTabs[size_t(i)]) ? 1 : 0;
            }
            EXPECT_EQ(shownFirst, count);
            EXPECT_LT(shownOther, count) << "the other bar stays on one row";
        }

        // On several rows the arrows and Alt + the arrows go on from the end of a row to the next.
        TEST_F(TabRowsTest, KeysGoOnAcrossRows)
        {
            layout = &callerLayout;
            std::pair<int, int> moved{ -1, -1 };
            onReorder = [&](int from, int to) { moved = { from, to }; };
            Settle();
            ASSERT_GT(Rows(), 1);
            // The last tab on the first row.
            int last = 0;
            const int32_t firstRow = app.GetElementRect(tabIDs[0]).y;
            while (last + 1 < count && app.GetElementRect(tabIDs[size_t(last + 1)]).y == firstRow)
                ++last;

            selected = last;
            Settle();
            app.ClickAt(app.CenterOf(tabIDs[size_t(last)]));
            Settle();
            app.InjectKey(KeyCode::Right);
            Settle();
            EXPECT_EQ(selected, last + 1);
            EXPECT_GT(app.GetElementRect(tabIDs[size_t(selected)]).y, firstRow) << "on the next row";

            app.InjectKey(KeyCode::Left);
            Settle();
            ASSERT_EQ(selected, last);
            app.InjectKey(KeyCode::Right, KeyAction::KeyDown, KeyModifier::Alt);
            Settle();
            EXPECT_EQ(moved, (std::pair<int, int>{ last, last + 1 }));
        }
        // On several rows a tab dragged onto the next row lands where it is dropped.
        TEST_F(TabRowsTest, ATabDraggedOntoTheNextRowLandsThere)
        {
            layout = &callerLayout;
            std::pair<int, int> moved{ -1, -1 };
            onReorder = [&](int from, int to) { moved = { from, to }; };
            Settle();
            ASSERT_GT(Rows(), 1);
            const int32_t firstRow = app.GetElementRect(tabIDs[0]).y;
            int secondRowStart = 0;
            while (app.GetElementRect(tabIDs[size_t(secondRowStart)]).y == firstRow)
                ++secondRowStart;

            const int2 from = app.CenterOf(tabIDs[0]);
            const IntRect target = app.GetElementRect(tabIDs[size_t(secondRowStart)]);
            const int2 to{ target.x + target.w * 3 / 4, target.y + target.h / 2 };
            app.InjectMouseMove(from);
            app.InjectMouseDown(from);
            Frame();
            app.InjectMouseMove({ from.x + 10, from.y + 4 }, MouseButtons::Left);
            Frame();
            app.InjectMouseMove(to, MouseButtons::Left);
            Frame();
            app.InjectMouseUp(to);
            Frame();
            EXPECT_EQ(moved, (std::pair<int, int>{ 0, secondRowStart })) << "past the middle of the second row's first tab";
        }
    }
}
