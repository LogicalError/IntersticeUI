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

namespace tachyon
{
    using GUITest::HeadlessGUIApp;

    namespace
    {

        class ControlsMenuTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;
            void SetUp()    override { ASSERT_TRUE(app.Initialize()); }
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
                app.BeginMenu(menuID, "File"); app.EndMenu();
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
                app.BeginMenu(menuID, "File"); app.EndMenu();
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
                }
                app.EndMenu();
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

        TEST_F(ControlsMenuTest, ClickingOutsideClosesTheMenu)
        {
            ID menuID   = app.idManager.CreateStaticID();
            ID buttonID = app.idManager.CreateStaticID();
            ID areaID   = app.idManager.CreateStaticID();
            auto draw = [&] {
                app.BeginRootWindow();
                app.BeginMenuBar();
                if (app.BeginMenu(menuID, "File", app.theme.menuBarTriggerStyleRef, app.theme.menuStyleRef, app.theme.menuItemRowStyleRef))
                {
                    app.MenuItem("New", nullptr);
                }
                app.EndMenu();
                app.EndMenuBar();
                app.Button(buttonID, "Other", [] {});
                app.BeginVertical(areaID, LayoutOverride::Auto().Height(200));
                app.EndVertical();
                app.EndRootWindow();
            };

            for (const ID target : { buttonID, areaID })
            {
                app.Frame(draw);
                app.ClickAt(app.CenterOf(menuID));
                app.Frame(draw);
                app.Frame(draw);
                ASSERT_TRUE(app.menuManager.IsOpen());

                app.ClickAt(app.CenterOf(target));
                app.Frame(draw);
                app.Frame(draw);
                EXPECT_FALSE(app.menuManager.IsOpen()) << (target == buttonID ? "click on a control" : "click on empty space");
                app.menuManager.CloseAll(app.guiStateManager);
            }
        }

        // A disabled item shows but can't be chosen: a click doesn't run it, and opening
        // the menu doesn't put the keyboard on it. A destructive item takes its own styles.
        TEST_F(ControlsMenuTest, DisabledItemsCantBeChosenAndDestructiveOnesStandOut)
        {
            const ID menuID = app.idManager.CreateStaticID();
            const ID disabledID = app.idManager.CreateStaticID();
            const ID deleteID = app.idManager.CreateStaticID();
            int disabledRuns = 0, deleteRuns = 0;
            auto draw = [&] {
                app.BeginRootWindow();
                if (app.BeginMenu(menuID, "Edit"))
                {
                    app.BeginDisabled();
                    app.MenuItem(disabledID, "Undo", [&] { ++disabledRuns; });
                    app.EndDisabled();
                    app.DestructiveMenuItem(deleteID, { app.theme.deleteIcon, "Delete" }, {}, [&] { ++deleteRuns; });
                }
                app.EndMenu();
                app.EndRootWindow();
            };
            app.Frame(draw);
            app.ClickAt(app.CenterOf(menuID));
            app.Frame(draw);
            app.Frame(draw);
            ASSERT_TRUE(app.HasElement(disabledID));
            EXPECT_NE(app.menuManager.levels[0].activeItemID, disabledID) << "the keyboard skips it";

            app.ClickAt(app.CenterOf(disabledID));
            app.Frame(draw);
            app.Frame(draw);
            EXPECT_EQ(disabledRuns, 0);
            ASSERT_TRUE(app.menuManager.IsOpen()) << "the click didn't close the menu either";

            bool destructiveLabel = false;
            const auto& storage = app.elementStorage;
            for (auto it = storage.TopDownDepthScanBegin(); it != storage.TopDownDepthScanEnd(); ++it)
                for (const Element& e : it->GetChildren(storage))
                    if (e.styleRef.visualBoxStyleIndex == app.theme.menuItemDestructiveLabelStyleRef.visualBoxStyleIndex
                        && e.styleRef.layoutStyleIndex == app.theme.menuItemDestructiveLabelStyleRef.layoutStyleIndex)
                        destructiveLabel = true;
            EXPECT_TRUE(destructiveLabel);

            app.ClickAt(app.CenterOf(deleteID));
            app.Frame(draw);
            app.Frame(draw);
            EXPECT_EQ(deleteRuns, 1) << "test setup: an enabled item runs on a click";
        }

