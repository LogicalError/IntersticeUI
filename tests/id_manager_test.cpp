#include "gtest/gtest.h"
#include <gui_core/gui_id.h>
#include <gui_core/gui_id_manager.h>

namespace tachyon
{

    static void VERIFY([[maybe_unused]] bool result)
    {
        assert(result);
    }


    // Test-only access to IDManager's private hooks.
    struct IDManagerTestAccess
    {
        static bool SetGenerationForSlot(IDManager& manager, uint32_t index, int32_t generation)
        {
            return manager.SetGenerationForSlot(index, generation);
        }
    };

    class IDManagerTest : public ::testing::Test {
    protected:
        void SetUp() override {
            VERIFY(manager.BeginFrame());
        }

        void TearDown() override {
            manager.EndFrame();
        }

        IDManager manager;
    };

    // ---------------------
    // Static ID Tests
    // ---------------------

    TEST_F(IDManagerTest, StaticIDCreationAndValidation) {
        ID id = manager.CreateStaticID();
        EXPECT_EQ(id.GetMode(), IDMode::Static);
        EXPECT_TRUE(manager.IsStaticIDValid(id));

        // After releasing, it should be invalid
        EXPECT_TRUE(manager.ReleaseStaticID(id));
        EXPECT_FALSE(manager.IsStaticIDValid(id));
    }

    TEST_F(IDManagerTest, StaticIDGenerationWraparound) {
        ID id = manager.CreateStaticID();
        EXPECT_NE(id, ID::Invalid());
        EXPECT_EQ(id.value.staticParts.generation, 1);
        EXPECT_TRUE(manager.ReleaseStaticID(id));

        ID id2 = manager.CreateStaticID();
        EXPECT_EQ(id.value.staticParts.slotIndex, id2.value.staticParts.slotIndex);
        EXPECT_EQ(id.value.staticParts.mode, id2.value.staticParts.mode);
        EXPECT_NE(id.value.staticParts.generation, id2.value.staticParts.generation);
        EXPECT_TRUE(manager.ReleaseStaticID(id2));

        uint32_t index = id.value.staticParts.slotIndex;
        EXPECT_TRUE(IDManagerTestAccess::SetGenerationForSlot(manager, index, -static_cast<int32_t>(ID::MAX_STATIC_GEN_MASK)));

        ID id3 = manager.CreateStaticID();
        EXPECT_EQ(id3.value.staticParts.slotIndex, id2.value.staticParts.slotIndex);
        EXPECT_EQ(id3.value.staticParts.mode, id2.value.staticParts.mode);
        EXPECT_EQ(id3.value.staticParts.generation, ID::MAX_STATIC_GEN_MASK);
        EXPECT_TRUE(manager.ReleaseStaticID(id3));

        ID id4 = manager.CreateStaticID();
        EXPECT_EQ(id4.value.staticParts.slotIndex, id2.value.staticParts.slotIndex);
        EXPECT_EQ(id4.value.staticParts.mode, id2.value.staticParts.mode);
        EXPECT_EQ(id4.value.staticParts.generation, 1);
    }


    TEST_F(IDManagerTest, StaticIDUnique) {
        ID id1 = manager.CreateStaticID();
        ID id2 = manager.CreateStaticID();
        EXPECT_NE(id1, ID::Invalid());
        EXPECT_NE(id2, ID::Invalid());
        EXPECT_NE(id1, id2);
    }

    TEST_F(IDManagerTest, StaticIDGenerationMismatch) {
        ID id = manager.CreateStaticID();
        EXPECT_NE(id, ID::Invalid());
        EXPECT_TRUE(manager.IsStaticIDValid(id));
        // Release first, then manually corrupt generation
        EXPECT_TRUE(manager.ReleaseStaticID(id));
        EXPECT_FALSE(manager.IsStaticIDValid(id));
    }

    TEST_F(IDManagerTest, IsStaticIDValidOnlyWorksWithStaticID) {
        EXPECT_FALSE(manager.IsStaticIDValid(ID::Invalid()));
        EXPECT_FALSE(manager.IsStaticIDValid(manager.AllocateDynamicID(__FILE__, __LINE__)));
        EXPECT_FALSE(manager.IsStaticIDValid(manager.AllocateRangedID(100, __FILE__, __LINE__)));
        EXPECT_FALSE(manager.IsStaticIDValid(manager.GetSubID(manager.AllocateRangedID(100, __FILE__, __LINE__), 0)));
        EXPECT_TRUE(manager.IsStaticIDValid(manager.CreateStaticID()));
    }

