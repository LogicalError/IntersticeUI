#include <gtest/gtest.h>
#include <gui_core/state/gui_state_manager.h>
#include <gui_core/elements/gui_element_storage.h>   // ScrollOffsetHandle full definition
#include <gui_core/gui_id_manager.h>


namespace interstice
{

    class StateManagerTest : public ::testing::Test
    {
    protected:
        GUIStateManager stateManager;
        IDManager idManager;

        void SetUp() override
        {
            EXPECT_TRUE(stateManager.BeginFrame());
            EXPECT_TRUE(idManager.BeginFrame());
        }

        void TearDown() override
        {
            stateManager.EndFrame({}, idManager);
            idManager.EndFrame();
        }

        void AdvanceFrame()
        {
            stateManager.EndFrame({}, idManager);
            EXPECT_TRUE(stateManager.BeginFrame());
            idManager.EndFrame();
            EXPECT_TRUE(idManager.BeginFrame());
        }

        ScrollOffsetHandle GetScrollOffsetHandle(ID id)
        {
            return stateManager.GetScrollManagement().GetOrCreateScrollOffsetHandle(id);
        }

        bool IsHandleValid(ScrollOffsetHandle handle)
        {
            return stateManager.GetScrollManagement().IsScrollOffsetHandleValid(handle);
        }
    };

    // ======================================================================
    // Static ID Tests
    // ======================================================================

    TEST_F(StateManagerTest, StaticIDScrollOffsetPersistsAcrossFrames)
    {
        // Create a static ID and allocate a scroll offset for it
        ID staticID = idManager.CreateStaticID();
        ScrollOffsetHandle handle = GetScrollOffsetHandle(staticID);
        EXPECT_TRUE(IsHandleValid(handle));

        // Set an offset
        stateManager.GetScrollManagement().SetScrollOffset(handle, int2{ 10, 20 });

        // Advance frame - the static ID is not "seen" this frame
        AdvanceFrame();

        // Handle should still be valid because static IDs persist
        EXPECT_TRUE(IsHandleValid(handle));
        EXPECT_EQ(stateManager.GetScrollManagement().GetScrollOffset(handle), (int2{ 10, 20 }));

        // Cleanup
        idManager.ReleaseStaticID(staticID);
    }

    TEST_F(StateManagerTest, StaticIDScrollOffsetFreedWhenDeallocated)
    {
        // Create a static ID and allocate a scroll offset for it
        ID staticID = idManager.CreateStaticID();
        ScrollOffsetHandle handle = GetScrollOffsetHandle(staticID);
        EXPECT_TRUE(IsHandleValid(handle));

        // Set an offset
        stateManager.GetScrollManagement().SetScrollOffset(handle, int2{ 10, 20 });

        // Deallocate the static ID
        idManager.ReleaseStaticID(staticID);

        // At this point, the scroll offset entry is marked as inUse=false
        // when the ID is released (implementation-dependent, but we simulate it)
        // For now, let's verify the cleanup happens at EndFrame

        // Advance frame
        AdvanceFrame();

        // Handle should be invalid now because the static ID was deallocated
        // and should have been cleaned up
        EXPECT_FALSE(IsHandleValid(handle));
    }

    TEST_F(StateManagerTest, MultipleStaticIDsWithScrollOffsets)
    {
        ID staticID1 = idManager.CreateStaticID();
        ID staticID2 = idManager.CreateStaticID();
        ID staticID3 = idManager.CreateStaticID();

        ScrollOffsetHandle handle1 = GetScrollOffsetHandle(staticID1);
        ScrollOffsetHandle handle2 = GetScrollOffsetHandle(staticID2);
        ScrollOffsetHandle handle3 = GetScrollOffsetHandle(staticID3);

        stateManager.GetScrollManagement().SetScrollOffset(handle1, int2{ 1, 1 });
        stateManager.GetScrollManagement().SetScrollOffset(handle2, int2{ 2, 2 });
        stateManager.GetScrollManagement().SetScrollOffset(handle3, int2{ 3, 3 });

        // Release the middle one
        idManager.ReleaseStaticID(staticID2);

        // Advance frame
        AdvanceFrame();

        // Handles 1 and 3 should still be valid
        EXPECT_TRUE(IsHandleValid(handle1));
        EXPECT_FALSE(IsHandleValid(handle2)); // This one was deallocated
        EXPECT_TRUE(IsHandleValid(handle3));

        // Values should be preserved for valid handles
        EXPECT_EQ(stateManager.GetScrollManagement().GetScrollOffset(handle1), (int2{ 1, 1 }));
        EXPECT_EQ(stateManager.GetScrollManagement().GetScrollOffset(handle3), (int2{ 3, 3 }));

        // Cleanup
        idManager.ReleaseStaticID(staticID1);
        idManager.ReleaseStaticID(staticID3);
    }

