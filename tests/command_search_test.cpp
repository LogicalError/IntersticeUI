#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <string>
#include <vector>

namespace
{
    using namespace tachyon;
    using GUITest::HeadlessGUIApp;

    // The IDs of an element's children, in order.
    std::vector<ID> ChildIDs(HeadlessGUIApp& app, ID parent)
    {
        std::vector<ID> ids;
        const ElementRef ref = app.elementStorage.FindElementRefByID(parent);
        if (!ref.IsValid())
            return ids;
        const auto range = app.elementStorage.GetChildRange(ref);
        for (uint32_t i = range.begin; i < range.end; ++i)
            if (const auto* element = app.elementStorage.GetElementPtr(ElementRef{ ref.depth + 1, i }))
                ids.push_back(element->id);
        return ids;
    }

    class CommandSearchTest : public ::testing::Test
    {
    protected:
        HeadlessGUIApp           app;
        CommandSearchState       state;
        ID                       searchID = ID::Invalid();
        std::vector<std::string> ran;
        bool                     returned = false;

        void SetUp() override
        {
            ASSERT_TRUE(app.Initialize());
            searchID = app.idManager.CreateStaticID();
            for (const char* name : { "Open", "Save", "Save As", "Exit" })
                app.commandManager.RegisterCommand(IconID::Invalid, [this, name] { ran.push_back(name); }, name);
        }

        void Frame()
        {
            app.Frame([&] {
                app.BeginRootWindow();
                returned = app.CommandSearch(searchID, state) || returned;
                app.EndRootWindow();
            });
        }

        // Focuses the search and lets the text editor settle.
        void Focus()
        {
            Frame();
            GUIApplication::FocusCommandSearch(state);
            Frame();
            Frame();
        }

        std::vector<ID> Rows() { return ChildIDs(app, state.listWindowID); }
    };

    TEST_F(CommandSearchTest, ListsNothingUntilFocused)
    {
        Frame();
        Frame();
        EXPECT_TRUE(Rows().empty());
    }

    TEST_F(CommandSearchTest, FocusedItListsEveryCommand)
    {
        Focus();
        EXPECT_EQ(Rows().size(), 4u);
    }

    TEST_F(CommandSearchTest, ClickingTheBoxFocusesIt)
    {
        Frame();
        app.ClickAt(app.CenterOf(searchID));
        Frame();
        Frame();
        EXPECT_EQ(Rows().size(), 4u);
    }

    TEST_F(CommandSearchTest, ClickingTheBoxInAMenuBarFocusesIt)
    {
        auto draw = [&] {
            app.BeginRootWindow();
            app.BeginMenuBar();
            if (app.BeginMenu(app.idManager.AllocateDynamicID("file"), "File",
                              app.theme.menuBarTriggerStyleRef, app.theme.menuStyleRef, app.theme.menuItemRowStyleRef))
            {
                app.MenuItem("New");
            }
            app.EndMenu();
            app.CommandSearch(searchID, state);
            app.EndMenuBar();
            app.EndRootWindow();
        };
        app.Frame(draw);
        app.ClickAt(app.CenterOf(searchID));
        app.Frame(draw);
        app.Frame(draw);
        EXPECT_EQ(Rows().size(), 4u);
    }

    TEST_F(CommandSearchTest, TypingFiltersByDescriptionIgnoringCase)
    {
        Focus();
        app.InjectText("sA");
        Frame();
        Frame();
        EXPECT_EQ(state.query, "sA");
        EXPECT_EQ(Rows().size(), 2u) << "Save and Save As";
    }

    TEST_F(CommandSearchTest, EnterRunsTheHighlightedCommand)
    {
        Focus();
        app.InjectText("save");
        Frame();
        app.InjectKey(KeyCode::Down);
        Frame();
        app.InjectKey(KeyCode::Return);
        Frame();
        Frame();
        ASSERT_EQ(ran.size(), 1u);
        EXPECT_EQ(ran[0], "Save As");
        EXPECT_TRUE(returned);
        EXPECT_TRUE(state.query.empty());
        EXPECT_TRUE(Rows().empty()) << "running a command leaves the search";
    }

    TEST_F(CommandSearchTest, UpWrapsToTheLastMatch)
    {
        Focus();
        app.InjectKey(KeyCode::Up);
        Frame();
        app.InjectKey(KeyCode::Return);
        Frame();
        ASSERT_EQ(ran.size(), 1u);
        EXPECT_EQ(ran[0], "Exit");
    }

    TEST_F(CommandSearchTest, ClickingARowRunsIt)
    {
        Focus();
        const auto rows = Rows();
        ASSERT_EQ(rows.size(), 4u);
        app.ClickAt(app.CenterOf(rows[1]));
        Frame();
        Frame();
        ASSERT_EQ(ran.size(), 1u);
        EXPECT_EQ(ran[0], "Save");
    }

    TEST_F(CommandSearchTest, EscapeLeavesWithoutRunningAnything)
    {
        Focus();
        app.InjectText("ex");
        Frame();
        app.InjectKey(KeyCode::Escape);
        Frame();
        Frame();
        EXPECT_TRUE(ran.empty());
        EXPECT_FALSE(returned);
        EXPECT_TRUE(state.query.empty());
        EXPECT_TRUE(Rows().empty());
    }

    TEST_F(CommandSearchTest, OtherModesCommandsAreLeftOut)
    {
        const CommandMode sculpt = app.commandManager.RegisterMode("Sculpt");
        app.commandManager.RegisterCommand(sculpt, KeyCode::KeyB, KeyModifier::None, [] {}, "Brush");
        Focus();
        EXPECT_EQ(Rows().size(), 4u);
        app.commandManager.SetMode(sculpt);
        Frame();
        EXPECT_EQ(Rows().size(), 5u);
    }

    TEST(StatusBar, HoldsItsTexts)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        const ID barID = app.idManager.CreateStaticID();
        app.Frame([&] {
            app.BeginRootWindow();
            app.BeginStatusBar(barID);
            app.StatusText("Ready");
            app.StatusText("12 ms");
            app.EndStatusBar();
            app.EndRootWindow();
        });
        EXPECT_EQ(ChildIDs(app, barID).size(), 2u);
    }
}
