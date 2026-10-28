#include <gtest/gtest.h>
#include <gui_core/state/gui_tab_chain_manager.h>
#include <gui_core/state/gui_state_manager.h>
#include <gui_core/gui_id_manager.h>

using namespace tachyon;
using namespace testing;

class TabChainManagerTest : public ::testing::Test
{
protected:
    IDManager idManager;
    GUIStateManager stateManager;
    TabChainManager tabChainManager;

    ID windowID1;
    ID windowID2;
    ID elementID1;
    ID elementID2;
    ID elementID3;
    ID elementID4;

    void SetUp() override
    {
        ASSERT_TRUE(idManager.BeginFrame());
        stateManager.BeginFrame();

        // Allocate test IDs
        windowID1 = idManager.AllocateDynamicID();
        windowID2 = idManager.AllocateDynamicID();
        elementID1 = idManager.AllocateDynamicID();
        elementID2 = idManager.AllocateDynamicID();
        elementID3 = idManager.AllocateDynamicID();
        elementID4 = idManager.AllocateDynamicID();
    }

    void TearDown() override
    {
        idManager.EndFrame();
    }

    void CommitFocused(ID id)
    {
        stateManager.Interaction().SetFocusID(id);
        stateManager.BeginFrame();
    }
};

TEST_F(TabChainManagerTest, NoFocus_DefaultsToFirstElement)
{
    stateManager.Interaction().SetActiveWindowID(windowID1);
    tabChainManager.BeginFrame(stateManager);

    tabChainManager.BeginWindow(windowID1);
    tabChainManager.RegisterTabTarget(elementID1);
    tabChainManager.RegisterTabTarget(elementID2);
    tabChainManager.RegisterTabTarget(elementID3);
    tabChainManager.EndWindow();

    tabChainManager.EndFrame();

    // With no focus, both next and prev should point to first element
    EXPECT_EQ(tabChainManager.GetNextTabTarget(), elementID1);
    EXPECT_EQ(tabChainManager.GetPrevTabTarget(), elementID1);
}

TEST_F(TabChainManagerTest, TabForward_NavigatesToNextElement)
{
    CommitFocused(elementID2);
    stateManager.Interaction().SetActiveWindowID(windowID1);
    tabChainManager.BeginFrame(stateManager);

    tabChainManager.BeginWindow(windowID1);
    tabChainManager.RegisterTabTarget(elementID1);
    tabChainManager.RegisterTabTarget(elementID2);
    tabChainManager.RegisterTabTarget(elementID3);
    tabChainManager.EndWindow();

    tabChainManager.EndFrame();

    EXPECT_EQ(tabChainManager.GetNextTabTarget(), elementID3);
}

TEST_F(TabChainManagerTest, TabBackward_NavigatesToPreviousElement)
{
    CommitFocused(elementID2);
    stateManager.Interaction().SetActiveWindowID(windowID1);
    tabChainManager.BeginFrame(stateManager);

    tabChainManager.BeginWindow(windowID1);
    tabChainManager.RegisterTabTarget(elementID1);
    tabChainManager.RegisterTabTarget(elementID2);
    tabChainManager.RegisterTabTarget(elementID3);
    tabChainManager.EndWindow();

    tabChainManager.EndFrame();

    EXPECT_EQ(tabChainManager.GetPrevTabTarget(), elementID1);
}

TEST_F(TabChainManagerTest, TabForwardFromLast_WrapsToFirst)
{
    CommitFocused(elementID3);
    stateManager.Interaction().SetActiveWindowID(windowID1);
    tabChainManager.BeginFrame(stateManager);

    tabChainManager.BeginWindow(windowID1);
    tabChainManager.RegisterTabTarget(elementID1);
    tabChainManager.RegisterTabTarget(elementID2);
    tabChainManager.RegisterTabTarget(elementID3);
    tabChainManager.EndWindow();

    tabChainManager.EndFrame();

    EXPECT_EQ(tabChainManager.GetNextTabTarget(), elementID1);
}

TEST_F(TabChainManagerTest, TabBackwardFromFirst_WrapsToLast)
{
    CommitFocused(elementID1);
    stateManager.Interaction().SetActiveWindowID(windowID1);
    tabChainManager.BeginFrame(stateManager);

    tabChainManager.BeginWindow(windowID1);
    tabChainManager.RegisterTabTarget(elementID1);
    tabChainManager.RegisterTabTarget(elementID2);
    tabChainManager.RegisterTabTarget(elementID3);
    tabChainManager.EndWindow();

    tabChainManager.EndFrame();

    EXPECT_EQ(tabChainManager.GetPrevTabTarget(), elementID3);
}

