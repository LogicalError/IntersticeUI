// MessageDialog: a modal question with verb buttons. The default button has the keyboard;
// a click, Esc (the cancel button) or Alt + a button's first letter chooses, reported once
// on the next call.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

namespace
{
    using namespace tachyon;
    using GUITest::HeadlessGUIApp;

    class MessageDialogTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ASSERT_TRUE(app.Initialize());
            dialogID = app.idManager.CreateStaticID();
            aboveID  = app.idManager.CreateStaticID();
            belowID  = app.idManager.CreateStaticID();
        }

        // One frame asking (or not); returns what MessageDialog returned.
        int Emit(bool asking = true)
        {
            static constexpr MessageButton kButtons[] = {
                { .label = "Cancel" },
                { .label = "Quit", .kind = MessageButtonKind::Danger },
            };
            int choice = -1;
            app.Frame([&] {
                app.BeginRootWindow({ 1200, 800 });
                app.Button(aboveID, "Above", [] {});
                if (asking)
                choice = app.MessageDialog(dialogID, MessageDialogDesc{
                    .icon = MessageIcon::Quit, .title = "Quit Test?", .message = "Running tasks will be stopped.",
                    .buttons = kButtons, .defaultButton = 0, .cancelButton = 0 });
                app.Button(belowID, "Below", [] {});
                app.EndRootWindow();
            });
            return choice;
        }

        ID FindByStyle(StyleRef style)
        {
            const auto& storage = app.elementStorage;
            for (auto it = storage.TopDownDepthScanBegin(); it != storage.TopDownDepthScanEnd(); ++it)
                for (const Element& e : it->GetChildren(storage))
                    if (e.styleRef.layoutStyleIndex == style.layoutStyleIndex
                        && e.styleRef.visualBoxStyleIndex == style.visualBoxStyleIndex)
                        return e.id;
            return ID::Invalid();
        }

        HeadlessGUIApp app;
        ID             dialogID = ID::Invalid();
        ID             aboveID  = ID::Invalid();
        ID             belowID  = ID::Invalid();
    };

    TEST_F(MessageDialogTest, TheDefaultButtonHasTheKeyboard)
    {
        Emit();
        Emit();
        const ID cancel = FindByStyle(app.theme.dialogButtonStyleRef);
        ASSERT_NE(cancel, ID::Invalid());
        EXPECT_EQ(app.guiStateManager.Interaction().GetFocusedID(), cancel);
    }

    TEST_F(MessageDialogTest, AClickedButtonIsReportedOnce)
    {
        Emit();
        const ID quit = FindByStyle(app.theme.dialogDangerButtonStyleRef);
        ASSERT_NE(quit, ID::Invalid());
        app.ClickAt(app.CenterOf(quit));
        int choice = Emit();
        if (choice < 0) choice = Emit();
        EXPECT_EQ(choice, 1);
        EXPECT_EQ(Emit(), -1) << "reported once";
    }

    TEST_F(MessageDialogTest, EscChoosesTheCancelButton)
    {
        Emit();
        Emit();
        app.InjectKey(KeyCode::Escape);
        int choice = Emit();
        if (choice < 0) choice = Emit();
        EXPECT_EQ(choice, 0);
    }

    TEST_F(MessageDialogTest, AltAndAButtonsFirstLetterChoosesIt)
    {
        Emit();
        Emit();
        app.InjectKey(KeyCode::KeyQ, KeyAction::KeyDown, KeyModifier::Alt);
        int choice = Emit();
        if (choice < 0) choice = Emit();
        EXPECT_EQ(choice, 1);
    }

    // Tab goes through every control in the dialog, and the focus stays where Tab put it;
    // Esc cancels wherever the focus is in the dialog.
    TEST_F(MessageDialogTest, TabReachesTheDialogsOtherControls)
    {
        static constexpr MessageButton kButtons[] = { { .label = "Cancel" }, { .label = "Quit" } };
        bool dontAsk = false;
        int choice = -1;
        auto emit = [&] {
            app.Frame([&] {
                app.BeginRootWindow({ 1200, 800 });
                choice = app.MessageDialog(dialogID, MessageDialogDesc{
                    .title = "Quit Test?", .message = "Running tasks will be stopped.",
                    .buttons = kButtons, .defaultButton = 0, .cancelButton = 0, .dontAskAgain = &dontAsk, .details = "Task 3 of 5" });
                app.EndRootWindow();
            });
        };
        app.activeWindow = dialogID;   // the modal dialog has the keyboard
        emit();
        emit();
        const ID cancel = FindByStyle(app.theme.dialogButtonStyleRef);
        ASSERT_EQ(app.guiStateManager.Interaction().GetFocusedID(), cancel);

        // Round the dialog: every control reached once, none of them pulled back.
        std::vector<ID> reached;
        for (int i = 0; i < 8; ++i)
        {
            app.InjectKey(KeyCode::Tab);
            emit();
            emit();
            const ID now = app.guiStateManager.Interaction().GetFocusedID();
            if (std::find(reached.begin(), reached.end(), now) == reached.end())
                reached.push_back(now);
        }
        EXPECT_GE(reached.size(), 5u) << "close, don't ask again, details and both buttons";

        // Focus on the checkbox: Esc still cancels.
        const ID dontAskID = app.FindByText("Don't ask again");
        ASSERT_NE(dontAskID, ID::Invalid());
        for (int i = 0; i < 8 && (app.guiStateManager.GetCurrentState(dontAskID) & ElementState::Focused) == ElementState::None; ++i)
        {
            app.InjectKey(KeyCode::Tab);
            emit();
            emit();
        }
        app.InjectKey(KeyCode::Escape);
        emit();
        if (choice < 0) emit();
        EXPECT_EQ(choice, 0);
        EXPECT_FALSE(dontAsk) << "Esc didn't reach the checkbox";
    }

    // The dialog is its own window over the one it's asked from: it takes no room in that
    // window's layout, and dims it (the window's scrim draw) only while asking.
    TEST_F(MessageDialogTest, TheDialogTakesNoRoomAndDimsItsWindow)
    {
        Emit(false);
        const IntRect alone = app.GetElementRect(belowID);
        const ElementRef root = app.elementStorage.FindElementRefByID(app.RootWindowID());
        EXPECT_EQ(app.elementStorage.GetCustomDraw(root), nullptr);

        Emit();
        app.AdvanceTime(60);
        Emit();
        EXPECT_EQ(app.GetElementRect(belowID).position, alone.position) << "the window's content stays put";
        EXPECT_NE(app.elementStorage.GetCustomDraw(app.elementStorage.FindElementRefByID(app.RootWindowID())), nullptr)
            << "the scrim";
    }

    // The scrim fades in over 120 ms while asking, and out again after the dialog closes.
    TEST_F(MessageDialogTest, TheScrimFadesInAndOut)
    {
        auto scrimDrawn = [&] { return app.elementStorage.GetCustomDraw(app.elementStorage.FindElementRefByID(app.RootWindowID())) != nullptr; };
        Emit();
        app.AdvanceTime(60);
        Emit();
        EXPECT_TRUE(scrimDrawn()) << "fading in";

        app.AdvanceTime(30);
        Emit(false);
        EXPECT_TRUE(scrimDrawn()) << "still fading out after the dialog closed";
        for (int i = 0; i < 4; ++i)
        {
            app.AdvanceTime(40);
            Emit(false);
        }
        EXPECT_FALSE(scrimDrawn()) << "gone 120 ms later";
    }
}