    TEST_F(IDManagerTest, StaticIDReuse) {
        ID id1 = manager.CreateStaticID();
        uint32_t index = id1.value.staticParts.slotIndex;
        EXPECT_TRUE(manager.ReleaseStaticID(id1));

        ID id2 = manager.CreateStaticID();
        EXPECT_EQ(id2.value.staticParts.slotIndex, index); // same slot reused
        EXPECT_NE(id2.value.staticParts.generation, id1.value.staticParts.generation); // generation incremented
    }

    // ---------------------
    // Dynamic ID Tests
    // ---------------------

    TEST_F(IDManagerTest, DynamicIDCreationAndValidation) {
        ID id1 = manager.AllocateDynamicID("button1");
        ID id2 = manager.AllocateDynamicID("button2");
        EXPECT_EQ(id1.GetMode(), IDMode::Dynamic);
        EXPECT_EQ(id2.GetMode(), IDMode::Dynamic);
        EXPECT_NE(id1, ID::Invalid());
        EXPECT_NE(id2, ID::Invalid());
        EXPECT_NE(id1, id2);

        EXPECT_TRUE(manager.IsDynamicIDValid(id1));
        EXPECT_TRUE(manager.IsDynamicIDValid(id2));
    }

    TEST_F(IDManagerTest, DynamicIDSequenceUniqueness) {
        ID id1 = manager.AllocateDynamicID("button");
        ID id2 = manager.AllocateDynamicID("button");
        EXPECT_NE(id1, id2);
    }

#ifdef TACHYON_SUPPORT_CPP20
    TEST_F(IDManagerTest, DynamicIDWithSourceLocation) {
        ID id = manager.AllocateDynamicID();
        EXPECT_EQ(id.GetMode(), IDMode::Dynamic);
        EXPECT_TRUE(manager.IsDynamicIDValid(id));
    }
#endif

    TEST_F(IDManagerTest, DynamicIDWithFileAndLine) {
        ID id = manager.AllocateDynamicID(__FILE__, __LINE__);
        EXPECT_EQ(id.GetMode(), IDMode::Dynamic);
        EXPECT_TRUE(manager.IsDynamicIDValid(id));
    }

    TEST_F(IDManagerTest, DeepHierarchyUniqueness) {
        ID p1 = manager.AllocateDynamicID("level1");
        VERIFY(manager.PushParent(p1));
        ID p2 = manager.AllocateDynamicID("level2");
        VERIFY(manager.PushParent(p2));

        ID child = manager.AllocateDynamicID("leaf");
        EXPECT_TRUE(manager.IsIDValid(child));
        EXPECT_TRUE(manager.IsIDValid(p1));
        EXPECT_TRUE(manager.IsIDValid(p2));
        EXPECT_NE(child, p1);
        EXPECT_NE(child, p2);
        EXPECT_TRUE(manager.IsInParentStack(p1));
        EXPECT_TRUE(manager.IsInParentStack(p2));

        VERIFY(manager.PopParent());
        EXPECT_TRUE(manager.IsInParentStack(p1));
        EXPECT_FALSE(manager.IsInParentStack(p2));

        VERIFY(manager.PopParent());
        EXPECT_FALSE(manager.IsInParentStack(p1));
        EXPECT_FALSE(manager.IsInParentStack(p2));
    }

    TEST_F(IDManagerTest, IsDynamicIDValidOnlyWorksWithDynamicID) {
        EXPECT_FALSE(manager.IsDynamicIDValid(ID::Invalid()));
        EXPECT_FALSE(manager.IsDynamicIDValid(manager.CreateStaticID()));
        EXPECT_FALSE(manager.IsDynamicIDValid(manager.AllocateRangedID(100, __FILE__, __LINE__)));
        EXPECT_FALSE(manager.IsDynamicIDValid(manager.GetSubID(manager.AllocateRangedID(100, __FILE__, __LINE__), 0)));
        EXPECT_TRUE(manager.IsDynamicIDValid(manager.AllocateDynamicID(__FILE__, __LINE__)));
    }

    // ---------------------
    // Ranged ID Tests
    // ---------------------