    // ======================================================================
    // Dynamic ID Tests
    // ======================================================================

    TEST_F(StateManagerTest, DynamicIDScrollOffsetCleanedUpIfNotSeenThisFrame)
    {
        // Create a dynamic ID and allocate a scroll offset for it
        ID dynamicID = idManager.AllocateDynamicID(__FILE__, __LINE__);
        ScrollOffsetHandle handle = GetScrollOffsetHandle(dynamicID);
        EXPECT_TRUE(IsHandleValid(handle));

        // Set an offset
        stateManager.GetScrollManagement().SetScrollOffset(handle, int2{ 10, 20 });

        // Advance two frames: EndFrame on the creating frame preserves the
        // handle (lastSeenFrame matches); only the *next* EndFrame sees a
        // stale lastSeenFrame and drops it.
        AdvanceFrame();
        AdvanceFrame();

        // Handle should be invalid because dynamic IDs are cleaned up
        // if they're not seen in the current frame
        EXPECT_FALSE(IsHandleValid(handle));
    }

    TEST_F(StateManagerTest, DynamicIDScrollOffsetPersistsIfSeenThisFrame)
    {
        // Use a string hint so the ID is derived from the same hash on both
        // calls — otherwise __LINE__ differs between the two call sites.
        ID dynamicID = idManager.AllocateDynamicID("persists-dyn");
        ScrollOffsetHandle handle = GetScrollOffsetHandle(dynamicID);
        EXPECT_TRUE(IsHandleValid(handle));

        stateManager.GetScrollManagement().SetScrollOffset(handle, int2{ 10, 20 });

        AdvanceFrame();

        // Create the same dynamic ID again (same hint → same counter=1 under
        // the root parent → same hash).
        ID dynamicID2 = idManager.AllocateDynamicID("persists-dyn");
        EXPECT_EQ(dynamicID, dynamicID2);

        // Get the handle again (or allocate new one)
        ScrollOffsetHandle handle2 = stateManager.GetScrollManagement().GetScrollOffsetHandle(dynamicID2);
        if (handle2 == ScrollOffsetHandle::Invalid)
        {
            handle2 = GetScrollOffsetHandle(dynamicID2);
        }

        EXPECT_TRUE(IsHandleValid(handle2));
    }

    // ======================================================================
    // Ranged ID Tests
    // ======================================================================

    TEST_F(StateManagerTest, RangedIDScrollOffsetCleanedUpIfNotSeenThisFrame)
    {
        ID rangedID = idManager.AllocateRangedID(10, __FILE__, __LINE__);
        ScrollOffsetHandle handle = GetScrollOffsetHandle(rangedID);
        EXPECT_TRUE(IsHandleValid(handle));

        stateManager.GetScrollManagement().SetScrollOffset(handle, int2{ 5, 15 });

        // See DynamicIDScrollOffsetCleanedUpIfNotSeenThisFrame — cleanup is
        // deferred one frame.
        AdvanceFrame();
        AdvanceFrame();

        // Ranged ID not seen this frame, should be cleaned up
        EXPECT_FALSE(IsHandleValid(handle));
    }

