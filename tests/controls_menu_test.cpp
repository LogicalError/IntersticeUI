// Integration tests for BeginMenu / MenuItem / EndMenu. CreateWindow is mocked,
// so popups register but get no OS input; tests drive the trigger and observe
// MenuManager state.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_core/gui_id.h>
#include <gui_core/gui_id_manager.h>
#include <gui_core/state/gui_state_manager.h>
#include <gui_core/input/gui_input_enums.h>
#include <gui_core/input/gui_input_state.h>
#include <gui_core/elements/gui_element_storage.h>
#include <gui_controls/gui_menu_manager.h>

namespace GUI
{
    using GUITest::HeadlessGUIApp;

    namespace
    {

        class ControlsMenuTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;
            void SetUp()    override { app.Initialize(); }
            void TearDown() override { app.Cleanup(); }
        };

        // ---------------------------------------------------------------------
        // Trigger button
        // ---------------------------------------------------------------------

        TEST_F(ControlsMenuTest, BeginMenuRendersTriggerButton)
        {
            ID menuID = app.idManager.CreateStaticID();

            app.Frame([&] {
                app.BeginRootWindow();
                if (app.BeginMenu(menuID, "File")) app.EndMenu();
                app.EndRootWindow();
            });

            ASSERT_TRUE(app.HasElement(menuID));
            EXPECT_GT(app.GetElementRect(menuID).w, 0);
            EXPECT_FALSE(app.menuManager.IsOpen());
        }

        TEST_F(ControlsMenuTest, ClickingTriggerOpensMenu)
        {
            ID menuID = app.idManager.CreateStaticID();
            auto draw = [&] {
                app.BeginRootWindow();
                if (app.BeginMenu(menuID, "File")) app.EndMenu();
                app.EndRootWindow();
            };

            app.Frame(draw);
            ASSERT_FALSE(app.menuManager.IsOpen());

            app.ClickAt(app.CenterOf(menuID));
            app.Frame(draw);

            EXPECT_TRUE(app.menuManager.IsOpen());
            EXPECT_EQ(app.menuManager.levels[0].triggerID, menuID);
        }

        // Closing a menu only hides its popup OS window, and OSWindowManager revives a
        // hidden popup only for the same window ID. A menu level must therefore keep
        // its window ID across close/reopen, or every open creates (and leaks) a new
        // OS window + swapchain. (Headless creates no OS windows, so check the ID.)
        TEST_F(ControlsMenuTest, ReopeningMenuKeepsItsPopupWindowID)
        {
            ID menuID = app.idManager.CreateStaticID();
            ID itemID = app.idManager.CreateStaticID();
            auto draw = [&] {
                app.BeginRootWindow();
                if (app.BeginMenu(menuID, "File"))
                {
                    app.MenuItem(itemID, "Open");
                    app.EndMenu();
                }
                app.EndRootWindow();
            };
            app.Frame(draw);

            auto openThenClose = [&]() -> ID {
                app.ClickAt(app.CenterOf(menuID));
                app.Frame(draw);
                app.Frame(draw);
                EXPECT_TRUE(app.menuManager.IsOpen());
                const ID windowID = app.menuManager.levels[0].windowID;
                app.ClickAt(app.CenterOf(menuID));
                app.Frame(draw);
                app.Frame(draw);
                EXPECT_FALSE(app.menuManager.IsOpen());
                return windowID;
            };

            const ID first = openThenClose();
            ASSERT_NE(first, ID::Invalid());
            for (int i = 0; i < 3; ++i)
                EXPECT_EQ(openThenClose(), first);
        }

        TEST_F(ControlsMenuTest, ClickingOpenTriggerAgainTogglesItClosed)
        {
            ID menuID = app.idManager.CreateStaticID();
            auto draw = [&] {
                app.BeginRootWindow();
                if (app.BeginMenu(menuID, "File")) app.EndMenu();
                app.EndRootWindow();
            };
            app.Frame(draw);

            app.ClickAt(app.CenterOf(menuID));
            app.Frame(draw);
            ASSERT_TRUE(app.menuManager.IsOpen());

            app.ClickAt(app.CenterOf(menuID));
            app.Frame(draw);
            EXPECT_FALSE(app.menuManager.IsOpen());
        }