    TEST_F(IDManagerTest, RangedIDCreationAndValidation) {
        uint32_t size = 5;
        ID range_id = manager.AllocateRangedID(size, "menu_items");
        EXPECT_EQ(range_id.GetMode(), IDMode::Ranged);
        EXPECT_EQ(manager.GetRangeSize(range_id), size);
        EXPECT_TRUE(manager.IsRangedIDValid(range_id));

        // Create sub-IDs
        for (uint32_t i = 0; i < size; ++i)
        {
            ID sub = manager.GetSubID(range_id, i);
            EXPECT_EQ(sub.GetMode(), IDMode::SubRange);
            EXPECT_TRUE(manager.IsSubRangeIDValid(sub));
        }
    }

    TEST_F(IDManagerTest, IsRangedIDValidOnlyWorksWithRangedID) {
        EXPECT_FALSE(manager.IsRangedIDValid(ID::Invalid()));
        EXPECT_FALSE(manager.IsRangedIDValid(manager.CreateStaticID()));
        EXPECT_FALSE(manager.IsRangedIDValid(manager.AllocateDynamicID(__FILE__, __LINE__)));
        EXPECT_FALSE(manager.IsRangedIDValid(manager.GetSubID(manager.AllocateRangedID(100, __FILE__, __LINE__), 0)));
        EXPECT_TRUE(manager.IsRangedIDValid(manager.AllocateRangedID(100, __FILE__, __LINE__)));
    }

    // ---------------------
    // Sub Range ID Tests
    // ---------------------

    TEST_F(IDManagerTest, SubIDWithSameIndexAreIdentical) {
        ID range = manager.AllocateRangedID(10, "range");

        ID sub1 = manager.GetSubID(range, 5);
        ID sub2 = manager.GetSubID(range, 5);

        EXPECT_EQ(sub1, sub2);
    }


    TEST_F(IDManagerTest, SubIDIsolationBetweenRanges) {
        ID range1 = manager.AllocateRangedID(10, "list_a");
        ID range2 = manager.AllocateRangedID(10, "list_b");

        ID sub1 = manager.GetSubID(range1, 5);
        ID sub2 = manager.GetSubID(range2, 5);

        EXPECT_NE(sub1, sub2);
    }

    TEST_F(IDManagerTest, SubIDOutOfRange) {
        uint32_t size = 3;
        ID range_id = manager.AllocateRangedID(size, "options");

        // In release builds, asserts are disabled, so this won't crash
        EXPECT_EQ(manager.GetSubID(range_id, size), ID::Invalid());
        EXPECT_EQ(manager.GetSubID(range_id, static_cast<uint32_t>(-1)), ID::Invalid());
    }

    TEST_F(IDManagerTest, IsSubRangedIDValidOnlyWorksWithSubRangedID) {
        EXPECT_FALSE(manager.IsSubRangeIDValid(ID::Invalid()));
        EXPECT_FALSE(manager.IsSubRangeIDValid(manager.CreateStaticID()));
        EXPECT_FALSE(manager.IsSubRangeIDValid(manager.AllocateDynamicID(__FILE__, __LINE__)));
        EXPECT_FALSE(manager.IsSubRangeIDValid(manager.AllocateRangedID(100, __FILE__, __LINE__)));
        EXPECT_TRUE(manager.IsSubRangeIDValid(manager.GetSubID(manager.AllocateRangedID(100, __FILE__, __LINE__), 0)));
    }

    // ---------------------
    // Parent Stack Tests
    // ---------------------

    TEST_F(IDManagerTest, ParentStackAffectsDynamicID) {
        ID parent = manager.AllocateDynamicID("parent");
        VERIFY(manager.PushParent(parent));

        ID child1 = manager.AllocateDynamicID("child");
        ID child2 = manager.AllocateDynamicID("child");

        // Children with same hint but different parent should differ
        EXPECT_NE(child1, child2);

        VERIFY(manager.PopParent());
    }

    TEST_F(IDManagerTest, ParentStackUnderflowFails) {
        EXPECT_EQ(manager.PopParent(), false);
    }

    TEST_F(IDManagerTest, IsInParentStack) {
        EXPECT_FALSE(manager.IsInParentStack(tachyon::ID::Invalid()));

        // Create a static ID and push it as parent
        tachyon::ID staticID = manager.CreateStaticID();
        EXPECT_FALSE(manager.IsInParentStack(staticID));

        EXPECT_TRUE(manager.PushParent(staticID));
        EXPECT_TRUE(manager.IsInParentStack(staticID));

        // Pop the parent and check
        EXPECT_TRUE(manager.PopParent());
        EXPECT_FALSE(manager.IsInParentStack(staticID));
    }