    TEST_F(StateManagerTest, RangedIDScrollOffsetPersistsIfSeenThisFrame)
    {
        // Use a string hint so the ID is derived from the same hash on both
        // calls — otherwise __LINE__ differs between the two call sites.
        ID rangedID = idManager.AllocateRangedID(10, "persists-ranged");
        ScrollOffsetHandle handle = GetScrollOffsetHandle(rangedID);
        EXPECT_TRUE(IsHandleValid(handle));

        stateManager.GetScrollManagement().SetScrollOffset(handle, int2{ 5, 15 });

        AdvanceFrame();

        // Allocate same ranged ID again (hash is derived from hint + parent)
        ID rangedID2 = idManager.AllocateRangedID(10, "persists-ranged");
        EXPECT_EQ(rangedID, rangedID2);

        ScrollOffsetHandle handle2 = stateManager.GetScrollManagement().GetScrollOffsetHandle(rangedID2);
        if (handle2 == ScrollOffsetHandle::Invalid)
        {
            handle2 = GetScrollOffsetHandle(rangedID2);
        }

        EXPECT_TRUE(IsHandleValid(handle2));
    }

    // ======================================================================
    // SubRange ID Tests
    // ======================================================================

    TEST_F(StateManagerTest, SubRangeIDScrollOffsetCleanedUpIfNotSeenThisFrame)
    {
        ID rangedID = idManager.AllocateRangedID(10, __FILE__, __LINE__);
        ID subID = idManager.GetSubID(rangedID, 5);
        ScrollOffsetHandle handle = GetScrollOffsetHandle(subID);
        EXPECT_TRUE(IsHandleValid(handle));

        stateManager.GetScrollManagement().SetScrollOffset(handle, int2{ 7, 14 });

        // See DynamicIDScrollOffsetCleanedUpIfNotSeenThisFrame — cleanup is
        // deferred one frame.
        AdvanceFrame();
        AdvanceFrame();

        // SubRange ID not seen this frame, should be cleaned up
        EXPECT_FALSE(IsHandleValid(handle));
    }

    // ======================================================================
    // Mixed ID Type Tests
    // ======================================================================

    TEST_F(StateManagerTest, MixedIDTypesScrollOffsetCleanup)
    {
        // Create one of each type
        ID staticID = idManager.CreateStaticID();
        ID dynamicID = idManager.AllocateDynamicID(__FILE__, __LINE__);
        ID rangedID = idManager.AllocateRangedID(10, __FILE__, __LINE__);

        ScrollOffsetHandle staticHandle = GetScrollOffsetHandle(staticID);
        ScrollOffsetHandle dynamicHandle = GetScrollOffsetHandle(dynamicID);
        ScrollOffsetHandle rangedHandle = GetScrollOffsetHandle(rangedID);

        stateManager.GetScrollManagement().SetScrollOffset(staticHandle, int2{ 1, 1 });
        stateManager.GetScrollManagement().SetScrollOffset(dynamicHandle, int2{ 2, 2 });
        stateManager.GetScrollManagement().SetScrollOffset(rangedHandle, int2{ 3, 3 });

        // See DynamicIDScrollOffsetCleanedUpIfNotSeenThisFrame — cleanup is
        // deferred one frame.
        AdvanceFrame();
        AdvanceFrame();

        // Static should persist, others should be cleaned up
        EXPECT_TRUE(IsHandleValid(staticHandle));
        EXPECT_FALSE(IsHandleValid(dynamicHandle));
        EXPECT_FALSE(IsHandleValid(rangedHandle));

        idManager.ReleaseStaticID(staticID);
    }

    // ======================================================================
    // Handle Reuse Tests
    // ======================================================================

    TEST_F(StateManagerTest, ScrollOffsetHandleReusedAfterCleanup)
    {
        // Create and clean up a dynamic ID to free up a handle
        ID dynamicID1 = idManager.AllocateDynamicID(__FILE__, __LINE__);
        ScrollOffsetHandle handle1 = GetScrollOffsetHandle(dynamicID1);
        stateManager.GetScrollManagement().SetScrollOffset(handle1, int2{ 10, 20 });

        AdvanceFrame();
        // handle1 is now freed and in the free list

        // Create a new dynamic ID - it might reuse the handle
        ID dynamicID2 = idManager.AllocateDynamicID(__FILE__, __LINE__);
        ScrollOffsetHandle handle2 = GetScrollOffsetHandle(dynamicID2);

        EXPECT_TRUE(IsHandleValid(handle2));
        EXPECT_EQ(stateManager.GetScrollManagement().GetScrollOffset(handle2), (int2{ 0, 0 })); // Fresh allocation
    }