TEST_F(TabChainManagerTest, SingleElement_WrapsToItself)
{
    CommitFocused(elementID1);
    stateManager.Interaction().SetActiveWindowID(windowID1);
    tabChainManager.BeginFrame(stateManager);

    tabChainManager.BeginWindow(windowID1);
    tabChainManager.RegisterTabTarget(elementID1);
    tabChainManager.EndWindow();

    tabChainManager.EndFrame();

    EXPECT_EQ(tabChainManager.GetNextTabTarget(), elementID1);
    EXPECT_EQ(tabChainManager.GetPrevTabTarget(), elementID1);
}

TEST_F(TabChainManagerTest, NoElements_ReturnsInvalid)
{
    stateManager.Interaction().SetActiveWindowID(windowID1);
    tabChainManager.BeginFrame(stateManager);

    tabChainManager.BeginWindow(windowID1);
    // No elements registered
    tabChainManager.EndWindow();

    tabChainManager.EndFrame();

    EXPECT_EQ(tabChainManager.GetNextTabTarget(), ID::Invalid());
    EXPECT_EQ(tabChainManager.GetPrevTabTarget(), ID::Invalid());
}

TEST_F(TabChainManagerTest, PerWindowScoping_FocusInWindow1)
{
    CommitFocused(elementID2);
    stateManager.Interaction().SetActiveWindowID(windowID1);
    tabChainManager.BeginFrame(stateManager);

    // Window 1 - contains focused element
    tabChainManager.BeginWindow(windowID1);
    tabChainManager.RegisterTabTarget(elementID1);
    tabChainManager.RegisterTabTarget(elementID2);
    tabChainManager.RegisterTabTarget(elementID3);
    tabChainManager.EndWindow();

    // Window 2 - should be ignored
    tabChainManager.BeginWindow(windowID2);
    tabChainManager.RegisterTabTarget(elementID4);
    tabChainManager.EndWindow();

    tabChainManager.EndFrame();

    // Should navigate within window 1 only
    EXPECT_EQ(tabChainManager.GetNextTabTarget(), elementID3);
    EXPECT_EQ(tabChainManager.GetPrevTabTarget(), elementID1);
}

TEST_F(TabChainManagerTest, PerWindowScoping_FocusInWindow2)
{
    CommitFocused(elementID4);
    stateManager.Interaction().SetActiveWindowID(windowID2);
    tabChainManager.BeginFrame(stateManager);

    // Window 1 - should be ignored
    tabChainManager.BeginWindow(windowID1);
    tabChainManager.RegisterTabTarget(elementID1);
    tabChainManager.RegisterTabTarget(elementID2);
    tabChainManager.RegisterTabTarget(elementID3);
    tabChainManager.EndWindow();

    // Window 2 - contains focused element
    tabChainManager.BeginWindow(windowID2);
    tabChainManager.RegisterTabTarget(elementID4);
    tabChainManager.EndWindow();

    tabChainManager.EndFrame();

    // Should wrap to itself since only one element in window 2
    EXPECT_EQ(tabChainManager.GetNextTabTarget(), elementID4);
    EXPECT_EQ(tabChainManager.GetPrevTabTarget(), elementID4);
}

TEST_F(TabChainManagerTest, DefaultFocusID_OverridesFirstElement)
{
    // No focus set
    stateManager.Interaction().SetActiveWindowID(windowID1);
    tabChainManager.BeginFrame(stateManager);

    // Set default focus to elementID3
    tabChainManager.SetDefaultFocusID(elementID3);

    tabChainManager.BeginWindow(windowID1);
    tabChainManager.RegisterTabTarget(elementID1);
    tabChainManager.RegisterTabTarget(elementID2);
    tabChainManager.RegisterTabTarget(elementID3);
    tabChainManager.EndWindow();

    tabChainManager.EndFrame();

    // Should default to elementID3 instead of elementID1
    EXPECT_EQ(tabChainManager.GetNextTabTarget(), elementID3);
    EXPECT_EQ(tabChainManager.GetPrevTabTarget(), elementID3);
}

TEST_F(TabChainManagerTest, DefaultFocusID_UsedWhenNoFocus)
{
    stateManager.Interaction().SetActiveWindowID(windowID1);
    tabChainManager.BeginFrame(stateManager);

    tabChainManager.SetDefaultFocusID(elementID2);

    tabChainManager.BeginWindow(windowID1);
    tabChainManager.RegisterTabTarget(elementID1);
    tabChainManager.RegisterTabTarget(elementID2);
    tabChainManager.RegisterTabTarget(elementID3);
    tabChainManager.EndWindow();

    tabChainManager.EndFrame();

    EXPECT_EQ(tabChainManager.GetNextTabTarget(), elementID2);
}