    // ---------------------
    // Frame Management Tests
    // ---------------------

    TEST_F(IDManagerTest, StaticIDCrossFramePersistence) {
        // 1. Create a static ID in the first frame
        ID static_id = manager.CreateStaticID();

        EXPECT_TRUE(manager.IsStaticIDValid(static_id));

        // 2. Simulate frame transition
        manager.EndFrame();
        VERIFY(manager.BeginFrame());

        // 3. Verify the ID from the previous frame is still valid
        EXPECT_TRUE(manager.IsStaticIDValid(static_id));
        EXPECT_EQ(manager.GetStaticIDCount(), 1);

        // 4. Create a NEW static ID and ensure it doesn't collide
        ID second_static = manager.CreateStaticID();
        EXPECT_NE(static_id, second_static);
        EXPECT_TRUE(manager.IsStaticIDValid(second_static));

        // 5. Release the original ID in the new frame
        EXPECT_TRUE(manager.ReleaseStaticID(static_id));
        EXPECT_FALSE(manager.IsStaticIDValid(static_id));
    }

    TEST_F(IDManagerTest, FrameStateConstraints) {
        tachyon::IDManager manualManager;

        // --- PHASE 1: OUTSIDE FRAME ---
        EXPECT_FALSE(manualManager.IsInsideFrame());

        // Static IDs can be managed anytime (global registry).
        ID staticId = manualManager.CreateStaticID();
        EXPECT_TRUE(manualManager.IsStaticIDValid(staticId));
        EXPECT_TRUE(manualManager.ReleaseStaticID(staticId));

        // Outside a frame, pushing a parent and allocating dynamic / ranged IDs fail (reported).
        EXPECT_FALSE(manualManager.PushParent(staticId));
        EXPECT_EQ(manualManager.AllocateDynamicID("fail"), ID::Invalid());
        EXPECT_EQ(manualManager.AllocateRangedID(10, "fail"), ID::Invalid());


        // --- PHASE 2: INSIDE FRAME ---
        VERIFY(manualManager.BeginFrame());

        ID dynId = manualManager.AllocateDynamicID("valid");
        EXPECT_TRUE(manualManager.IsDynamicIDValid(dynId));

        manualManager.EndFrame();


        // --- PHASE 3: AFTER FRAME ---

        // EndFrame clears the stack, so allocating fails again.
        EXPECT_EQ(manualManager.AllocateDynamicID("fail_again"), ID::Invalid());

        // ... and the next frame works.
        VERIFY(manualManager.BeginFrame());
        EXPECT_TRUE(manualManager.IsDynamicIDValid(manualManager.AllocateDynamicID("next")));
        manualManager.EndFrame();
    }

    TEST_F(IDManagerTest, FrameDetection)
    {
        IDManager manualManager;

        EXPECT_FALSE(manualManager.IsInsideFrame());

        // Begin a frame
        VERIFY(manualManager.BeginFrame());

        EXPECT_TRUE(manualManager.IsInsideFrame());

        ID dummy_parent = manualManager.AllocateDynamicID("parent");
        VERIFY(manualManager.PushParent(dummy_parent));
        EXPECT_TRUE(manualManager.IsInsideFrame());

        // Still inside after popping (the root stays on the stack).
        VERIFY(manualManager.PopParent());
        EXPECT_TRUE(manualManager.IsInsideFrame());

        manualManager.EndFrame();

        EXPECT_FALSE(manualManager.IsInsideFrame());
    }

    TEST_F(IDManagerTest, AParentLeftPushedAtEndFrameIsCleared)
    {
        IDManager manualManager;
        VERIFY(manualManager.BeginFrame());
        const ID top = manualManager.AllocateDynamicID("top");
        VERIFY(manualManager.PushParent(manualManager.AllocateDynamicID("parent")));   // never popped
        manualManager.EndFrame();

        VERIFY(manualManager.BeginFrame());
        EXPECT_EQ(manualManager.GetStackParent(), ID::Invalid()) << "the next frame starts at the root";
        EXPECT_EQ(manualManager.AllocateDynamicID("top"), top) << "and gives the same IDs";
        manualManager.EndFrame();
    }