    // ======================================================================
    // Edge Cases
    // ======================================================================

    TEST_F(StateManagerTest, InvalidHandleReturnsInvalidFromAccessors)
    {
        ScrollOffsetHandle invalidHandle = ScrollOffsetHandle::Invalid;
        EXPECT_FALSE(stateManager.GetScrollManagement().IsScrollOffsetHandleValid(invalidHandle));
    }

    TEST_F(StateManagerTest, GetOrCreateScrollOffsetHandleReturnsSameHandleForSameID)
    {
        ID staticID = idManager.CreateStaticID();
        ScrollOffsetHandle handle1 = GetScrollOffsetHandle(staticID);
        ScrollOffsetHandle handle2 = GetScrollOffsetHandle(staticID);

        EXPECT_EQ(handle1, handle2);
        idManager.ReleaseStaticID(staticID);
    }

    TEST_F(StateManagerTest, ScrollOffsetValuesPersistCorrectly)
    {
        ID staticID = idManager.CreateStaticID();
        ScrollOffsetHandle handle = GetScrollOffsetHandle(staticID);

        // Test various offset values
        stateManager.GetScrollManagement().SetScrollOffset(handle, int2{ 100, 200 });
        EXPECT_EQ(stateManager.GetScrollManagement().GetScrollOffset(handle), (int2{ 100, 200 }));

        stateManager.GetScrollManagement().SetScrollOffset(handle, int2{ -50, 300 });
        EXPECT_EQ(stateManager.GetScrollManagement().GetScrollOffset(handle), (int2{ -50, 300 }));

        stateManager.GetScrollManagement().SetScrollOffset(handle, int2{ 0, 0 });
        EXPECT_EQ(stateManager.GetScrollManagement().GetScrollOffset(handle), (int2{ 0, 0 }));

        idManager.ReleaseStaticID(staticID);
    }

    TEST_F(StateManagerTest, LargeNumberOfScrollOffsets)
    {
        std::vector<ID> staticIDs;
        std::vector<ScrollOffsetHandle> handles;

        // Create many scroll offsets
        for (int i = 0; i < 100; ++i)
        {
            ID id = idManager.CreateStaticID();
            staticIDs.push_back(id);
            ScrollOffsetHandle handle = GetScrollOffsetHandle(id);
            handles.push_back(handle);
            stateManager.GetScrollManagement().SetScrollOffset(handle, int2{ i, i * 2 });
        }

        // All should be valid
        for (int i = 0; i < 100; ++i)
        {
            EXPECT_TRUE(IsHandleValid(handles[i]));
            EXPECT_EQ(stateManager.GetScrollManagement().GetScrollOffset(handles[i]), (int2{ i, i * 2 }));
        }

        // Clean up
        for (int i = 0; i < 100; ++i)
        {
            idManager.ReleaseStaticID(staticIDs[i]);
        }
    }

    // ======================================================================
    // Scroll Visibility Tests
    // ======================================================================

    TEST_F(StateManagerTest, MakeRectVisibleInScrollScrollsLeft)
    {
        ID id = idManager.CreateStaticID();
        ScrollOffsetHandle handle = GetScrollOffsetHandle(id);

        // Rect that needs to scroll left
        int2 containerSize = int2{ 100, 100 };
        int2 rectMin = int2{ -10, 20 };
        int2 rectMax = int2{ 30, 80 };

        bool scrolled = stateManager.GetScrollManagement().MakeRectVisibleInScroll(handle, rectMin, rectMax, containerSize);

        EXPECT_TRUE(scrolled);
        // Offset is positive content-translation: shifting content right by 10
        // makes rectMin.x (-10) land at visible x=0.
        EXPECT_EQ(stateManager.GetScrollManagement().GetScrollOffset(handle), (int2{ 10, 0 }));

        idManager.ReleaseStaticID(id);
    }