        // Closing the menu gives the focus back to where it was, unless the chosen item moved
        // it (Edit > Undo focusing the field it changed): then it stays there.
        TEST_F(ControlsMenuTest, AnItemThatMovesTheFocusKeepsIt)
        {
            const ID menuID = app.idManager.CreateStaticID();
            const ID keepID = app.idManager.CreateStaticID();
            const ID moveID = app.idManager.CreateStaticID();
            const ID fieldID = app.idManager.CreateStaticID();
            std::string text = "abc";
            auto draw = [&] {
                app.BeginRootWindow();
                if (app.BeginMenu(menuID, "Edit"))
                {
                    app.MenuItem(keepID, "Copy", [] {});
                    app.MenuItem(moveID, "Undo", [&] { app.guiStateManager.Interaction().SetFocusID(fieldID); });
                }
                app.EndMenu();
                app.Field(fieldID, text);
                app.EndRootWindow();
            };
            app.Frame(draw);

            app.ClickAt(app.CenterOf(menuID));
            app.Frame(draw);
            app.Frame(draw);
            app.ClickAt(app.CenterOf(keepID));
            app.Frame(draw);
            app.Frame(draw);
            ASSERT_FALSE(app.menuManager.IsOpen());
            EXPECT_EQ(app.guiStateManager.Interaction().GetFocusedID(), menuID)
                << "the focus goes back to what had it when the menu opened (the clicked trigger), not the gone item";

            app.ClickAt(app.CenterOf(menuID));
            app.Frame(draw);
            app.Frame(draw);
            app.ClickAt(app.CenterOf(moveID));
            app.Frame(draw);
            app.Frame(draw);
            ASSERT_FALSE(app.menuManager.IsOpen());
            EXPECT_EQ(app.guiStateManager.Interaction().GetFocusedID(), fieldID) << "closing the menu took the focus back";
        }

        // A menu with a filter: the filter has the keyboard when it opens, and items not
        // containing its text (in any case) are left out, with the separators.
        TEST_F(ControlsMenuTest, AMenuFilterTakesTheKeyboardAndHidesWhatDoesntMatch)
        {
            const ID menuID = app.idManager.CreateStaticID();
            const char* const views[] = { "Perspective", "Orthographic", "Top", "Front", "Side" };
            ID itemIDs[5] = {};
            for (ID& id : itemIDs) id = app.idManager.CreateStaticID();
            auto draw = [&] {
                app.BeginRootWindow();
                if (app.BeginMenu(menuID, "Camera"))
                {
                    app.MenuFilter();
                    for (int i = 0; i < 5; ++i)
                    {
                        app.MenuItem(itemIDs[i], views[i], [] {});
                        if (i == 1) app.MenuSeparator();
                    }
                }
                app.EndMenu();
                app.EndRootWindow();
            };
            app.Frame(draw);
            app.ClickAt(app.CenterOf(menuID));
            app.Frame(draw);
            app.Frame(draw);
            ASSERT_TRUE(app.menuManager.IsOpen());
            const ID active = app.menuManager.levels[0].activeItemID;
            for (const ID& id : itemIDs)
                EXPECT_NE(active, id) << "no item took the keyboard";
            EXPECT_EQ(app.guiStateManager.TextEditorFocus().GetFocusedID(), active) << "the filter has it";

            app.menuManager.levels[0].filter = "O";
            app.Frame(draw);
            EXPECT_FALSE(app.HasElement(itemIDs[0])) << "Perspective";
            EXPECT_TRUE(app.HasElement(itemIDs[1]))  << "Orthographic";
            EXPECT_TRUE(app.HasElement(itemIDs[2]))  << "Top";
            EXPECT_TRUE(app.HasElement(itemIDs[3]))  << "Front";
            EXPECT_FALSE(app.HasElement(itemIDs[4])) << "Side";
        }

        // Hovering a row focuses it the next frame; the row must stay highlighted.
        TEST_F(ControlsMenuTest, AHoveredRowStaysHighlightedOnceFocused)
        {
            const StyleRef row = app.theme.menuItemRowStyleRef;
            auto background = [&](ElementState state) {
                const VisualBoxStyle* style = app.styleManager.GetVisualBoxStyle(row.visualBoxStyleIndex, state);
                return style ? style->backgroundColorIndex : VisualColorStyleIndex{};
            };
            EXPECT_NE(background(ElementState::Hover), background(ElementState::None));
            EXPECT_EQ(background(ElementState::Focused), background(ElementState::Hover));
            EXPECT_EQ(background(ElementState::Hover | ElementState::Focused), background(ElementState::Hover));
        }