    TEST_F(IDManagerTest, FrameInFrame)
    {
        IDManager manualManager;

        // Begin a frame
        EXPECT_TRUE(manualManager.BeginFrame());

        // In release builds, it should fail gracefully
        EXPECT_FALSE(manualManager.BeginFrame());
    }


    // ---------------------
    // Leak Detection / Counters
    // ---------------------

    TEST_F(IDManagerTest, IDCounts) {
        size_t initial_static = manager.GetStaticIDCount();
        size_t initial_dynamic = manager.GetDynamicIDCount();
        size_t initial_ranged = manager.GetRangedIDCount();

        ID static_id = manager.CreateStaticID();
        [[maybe_unused]] ID dynamic_id = manager.AllocateDynamicID("foo");
        [[maybe_unused]] ID range_id = manager.AllocateRangedID(4, "range");

        EXPECT_EQ(manager.GetStaticIDCount(), initial_static + 1);
        EXPECT_EQ(manager.GetDynamicIDCount(), initial_dynamic + 1);
        EXPECT_EQ(manager.GetRangedIDCount(), initial_ranged + 1);

        EXPECT_TRUE(manager.ReleaseStaticID(static_id));
        EXPECT_EQ(manager.GetStaticIDCount(), initial_static);
    }

    // ---------------------
    // API misuse Tests
    // ---------------------

    TEST_F(IDManagerTest, ValidationFunctionMisuse) {
        ID static_id = manager.CreateStaticID();
        ID dynamic_id = manager.AllocateDynamicID("dynamic");
        ID ranged_id = manager.AllocateRangedID(2, "range");
        ID sub_id = manager.GetSubID(ranged_id, 0);

        // Static validation misuse
        EXPECT_FALSE(manager.IsStaticIDValid(dynamic_id));
        EXPECT_FALSE(manager.IsStaticIDValid(ranged_id));
        EXPECT_FALSE(manager.IsStaticIDValid(sub_id));

        // Dynamic validation misuse
        EXPECT_FALSE(manager.IsDynamicIDValid(static_id));
        EXPECT_FALSE(manager.IsDynamicIDValid(ranged_id));
        EXPECT_FALSE(manager.IsDynamicIDValid(sub_id));

        // Ranged validation misuse
        EXPECT_FALSE(manager.IsRangedIDValid(static_id));
        EXPECT_FALSE(manager.IsRangedIDValid(dynamic_id));
        EXPECT_FALSE(manager.IsRangedIDValid(sub_id));

        // SubRange validation misuse
        EXPECT_FALSE(manager.IsSubRangeIDValid(static_id));
        EXPECT_FALSE(manager.IsSubRangeIDValid(dynamic_id));
        EXPECT_FALSE(manager.IsSubRangeIDValid(ranged_id));
    }

    TEST_F(IDManagerTest, StaticIDReleaseMisuse) {
        ID static_id = manager.CreateStaticID();
        ID dynamic_id = manager.AllocateDynamicID("foo");
        ID range_id = manager.AllocateRangedID(100, "foo");
        ID sub_range_id = manager.GetSubID(range_id, 0);

        EXPECT_TRUE(manager.ReleaseStaticID(static_id));   // correct
        EXPECT_FALSE(manager.ReleaseStaticID(dynamic_id)); // wrong type
        EXPECT_FALSE(manager.ReleaseStaticID(range_id)); // wrong type
        EXPECT_FALSE(manager.ReleaseStaticID(sub_range_id)); // wrong type
        EXPECT_FALSE(manager.ReleaseStaticID(static_id));  // double release
        EXPECT_FALSE(manager.ReleaseStaticID(ID::Invalid()));// invalid ID
    }

    TEST_F(IDManagerTest, SubIDMisuse) {
        ID invalid_range = ID::Invalid();
        EXPECT_EQ(manager.GetSubID(invalid_range, 0), ID::Invalid()); // not a ranged ID

        ID dynamic_id = manager.AllocateDynamicID("dynamic");
        EXPECT_EQ(manager.GetSubID(dynamic_id, 0), ID::Invalid()); // wrong parent type
    }

    TEST_F(IDManagerTest, ParentStackMisuse) {
        EXPECT_FALSE(manager.PushParent(ID::Invalid())); // release build: fails gracefully

        ID child = manager.AllocateDynamicID("child");
        EXPECT_TRUE(manager.IsDynamicIDValid(child));

        EXPECT_FALSE(manager.PopParent()); // release build: fails gracefully
    }
}