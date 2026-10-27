// The tutorial's app (examples/tutorial) behaves as docs/tutorial.md says. Each test names the
// tutorial page whose claim it checks; change the page and the test together.
// tutorial_snippets_test.cpp checks the pages show the same code as examples/tutorial.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include "../examples/tutorial/my_app.h"

namespace
{
    using namespace tachyon;
    using GUITest::HeadlessGUIApp;

    bool Valid(ID id) { return id != ID::Invalid(); }

    // The tutorial's two panels side by side, as the dock layout shows them.
    class TutorialTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ASSERT_TRUE(app.Initialize());
            Frame();
        }

        void Frame()
        {
            app.Frame([&] {
                app.BeginRootWindow();
                app.BeginHorizontal(LayoutOverride{}.FlexGrow(1));
                app.BeginVertical(LayoutOverride{}.Width(240));
                counter.Draw(app);
                app.EndVertical();
                app.BeginVertical(LayoutOverride{}.FlexGrow(1));
                greeting.Draw(app);
                app.EndVertical();
                app.EndHorizontal();
                app.EndRootWindow();
            });
        }

        // Clicks what shows `text`, and runs the frame that handles the click.
        void Click(std::string_view text)
        {
            const ID id = app.FindByText(text);
            ASSERT_TRUE(Valid(id)) << "nothing shows \"" << text << "\"";
            ClickOn(id);
        }

        void ClickOn(ID id)
        {
            app.ClickAt(app.CenterOf(id));
            Frame();
            Frame();
        }

        HeadlessGUIApp        app;
        my_app::CounterPanel  counter;
        my_app::GreetingPanel greeting;
    };

    // docs/tutorial/buttons.md: the panel shows the count.
    TEST_F(TutorialTest, TheCounterShowsTheCount)
    {
        EXPECT_TRUE(Valid(app.FindByText("Count: 0")));
    }

    // docs/tutorial/buttons.md: "Add one" adds one, and the next frame shows it.
    TEST_F(TutorialTest, AddOneAddsOne)
    {
        Click("Add one");
        Click("Add one");
        EXPECT_EQ(counter.count, 2);
        EXPECT_TRUE(Valid(app.FindByText("Count: 2")));
    }

    // docs/tutorial/buttons.md: "Reset" sets the count back to 0.
    TEST_F(TutorialTest, ResetSetsTheCountToZero)
    {
        Click("Add one");
        Click("Reset");
        EXPECT_EQ(counter.count, 0);
        EXPECT_TRUE(Valid(app.FindByText("Count: 0")));
    }

    // docs/tutorial/fields.md: each label is followed by the field that edits its value.
    TEST_F(TutorialTest, TheFieldsShowTheValues)
    {
        EXPECT_EQ(app.TextOf(app.FindControlAfterLabel("Name")), "World");
        EXPECT_EQ(app.TextOf(app.FindControlAfterLabel("Excited")), "No");
        EXPECT_TRUE(Valid(app.FindControlAfterLabel("Times")));
        EXPECT_TRUE(Valid(app.FindByText("Hello, World.")));
    }

    // docs/tutorial/fields.md: typing a name changes the value, and the greeting with it.
    TEST_F(TutorialTest, TypingANameChangesTheGreeting)
    {
        const ID name = app.FindControlAfterLabel("Name");
        ASSERT_TRUE(Valid(name));
        ClickOn(name);
        app.InjectText(" XL");
        Frame();
        Frame();
        EXPECT_EQ(greeting.name, "World XL");
        EXPECT_TRUE(Valid(app.FindByText("Hello, World XL.")));
    }

    // docs/tutorial/fields.md: the toggle flips the value and its text.
    TEST_F(TutorialTest, TheToggleFlipsTheValue)
    {
        const ID toggle = app.FindControlAfterLabel("Excited");
        ASSERT_TRUE(Valid(toggle));
        ClickOn(toggle);
        EXPECT_TRUE(greeting.excited);
        EXPECT_EQ(app.TextOf(app.FindControlAfterLabel("Excited")), "Yes");
        EXPECT_TRUE(Valid(app.FindByText("Hello, World!")));
    }

    // docs/tutorial/fields.md: the greeting is drawn `times` times.
    TEST_F(TutorialTest, TheGreetingIsDrawnTimesTimes)
    {
        EXPECT_EQ(app.CountByText("Hello, World."), 1);
        greeting.times = 3;
        Frame();
        EXPECT_EQ(app.CountByText("Hello, World."), 3);
    }
}
