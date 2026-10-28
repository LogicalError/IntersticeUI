// Dropdown: a field showing the selected option, whose list opens under it on the current
// option and sets the selection when an option is picked.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_controls/gui_menu_manager.h>

#include <string>
#include <string_view>
#include <vector>

namespace tachyon
{
    using GUITest::HeadlessGUIApp;

    namespace
    {
        constexpr std::string_view kQualities[] = { "Low", "Medium", "High" };

        class DropdownTest : public ::testing::Test
        {
        protected:
            void SetUp()    override { ASSERT_TRUE(app.Initialize()); Frame(); }
            void TearDown() override { app.Cleanup(); }

            void Frame()
            {
                app.Frame([&] {
                    app.BeginRootWindow();
                    if (app.Dropdown(dropdownID, selected, kQualities))
                        ++changes;
                    app.EndRootWindow();
                });
            }

            void Open()
            {
                app.ClickAt(app.CenterOf(dropdownID));
                Frame();
                Frame();
                ASSERT_TRUE(app.menuManager.IsOpen());
            }

            void Key(KeyCode key)
            {
                app.InjectKey(key, KeyAction::KeyDown);
                Frame();
                app.InjectKey(key, KeyAction::KeyUp);
                Frame();
            }

            // An option's text in the open list (the field shows the selected one too).
            ID Option(std::string_view text) const
            {
                const auto& storage = app.elementStorage;
                for (auto it = storage.TopDownDepthScanBegin(); it != storage.TopDownDepthScanEnd(); ++it)
                    for (const Element& e : it->GetChildren(storage))
                        if (e.id != dropdownID && app.TextOf(e.id) == text)
                            return e.id;
                return ID::Invalid();
            }

            // The text of the row the keyboard is on.
            std::string_view ActiveOption() const
            {
                const ID active = app.menuManager.levels[0].activeItemID;
                for (std::string_view option : kQualities)
                {
                    const ID label = Option(option);
                    if (active != ID::Invalid() && label != ID::Invalid() && app.GetElementRect(active).Contains(app.CenterOf(label)))
                        return option;
                }
                return {};
            }

            HeadlessGUIApp app;
            ID             dropdownID = app.idManager.CreateStaticID();
            int            selected   = 1;
            int            changes    = 0;
        };

        TEST_F(DropdownTest, ShowsTheSelectedOption)
        {
            EXPECT_EQ(app.TextOf(dropdownID), "Medium");
            EXPECT_FALSE(app.menuManager.IsOpen());
        }

        TEST_F(DropdownTest, OpensOnTheCurrentOption)
        {
            Open();
            EXPECT_NE(Option("Low"), ID::Invalid());
            EXPECT_NE(Option("High"), ID::Invalid());
            EXPECT_EQ(ActiveOption(), "Medium") << "the list starts on the current option, not the first";
        }

        TEST_F(DropdownTest, PickingAnOptionSelectsItAndCloses)
        {
            Open();
            app.ClickAt(app.CenterOf(Option("High")));
            Frame();
            Frame();
            Frame();
            EXPECT_EQ(selected, 2);
            EXPECT_EQ(changes, 1) << "true on one frame only";
            EXPECT_FALSE(app.menuManager.IsOpen());
            EXPECT_EQ(app.TextOf(dropdownID), "High") << "the field shows it from the next frame";
        }

        TEST_F(DropdownTest, PickingTheCurrentOptionIsNoChange)
        {
            Open();
            app.ClickAt(app.CenterOf(Option("Medium")));
            Frame();
            Frame();
            EXPECT_EQ(selected, 1);
            EXPECT_EQ(changes, 0);
            EXPECT_FALSE(app.menuManager.IsOpen());
        }

        TEST_F(DropdownTest, EscapeClosesWithoutAChange)
        {
            Open();
            Key(KeyCode::Escape);
            EXPECT_FALSE(app.menuManager.IsOpen());
            EXPECT_EQ(selected, 1);
            EXPECT_EQ(changes, 0);
        }

        TEST_F(DropdownTest, TheKeyboardPicksAnOption)
        {
            app.guiStateManager.Interaction().SetFocusID(dropdownID);
            Frame();
            Key(KeyCode::Down);
            ASSERT_TRUE(app.menuManager.IsOpen()) << "Down opens the list";
            EXPECT_EQ(ActiveOption(), "Medium");

            Key(KeyCode::Down);
            EXPECT_EQ(ActiveOption(), "High");

            Key(KeyCode::Return);
            EXPECT_EQ(selected, 2);
            EXPECT_EQ(changes, 1);
            EXPECT_FALSE(app.menuManager.IsOpen());
        }

        // A list taller than the screen scrolls to show the current option when it opens.
        TEST(Dropdown, ALongListOpensScrolledToTheCurrentOption)
        {
            HeadlessGUIApp app;
            ASSERT_TRUE(app.Initialize());
            std::vector<std::string> names;
            for (int i = 1; i <= 200; ++i)
                names.push_back("Option " + std::to_string(i));
            const std::vector<std::string_view> options(names.begin(), names.end());
            const ID dropdownID = app.idManager.CreateStaticID();
            int selected = 180;
            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow();
                    app.Dropdown(dropdownID, selected, options);
                    app.EndRootWindow();
                });
            };
            frame();
            app.ClickAt(app.CenterOf(dropdownID));
            for (int i = 0; i < 4; ++i)
                frame();
            ASSERT_TRUE(app.menuManager.IsOpen());

            const IntRect list = app.GetElementRect(app.menuManager.levels[0].windowID);
            const IntRect row  = app.GetElementRect(app.menuManager.levels[0].activeItemID);
            ASSERT_GT(row.h, 0);
            EXPECT_LT(list.h, 200 * row.h) << "test setup: the list is shorter than its rows";
            EXPECT_GE(row.y, list.y);
            EXPECT_LE(row.y + row.h, list.y + list.h) << "Option 181 is in view";
            app.Cleanup();
        }

        // Options written in place, a call-site ID, and no valid selection: the field is
        // empty and the list opens on the first option.
        TEST(Dropdown, OptionsInPlaceAndNoSelection)
        {
            HeadlessGUIApp app;
            ASSERT_TRUE(app.Initialize());
            int  selected = -1;
            ID   fieldID  = ID::Invalid();
            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow();
                    app.Dropdown(selected, { "Red", "Green" });
                    fieldID = app.elementStorage.GetLastAddedID();
                    app.EndRootWindow();
                });
            };
            frame();
            frame();
            ASSERT_NE(fieldID, ID::Invalid());

            app.ClickAt(app.CenterOf(fieldID));
            frame();
            frame();
            ASSERT_TRUE(app.menuManager.IsOpen());
            app.ClickAt(app.CenterOf(app.FindByText("Green")));
            frame();
            frame();
            EXPECT_EQ(selected, 1);
            app.Cleanup();
        }
    }
}