TEST_F(TabChainManagerTest, GetDefaultFocus_ReturnsCorrectValue)
{
    stateManager.Interaction().SetActiveWindowID(windowID1);
    tabChainManager.BeginFrame(stateManager);

    tabChainManager.SetDefaultFocusID(elementID3);

    tabChainManager.BeginWindow(windowID1);
    tabChainManager.RegisterTabTarget(elementID1);
    tabChainManager.RegisterTabTarget(elementID2);
    tabChainManager.RegisterTabTarget(elementID3);
    tabChainManager.EndWindow();

    tabChainManager.EndFrame();

    EXPECT_EQ(tabChainManager.GetDefaultFocus(), elementID3);
}

TEST_F(TabChainManagerTest, GetDefaultFocus_FallsBackToFirst)
{
    stateManager.Interaction().SetActiveWindowID(windowID1);
    tabChainManager.BeginFrame(stateManager);

    // No default set

    tabChainManager.BeginWindow(windowID1);
    tabChainManager.RegisterTabTarget(elementID1);
    tabChainManager.RegisterTabTarget(elementID2);
    tabChainManager.EndWindow();

    tabChainManager.EndFrame();

    EXPECT_EQ(tabChainManager.GetDefaultFocus(), elementID1);
}

// Focus outside the chain: Tab enters it at the first element, Shift+Tab at the last.
TEST_F(TabChainManagerTest, FocusOutsideTheChain_EntersItAtEitherEnd)
{
    ID orphanElementID = idManager.AllocateDynamicID();
    stateManager.Interaction().SetFocusID(orphanElementID);
    stateManager.Interaction().SetActiveWindowID(windowID1);
    tabChainManager.BeginFrame(stateManager);

    tabChainManager.BeginWindow(windowID1);
    tabChainManager.RegisterTabTarget(elementID1);
    tabChainManager.RegisterTabTarget(elementID2);
    tabChainManager.EndWindow();

    tabChainManager.EndFrame();

    EXPECT_EQ(tabChainManager.GetNextTabTarget(), elementID1);
    EXPECT_EQ(tabChainManager.GetPrevTabTarget(), elementID2);
}

// Without a default focus set, the default is the first element.
TEST_F(TabChainManagerTest, GetDefaultFocus_IsTheFirstElementWithoutADefault)
{
    stateManager.Interaction().SetFocusID(elementID2);
    stateManager.Interaction().SetActiveWindowID(windowID1);
    tabChainManager.BeginFrame(stateManager);

    tabChainManager.BeginWindow(windowID1);
    tabChainManager.RegisterTabTarget(elementID1);
    tabChainManager.RegisterTabTarget(elementID2);
    tabChainManager.RegisterTabTarget(elementID3);
    tabChainManager.EndWindow();

    tabChainManager.EndFrame();

    EXPECT_EQ(tabChainManager.GetDefaultFocus(), elementID1);
}

TEST_F(TabChainManagerTest, MultipleWindows_OnlyActiveWindowBuildsChain)
{
    stateManager.Interaction().SetFocusID(elementID2);
    stateManager.Interaction().SetActiveWindowID(windowID1);
    tabChainManager.BeginFrame(stateManager);

    // Window 1 - active, contains focus
    tabChainManager.BeginWindow(windowID1);
    tabChainManager.RegisterTabTarget(elementID1);
    tabChainManager.RegisterTabTarget(elementID2);
    tabChainManager.EndWindow();

    // Window 2 - not active
    tabChainManager.BeginWindow(windowID2);
    tabChainManager.RegisterTabTarget(elementID3);
    tabChainManager.RegisterTabTarget(elementID4);
    tabChainManager.EndWindow();

    tabChainManager.EndFrame();

    // Should navigate within window 1
    EXPECT_EQ(tabChainManager.GetNextTabTarget(), elementID1); // wraps to first
    EXPECT_EQ(tabChainManager.GetPrevTabTarget(), elementID1);
}

// A popup made inside a window (a field's suggestion list) doesn't end that window's tab
// chain: the targets after it still follow the focused one.
TEST_F(TabChainManagerTest, APopupMadeInsideAWindowKeepsItsChainGoing)
{
    stateManager.Interaction().SetActiveWindowID(windowID1);
    CommitFocused(elementID1);
    tabChainManager.BeginFrame(stateManager);

    tabChainManager.BeginWindow(windowID1);
    tabChainManager.RegisterTabTarget(elementID1);
    tabChainManager.BeginWindow(windowID2);     // the popup, not the active window
    tabChainManager.RegisterTabTarget(elementID3);
    tabChainManager.EndWindow();
    tabChainManager.RegisterTabTarget(elementID2);
    tabChainManager.EndWindow();
    tabChainManager.EndFrame();

    EXPECT_EQ(tabChainManager.GetNextTabTarget(), elementID2);
}
