// AutocompleteField: a text field with a list of the suggestions that contain its text.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <string>
#include <string_view>

namespace tachyon
{
    using GUITest::HeadlessGUIApp;

    namespace
    {
        constexpr std::string_view kFruits[] = { "Apple", "Banana", "Cherry", "Mango", "Orange", "Pear" };

        class AutocompleteTest : public ::testing::Test
        {
        protected:
            void SetUp() override
            {
                ASSERT_TRUE(app.Initialize());
                outsideID = app.idManager.CreateStaticID();
                Frame();
            }
            void TearDown() override { app.Cleanup(); }

            void Frame()
            {
                app.Frame([&] {
                    app.BeginRootWindow();
                    if (app.AutocompleteField(fieldID, value, kFruits))
                        ++changes;
                    app.BeginVertical(outsideID, LayoutOverride::Auto().Height(300));
                    app.EndVertical();
                    app.EndRootWindow();
                });
            }

            void Focus()
            {
                app.ClickAt(app.CenterOf(fieldID));
                Frame();
                Frame();
            }

            void Type(std::string_view text)
            {
                app.InjectText(text);
                Frame();
                Frame();
            }

            void Key(KeyCode key)
            {
                app.InjectKey(key, KeyAction::KeyDown);
                Frame();
                app.InjectKey(key, KeyAction::KeyUp);
                Frame();
            }

            bool Listed(std::string_view fruit) const { return app.CountByText(fruit) > (value == fruit ? 1 : 0); }
            int  ListedCount() const
            {
                int n = 0;
                for (std::string_view fruit : kFruits)
                    n += Listed(fruit) ? 1 : 0;
                return n;
            }

            HeadlessGUIApp app;
            ID             fieldID = app.idManager.CreateStaticID();
            ID             outsideID;
            std::string    value;
            int            changes = 0;
        };

        TEST_F(AutocompleteTest, ListsNothingUntilSomethingIsTyped)
        {
            EXPECT_EQ(ListedCount(), 0);
            Focus();
            EXPECT_EQ(ListedCount(), 0) << "focusing an empty field shows no list";
            Type("a");
            EXPECT_GT(ListedCount(), 0);
        }

        TEST_F(AutocompleteTest, TypingFiltersIgnoringCase)
        {
            Focus();
            Type("AN");
            EXPECT_EQ(value, "AN");
            EXPECT_TRUE(Listed("Banana"));
            EXPECT_TRUE(Listed("Mango"));
            EXPECT_TRUE(Listed("Orange"));
            EXPECT_FALSE(Listed("Apple"));
            EXPECT_EQ(ListedCount(), 3);
        }

        TEST_F(AutocompleteTest, DownAndEnterPickTheHighlightedSuggestion)
        {
            Focus();
            Type("an");
            changes = 0;
            Key(KeyCode::Down);
            Key(KeyCode::Down);
            Key(KeyCode::Return);
            EXPECT_EQ(value, "Mango");
            EXPECT_EQ(changes, 1);
            EXPECT_EQ(ListedCount(), 0) << "picking closes the list";
        }

        TEST_F(AutocompleteTest, EnterWithNothingHighlightedKeepsTheTypedText)
        {
            Focus();
            Type("an");
            Key(KeyCode::Return);
            EXPECT_EQ(value, "an");
        }

        TEST_F(AutocompleteTest, ClickingASuggestionPicksIt)
        {
            Focus();
            Type("e");
            ASSERT_TRUE(Listed("Cherry"));
            app.ClickAt(app.CenterOf(app.FindByText("Cherry")));
            Frame();
            Frame();
            EXPECT_EQ(value, "Cherry");
            EXPECT_EQ(ListedCount(), 0);
        }

        TEST_F(AutocompleteTest, EscapeClosesTheListAndTypingOpensItAgain)
        {
            Focus();
            Type("p");
            ASSERT_GT(ListedCount(), 0);
            Key(KeyCode::Escape);
            EXPECT_EQ(ListedCount(), 0);
            EXPECT_EQ(value, "p");
            Type("e");
            EXPECT_TRUE(Listed("Pear"));
        }

        TEST_F(AutocompleteTest, AClickElsewhereClosesTheList)
        {
            Focus();
            Type("a");
            ASSERT_GT(ListedCount(), 0);
            app.ClickAt(app.CenterOf(outsideID));
            Frame();
            Frame();
            EXPECT_EQ(ListedCount(), 0);
            EXPECT_EQ(value, "a");
        }

        // Tab with the list open goes on to the next field, not back to the first control.
        TEST(Autocomplete, TabWithTheListOpenGoesToTheNextField)
        {
            HeadlessGUIApp app;
            ASSERT_TRUE(app.Initialize());
            const ID firstID = app.idManager.CreateStaticID();
            const ID fieldID = app.idManager.CreateStaticID();
            const ID nextID  = app.idManager.CreateStaticID();
            std::string first, value, next;
            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow();
                    app.Field(firstID, first);
                    app.AutocompleteField(fieldID, value, kFruits);
                    app.Field(nextID, next);
                    app.EndRootWindow();
                });
            };
            frame();
            app.ClickAt(app.CenterOf(fieldID));
            frame();
            frame();
            app.InjectText("an");
            frame();
            frame();
            ASSERT_GT(app.CountByText("Banana"), 0) << "test setup: the list is open";

            app.InjectKey(KeyCode::Tab, KeyAction::KeyDown);
            frame();
            frame();
            EXPECT_EQ(app.guiStateManager.Interaction().GetFocusedID(), nextID);
            app.Cleanup();
        }

        TEST_F(AutocompleteTest, NoListForAnExactMatch)
        {
            Focus();
            Type("Pear");
            EXPECT_EQ(ListedCount(), 0) << "the only suggestion is what's typed";
        }
    }
}
