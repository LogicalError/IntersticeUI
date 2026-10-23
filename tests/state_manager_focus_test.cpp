#include <gtest/gtest.h>
#include <gui_core/style/gui_style.h>
#include <gui_core/state/gui_state_manager.h>

namespace interstice
{

    namespace
    {
        inline ID MakeID(uint64_t value)
        {
            return ID{ .value{.integer = value } };
        }
    }

    TEST(StateManagerFocusTest, FocusIsAppliedImmediately)
    {
        GUIStateManager guiStateManager;

        guiStateManager.BeginFrame();

        const ID target = MakeID(10);
        guiStateManager.Interaction().SetFocusID(target);

        EXPECT_EQ(guiStateManager.Interaction().GetFocusedID(), target);
    }

    TEST(StateManagerFocusTest, InvalidFocusClearsImmediately)
    {
        GUIStateManager guiStateManager;

        guiStateManager.BeginFrame();

        const ID focused = MakeID(11);
        guiStateManager.Interaction().SetFocusID(focused);
        EXPECT_EQ(guiStateManager.Interaction().GetFocusedID(), focused);

        guiStateManager.Interaction().SetFocusID(ID::Invalid());

        EXPECT_EQ(guiStateManager.Interaction().GetFocusedID(), ID::Invalid());
    }

    TEST(StateManagerFocusTest, AssociatedFocusedLabelGetsFocusedState)
    {
        GUIStateManager guiStateManager;

        guiStateManager.BeginFrame();

        const ID control = MakeID(21);
        const ID label = MakeID(22);

        guiStateManager.Interaction().SetFocusID(control);
        guiStateManager.Labels().ClearFocusedAssociatedLabelIDs();
        guiStateManager.Labels().AddFocusedAssociatedLabelID(label);

        const ElementState controlState = guiStateManager.GetCurrentState(control);
        const ElementState labelState = guiStateManager.GetCurrentState(label);

        EXPECT_NE((controlState & ElementState::Focused), ElementState::None);
        EXPECT_NE((labelState & ElementState::Focused), ElementState::None);
    }

    TEST(StateManagerFocusTest, AssociatedFocusedLabelIsCallerManaged)
    {
        GUIStateManager guiStateManager;

        guiStateManager.BeginFrame();

        const ID label = MakeID(33);
        guiStateManager.Labels().ClearFocusedAssociatedLabelIDs();
        guiStateManager.Labels().AddFocusedAssociatedLabelID(label);

        EXPECT_NE((guiStateManager.GetCurrentState(label) & ElementState::Focused), ElementState::None);

        guiStateManager.Labels().ClearFocusedAssociatedLabelIDs();
        guiStateManager.Labels().AddFocusedAssociatedLabelID(ID::Invalid());

        EXPECT_EQ((guiStateManager.GetCurrentState(label) & ElementState::Focused), ElementState::None);
    }
}