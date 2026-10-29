#include <gtest/gtest.h>

#include <gui_core/style/gui_style.h>
#include <gui_core/style/gui_gradient_manager.h>
#include <gui_core/style/gui_style_manager.h>

#include <gui_core/elements/gui_element_storage.h>
#include <gui_core/windows/gui_window_registry.h>
#include <vector>

namespace tachyon
{

    class ElementStorageTest : public ::testing::Test
    {
    protected:
        GradientManager gradientManager{256};
        StyleManager styleManager{gradientManager};
        StyleRef defaultStyleIndex{styleManager.Register(Style{})};
        ElementStorage storage{styleManager};
    
        // Helper to create a basic element with a specific ID
        Element CreateElement(uint64_t id_val = 1, ElementFlags flags = ElementFlags::None)
        {
            Element e{.styleRef = defaultStyleIndex, .flags = flags};
            e.id = ID{ .value {.integer = id_val } };
            return e;
        }

        // Helper: find ElementRef for an element with given id in a specific depth.
        ElementRef FindRefByIDAtDepth(uint64_t id_val, uint32_t depth) const
        {
            if (depth >= storage.NumberOfDepthLevels()) return ElementRef::Invalid();
            for (uint32_t i = 0; i < storage.DepthSizeOf(depth); ++i)
            {
                ElementRef ref{ depth, i };
                const Element* e = storage.GetElementPtr(ref);
                if (e && e->id.Value() == id_val) return ref;
            }
            return ElementRef::Invalid();
        }

        // Helper: collect element IDs in depth-first (preorder) traversal
        std::vector<uint64_t> CollectDepthFirstIDs() const
        {
            std::vector<uint64_t> out;
            if (storage.Empty()) return out;

            std::vector<ElementRef> stack;
    
            // Push roots in reverse order to process them from first to last
            uint32_t rootCount = storage.DepthSizeOf(0);
            for (int i = static_cast<int>(rootCount) - 1; i >= 0; --i)
            {
                stack.push_back(ElementRef{ 0, static_cast<uint32_t>(i) });
            }

            while (!stack.empty())
            {
                ElementRef cur = stack.back();
                stack.pop_back();

                const Element* e = storage.GetElementPtr(cur);
                if (e) out.push_back(e->id.Value());

                auto children = storage.GetChildrenSpan(cur);
        
                // Push children in reverse order so the first child is processed next (preorder)
                for (int i = static_cast<int>(children.size()) - 1; i >= 0; --i)
                {
                    // Children of P live at P.depth + 1, from its child range begin.
                    uint32_t childIndex = storage.GetChildRange(cur).begin + i;
                    stack.push_back(ElementRef{ cur.depth + 1, childIndex });
                }
            }
            return out;
        }
    };

    TEST_F(ElementStorageTest, DepthFirstTraversalOrderMultiRoot)
    {
        /* Tree Structure:
           Root 0 (ID: 10)
             |__ Child 0.0 (ID: 11)
             |__ Child 0.1 (ID: 12)
                 |__ Child 0.1.0 (ID: 13)
           Root 1 (ID: 20)
        */

        ASSERT_TRUE(storage.BeginConstruction() == ElementStorageResult::Success);

        // Root 0
        EXPECT_NE(storage.Add(CreateElement(10)).ref, ElementRef::Invalid());
        if (storage.BeginChildren() == ElementStorageResult::Success)
        {
            EXPECT_NE(storage.Add(CreateElement(11)).ref, ElementRef::Invalid());
            EXPECT_NE(storage.Add(CreateElement(12)).ref, ElementRef::Invalid());
            if (storage.BeginChildren() == ElementStorageResult::Success)
            {
                EXPECT_NE(storage.Add(CreateElement(13)).ref, ElementRef::Invalid());
                [[maybe_unused]]auto result = storage.EndChildren();
                assert(result == ElementStorageResult::Success);
            }
            [[maybe_unused]]auto result2 = storage.EndChildren();
            assert(result2 == ElementStorageResult::Success);
        }

        // Root 1
        EXPECT_NE(storage.Add(CreateElement(20)).ref, ElementRef::Invalid());

        ASSERT_TRUE(storage.EndConstruction() == ElementStorageResult::Success);

        // Expected ID sequence for Depth-First Search: 
        // 10 -> 11 -> 12 -> 13 -> 20
        std::vector<uint64_t> expectedOrder = { 10, 11, 12, 13, 20 };
        auto actualOrder = CollectDepthFirstIDs();

        EXPECT_EQ(actualOrder, expectedOrder);
    }

    TEST_F(ElementStorageTest, DepthFirstTraversalOrderSingleRoot)
    {
        /* Tree Structure:
           Root 0 (ID: 10)
             |__ Child 0.0 (ID: 11)
             |__ Child 0.1 (ID: 12)
                 |__ Child 0.1.0 (ID: 13)
        */

        ASSERT_TRUE(storage.BeginConstruction() == ElementStorageResult::Success);

        // Root 0
        EXPECT_NE(storage.Add(CreateElement(10)).ref, ElementRef::Invalid());
        if (storage.BeginChildren() == ElementStorageResult::Success)
        {
            EXPECT_NE(storage.Add(CreateElement(11)).ref, ElementRef::Invalid());
            EXPECT_NE(storage.Add(CreateElement(12)).ref, ElementRef::Invalid());
            if (storage.BeginChildren() == ElementStorageResult::Success)
            {
                EXPECT_NE(storage.Add(CreateElement(13)).ref, ElementRef::Invalid());
                [[maybe_unused]] auto result = storage.EndChildren();
                assert(result == ElementStorageResult::Success);
            }
            [[maybe_unused]] auto result2 = storage.EndChildren();
            assert(result2 == ElementStorageResult::Success);
        }

        ASSERT_TRUE(storage.EndConstruction() == ElementStorageResult::Success);

        // Expected ID sequence for Depth-First Search: 
        // 10 -> 11 -> 12 -> 13
        std::vector<uint64_t> expectedOrder = { 10, 11, 12, 13 };
        auto actualOrder = CollectDepthFirstIDs();

        EXPECT_EQ(actualOrder, expectedOrder);
    }