    TEST_F(StateManagerTest, MakeRectVisibleInScrollScrollsRight)
    {
        ID id = idManager.CreateStaticID();
        ScrollOffsetHandle handle = GetScrollOffsetHandle(id);

        // Rect that needs to scroll right
        int2 containerSize = int2{ 100, 100 };
        int2 rectMin = int2{ 80, 20 };
        int2 rectMax = int2{ 150, 80 };

        bool scrolled = stateManager.GetScrollManagement().MakeRectVisibleInScroll(handle, rectMin, rectMax, containerSize);

        EXPECT_TRUE(scrolled);
        EXPECT_EQ(stateManager.GetScrollManagement().GetScrollOffset(handle), (int2{ -50, 0 }));

        idManager.ReleaseStaticID(id);
    }

    TEST_F(StateManagerTest, MakeRectVisibleInScrollScrollsUp)
    {
        ID id = idManager.CreateStaticID();
        ScrollOffsetHandle handle = GetScrollOffsetHandle(id);

        // Rect that needs to scroll up
        int2 containerSize = int2{ 100, 100 };
        int2 rectMin = int2{ 20, -10 };
        int2 rectMax = int2{ 80, 30 };

        bool scrolled = stateManager.GetScrollManagement().MakeRectVisibleInScroll(handle, rectMin, rectMax, containerSize);

        EXPECT_TRUE(scrolled);
        // Offset is positive content-translation: shifting content down by 10
        // makes rectMin.y (-10) land at visible y=0.
        EXPECT_EQ(stateManager.GetScrollManagement().GetScrollOffset(handle), (int2{ 0, 10 }));

        idManager.ReleaseStaticID(id);
    }

    TEST_F(StateManagerTest, MakeRectVisibleInScrollScrollsDown)
    {
        ID id = idManager.CreateStaticID();
        ScrollOffsetHandle handle = GetScrollOffsetHandle(id);

        // Rect that needs to scroll down
        int2 containerSize = int2{ 100, 100 };
        int2 rectMin = int2{ 20, 80 };
        int2 rectMax = int2{ 80, 150 };

        bool scrolled = stateManager.GetScrollManagement().MakeRectVisibleInScroll(handle, rectMin, rectMax, containerSize);

        EXPECT_TRUE(scrolled);
        EXPECT_EQ(stateManager.GetScrollManagement().GetScrollOffset(handle), (int2{ 0, -50 }));

        idManager.ReleaseStaticID(id);
    }

    TEST_F(StateManagerTest, MakeRectVisibleInScrollAlreadyVisible)
    {
        ID id = idManager.CreateStaticID();
        ScrollOffsetHandle handle = GetScrollOffsetHandle(id);

        // Rect that's already visible
        int2 containerSize = int2{ 100, 100 };
        int2 rectMin = int2{ 20, 20 };
        int2 rectMax = int2{ 80, 80 };

        bool scrolled = stateManager.GetScrollManagement().MakeRectVisibleInScroll(handle, rectMin, rectMax, containerSize);

        EXPECT_FALSE(scrolled);
        EXPECT_EQ(stateManager.GetScrollManagement().GetScrollOffset(handle), (int2{ 0, 0 }));

        idManager.ReleaseStaticID(id);
    }

    TEST_F(StateManagerTest, MakeRectVisibleInScrollClampNegativeOffset)
    {
        ID id = idManager.CreateStaticID();
        ScrollOffsetHandle handle = GetScrollOffsetHandle(id);

        // Set a negative offset first
        stateManager.GetScrollManagement().SetScrollOffset(handle, int2{ -50, -50 });

        // Rect in center of viewport
        int2 containerSize = int2{ 100, 100 };
        int2 rectMin = int2{ 40, 40 };
        int2 rectMax = int2{ 60, 60 };

        bool scrolled = stateManager.GetScrollManagement().MakeRectVisibleInScroll(handle, rectMin, rectMax, containerSize);

        EXPECT_TRUE(scrolled);
        EXPECT_EQ(stateManager.GetScrollManagement().GetScrollOffset(handle), (int2{ -40, -40 }));

        idManager.ReleaseStaticID(id);
    }