        TEST_F(ControlsMenuTest, RightClickOnTriggerDoesNotOpenMenu)
        {
            ID menuID = app.idManager.CreateStaticID();
            auto draw = [&] {
                app.BeginRootWindow();
                if (app.BeginMenu(menuID, "File")) app.EndMenu();
                app.EndRootWindow();
            };
            app.Frame(draw);

            app.ClickAt(app.CenterOf(menuID), MouseButtons::Right);
            app.Frame(draw);
            EXPECT_FALSE(app.menuManager.IsOpen());
        }

        TEST_F(ControlsMenuTest, KeyboardActivationKeyOpensMenuFromTrigger)
        {
            ID menuID = app.idManager.CreateStaticID();
            auto draw = [&] {
                app.BeginRootWindow();
                if (app.BeginMenu(menuID, "File")) app.EndMenu();
                app.EndRootWindow();
            };
            app.Frame(draw);

            // Focus the trigger first (the keyboard handler requires focus).
            app.guiStateManager.Interaction().SetFocusID(menuID);
            app.Frame(draw);

            app.InjectKey(KeyCode::Return, KeyAction::KeyDown);
            app.Frame(draw);

            EXPECT_TRUE(app.menuManager.IsOpen());
            EXPECT_TRUE(app.menuManager.mouseHiddenForMenu);  // keyboard nav hides mouse
        }

        TEST_F(ControlsMenuTest, DownArrowOnFocusedTriggerOpensMenu)
        {
            ID menuID = app.idManager.CreateStaticID();
            auto draw = [&] {
                app.BeginRootWindow();
                if (app.BeginMenu(menuID, "File")) app.EndMenu();
                app.EndRootWindow();
            };
            app.Frame(draw);

            app.guiStateManager.Interaction().SetFocusID(menuID);
            app.Frame(draw);

            app.InjectKey(KeyCode::Down, KeyAction::KeyDown);
            app.Frame(draw);

            EXPECT_TRUE(app.menuManager.IsOpen());
        }

        // ---------------------------------------------------------------------
        // MenuManager state transitions (unit-style, complement integration).
        // ---------------------------------------------------------------------

        TEST_F(ControlsMenuTest, ToggleOrOpenMenuAlternatesIsOpen)
        {
            ID menuID = app.idManager.CreateStaticID();
            // Allocate a window ID for the level by calling BeginMenu once.
            app.Frame([&] {
                app.BeginRootWindow();
                if (app.BeginMenu(menuID, "M")) app.EndMenu();
                app.EndRootWindow();
            });

            app.menuManager.ToggleOrOpenMenu(app.guiStateManager, menuID);
            EXPECT_TRUE(app.menuManager.IsOpen());

            app.menuManager.ToggleOrOpenMenu(app.guiStateManager, menuID);
            EXPECT_FALSE(app.menuManager.IsOpen());
        }

        TEST_F(ControlsMenuTest, OpenMenuOpensRegardlessOfPriorState)
        {
            ID menuID = app.idManager.CreateStaticID();
            app.Frame([&] {
                app.BeginRootWindow();
                if (app.BeginMenu(menuID, "M")) app.EndMenu();
                app.EndRootWindow();
            });

            app.menuManager.OpenMenu(app.guiStateManager, menuID);
            EXPECT_TRUE(app.menuManager.IsOpen());

            // Calling OpenMenu twice keeps it open (idempotent for the same id).
            app.menuManager.OpenMenu(app.guiStateManager, menuID);
            EXPECT_TRUE(app.menuManager.IsOpen());
        }