    TEST_F(ElementStorageTest, MultipleRootsAndDeepClimbBack)
    {
        /* Climbing from deep nesting back to the next root.
           Tree Structure:
           Root 0 (ID: 1)
             |__ Child 0.1 (ID: 11)
                 |__ Child 0.1.1 (ID: 111) <-- Leaf
           Root 1 (ID: 2)                  <-- Must jump here after ID 111
           Root 2 (ID: 3)
             |__ Child 2.1 (ID: 31)
        */

        ASSERT_TRUE(storage.BeginConstruction() == ElementStorageResult::Success);

        // Tree 1: Deeply nested
        EXPECT_NE(storage.Add(CreateElement(1)).ref, ElementRef::Invalid());
        if (storage.BeginChildren() == ElementStorageResult::Success)
        {
            EXPECT_NE(storage.Add(CreateElement(11)).ref, ElementRef::Invalid());
            if (storage.BeginChildren() == ElementStorageResult::Success)
            {
                EXPECT_NE(storage.Add(CreateElement(111)).ref, ElementRef::Invalid());
                [[maybe_unused]] auto result = storage.EndChildren();
                assert(result == ElementStorageResult::Success);
            }
            [[maybe_unused]] auto result2 = storage.EndChildren();
            assert(result2 == ElementStorageResult::Success);
        }

        // Tree 2: Bare root (The "Jump" target)
        EXPECT_NE(storage.Add(CreateElement(2)).ref, ElementRef::Invalid());

        // Tree 3: Root with one child
        EXPECT_NE(storage.Add(CreateElement(3)).ref, ElementRef::Invalid());
        if (storage.BeginChildren() == ElementStorageResult::Success)
        {
            EXPECT_NE(storage.Add(CreateElement(31)).ref, ElementRef::Invalid());
            [[maybe_unused]] auto result3 = storage.EndChildren();
            assert(result3 == ElementStorageResult::Success);
        }

        ASSERT_TRUE(storage.EndConstruction() == ElementStorageResult::Success);

        // Expected Depth-First Order: 1 -> 11 -> 111 -> 2 -> 3 -> 31
        std::vector<uint64_t> expectedOrder = {1, 11, 111, 2, 3, 31};
        auto actualOrder = CollectDepthFirstIDs();

        EXPECT_EQ(actualOrder, expectedOrder);
    }

    TEST_F(ElementStorageTest, EmptyStorageIteration)
    {
        ASSERT_TRUE(storage.BeginConstruction() == ElementStorageResult::Success);
        ASSERT_TRUE(storage.EndConstruction() == ElementStorageResult::Success);

        auto ids = CollectDepthFirstIDs();
        EXPECT_TRUE(ids.empty());
    }

    TEST_F(ElementStorageTest, SingleElementIteration)
    {
        ASSERT_TRUE(storage.BeginConstruction() == ElementStorageResult::Success);
        EXPECT_NE(storage.Add(CreateElement(100)).ref, ElementRef::Invalid());
        ASSERT_TRUE(storage.EndConstruction() == ElementStorageResult::Success);

        auto ids = CollectDepthFirstIDs();
        ASSERT_EQ(ids.size(), 1);
        EXPECT_EQ(ids[0], 100);
    }

    TEST_F(ElementStorageTest, BeginChildrenWithoutBeginConstructionFails)
    {
        auto result = storage.BeginChildren();
        EXPECT_EQ(result, ElementStorageResult::NotConstructing);
    }

    TEST_F(ElementStorageTest, AddWithoutBeginConstructionMakesBeginChildrenFail)
    {
        EXPECT_EQ(storage.Add(CreateElement(1)), ElementStorageResult::NotConstructing);

        auto result = storage.BeginChildren();
        EXPECT_EQ(result, ElementStorageResult::NotConstructing);
    }

    TEST_F(ElementStorageTest, BeginChildrenWithoutAddFails)
    {
        ASSERT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);