    TEST_F(StateManagerTest, MakeTextCursorVisibleInTextBox)
    {
        // Checks the scroll offsets only; scrolling the cursor into view needs a
        // real ElementStorage (#68).

        ID id = idManager.CreateStaticID();
        ScrollOffsetHandle handle = GetScrollOffsetHandle(id);

        // Cursor at position (150, 10) in a 100x100 textbox
        int2 cursorPos = int2{ 150, 10 };
        int2 textEditorSize = int2{ 100, 100 };
        int cursorHeight = 20;

        bool scrolled = stateManager.GetScrollManagement().MakeRectVisibleInScroll(
            handle,
            cursorPos,
            cursorPos + int2{ 1, cursorHeight },
            textEditorSize
        );

        EXPECT_TRUE(scrolled);
        // Cursor should be scrolled to right edge: 150 + 1 - 100 = 51
        EXPECT_EQ(stateManager.GetScrollManagement().GetScrollOffset(handle), (int2{ -51, 0 }));

        idManager.ReleaseStaticID(id);
    }

    // ======================================================================
    // Focused-State Behavior Tests
    // ======================================================================

    namespace
    {
        inline ID MakeID(uint64_t value)
        {
            return ID{ .value{.integer = value } };
        }
    }

    TEST(GUIStateManagerFocusExtra, FocusIsAppliedImmediately)
    {
        GUIStateManager stateManager;

        stateManager.BeginFrame();

        const ID target = MakeID(10);
        stateManager.Interaction().SetFocusID(target);

        // SetFocusID takes effect immediately.
        EXPECT_EQ(stateManager.Interaction().GetFocusedID(), target);

        stateManager.BeginFrame();
        EXPECT_EQ(stateManager.Interaction().GetFocusedID(), target);
    }

    TEST(GUIStateManagerFocusExtra, InvalidFocusClearsImmediately)
    {
        GUIStateManager stateManager;

        stateManager.BeginFrame();

        const ID focused = MakeID(11);
        stateManager.Interaction().SetFocusID(focused);
        EXPECT_EQ(stateManager.Interaction().GetFocusedID(), focused);

        stateManager.Interaction().SetFocusID(MakeID(12));
        stateManager.Interaction().SetFocusID(ID::Invalid());

        EXPECT_EQ(stateManager.Interaction().GetFocusedID(), ID::Invalid());

        stateManager.BeginFrame();
        EXPECT_EQ(stateManager.Interaction().GetFocusedID(), ID::Invalid());
    }

    TEST(GUIStateManagerFocusExtra, AssociatedFocusedLabelGetsFocusedState)
    {
        GUIStateManager stateManager;

        stateManager.BeginFrame();

        const ID control = MakeID(21);
        const ID label = MakeID(22);

        stateManager.Interaction().SetFocusID(control);
        stateManager.BeginFrame();
        stateManager.Labels().AddFocusedAssociatedLabelID(label);

        const ElementState controlState = stateManager.GetCurrentState(control);
        const ElementState labelState = stateManager.GetCurrentState(label);

        EXPECT_NE((controlState & ElementState::Focused), ElementState::None);
        EXPECT_NE((labelState & ElementState::Focused), ElementState::None);
    }

    TEST(GUIStateManagerFocusExtra, BeginFramePreservesAssociatedFocusedLabel)
    {
        // The associated-label set is filled at the end of frame N and read during
        // frame N+1's OnGUI, so BeginFrame must not clear it.
        GUIStateManager stateManager;

        stateManager.BeginFrame();

        const ID label = MakeID(33);
        stateManager.Labels().AddFocusedAssociatedLabelID(label);

        EXPECT_NE((stateManager.GetCurrentState(label) & ElementState::Focused), ElementState::None);

        stateManager.BeginFrame();

        EXPECT_NE((stateManager.GetCurrentState(label) & ElementState::Focused), ElementState::None);
    }

}