        TEST_F(ControlsMenuTest, CloseAllRequestsAndCommitsClosure)
        {
            ID menuID = app.idManager.CreateStaticID();
            auto draw = [&] {
                app.BeginRootWindow();
                if (app.BeginMenu(menuID, "M")) app.EndMenu();
                app.EndRootWindow();
            };
            app.Frame(draw);
            app.menuManager.OpenMenu(app.guiStateManager, menuID);
            ASSERT_TRUE(app.menuManager.IsOpen());

            app.menuManager.CloseAll(app.guiStateManager);
            EXPECT_FALSE(app.menuManager.IsOpen());
        }

        // ---------------------------------------------------------------------
        // MakeMenuItemHandler with synthetic events (the harness doesn't dispatch
        // into popup windows).
        // ---------------------------------------------------------------------

        // ElementEvent holds const refs that must outlive the handler call.
        static ElementEvent MakeEvent(ID id, EventType type, InputState& state)
        {
            return ElementEvent{ id, type, state, nullptr, ElementRef::Invalid() };
        }
        static InputState MakeLeftDownState()
        {
            InputState s{};
            s.buttons     = MouseButtons::Left;
            s.prevButtons = MouseButtons::None;
            return s;
        }
        static InputState MakeRightDownState()
        {
            InputState s{};
            s.buttons     = MouseButtons::Right;
            s.prevButtons = MouseButtons::None;
            return s;
        }

        TEST_F(ControlsMenuTest, MenuItemHandlerInvokesOnClickOnLeftMouseDown)
        {
            int callCount = 0;
            auto handler = MakeMenuItemHandler(app.idManager, app.guiStateManager,
                                               app.elementStorage, app.menuManager,
                                               /*depth=*/0,
                                               [&] { callCount++; });

            ID itemID = app.idManager.CreateStaticID();
            InputState state = MakeLeftDownState();
            handler(MakeEvent(itemID, EventType::MouseDown, state));
            EXPECT_EQ(callCount, 1);
        }

        TEST_F(ControlsMenuTest, MenuItemHandlerIgnoresRightMouseDown)
        {
            int callCount = 0;
            auto handler = MakeMenuItemHandler(app.idManager, app.guiStateManager,
                                               app.elementStorage, app.menuManager,
                                               /*depth=*/0,
                                               [&] { callCount++; });

            ID itemID = app.idManager.CreateStaticID();
            InputState state = MakeRightDownState();
            handler(MakeEvent(itemID, EventType::MouseDown, state));
            EXPECT_EQ(callCount, 0);
        }

        TEST_F(ControlsMenuTest, MenuItemHandlerNullCallbackIsSafe)
        {
            auto handler = MakeMenuItemHandler(app.idManager, app.guiStateManager,
                                               app.elementStorage, app.menuManager,
                                               0, nullptr);

            ID itemID = app.idManager.CreateStaticID();
            InputState state = MakeLeftDownState();
            EXPECT_NO_THROW(handler(MakeEvent(itemID, EventType::MouseDown, state)));
        }

        // ---------------------------------------------------------------------
        // GetOrCreateNextLevel / window ID allocation
        // ---------------------------------------------------------------------

        TEST_F(ControlsMenuTest, GetOrCreateNextLevelAssignsAStableWindowID)
        {
            auto& lvl = app.menuManager.GetOrCreateNextLevel(app.idManager);
            ID firstID = lvl.windowID;
            ASSERT_NE(firstID, ID::Invalid());

            app.menuManager.currentDepth = -1;
            auto& lvl2 = app.menuManager.GetOrCreateNextLevel(app.idManager);
            EXPECT_EQ(lvl2.windowID, firstID);
        }

        TEST_F(ControlsMenuTest, EachDepthGetsItsOwnWindowID)
        {
            auto& lvl0 = app.menuManager.GetOrCreateNextLevel(app.idManager);
            ID id0 = lvl0.windowID;

            app.menuManager.currentDepth = 0;
            auto& lvl1 = app.menuManager.GetOrCreateNextLevel(app.idManager);
            EXPECT_NE(lvl1.windowID, id0);
            EXPECT_NE(lvl1.windowID, ID::Invalid());
        }