        // Each menu has its own popup window, so switching never shows another menu's
        // last frame while the new one renders.
        TEST_F(ControlsMenuTest, SwitchingMenusUsesEachMenusOwnPopupWindow)
        {
            ID fileID = app.idManager.CreateStaticID();
            ID editID = app.idManager.CreateStaticID();
            auto draw = [&] {
                app.BeginRootWindow();
                app.BeginMenuBar();
                for (const ID id : { fileID, editID })
                {
                    if (app.BeginMenu(id, id == fileID ? "File" : "Edit", app.theme.menuBarTriggerStyleRef, app.theme.menuStyleRef, app.theme.menuItemRowStyleRef))
                        app.MenuItem(id == fileID ? "New" : "Undo", nullptr);
                    app.EndMenu();
                }
                app.EndMenuBar();
                app.EndRootWindow();
            };
            auto open = [&](ID trigger) {
                app.ClickAt(app.CenterOf(trigger));
                app.Frame(draw);
                app.Frame(draw);
                EXPECT_TRUE(app.menuManager.IsOpen());
                EXPECT_EQ(app.menuManager.levels[0].triggerID, trigger);
                return app.menuManager.levels[0].windowID;
            };

            app.Frame(draw);
            const ID fileWindow = open(fileID);
            const ID editWindow = open(editID);
            EXPECT_NE(fileWindow, editWindow);
            EXPECT_EQ(open(fileID), fileWindow) << "a menu keeps its window";
        }

        // OpenContextMenu opens a context menu at a point, for something that isn't an
        // element (a node-view wire); a right-click alone never opens it.
        TEST_F(ControlsMenuTest, OpenContextMenuOpensAtThePoint)
        {
            const ID owner = app.idManager.CreateStaticID();
            bool built = false;
            auto draw = [&] {
                app.BeginRootWindow();
                built = app.BeginContextMenu(owner);
                if (built)
                    app.MenuItem("Delete", nullptr);
                app.EndContextMenu();
                app.EndRootWindow();
            };

            app.Frame(draw);
            app.ClickAt({ 300, 200 }, MouseButtons::Right);
            app.Frame(draw);
            EXPECT_FALSE(built) << "nothing to right-click";

            app.OpenContextMenu(owner, { 300, 200 });
            app.Frame(draw);
            app.Frame(draw);
            ASSERT_TRUE(built);
            const auto rect = app.GetElementRect(app.menuManager.levels[0].windowID);
            EXPECT_EQ(rect.x, 300);
            EXPECT_EQ(rect.y, 200);
        }

        // A right-click opens the menu where it was clicked, also deep inside a tall target;
        // from the keyboard it opens below the target.
        TEST_F(ControlsMenuTest, ARightClickOpensTheContextMenuAtThePointer)
        {
            const ID target = app.idManager.CreateStaticID();
            auto draw = [&] {
                app.BeginRootWindow();
                app.Button(target, "Target", nullptr, LayoutOverride{}.Size(400, 200));
                if (app.BeginContextMenu(target))
                    app.MenuItem("Delete", nullptr);
                app.EndContextMenu();
                app.EndRootWindow();
            };
            app.Frame(draw);
            const IntRect box = app.GetElementRect(target);
            const int2 at{ box.x + 300, box.y + 40 };
            app.InjectMouseMove(at);
            app.Frame(draw);
            app.ClickAt(at, MouseButtons::Right);
            app.Frame(draw);
            app.Frame(draw);
            ASSERT_TRUE(app.menuManager.IsOpen());
            IntRect menu = app.GetElementRect(app.menuManager.levels[0].windowID);
            EXPECT_EQ(menu.x, at.x);
            EXPECT_EQ(menu.y, at.y);

            app.InjectKey(KeyCode::Escape);
            app.Frame(draw);
            ASSERT_FALSE(app.menuManager.IsOpen());
            app.ClickAt(app.CenterOf(target));
            app.Frame(draw);
            app.InjectKey(KeyCode::F10, KeyAction::KeyDown, KeyModifier::Shift);
            app.Frame(draw);
            app.Frame(draw);
            ASSERT_TRUE(app.menuManager.IsOpen());
            menu = app.GetElementRect(app.menuManager.levels[0].windowID);
            EXPECT_EQ(menu.x, box.x);
            EXPECT_EQ(menu.y, box.y + box.h) << "below the target";
        }

        TEST_F(ControlsMenuTest, ClickingOpenTriggerAgainTogglesItClosed)
        {
            ID menuID = app.idManager.CreateStaticID();
            auto draw = [&] {
                app.BeginRootWindow();
                app.BeginMenu(menuID, "File"); app.EndMenu();
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
                app.BeginMenu(menuID, "File"); app.EndMenu();
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
                app.BeginMenu(menuID, "File"); app.EndMenu();
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
                app.BeginMenu(menuID, "File"); app.EndMenu();
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
                app.BeginMenu(menuID, "M"); app.EndMenu();
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
                app.BeginMenu(menuID, "M"); app.EndMenu();
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
                app.BeginMenu(menuID, "M"); app.EndMenu();
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
                }
                app.EndMenu();
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
                }
                app.EndMenu();
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
} // namespace tachyon
