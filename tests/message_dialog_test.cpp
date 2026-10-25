// MessageDialog: a modal question with verb buttons. The default button has the keyboard;
// a click, Esc (the cancel button) or Alt + a button's first letter chooses, reported once
// on the next call.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

namespace
{
    using namespace interstice;
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

    // The dialog is its own window over the one it's asked from: it takes no room in that
    // window's layout, and dims it (the window's scrim draw) only while asking.
    TEST_F(MessageDialogTest, TheDialogTakesNoRoomAndDimsItsWindow)
    {
        Emit(false);
        const IntRect alone = app.GetElementRect(belowID);
        const ElementRef root = app.elementStorage.FindElementRefByID(app.RootWindowID());
        EXPECT_EQ(app.elementStorage.GetCustomDraw(root), nullptr);

        Emit();
        Emit();
        EXPECT_EQ(app.GetElementRect(belowID).position, alone.position) << "the window's content stays put";
        EXPECT_NE(app.elementStorage.GetCustomDraw(app.elementStorage.FindElementRefByID(app.RootWindowID())), nullptr)
            << "the scrim";
    }
}