        // ---------------------------------------------------------------------
        // Auto-flip / side-placement
        //
        // A popup that fits neither below nor above its trigger is placed beside it.
        // Later passes must not re-apply the anchor X, or the window snaps back below
        // the trigger while the items stay beside it and the popup shows empty.
        // ---------------------------------------------------------------------

        TEST_F(ControlsMenuTest, BigMenuSidePlacesBesideTriggerWhenContentExceedsSpaceBelow)
        {
            ID menuID = app.idManager.CreateStaticID();
            auto draw = [&] {
                app.BeginRootWindow();
                // Narrow trigger so a side-placed popup fits beside it (a lone child
                // would otherwise stretch to fill the viewport).
                if (app.BeginMenu(menuID, "Big Menu", LayoutOverride{}.Width(120).NoGrow()))
                {
                    for (int i = 0; i < 100; i++)
                    {
                        char label[32];
                        snprintf(label, sizeof(label), "Item %d", i + 1);
                        app.MenuItem(label, nullptr);
                    }
                    app.EndMenu();
                }
                app.EndRootWindow();
            };

            app.Frame(draw); // register trigger
            const auto triggerRect = app.GetElementRect(menuID);

            app.ClickAt(app.CenterOf(menuID));
            app.Frame(draw); // open + first layout (uses trigger width)
            app.Frame(draw); // second frame, layout settles using last frame's window rect

            ASSERT_TRUE(app.menuManager.IsOpen());
            const ID windowID = app.menuManager.levels[0].windowID;
            ASSERT_NE(windowID, ID::Invalid());
            ASSERT_TRUE(app.HasElement(windowID));

            const auto windowRect = app.GetElementRect(windowID);

            const int32_t triggerLeft  = triggerRect.x;
            const int32_t triggerRight = triggerRect.x + triggerRect.w;
            const int32_t windowLeft   = windowRect.x;
            const int32_t windowRight  = windowRect.x + windowRect.w;

            const bool sidePlaced = (windowRight <= triggerLeft) || (windowLeft >= triggerRight);
            EXPECT_TRUE(sidePlaced)
                << "Big Menu should auto-flip to the side of the trigger when its content "
                << "exceeds the available space below. trigger.x=[" << triggerLeft << ".."
                << triggerRight << "] window.x=[" << windowLeft << ".." << windowRight << "]";
        }

        TEST_F(ControlsMenuTest, SmallMenuStaysAnchoredBelowTrigger)
        {
            // Control for the test above: a menu that fits below its trigger must not
            // side-place; it stays anchored InsideLeft / OutsideBottom.
            ID menuID = app.idManager.CreateStaticID();
            auto draw = [&] {
                app.BeginRootWindow();
                if (app.BeginMenu(menuID, "Small"))
                {
                    app.MenuItem("Item 1", nullptr);
                    app.MenuItem("Item 2", nullptr);
                    app.MenuItem("Item 3", nullptr);
                    app.EndMenu();
                }
                app.EndRootWindow();
            };

            app.Frame(draw);
            const auto triggerRect = app.GetElementRect(menuID);

            app.ClickAt(app.CenterOf(menuID));
            app.Frame(draw);
            app.Frame(draw);

            ASSERT_TRUE(app.menuManager.IsOpen());
            const ID windowID = app.menuManager.levels[0].windowID;
            ASSERT_NE(windowID, ID::Invalid());

            const auto windowRect = app.GetElementRect(windowID);

            EXPECT_EQ(windowRect.x, triggerRect.x)
                << "Small menu should stay X-aligned with its trigger (InsideLeft anchor).";
            EXPECT_GE(windowRect.y, triggerRect.y + triggerRect.h)
                << "Small menu should appear below the trigger.";
        }

    } // namespace
} // namespace GUI