        auto result = storage.BeginChildren();
        EXPECT_EQ(result, ElementStorageResult::MissingAdd);
    }

    TEST_F(ElementStorageTest, BeginChildrenCalledTwiceFails)
    {
        ASSERT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);

        EXPECT_EQ(storage.Add(CreateElement(1)), ElementStorageResult::Success);
        ASSERT_EQ(storage.BeginChildren(), ElementStorageResult::Success);

        auto result = storage.BeginChildren();
        EXPECT_EQ(result, ElementStorageResult::DuplicateCall);
    }

    TEST_F(ElementStorageTest, EndChildrenWithoutBeginChildrenFails)
    {
        ASSERT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);

        auto result = storage.EndChildren();
        EXPECT_EQ(result, ElementStorageResult::MissingBeginChildren);
    }

    TEST_F(ElementStorageTest, EndChildrenWithoutBeginConstructionFails)
    {
        auto result = storage.EndChildren();
        EXPECT_EQ(result, ElementStorageResult::NotConstructing);
    }

    TEST_F(ElementStorageTest, BeginChildrenAfterEndChildrenWithoutAddFails)
    {
        ASSERT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);

        ASSERT_EQ(storage.Add(CreateElement(1)), ElementStorageResult::Success);
        ASSERT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        ASSERT_EQ(storage.EndChildren(), ElementStorageResult::Success);

        auto result = storage.BeginChildren();
        EXPECT_EQ(result, ElementStorageResult::MissingAdd);
    }

    TEST_F(ElementStorageTest, AddOutsideOfConstructionIsInvalid)
    {
        auto ref = storage.Add(CreateElement(1));
        EXPECT_EQ(ref.ref, ElementRef::Invalid());
        EXPECT_EQ(ref.status, ElementStorageResult::NotConstructing);
    }

    TEST_F(ElementStorageTest, BeginChildrenOutsideOfConstructionIsInvalid)
    {
        auto result = storage.BeginChildren();
        EXPECT_EQ(result, ElementStorageResult::NotConstructing);
    }

    TEST_F(ElementStorageTest, EndConstructionWithoutBeginConstructionIsInvalid)
    {
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::NotConstructing);
    }

    TEST_F(ElementStorageTest, EndConstructionWithOpenChildrenIsInvalid)
    {
        ASSERT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);

        ASSERT_EQ(storage.Add(CreateElement(1)), ElementStorageResult::Success);
        ASSERT_EQ(storage.BeginChildren(), ElementStorageResult::Success);

        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::MissingEndChildren);
        // The open scope was closed, so the next frame starts cleanly.
        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
    }

    TEST_F(ElementStorageTest, ValidAddBeginEndChildrenSequenceSucceeds)
    {
        ASSERT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);

        ASSERT_EQ(storage.Add(CreateElement(1)), ElementStorageResult::Success);
        ASSERT_EQ(storage.BeginChildren(), ElementStorageResult::Success);

        ASSERT_EQ(storage.Add(CreateElement(2)), ElementStorageResult::Success);
        ASSERT_EQ(storage.Add(CreateElement(3)), ElementStorageResult::Success);

        ASSERT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        ASSERT_EQ(storage.EndConstruction(), ElementStorageResult::Success);
    }

    TEST_F(ElementStorageTest, AddRootAllowedWithoutSpecialFlag)
    {
        ASSERT_TRUE(storage.BeginConstruction() == ElementStorageResult::Success);

        // Adding a root without special flags should succeed
        auto result = storage.Add(CreateElement(10, ElementFlags::None));
        EXPECT_TRUE(result);
        EXPECT_NE(result.ref, ElementRef::Invalid());

        ASSERT_EQ(storage.EndConstruction(), ElementStorageResult::Success);
    }

    TEST_F(ElementStorageTest, TopDownIterationProvidesCorrectChildRanges)
    {
        /* Root (ID: 1) -> Children: [11, 12]
           Child (ID: 11) -> No children
           Child (ID: 12) -> Children: [121]
        */
        ASSERT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        ASSERT_EQ(storage.Add(CreateElement(1)), ElementStorageResult::Success);
        ASSERT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
            ASSERT_EQ(storage.Add(CreateElement(11)), ElementStorageResult::Success);
            ASSERT_EQ(storage.Add(CreateElement(12)), ElementStorageResult::Success);
            ASSERT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
                ASSERT_EQ(storage.Add(CreateElement(121)), ElementStorageResult::Success);
            ASSERT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        ASSERT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        ASSERT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        int elementsVisited = 0;
        for (auto it = storage.TopDownDepthScanBegin(); it != storage.TopDownDepthScanEnd(); ++it)
        {
            elementsVisited++;
            uint64_t currentID = storage.GetElementUnsafe(it->CurrentRef()).id.Value();
        
            if (currentID == 1) {
                EXPECT_EQ(it->childRange.Size(), 2u); // IDs 11 and 12
                auto children = it->GetChildren(storage);
                EXPECT_EQ(children[0].id.Value(), 11);
                EXPECT_EQ(children[1].id.Value(), 12);
            }
            else if (currentID == 12) {
                EXPECT_EQ(it->childRange.Size(), 1u); // ID 121
                EXPECT_EQ(it->GetChildren(storage)[0].id.Value(), 121);
            }
            else {
                EXPECT_TRUE(it->childRange.Empty());
            }
        }
        EXPECT_EQ(elementsVisited, 4);
    }

    TEST_F(ElementStorageTest, BottomUpIterationOrder)
    {
        ASSERT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        ASSERT_EQ(storage.Add(CreateElement(1)), ElementStorageResult::Success); // Depth 0
        ASSERT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
            ASSERT_EQ(storage.Add(CreateElement(11)), ElementStorageResult::Success); // Depth 1
        ASSERT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        ASSERT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        auto it = storage.BottomUpDepthScanBegin();
    
        // First element should be ID 11 (Depth 1)
        EXPECT_EQ(storage.GetElementUnsafe(it->CurrentRef()).id.Value(), 11);
        ++it;
    
        // Second element should be ID 1 (Depth 0)
        EXPECT_EQ(storage.GetElementUnsafe(it->CurrentRef()).id.Value(), 1);
        ++it;
    
        EXPECT_EQ(it, storage.BottomUpDepthScanEnd());
    }

    TEST(SizeConstraintsTest, ApplyHonorsClampAndFixedOverrides)
    {
        LayoutOverride sizeConstraints;
        sizeConstraints.width.min = 10;
        sizeConstraints.width.max = 100;
        sizeConstraints.height.min = 20;
        sizeConstraints.height.max = 200;

        int2 clamped = sizeConstraints.ApplySize(int2{ 1, 999 });
        EXPECT_EQ(clamped.x, 10);
        EXPECT_EQ(clamped.y, 200);

        sizeConstraints.width.value = 77;
        sizeConstraints.height.value = 55;
        EXPECT_TRUE(sizeConstraints.HasFixedWidth());
        EXPECT_TRUE(sizeConstraints.HasFixedHeight());

        int2 fixed = sizeConstraints.ApplySize(int2{ 1, 999 });
        EXPECT_EQ(fixed.x, 77);
        EXPECT_EQ(fixed.y, 55);
    }

    TEST(PositionConstraintsTest, HelpersCreateExpectedModes)
    {
        const LayoutOverride relative = LayoutOverride{}.Relative();
        EXPECT_EQ(relative.x.type, PositionType::Relative);
        EXPECT_EQ(relative.y.type, PositionType::Relative);
        EXPECT_TRUE(relative.IsInFlow());

        EdgeInset absoluteOffset {};
        absoluteOffset.left = 12;
        absoluteOffset.top = 9;
        const LayoutOverride absolute = LayoutOverride{}.Absolute(absoluteOffset);
        EXPECT_EQ(absolute.x.type, PositionType::Absolute);
        EXPECT_EQ(absolute.y.type, PositionType::Absolute);
        EXPECT_FALSE(absolute.IsInFlow());
        EXPECT_EQ(absolute.x.near, 12);
        EXPECT_EQ(absolute.y.near, 9);

        const LayoutOverride anchored = LayoutOverride{}.Anchored(ID::Invalid(), AnchorPlacement::OutsideBottomLeft);
        EXPECT_EQ(anchored.x.type, PositionType::Anchored);
        EXPECT_EQ(anchored.y.type, PositionType::Anchored);
        EXPECT_EQ(anchored.x.anchorPlacement, GetHorizontalAxis(AnchorPlacement::OutsideBottomLeft));
        EXPECT_EQ(anchored.y.anchorPlacement, GetVerticalAxis(AnchorPlacement::OutsideBottomLeft));
    }

    TEST(PositionConstraintsTest, WithTypeX_SetsXAxisIndependently)
    {
        const LayoutOverride pc = LayoutOverride{}.Relative().WithTypeX(PositionType::Absolute);
        EXPECT_EQ(pc.x.type, PositionType::Absolute);
        EXPECT_EQ(pc.y.type, PositionType::Relative);
    }

    TEST(PositionConstraintsTest, WithTypeY_SetsYAxisIndependently)
    {
        const LayoutOverride pc = LayoutOverride{}.Relative().WithTypeY(PositionType::Absolute);
        EXPECT_EQ(pc.x.type, PositionType::Relative);
        EXPECT_EQ(pc.y.type, PositionType::Absolute);
    }

    TEST(PositionConstraintsTest, MixedAxisTypes_IsInFlowVariants)
    {
        // AbsoluteX, RelativeY
        {
            const LayoutOverride pc = LayoutOverride{}.Relative().WithTypeX(PositionType::Absolute);
            EXPECT_FALSE(pc.IsInFlowX());
            EXPECT_TRUE (pc.IsInFlowY());
            EXPECT_FALSE(pc.IsInFlow());
            EXPECT_FALSE(pc.IsInFlowAxis(0));
            EXPECT_TRUE (pc.IsInFlowAxis(1));
        }
        // RelativeX, AbsoluteY
        {
            const LayoutOverride pc = LayoutOverride{}.Relative().WithTypeY(PositionType::Absolute);
            EXPECT_TRUE (pc.IsInFlowX());
            EXPECT_FALSE(pc.IsInFlowY());
            EXPECT_FALSE(pc.IsInFlow());
            EXPECT_TRUE (pc.IsInFlowAxis(0));
            EXPECT_FALSE(pc.IsInFlowAxis(1));
        }
        // CenteredX, RelativeY  (both still in-flow)
        {
            const LayoutOverride pc = LayoutOverride{}.Relative().WithTypeX(PositionType::Centered);
            EXPECT_TRUE(pc.IsInFlowX());
            EXPECT_TRUE(pc.IsInFlowY());
            EXPECT_TRUE(pc.IsInFlow());
            EXPECT_TRUE(pc.IsInFlowAxis(0));
            EXPECT_TRUE(pc.IsInFlowAxis(1));
        }
    }

    TEST_F(ElementStorageTest, AddStoresSizeAndPositionConstraints)
    {
        ASSERT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);

        LayoutOverride lo = LayoutOverride::FixedSize(80, 30).Relative(11, 7);
        lo.width.min = 15;
        lo.width.max = 120;
        lo.height.min = 9;
        lo.height.max = 90;

        auto addResult = storage.Add(CreateElement(999), std::move(lo));
        ASSERT_EQ(addResult.status, ElementStorageResult::Success);
        ASSERT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        const ElementRect& rect = storage.GetElementRectUnsafe(addResult.ref);
        EXPECT_EQ(rect.constraints.width.value, 80);
        EXPECT_EQ(rect.constraints.height.value, 30);
        EXPECT_EQ(rect.constraints.width.min, 15);
        EXPECT_EQ(rect.constraints.height.min, 9);
        EXPECT_EQ(rect.constraints.x.type, PositionType::Relative);
        EXPECT_EQ(rect.constraints.y.type, PositionType::Relative);
        EXPECT_EQ(rect.constraints.x.near, 11);
        EXPECT_EQ(rect.constraints.y.near, 7);
    }

    TEST_F(ElementStorageTest, AddWithoutExplicitConstraintsUsesAutoAndRelativeDefaults)
    {
        ASSERT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);

        auto addResult = storage.Add(CreateElement(1234));
        ASSERT_EQ(addResult.status, ElementStorageResult::Success);
        ASSERT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        const ElementRect& rect = storage.GetElementRectUnsafe(addResult.ref);
        EXPECT_EQ(rect.constraints.width.value, SizeConstraintAxis::kAuto);
        EXPECT_EQ(rect.constraints.height.value, SizeConstraintAxis::kAuto);
        EXPECT_EQ(rect.constraints.x.type, PositionType::Relative);
        EXPECT_EQ(rect.constraints.y.type, PositionType::Relative);
    }

    TEST_F(ElementStorageTest, GetElementIDAt_DoesNotSelectChildOutsideChildClipRect)
    {
        ASSERT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);

        auto rootResult = storage.Add(CreateElement(1));
        ASSERT_EQ(rootResult.status, ElementStorageResult::Success);

        ASSERT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        auto childResult = storage.Add(CreateElement(2));
        ASSERT_EQ(childResult.status, ElementStorageResult::Success);
        ASSERT_EQ(storage.EndChildren(), ElementStorageResult::Success);

        ASSERT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        auto& rootRect = storage.GetElementRectUnsafe(rootResult.ref);
        rootRect.rect = IntRect { .x = 0, .y = 0, .w = 100, .h = 100 };
        rootRect.clipRect = IntRect { .x = 0, .y = 0, .w = 100, .h = 100 };

        auto& childRect = storage.GetElementRectUnsafe(childResult.ref);
        childRect.rect = IntRect { .x = 10, .y = 10, .w = 80, .h = 80 };
        childRect.clipRect = IntRect { .x = 10, .y = 10, .w = 20, .h = 20 };

        const ID hitInsideVisibleChild = storage.GetElementIDAt(rootResult.ref, int2 { 15, 15 });
        EXPECT_EQ(hitInsideVisibleChild, storage.GetElementUnsafe(childResult.ref).id);

        const ID hitInsideChildButOutsideChildClip = storage.GetElementIDAt(rootResult.ref, int2 { 40, 40 });
        EXPECT_EQ(hitInsideChildButOutsideChildClip, storage.GetElementUnsafe(rootResult.ref).id);
    }

    TEST_F(ElementStorageTest, GetElementIDAt_RespectsRootClipRect)
    {
        ASSERT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        auto rootResult = storage.Add(CreateElement(10));
        ASSERT_EQ(rootResult.status, ElementStorageResult::Success);
        ASSERT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        auto& rootRect = storage.GetElementRectUnsafe(rootResult.ref);
        rootRect.rect = IntRect { .x = 0, .y = 0, .w = 100, .h = 100 };
        rootRect.clipRect = IntRect { .x = 0, .y = 0, .w = 30, .h = 30 };

        const ID hitInsideRootAndClip = storage.GetElementIDAt(rootResult.ref, int2 { 20, 20 });
        EXPECT_EQ(hitInsideRootAndClip, storage.GetElementUnsafe(rootResult.ref).id);

        const ID hitInsideRootButOutsideClip = storage.GetElementIDAt(rootResult.ref, int2 { 60, 20 });
        EXPECT_EQ(hitInsideRootButOutsideClip, ID::Invalid());
    }

    // ---------------------------------------------------------------------
    // ScreenToElementSpace - maps screen pixels through the ancestors'
    // contentTransforms into an element's own rect space.
    // ---------------------------------------------------------------------

    TEST_F(ElementStorageTest, ScreenToElementSpace_IdentityWhenNoAncestorHasTransform)
    {
        ASSERT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        auto rootResult = storage.Add(CreateElement(1));
        ASSERT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        auto childResult = storage.Add(CreateElement(2));
        ASSERT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        ASSERT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        auto& rootRect = storage.GetElementRectUnsafe(rootResult.ref);
        rootRect.rect = IntRect{ 0, 0, 200, 200 };
        rootRect.innerRect = rootRect.rect;
        // No contentTransform set -> identity.

        // Cursor at (50, 75) maps to (50, 75) in child's space (no transforms).
        const int2 mapped = storage.ScreenToElementSpace(int2{ 50, 75 }, childResult.ref);
        EXPECT_EQ(mapped.x, 50);
        EXPECT_EQ(mapped.y, 75);
    }

    TEST_F(ElementStorageTest, ScreenToElementSpace_AppliesAncestorContentTransformInverse)
    {
        ASSERT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        auto rootResult = storage.Add(CreateElement(1));
        ASSERT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        auto childResult = storage.Add(CreateElement(2));
        ASSERT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        ASSERT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        auto& rootRect = storage.GetElementRectUnsafe(rootResult.ref);
        rootRect.rect = IntRect{ 0, 0, 800, 600 };
        rootRect.innerRect = IntRect{ 0, 0, 800, 600 };
        // Centring transform: content (0, 0) maps to screen (400, 300).
        rootRect.contentTransform = float3x2::Translate(float2{ 400.f, 300.f });

        // Cursor at screen (60, 240) maps to child space (-340, -60) -
        // the same inverse that GetElementIDAt applies during descent.
        const int2 mapped = storage.ScreenToElementSpace(int2{ 60, 240 }, childResult.ref);
        EXPECT_EQ(mapped.x, -340);
        EXPECT_EQ(mapped.y, -60);
    }

    TEST_F(ElementStorageTest, ScreenToElementSpace_StacksTransformsAcrossMultipleAncestors)
    {
        // root (T1 = Translate(100, 0)) -> middle (T2 = Translate(50, 0)) -> leaf
        // Leaf-space x = screen x - 150.
        ASSERT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        auto rootResult = storage.Add(CreateElement(1));
        ASSERT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        auto midResult = storage.Add(CreateElement(2));
        ASSERT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        auto leafResult = storage.Add(CreateElement(3));
        ASSERT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        ASSERT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        ASSERT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        auto& r = storage.GetElementRectUnsafe(rootResult.ref);
        r.rect = r.innerRect = IntRect{ 0, 0, 1000, 1000 };
        r.contentTransform = float3x2::Translate(float2{ 100.f, 0.f });

        auto& m = storage.GetElementRectUnsafe(midResult.ref);
        m.rect = m.innerRect = IntRect{ 0, 0, 1000, 1000 };
        m.contentTransform = float3x2::Translate(float2{ 50.f, 0.f });

        // Cursor at screen 200 -> after T1^-1 around pivot (0,0): 100 -> after
        // T2^-1 around pivot (0,0): 50. So leaf-space x = 50.
        const int2 mapped = storage.ScreenToElementSpace(int2{ 200, 0 }, leafResult.ref);
        EXPECT_EQ(mapped.x, 50);
        EXPECT_EQ(mapped.y, 0);
    }

    TEST_F(ElementStorageTest, ScreenToElementSpace_RoundTripsWithGetElementIDAt)
    {
        // Build root (centring contentTransform) -> child at a negative-layout
        // position. The screen coord where the child renders, when fed through
        // ScreenToElementSpace and compared to child.rect, must land INSIDE
        // child.rect - this is the exact invariant event handlers depend on.
        ASSERT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        auto rootResult = storage.Add(CreateElement(1));
        ASSERT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        auto childResult = storage.Add(CreateElement(2));
        ASSERT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        ASSERT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        auto& r = storage.GetElementRectUnsafe(rootResult.ref);
        r.rect = r.innerRect = r.clipRect = IntRect{ 0, 0, 800, 600 };
        r.contentTransform = float3x2::Translate(float2{ 400.f, 300.f });

        auto& c = storage.GetElementRectUnsafe(childResult.ref);
        c.rect = IntRect{ -340, -60, 180, 130 };
        c.clipRect = IntRect{ -400, -300, 800, 600 };

        // Cursor at screen 65 lands inside child's LAYOUT range [-340, -160]
        // when mapped through ScreenToElementSpace.
        const int2 mapped = storage.ScreenToElementSpace(int2{ 65, 245 }, childResult.ref);
        EXPECT_GE(mapped.x, c.rect.x);
        EXPECT_LT(mapped.x, c.rect.x + c.rect.w);
        EXPECT_GE(mapped.y, c.rect.y);
        EXPECT_LT(mapped.y, c.rect.y + c.rect.h);

        // ...and the SAME cursor coords land on the child via the actual
        // hit-test path, so the two functions agree.
        EXPECT_EQ(storage.GetElementIDAt(rootResult.ref, int2{ 65, 245 }),
                  storage.GetElementUnsafe(childResult.ref).id);
    }

    // ---------- FindElementRefByID ------------------------------------------------

    TEST_F(ElementStorageTest, FindElementRefByID_FindsElementsAtEveryDepthDuringAndAfterConstruction)
    {
        ASSERT_TRUE(storage.BeginConstruction() == ElementStorageResult::Success);
        const auto root = storage.Add(CreateElement(10));
        ASSERT_TRUE(storage.BeginChildren() == ElementStorageResult::Success);
        const auto child = storage.Add(CreateElement(11));
        EXPECT_EQ(storage.FindElementRefByID(CreateElement(11).id), child.ref) << "visible right after Add";
        [[maybe_unused]] auto r = storage.EndChildren();
        const auto sibling = storage.Add(CreateElement(20));
        ASSERT_TRUE(storage.EndConstruction() == ElementStorageResult::Success);

        EXPECT_EQ(storage.FindElementRefByID(CreateElement(10).id), root.ref);
        EXPECT_EQ(storage.FindElementRefByID(CreateElement(11).id), child.ref);
        EXPECT_EQ(storage.FindElementRefByID(CreateElement(20).id), sibling.ref);
        EXPECT_EQ(storage.FindElementRefByID(CreateElement(99).id), ElementRef::Invalid());
        EXPECT_EQ(storage.FindElementRefByID(ID::Invalid()), ElementRef::Invalid());
    }

    TEST_F(ElementStorageTest, FindElementRefByID_DuplicateIdResolvesToTheShallowestFirstElement)
    {
        ASSERT_TRUE(storage.BeginConstruction() == ElementStorageResult::Success);
        [[maybe_unused]] auto a = storage.Add(CreateElement(10));
        ASSERT_TRUE(storage.BeginChildren() == ElementStorageResult::Success);
        [[maybe_unused]] auto deep = storage.Add(CreateElement(7));   // depth 1, added first
        [[maybe_unused]] auto e = storage.EndChildren();
        const auto shallow = storage.Add(CreateElement(7));           // depth 0, added later
        ASSERT_TRUE(storage.EndConstruction() == ElementStorageResult::Success);

        EXPECT_EQ(storage.FindElementRefByID(CreateElement(7).id), shallow.ref);
    }

    TEST_F(ElementStorageTest, FindElementRefByID_TracksElementsMovedByMoveChild)
    {
        ASSERT_TRUE(storage.BeginConstruction() == ElementStorageResult::Success);
        [[maybe_unused]] auto parent = storage.Add(CreateElement(1));
        ASSERT_TRUE(storage.BeginChildren() == ElementStorageResult::Success);
        [[maybe_unused]] auto c0 = storage.Add(CreateElement(100));
        [[maybe_unused]] auto c1 = storage.Add(CreateElement(101));
        [[maybe_unused]] auto c2 = storage.Add(CreateElement(102));
        [[maybe_unused]] auto e = storage.EndChildren();
        ASSERT_TRUE(storage.MoveChild(1, 0, 0, 2) == ElementStorageResult::Success); // 100 moves behind 102

        for (uint64_t id : { 100u, 101u, 102u })
        {
            const ElementRef ref = storage.FindElementRefByID(CreateElement(id).id);
            ASSERT_NE(ref, ElementRef::Invalid());
            EXPECT_EQ(storage.GetElementUnsafe(ref).id.Value(), id) << "lookup must follow the element to its new slot";
        }
        ASSERT_TRUE(storage.EndConstruction() == ElementStorageResult::Success);
    }


    // Siblings with differently shaped subtrees: P{ A{a0}, B{b0, b1} }, A moved behind B.
    // Every slot at depth 1 and 2 changes owner, so stored refs must follow their elements.
    class MoveChildRefsTest : public ElementStorageTest
    {
    protected:
        AddResult a0{};
        void Build()
        {
            ASSERT_TRUE(storage.BeginConstruction() == ElementStorageResult::Success);
            windowRef = storage.Add(CreateElement(1)).ref;
            ASSERT_TRUE(storage.BeginChildren() == ElementStorageResult::Success);
            aRef = storage.Add(CreateElement(100)).ref;
            ASSERT_TRUE(storage.BeginChildren() == ElementStorageResult::Success);
            a0 = storage.Add(CreateElement(200));
            ASSERT_TRUE(storage.EndChildren() == ElementStorageResult::Success);
            bRef = storage.Add(CreateElement(101)).ref;
            ASSERT_TRUE(storage.BeginChildren() == ElementStorageResult::Success);
            b0Ref = storage.Add(CreateElement(300)).ref;
            b1Ref = storage.Add(CreateElement(301)).ref;
            ASSERT_TRUE(storage.EndChildren() == ElementStorageResult::Success);
        }
        ElementRef windowRef{}, aRef{}, bRef{}, b0Ref{}, b1Ref{};
        uint64_t IdAt(ElementRef ref) { return storage.GetElementUnsafe(ref).id.Value(); }
    };

    TEST_F(MoveChildRefsTest, StoredRefsFollowTheMovedElements)
    {
        Build();
        ASSERT_TRUE(storage.AddPostLayout(a0.ref, [](const PostLayoutContext&) {}) == ElementStorageResult::Success);
        ASSERT_TRUE(storage.AddPostLayout(b1Ref, [](const PostLayoutContext&) {}) == ElementStorageResult::Success);
        ASSERT_TRUE(storage.AddCustomDrawAfter(aRef, [](CustomDrawContext&) {}) == ElementStorageResult::Success);

        ASSERT_TRUE(storage.MoveChild(1, aRef.index, 0, 1) == ElementStorageResult::Success);
        ASSERT_TRUE(storage.EndChildren() == ElementStorageResult::Success);

        const auto& postLayout = storage.GetPostLayoutEntries();
        ASSERT_EQ(postLayout.size(), 2u);
        EXPECT_EQ(IdAt(postLayout[0].ownerRef), 200u);
        EXPECT_EQ(IdAt(postLayout[1].ownerRef), 301u);
        EXPECT_EQ(IdAt(storage.GetPostDrawEntryUnsafe(0).ownerRef), 100u);
        ASSERT_TRUE(storage.EndConstruction() == ElementStorageResult::Success);
    }

    // The window registry's pre-order draw list follows the move, so every child
    // still draws after (on top of) its parent.
    TEST_F(MoveChildRefsTest, WindowRegistryDrawOrderFollowsTheMove)
    {
        Build();
        WindowRegistry registry;
        ASSERT_TRUE(registry.BeginWindowScope(CreateElement(1).id, windowRef) == RegistryResult::Success);
        for (ElementRef ref : { aRef, a0.ref, bRef, b0Ref, b1Ref })
            ASSERT_TRUE(registry.Add(ref) == RegistryResult::Success);

        ElementMove move;
        ASSERT_TRUE(storage.MoveChild(1, aRef.index, 0, 1, &move) == ElementStorageResult::Success);
        ASSERT_TRUE(storage.EndChildren() == ElementStorageResult::Success);
        registry.ApplyElementMove(move);

        std::vector<uint64_t> order;
        for (const auto& node : *registry.GetWindowMembers(0))
            order.push_back(IdAt(node.ref));
        EXPECT_EQ(order, (std::vector<uint64_t>{ 101, 300, 301, 100, 200 }));
        ASSERT_TRUE(registry.EndWindowScope() == RegistryResult::Success);
        ASSERT_TRUE(storage.EndConstruction() == ElementStorageResult::Success);
    }

    // ---- :first-child / :last-child / :only-child ----

    class ChildStyleTest : public ElementStorageTest
    {
    protected:
        static Style Padded(uint16_t left, uint16_t right)
        {
            Style s;
            s.padding = BoxEdges(1, right, 1, left);
            s.borderRadius = BoxCorners(left, right, right, left);   // a box style of its own for each variant
            return s;
        }

        StyleRef plain = styleManager.Register(Padded(2, 2));
        StyleRef grouped = styleManager.RegisterWithChildStyles(Padded(2, 2), Padded(9, 2), Padded(2, 9), Padded(9, 9));

        ElementRef AddWithStyle(uint64_t id, StyleRef style)
        {
            Element e = CreateElement(id);
            e.styleRef = style;
            const auto result = storage.Add(std::move(e));
            EXPECT_TRUE(result);
            return result.ref;
        }

        const LayoutStyle* LayoutOf(ElementRef ref) const { return styleManager.GetLayoutStyle(storage.GetElementPtr(ref)->styleRef.layoutStyleIndex); }
    };

    // A style registered with child styles is its own, even when another registration has the same values.
    TEST_F(ChildStyleTest, StylesWithChildStylesAreNeverShared)
    {
        EXPECT_NE(plain.layoutStyleIndex, grouped.layoutStyleIndex);
        EXPECT_EQ(styleManager.GetChildStyles(plain.layoutStyleIndex), nullptr);
        ASSERT_NE(styleManager.GetChildStyles(grouped.layoutStyleIndex), nullptr);
        EXPECT_EQ(styleManager.Register(Padded(2, 2)).layoutStyleIndex, plain.layoutStyleIndex);
    }

    TEST_F(ChildStyleTest, FirstAndLastChildrenTakeTheirStyles)
    {
        const StyleManager::ChildStyleRefs& variants = *styleManager.GetChildStyles(grouped.layoutStyleIndex);
        ASSERT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        AddWithStyle(1, defaultStyleIndex);
        ASSERT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
            const ElementRef first  = AddWithStyle(10, grouped);
            const ElementRef middle = AddWithStyle(11, grouped);
            const ElementRef last   = AddWithStyle(12, grouped);
            // Not yet: whether an element is the last is only known when its parent is done.
            EXPECT_EQ(storage.GetElementPtr(first)->styleRef, grouped);
        ASSERT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        ASSERT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        EXPECT_EQ(storage.GetElementPtr(first)->styleRef.layoutStyleIndex,     variants.first.layoutStyleIndex);
        EXPECT_EQ(storage.GetElementPtr(first)->styleRef.visualBoxStyleIndex,  variants.first.visualBoxStyleIndex);
        EXPECT_EQ(storage.GetElementPtr(middle)->styleRef, grouped);
        EXPECT_EQ(storage.GetElementPtr(last)->styleRef.layoutStyleIndex,      variants.last.layoutStyleIndex);
        EXPECT_EQ(storage.GetElementPtr(last)->styleRef.visualBoxStyleIndex,   variants.last.visualBoxStyleIndex);

        // Layout reads the style through the rect.
        EXPECT_EQ(storage.GetElementRectPtr(first)->layoutStyle, LayoutOf(first));
        EXPECT_EQ(storage.GetElementRectPtr(last)->layoutStyle,  LayoutOf(last));
        EXPECT_EQ(LayoutOf(first)->padding.left,  9u);
        EXPECT_EQ(LayoutOf(first)->padding.right, 2u);
        EXPECT_EQ(LayoutOf(last)->padding.left,   2u);
        EXPECT_EQ(LayoutOf(last)->padding.right,  9u);
    }

    TEST_F(ChildStyleTest, AnOnlyChildTakesTheOnlyStyle)
    {
        const StyleManager::ChildStyleRefs& variants = *styleManager.GetChildStyles(grouped.layoutStyleIndex);
        ASSERT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        AddWithStyle(1, defaultStyleIndex);
        ASSERT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
            const ElementRef only = AddWithStyle(10, grouped);
        ASSERT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        ASSERT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        EXPECT_EQ(storage.GetElementPtr(only)->styleRef.layoutStyleIndex, variants.only.layoutStyleIndex);
        EXPECT_EQ(LayoutOf(only)->padding.left,  9u);
        EXPECT_EQ(LayoutOf(only)->padding.right, 9u);
    }

    // Elements of other styles, and the ones between the ends, stay as they are; the text style is never swapped.
    TEST_F(ChildStyleTest, OnlyTheBoxOfStylesWithChildStylesIsSwapped)
    {
        // An element's own text, as a text override gives it.
        Style redText;
        redText.states[ElementState::None].textColor = ColorStyle::Create(color4b{ 255, 0, 0, 255 });
        StyleRef textVariant = grouped;
        textVariant.visualTextStyleIndex = styleManager.Register(redText).visualTextStyleIndex;
        ASSERT_NE(textVariant.visualTextStyleIndex, grouped.visualTextStyleIndex);

        ASSERT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        AddWithStyle(1, defaultStyleIndex);
        ASSERT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
            const ElementRef first = AddWithStyle(10, plain);
            const ElementRef last  = AddWithStyle(11, textVariant);
            ASSERT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
                const ElementRef nested = AddWithStyle(20, grouped);
            ASSERT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        ASSERT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        ASSERT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        EXPECT_EQ(storage.GetElementPtr(first)->styleRef, plain);
        const StyleRef lastStyle = storage.GetElementPtr(last)->styleRef;
        EXPECT_EQ(lastStyle.layoutStyleIndex, styleManager.GetChildStyles(grouped.layoutStyleIndex)->last.layoutStyleIndex);
        EXPECT_EQ(lastStyle.visualTextStyleIndex, textVariant.visualTextStyleIndex);
        // A child's own children are a list of their own.
        EXPECT_EQ(storage.GetElementPtr(nested)->styleRef.layoutStyleIndex, styleManager.GetChildStyles(grouped.layoutStyleIndex)->only.layoutStyleIndex);
    }
}
