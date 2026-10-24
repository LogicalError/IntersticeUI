#include <gtest/gtest.h>

#include <gui_controls/gui_dock_node.h>
#include <gui_core/gui_id.h>
#include <gui_core/gui_id_manager.h>

#include <map>
#include <memory>
#include <random>
#include <string>

namespace interstice
{

    namespace
    {
        // Distinct panel IDs without an IDManager (DockNode only compares IDs).
        ID MakePanelID(uint64_t seed)
        {
            ID id{};
            // Mode bits = Dynamic (1); upper 62 bits hold the payload.
            id.value.dynamicParts.mode = IDMode::Dynamic;
            id.value.dynamicParts.hashValue = seed + 1; // avoid 0 so id != Invalid()
            return id;
        }
    }

    // ---------- Factory helpers -------------------------------------------------

    TEST(DockNodeTest, MakeLeafBuildsLeafWithPanels)
    {
        ID a = MakePanelID(1);
        ID b = MakePanelID(2);
        ID c = MakePanelID(3);
        auto leaf = MakeLeaf({a, b, c});

        ASSERT_NE(leaf, nullptr);
        EXPECT_TRUE(leaf->isLeaf);
        EXPECT_EQ(leaf->selectedTab, 0);
        ASSERT_EQ(leaf->panelIds.size(), 3u);
        EXPECT_EQ(leaf->panelIds[0], a);
        EXPECT_EQ(leaf->panelIds[1], b);
        EXPECT_EQ(leaf->panelIds[2], c);
        EXPECT_EQ(leaf->nodeID, ID::Invalid());
        EXPECT_EQ(leaf->first,  nullptr);
        EXPECT_EQ(leaf->second, nullptr);
    }

    TEST(DockNodeTest, MakeLeafEmptyByDefault)
    {
        auto leaf = MakeLeaf();
        ASSERT_NE(leaf, nullptr);
        EXPECT_TRUE(leaf->isLeaf);
        EXPECT_TRUE(leaf->panelIds.empty());
    }

    TEST(DockNodeTest, MakeSplitBuildsSplitNode)
    {
        ID a = MakePanelID(1);
        ID b = MakePanelID(2);
        auto split = MakeSplit(DockSplitMode::LeftPinned, 250,
                               MakeLeaf({a}), MakeLeaf({b}),
                               100, 60);

        ASSERT_NE(split, nullptr);
        EXPECT_FALSE(split->isLeaf);
        EXPECT_EQ(split->mode, DockSplitMode::LeftPinned);
        EXPECT_EQ(split->splitSize, 250);
        EXPECT_EQ(split->minFirstSize, 100);
        EXPECT_EQ(split->minSecondSize, 60);
        ASSERT_NE(split->first, nullptr);
        ASSERT_NE(split->second, nullptr);
        EXPECT_TRUE(split->first->isLeaf);
        EXPECT_TRUE(split->second->isLeaf);
        ASSERT_EQ(split->first->panelIds.size(),  1u);
        ASSERT_EQ(split->second->panelIds.size(), 1u);
        EXPECT_EQ(split->first->panelIds[0],  a);
        EXPECT_EQ(split->second->panelIds[0], b);
    }

    TEST(DockNodeTest, MakeSplitRespectsRightPinnedMode)
    {
        auto split = MakeSplit(DockSplitMode::RightPinned, 200,
                               MakeLeaf({MakePanelID(1)}),
                               MakeLeaf({MakePanelID(2)}));
        EXPECT_EQ(split->mode, DockSplitMode::RightPinned);
        EXPECT_TRUE(IsHorizontalSplit(split->mode));
        EXPECT_TRUE(PinsSecondChild(split->mode));
    }

    TEST(DockNodeTest, ModeHelpersClassifyAllValues)
    {
        EXPECT_TRUE(IsHorizontalSplit(DockSplitMode::LeftPinned));
        EXPECT_TRUE(IsHorizontalSplit(DockSplitMode::RightPinned));
        EXPECT_FALSE(IsHorizontalSplit(DockSplitMode::TopPinned));
        EXPECT_FALSE(IsHorizontalSplit(DockSplitMode::BottomPinned));

        EXPECT_FALSE(PinsSecondChild(DockSplitMode::LeftPinned));
        EXPECT_TRUE (PinsSecondChild(DockSplitMode::RightPinned));
        EXPECT_FALSE(PinsSecondChild(DockSplitMode::TopPinned));
        EXPECT_TRUE (PinsSecondChild(DockSplitMode::BottomPinned));
    }

    // ---------- ReorderLeafTabs -------------------------------------------------

    TEST(DockNodeTest, ReorderLeafTabs_MovesTabForward)
    {
        ID a = MakePanelID(1), b = MakePanelID(2), c = MakePanelID(3), d = MakePanelID(4);
        DockNode leaf;
        leaf.isLeaf = true;
        leaf.panelIds = {a, b, c, d};
        leaf.selectedTab = 0;

        // Move 'a' from index 0 to index 2 -> [b, c, a, d]
        ReorderLeafTabs(leaf, 0, 2);

        ASSERT_EQ(leaf.panelIds.size(), 4u);
        EXPECT_EQ(leaf.panelIds[0], b);
        EXPECT_EQ(leaf.panelIds[1], c);
        EXPECT_EQ(leaf.panelIds[2], a);
        EXPECT_EQ(leaf.panelIds[3], d);
        // The moved tab was selected; selection follows it to index 2.
        EXPECT_EQ(leaf.selectedTab, 2);
    }

    TEST(DockNodeTest, ReorderLeafTabs_MovesTabBackward)
    {
        ID a = MakePanelID(1), b = MakePanelID(2), c = MakePanelID(3), d = MakePanelID(4);
        DockNode leaf;
        leaf.isLeaf = true;
        leaf.panelIds = {a, b, c, d};
        leaf.selectedTab = 3; // 'd' selected

        // Move 'd' from index 3 to index 1 -> [a, d, b, c]
        ReorderLeafTabs(leaf, 3, 1);

        EXPECT_EQ(leaf.panelIds[0], a);
        EXPECT_EQ(leaf.panelIds[1], d);
        EXPECT_EQ(leaf.panelIds[2], b);
        EXPECT_EQ(leaf.panelIds[3], c);
        EXPECT_EQ(leaf.selectedTab, 1); // follows 'd'
    }

    TEST(DockNodeTest, ReorderLeafTabs_SelectionShiftsWhenPassedOver_Forward)
    {
        ID a = MakePanelID(1), b = MakePanelID(2), c = MakePanelID(3), d = MakePanelID(4);
        DockNode leaf;
        leaf.isLeaf = true;
        leaf.panelIds = {a, b, c, d};
        leaf.selectedTab = 1; // 'b' selected

        // Move 'a' (index 0) to index 2 -> [b, c, a, d]
        // 'b' is now at index 0. from<to and sel (1) > from (0) and sel (1) <= to (2).
        ReorderLeafTabs(leaf, 0, 2);

        EXPECT_EQ(leaf.panelIds[0], b);
        EXPECT_EQ(leaf.selectedTab, 0);
    }

    TEST(DockNodeTest, ReorderLeafTabs_SelectionShiftsWhenPassedOver_Backward)
    {
        ID a = MakePanelID(1), b = MakePanelID(2), c = MakePanelID(3), d = MakePanelID(4);
        DockNode leaf;
        leaf.isLeaf = true;
        leaf.panelIds = {a, b, c, d};
        leaf.selectedTab = 1; // 'b' selected

        // Move 'd' (index 3) to index 0 -> [d, a, b, c]
        // 'b' moves from index 1 to index 2.
        ReorderLeafTabs(leaf, 3, 0);

        EXPECT_EQ(leaf.panelIds[0], d);
        EXPECT_EQ(leaf.panelIds[2], b);
        EXPECT_EQ(leaf.selectedTab, 2);
    }

    TEST(DockNodeTest, ReorderLeafTabs_NoOpWhenFromEqualsTo)
    {
        ID a = MakePanelID(1), b = MakePanelID(2), c = MakePanelID(3);
        DockNode leaf;
        leaf.isLeaf = true;
        leaf.panelIds = {a, b, c};
        leaf.selectedTab = 2;

        ReorderLeafTabs(leaf, 1, 1);

        EXPECT_EQ(leaf.panelIds[0], a);
        EXPECT_EQ(leaf.panelIds[1], b);
        EXPECT_EQ(leaf.panelIds[2], c);
        EXPECT_EQ(leaf.selectedTab, 2);
    }

    TEST(DockNodeTest, ReorderLeafTabs_OutOfRangeIsNoOp)
    {
        ID a = MakePanelID(1), b = MakePanelID(2);
        DockNode leaf;
        leaf.isLeaf = true;
        leaf.panelIds = {a, b};
        leaf.selectedTab = 0;

        ReorderLeafTabs(leaf, -1, 0);
        ReorderLeafTabs(leaf,  0, 5);
        ReorderLeafTabs(leaf,  5, 0);

        EXPECT_EQ(leaf.panelIds[0], a);
        EXPECT_EQ(leaf.panelIds[1], b);
        EXPECT_EQ(leaf.selectedTab, 0);
    }

    // ---------- CloseLeafTab -----------------------------------------------------

    TEST(DockNodeTest, CloseLeafTab_RemovesIndexAndShifts)
    {
        ID a = MakePanelID(1), b = MakePanelID(2), c = MakePanelID(3);
        DockNode leaf;
        leaf.isLeaf = true;
        leaf.panelIds = {a, b, c};
        leaf.selectedTab = 2; // 'c' selected

        bool empty = CloseLeafTab(leaf, 1);

        EXPECT_FALSE(empty);
        ASSERT_EQ(leaf.panelIds.size(), 2u);
        EXPECT_EQ(leaf.panelIds[0], a);
        EXPECT_EQ(leaf.panelIds[1], c);
        // Selection was at index 2, removed index 1 was before it -> shifts to 1.
        EXPECT_EQ(leaf.selectedTab, 1);
    }

    TEST(DockNodeTest, CloseLeafTab_ClampsSelectionWhenLastRemoved)
    {
        ID a = MakePanelID(1), b = MakePanelID(2);
        DockNode leaf;
        leaf.isLeaf = true;
        leaf.panelIds = {a, b};
        leaf.selectedTab = 1;

        bool empty = CloseLeafTab(leaf, 1);

        EXPECT_FALSE(empty);
        ASSERT_EQ(leaf.panelIds.size(), 1u);
        EXPECT_EQ(leaf.panelIds[0], a);
        EXPECT_EQ(leaf.selectedTab, 0);
    }

    TEST(DockNodeTest, CloseLeafTab_ReturnsTrueWhenEmpty)
    {
        ID a = MakePanelID(1);
        DockNode leaf;
        leaf.isLeaf = true;
        leaf.panelIds = {a};
        leaf.selectedTab = 0;

        bool empty = CloseLeafTab(leaf, 0);

        EXPECT_TRUE(empty);
        EXPECT_TRUE(leaf.panelIds.empty());
        EXPECT_EQ(leaf.selectedTab, 0);
    }

    TEST(DockNodeTest, CloseLeafTab_OutOfRangeIsNoOp)
    {
        ID a = MakePanelID(1), b = MakePanelID(2);
        DockNode leaf;
        leaf.isLeaf = true;
        leaf.panelIds = {a, b};
        leaf.selectedTab = 0;

        CloseLeafTab(leaf, -1);
        CloseLeafTab(leaf, 5);

        EXPECT_EQ(leaf.panelIds.size(), 2u);
        EXPECT_EQ(leaf.selectedTab, 0);
    }

    // ---------- CollapseEmptyLeaves ---------------------------------------------

    TEST(DockNodeTest, CollapseEmptyLeaves_LeafWithPanelsUntouched)
    {
        ID a = MakePanelID(1);
        auto root = MakeLeaf({a});
        DockNode* ptrBefore = root.get();

        CollapseEmptyLeaves(root);

        ASSERT_NE(root, nullptr);
        EXPECT_EQ(root.get(), ptrBefore); // same pointer, in place
        EXPECT_TRUE(root->isLeaf);
        ASSERT_EQ(root->panelIds.size(), 1u);
        EXPECT_EQ(root->panelIds[0], a);
    }

    TEST(DockNodeTest, CollapseEmptyLeaves_SplitWithEmptyFirstPromotesSecond)
    {
        ID a = MakePanelID(1);
        auto root = MakeSplit(DockSplitMode::LeftPinned, 200,
                              MakeLeaf(),           // empty leaf
                              MakeLeaf({a}));

        CollapseEmptyLeaves(root);

        ASSERT_NE(root, nullptr);
        EXPECT_TRUE(root->isLeaf);
        ASSERT_EQ(root->panelIds.size(), 1u);
        EXPECT_EQ(root->panelIds[0], a);
    }

    TEST(DockNodeTest, CollapseEmptyLeaves_SplitWithEmptySecondPromotesFirst)
    {
        ID a = MakePanelID(1);
        auto root = MakeSplit(DockSplitMode::TopPinned, 150,
                              MakeLeaf({a}),
                              MakeLeaf());           // empty leaf

        CollapseEmptyLeaves(root);

        ASSERT_NE(root, nullptr);
        EXPECT_TRUE(root->isLeaf);
        ASSERT_EQ(root->panelIds.size(), 1u);
        EXPECT_EQ(root->panelIds[0], a);
    }

    TEST(DockNodeTest, CollapseEmptyLeaves_NestedCollapse)
    {
        ID a = MakePanelID(1);
        // split(empty, split(empty, leaf{a})) should collapse to leaf{a}
        auto inner = MakeSplit(DockSplitMode::LeftPinned, 100,
                               MakeLeaf(),
                               MakeLeaf({a}));
        auto root = MakeSplit(DockSplitMode::TopPinned, 100,
                              MakeLeaf(),
                              std::move(inner));

        CollapseEmptyLeaves(root);

        ASSERT_NE(root, nullptr);
        EXPECT_TRUE(root->isLeaf);
        ASSERT_EQ(root->panelIds.size(), 1u);
        EXPECT_EQ(root->panelIds[0], a);
    }

    TEST(DockNodeTest, CollapseEmptyLeaves_BothChildrenEmptyBecomesEmptyLeaf)
    {
        auto root = MakeSplit(DockSplitMode::LeftPinned, 100,
                              MakeLeaf(),
                              MakeLeaf());

        CollapseEmptyLeaves(root);

        ASSERT_NE(root, nullptr); // never null
        EXPECT_TRUE(root->isLeaf);
        EXPECT_TRUE(root->panelIds.empty());
    }

    TEST(DockNodeTest, CollapseEmptyLeaves_NullRootBecomesEmptyLeaf)
    {
        std::unique_ptr<DockNode> root; // null

        CollapseEmptyLeaves(root);

        ASSERT_NE(root, nullptr);
        EXPECT_TRUE(root->isLeaf);
        EXPECT_TRUE(root->panelIds.empty());
    }

    // ---------- RemovePanel -----------------------------------------------------

    TEST(DockNodeTest, RemovePanel_FromDeepLeafCollapsesAncestors)
    {
        ID a = MakePanelID(1), b = MakePanelID(2);
        // split(leaf{a}, split(leaf{}, leaf{b}))
        auto root = MakeSplit(DockSplitMode::LeftPinned, 200,
                              MakeLeaf({a}),
                              MakeSplit(DockSplitMode::TopPinned, 100,
                                        MakeLeaf({b}),
                                        MakeLeaf({MakePanelID(99)})));

        // Now structure is: split(leaf{a}, split(leaf{b}, leaf{x})) where x is panel 99
        // Remove panel b -> split(leaf{a}, leaf{x})
        bool removed = RemovePanel(root, b);
        EXPECT_TRUE(removed);
        ASSERT_NE(root, nullptr);
        EXPECT_FALSE(root->isLeaf);
        ASSERT_NE(root->first,  nullptr);
        ASSERT_NE(root->second, nullptr);
        EXPECT_TRUE(root->first->isLeaf);
        EXPECT_TRUE(root->second->isLeaf);
        EXPECT_EQ(root->first->panelIds[0], a);
        EXPECT_EQ(root->second->panelIds.size(), 1u);
    }

    TEST(DockNodeTest, RemovePanel_NotFoundReturnsFalse)
    {
        ID a = MakePanelID(1);
        ID missing = MakePanelID(999);
        auto root = MakeLeaf({a});

        bool removed = RemovePanel(root, missing);

        EXPECT_FALSE(removed);
        ASSERT_NE(root, nullptr);
        ASSERT_EQ(root->panelIds.size(), 1u);
        EXPECT_EQ(root->panelIds[0], a);
    }

    TEST(DockNodeTest, RemovePanel_InvalidIDReturnsFalse)
    {
        ID a = MakePanelID(1);
        auto root = MakeLeaf({a});

        bool removed = RemovePanel(root, ID::Invalid());

        EXPECT_FALSE(removed);
    }

    TEST(DockNodeTest, RemovePanel_LastPanelInTreeLeavesEmptyLeaf)
    {
        ID a = MakePanelID(1);
        auto root = MakeLeaf({a});

        bool removed = RemovePanel(root, a);

        EXPECT_TRUE(removed);
        ASSERT_NE(root, nullptr);
        EXPECT_TRUE(root->isLeaf);
        EXPECT_TRUE(root->panelIds.empty());
    }

    // ---------- FindNode --------------------------------------------------------

    TEST(DockNodeTest, FindNode_ReturnsMatchingRoot)
    {
        auto root = MakeLeaf();
        root->nodeID = MakePanelID(42);

        DockNode* hit = FindNode(root.get(), root->nodeID);
        EXPECT_EQ(hit, root.get());
    }

    TEST(DockNodeTest, FindNode_ReturnsNestedNode)
    {
        auto leafA = MakeLeaf();  leafA->nodeID = MakePanelID(1);
        auto leafB = MakeLeaf();  leafB->nodeID = MakePanelID(2);
        ID leafBID = leafB->nodeID;
        auto root = MakeSplit(DockSplitMode::LeftPinned, 100,
                              std::move(leafA), std::move(leafB));
        root->nodeID = MakePanelID(10);

        DockNode* hit = FindNode(root.get(), leafBID);
        ASSERT_NE(hit, nullptr);
        EXPECT_EQ(hit->nodeID, leafBID);
    }

    TEST(DockNodeTest, FindNode_InvalidIDReturnsNull)
    {
        auto root = MakeLeaf();
        root->nodeID = MakePanelID(1);
        EXPECT_EQ(FindNode(root.get(), ID::Invalid()), nullptr);
    }

    TEST(DockNodeTest, FindNode_MissingIDReturnsNull)
    {
        auto root = MakeLeaf();
        root->nodeID = MakePanelID(1);
        EXPECT_EQ(FindNode(root.get(), MakePanelID(999)), nullptr);
    }

    TEST(DockNodeTest, FindNode_NullRootReturnsNull)
    {
        EXPECT_EQ(FindNode(static_cast<DockNode*>(nullptr), MakePanelID(1)), nullptr);
    }

    // ---------- ComputeSplitSizeRange / ClampSplitSize --------------------------

    TEST(DockNodeTest, ComputeSplitSizeRange_NormalCase)
    {
        DockNode split;
        split.isLeaf = false;
        split.minFirstSize  = 50;
        split.minSecondSize = 80;

        auto range = ComputeSplitSizeRange(split, /*container*/ 400, /*sizer*/ 4);

        // min = 50, max = 400 - 4 - 80 = 316
        EXPECT_EQ(range.min, 50);
        EXPECT_EQ(range.max, 316);
    }

    TEST(DockNodeTest, ComputeSplitSizeRange_DegenerateTooSmall)
    {
        DockNode split;
        split.isLeaf = false;
        split.minFirstSize  = 200;
        split.minSecondSize = 200;

        auto range = ComputeSplitSizeRange(split, /*container*/ 100, /*sizer*/ 4);

        // container can't fit both mins; range collapses to {minFirst, minFirst}
        EXPECT_EQ(range.min, 200);
        EXPECT_EQ(range.max, 200);
    }

    TEST(DockNodeTest, ClampSplitSize_WithinRange)
    {
        DockNode split;
        split.isLeaf = false;
        split.minFirstSize  = 50;
        split.minSecondSize = 80;
        split.splitSize = 150;

        EXPECT_EQ(ClampSplitSize(split, 400, 4), 150);
    }

    TEST(DockNodeTest, ClampSplitSize_BelowMinClampsUp)
    {
        DockNode split;
        split.isLeaf = false;
        split.minFirstSize  = 50;
        split.minSecondSize = 80;
        split.splitSize = 10;

        EXPECT_EQ(ClampSplitSize(split, 400, 4), 50);
    }

    TEST(DockNodeTest, ClampSplitSize_AboveMaxClampsDown)
    {
        DockNode split;
        split.isLeaf = false;
        split.minFirstSize  = 50;
        split.minSecondSize = 80;
        split.splitSize = 10000;

        // max = 400 - 4 - 80 = 316
        EXPECT_EQ(ClampSplitSize(split, 400, 4), 316);
    }

    TEST(DockNodeTest, ComputeSplitSizeRange_SecondPinnedUsesSecondMinAsFloor)
    {
        DockNode split;
        split.isLeaf = false;
        split.mode = DockSplitMode::RightPinned;
        split.minFirstSize  = 50;
        split.minSecondSize = 80;

        auto range = ComputeSplitSizeRange(split, /*container*/ 400, /*sizer*/ 4);

        // splitSize is the SECOND child's size: floor = minSecond, ceiling leaves minFirst for the other side.
        EXPECT_EQ(range.min, 80);
        EXPECT_EQ(range.max, 400 - 4 - 50);
    }

    TEST(DockNodeTest, ClampSplitSize_SecondPinnedAboveMaxClampsDown)
    {
        DockNode split;
        split.isLeaf = false;
        split.mode = DockSplitMode::BottomPinned;
        split.minFirstSize  = 50;
        split.minSecondSize = 80;
        split.splitSize = 10000;

        EXPECT_EQ(ClampSplitSize(split, 400, 4), 346);
    }

    // ---------- AssignMissingNodeIDs --------------------------------------------

    class DockNodeIDTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ASSERT_TRUE(idManager.BeginFrame());
        }
        void TearDown() override
        {
            idManager.EndFrame();
        }
        IDManager idManager;
    };

    TEST_F(DockNodeIDTest, AssignMissingNodeIDs_FillsInvalidIDs)
    {
        auto root = MakeSplit(DockSplitMode::LeftPinned, 200,
                              MakeLeaf(),
                              MakeSplit(DockSplitMode::TopPinned, 100,
                                        MakeLeaf(), MakeLeaf()));

        AssignMissingNodeIDs(root.get(), idManager);

        // All nodes should now have valid IDs
        EXPECT_NE(root->nodeID, ID::Invalid());
        ASSERT_NE(root->first,  nullptr);
        ASSERT_NE(root->second, nullptr);
        EXPECT_NE(root->first->nodeID,  ID::Invalid());
        EXPECT_NE(root->second->nodeID, ID::Invalid());
        EXPECT_NE(root->second->first->nodeID,  ID::Invalid());
        EXPECT_NE(root->second->second->nodeID, ID::Invalid());

        // All IDs should be unique
        EXPECT_NE(root->nodeID, root->first->nodeID);
        EXPECT_NE(root->nodeID, root->second->nodeID);
        EXPECT_NE(root->first->nodeID, root->second->nodeID);
        EXPECT_NE(root->second->first->nodeID, root->second->second->nodeID);

        // Leaves also get a tabBarID distinct from their nodeID: FindElementRefByID
        // returns the first match, so a shared ID would find the wrong element.
        EXPECT_NE(root->first->tabBarID,          ID::Invalid());
        EXPECT_NE(root->first->tabBarID,          root->first->nodeID);
        EXPECT_NE(root->second->first->tabBarID,  ID::Invalid());
        EXPECT_NE(root->second->first->tabBarID,  root->second->first->nodeID);
        EXPECT_NE(root->second->second->tabBarID, ID::Invalid());
        EXPECT_NE(root->second->second->tabBarID, root->second->second->nodeID);
    }

    TEST_F(DockNodeIDTest, AssignMissingNodeIDs_PreservesExistingIDs)
    {
        auto root = MakeLeaf();
        root->nodeID = MakePanelID(12345);
        const ID before = root->nodeID;

        AssignMissingNodeIDs(root.get(), idManager);

        EXPECT_EQ(root->nodeID, before);
    }

    TEST_F(DockNodeIDTest, AssignMissingNodeIDs_IdempotentOnSecondCall)
    {
        auto root = MakeSplit(DockSplitMode::LeftPinned, 100,
                              MakeLeaf(), MakeLeaf());

        AssignMissingNodeIDs(root.get(), idManager);
        const ID rootID      = root->nodeID;
        const ID firstID     = root->first->nodeID;
        const ID secondID    = root->second->nodeID;
        const ID firstTabID  = root->first->tabBarID;
        const ID secondTabID = root->second->tabBarID;

        AssignMissingNodeIDs(root.get(), idManager);

        EXPECT_EQ(root->nodeID,           rootID);
        EXPECT_EQ(root->first->nodeID,    firstID);
        EXPECT_EQ(root->second->nodeID,   secondID);
        EXPECT_EQ(root->first->tabBarID,  firstTabID);
        EXPECT_EQ(root->second->tabBarID, secondTabID);
    }

    TEST_F(DockNodeIDTest, AssignMissingNodeIDs_NullRootIsNoOp)
    {
        // Should not crash.
        AssignMissingNodeIDs(nullptr, idManager);
        SUCCEED();
    }

    // ============================================================================
    // ComputeDropZone
    // ============================================================================

    TEST(DockDropZone, OutsideReturnsNone)
    {
        IntRect rect { 100, 100, 200, 200 };
        EXPECT_EQ(ComputeDropZone(rect, { 50, 150 }),  DockDropZone::None);
        EXPECT_EQ(ComputeDropZone(rect, { 350, 150 }), DockDropZone::None);
        EXPECT_EQ(ComputeDropZone(rect, { 150, 50 }),  DockDropZone::None);
        EXPECT_EQ(ComputeDropZone(rect, { 150, 350 }), DockDropZone::None);
    }

    TEST(DockDropZone, CenterZone)
    {
        IntRect rect { 0, 0, 200, 200 };
        EXPECT_EQ(ComputeDropZone(rect, { 100, 100 }), DockDropZone::Center);
    }

    TEST(DockDropZone, LeftStrip)
    {
        IntRect rect { 0, 0, 200, 200 };
        EXPECT_EQ(ComputeDropZone(rect, { 10, 100 }), DockDropZone::Left);
    }

    TEST(DockDropZone, RightStrip)
    {
        IntRect rect { 0, 0, 200, 200 };
        EXPECT_EQ(ComputeDropZone(rect, { 190, 100 }), DockDropZone::Right);
    }

    TEST(DockDropZone, TopStrip)
    {
        IntRect rect { 0, 0, 200, 200 };
        EXPECT_EQ(ComputeDropZone(rect, { 100, 10 }), DockDropZone::Top);
    }

    TEST(DockDropZone, BottomStrip)
    {
        IntRect rect { 0, 0, 200, 200 };
        EXPECT_EQ(ComputeDropZone(rect, { 100, 190 }), DockDropZone::Bottom);
    }

    // ============================================================================
    // FindNodeOwner
    // ============================================================================

    TEST(DockFindNodeOwner, ReturnsRoot)
    {
        auto root = MakeLeaf({ ID::Invalid() });
        auto* owner = FindNodeOwner(root, root.get());
        ASSERT_NE(owner, nullptr);
        EXPECT_EQ(owner->get(), root.get());
    }

    TEST(DockFindNodeOwner, ReturnsChild)
    {
        auto root = MakeSplit(DockSplitMode::LeftPinned, 100, MakeLeaf(), MakeLeaf());
        DockNode* secondChild = root->second.get();
        auto* owner = FindNodeOwner(root, secondChild);
        ASSERT_NE(owner, nullptr);
        EXPECT_EQ(owner->get(), secondChild);
    }

    TEST(DockFindNodeOwner, ReturnsNullForUnknown)
    {
        auto root = MakeLeaf();
        DockNode detached;
        EXPECT_EQ(FindNodeOwner(root, &detached), nullptr);
    }

    // ============================================================================
    // SplitLeafAtZone
    // ============================================================================

    TEST(DockSplitAtZone, LeftCreatesHorizontalSplit)
    {
        auto root = MakeLeaf({ ID::Invalid() });
        ID panelID;
        panelID.value.integer = 42;
        DockNode* newLeaf = SplitLeafAtZone(root, panelID, DockDropZone::Left);

        ASSERT_NE(newLeaf, nullptr);
        EXPECT_FALSE(root->isLeaf);
        EXPECT_EQ(root->mode, DockSplitMode::LeftPinned);
        EXPECT_EQ(root->first.get(), newLeaf);
        EXPECT_EQ(newLeaf->panelIds.size(), 1u);
        EXPECT_EQ(newLeaf->panelIds[0], panelID);
        EXPECT_TRUE(root->second->isLeaf);
    }

    TEST(DockSplitAtZone, RightCreatesHorizontalSplit)
    {
        auto root = MakeLeaf({ ID::Invalid() });
        ID panelID;
        panelID.value.integer = 42;
        DockNode* newLeaf = SplitLeafAtZone(root, panelID, DockDropZone::Right);

        EXPECT_EQ(root->mode, DockSplitMode::RightPinned);
        EXPECT_EQ(root->second.get(), newLeaf);
        EXPECT_TRUE(root->first->isLeaf);
    }

    TEST(DockSplitAtZone, TopCreatesVerticalSplit)
    {
        auto root = MakeLeaf({ ID::Invalid() });
        ID panelID;
        panelID.value.integer = 42;
        DockNode* newLeaf = SplitLeafAtZone(root, panelID, DockDropZone::Top);

        EXPECT_EQ(root->mode, DockSplitMode::TopPinned);
        EXPECT_EQ(root->first.get(), newLeaf);
    }

    TEST(DockSplitAtZone, BottomCreatesVerticalSplit)
    {
        auto root = MakeLeaf({ ID::Invalid() });
        ID panelID;
        panelID.value.integer = 42;
        DockNode* newLeaf = SplitLeafAtZone(root, panelID, DockDropZone::Bottom);

        EXPECT_EQ(root->mode, DockSplitMode::BottomPinned);
        EXPECT_EQ(root->second.get(), newLeaf);
    }

    // ============================================================================
    // Full docking scenarios: simulates the exact sequence that controls_docking.cpp
    // performs during a drop (TransferPanel equivalent + SplitLeafAtZone +
    // CollapseEmptyLeaves), then verifies the tree structure.
    // ============================================================================

    namespace
    {
        // Simulate what Application::TransferPanel does to a DockNode:
        // remove panelID from the source node's panelIds, fix selectedTab.
        void SimulateTransferPanel(ID panelID, DockNode* sourceNode)
        {
            auto it = std::find(sourceNode->panelIds.begin(), sourceNode->panelIds.end(), panelID);
            if (it == sourceNode->panelIds.end()) return;
            int removedIdx = (int)(it - sourceNode->panelIds.begin());
            sourceNode->panelIds.erase(it);
            int cnt = (int)sourceNode->panelIds.size();
            if (cnt == 0) { sourceNode->selectedTab = 0; return; }
            int sel = sourceNode->selectedTab;
            if (removedIdx < sel) sel -= 1;
            if (sel >= cnt) sel = cnt - 1;
            if (sel < 0) sel = 0;
            sourceNode->selectedTab = sel;
        }

        // Count total leaves in a tree.
        int CountLeaves(const DockNode* n)
        {
            if (!n) return 0;
            if (n->isLeaf) return 1;
            return CountLeaves(n->first.get()) + CountLeaves(n->second.get());
        }

        // Count total panels across all leaves.
        int CountTotalPanels(const DockNode* n)
        {
            if (!n) return 0;
            if (n->isLeaf) return (int)n->panelIds.size();
            return CountTotalPanels(n->first.get()) + CountTotalPanels(n->second.get());
        }

        // Verify no panel ID appears more than once across all leaves.
        void CollectPanelIDs(const DockNode* n, std::vector<ID>& out)
        {
            if (!n) return;
            if (n->isLeaf) {
                for (auto id : n->panelIds) out.push_back(id);
                return;
            }
            CollectPanelIDs(n->first.get(), out);
            CollectPanelIDs(n->second.get(), out);
        }

        void ExpectNoDuplicatePanels(const DockNode* root)
        {
            std::vector<ID> ids;
            CollectPanelIDs(root, ids);
            std::sort(ids.begin(), ids.end());
            for (size_t i = 1; i < ids.size(); i++) {
                EXPECT_NE(ids[i-1], ids[i]) << "Duplicate panel ID found";
            }
        }
    }

    // Scenario: HSplit(left_leaf[2 panels], right_leaf[6 panels])
    // Action: drag panel from right_leaf, drop on LEFT edge of right_leaf (self-split)
    // Expected: HSplit(left_leaf[2], HSplit(new_leaf[1], right_leaf[5]))
    TEST(DockScenario, SelfSplitLeftEdge)
    {
        ID p1 = MakePanelID(1), p2 = MakePanelID(2);
        ID p3 = MakePanelID(3), p4 = MakePanelID(4), p5 = MakePanelID(5);
        ID p6 = MakePanelID(6), p7 = MakePanelID(7), p8 = MakePanelID(8);

        auto root = MakeSplit(DockSplitMode::LeftPinned, 220,
                              MakeLeaf({p1, p2}),
                              MakeLeaf({p3, p4, p5, p6, p7, p8}));

        DockNode* leftLeaf = root->first.get();
        DockNode* rightLeaf = root->second.get();

        // Drag p3 from rightLeaf, drop on LEFT edge of rightLeaf
        ID draggedPanel = p3;
        SimulateTransferPanel(draggedPanel, rightLeaf);

        // rightLeaf should now have 5 panels
        EXPECT_EQ(rightLeaf->panelIds.size(), 5u);
        EXPECT_EQ(std::find(rightLeaf->panelIds.begin(), rightLeaf->panelIds.end(), p3),
                  rightLeaf->panelIds.end()) << "p3 should be removed from right leaf";

        // Split right leaf at Left zone
        auto* ownerPtr = FindNodeOwner(root, rightLeaf);
        ASSERT_NE(ownerPtr, nullptr);
        EXPECT_EQ(ownerPtr, &root->second) << "Owner should be root->second";

        DockNode* newLeaf = SplitLeafAtZone(*ownerPtr, draggedPanel, DockDropZone::Left);
        ASSERT_NE(newLeaf, nullptr);

        // root->second is now a split, NOT the old rightLeaf
        EXPECT_FALSE(root->second->isLeaf);
        EXPECT_EQ(root->second->mode, DockSplitMode::LeftPinned);

        // The inner split's first child is the new leaf (zone=Left → newIsFirst)
        EXPECT_EQ(root->second->first.get(), newLeaf);
        EXPECT_EQ(newLeaf->panelIds.size(), 1u);
        EXPECT_EQ(newLeaf->panelIds[0], draggedPanel);

        // The inner split's second child is the OLD rightLeaf with 5 panels
        EXPECT_TRUE(root->second->second->isLeaf);
        EXPECT_EQ(root->second->second.get(), rightLeaf);
        EXPECT_EQ(rightLeaf->panelIds.size(), 5u);

        // Left leaf is untouched
        EXPECT_EQ(leftLeaf->panelIds.size(), 2u);
        EXPECT_EQ(root->first.get(), leftLeaf);

        // Total: 3 leaves, 8 panels, no duplicates
        EXPECT_EQ(CountLeaves(root.get()), 3);
        EXPECT_EQ(CountTotalPanels(root.get()), 8);
        ExpectNoDuplicatePanels(root.get());

        // CollapseEmptyLeaves should be a no-op (no empty leaves)
        CollapseEmptyLeaves(root);
        EXPECT_EQ(CountLeaves(root.get()), 3);
        EXPECT_EQ(CountTotalPanels(root.get()), 8);
    }

    // Scenario: same setup, but drop on RIGHT edge of left_leaf (cross-leaf)
    TEST(DockScenario, CrossLeafSplitRightEdge)
    {
        ID p1 = MakePanelID(1), p2 = MakePanelID(2);
        ID p3 = MakePanelID(3), p4 = MakePanelID(4), p5 = MakePanelID(5);

        auto root = MakeSplit(DockSplitMode::LeftPinned, 220,
                              MakeLeaf({p1, p2}),
                              MakeLeaf({p3, p4, p5}));

        DockNode* leftLeaf = root->first.get();
        DockNode* rightLeaf = root->second.get();

        // Drag p3 from rightLeaf, drop on RIGHT edge of leftLeaf
        SimulateTransferPanel(p3, rightLeaf);
        EXPECT_EQ(rightLeaf->panelIds.size(), 2u);

        auto* ownerPtr = FindNodeOwner(root, leftLeaf);
        ASSERT_NE(ownerPtr, nullptr);
        EXPECT_EQ(ownerPtr, &root->first);

        DockNode* newLeaf = SplitLeafAtZone(*ownerPtr, p3, DockDropZone::Right);
        ASSERT_NE(newLeaf, nullptr);

        // root->first is now a split
        EXPECT_FALSE(root->first->isLeaf);
        EXPECT_EQ(root->first->mode, DockSplitMode::RightPinned);

        // Right zone → existing is first, new is second
        EXPECT_EQ(root->first->first.get(), leftLeaf);
        EXPECT_EQ(root->first->second.get(), newLeaf);
        EXPECT_EQ(leftLeaf->panelIds.size(), 2u);
        EXPECT_EQ(newLeaf->panelIds.size(), 1u);
        EXPECT_EQ(newLeaf->panelIds[0], p3);

        // Right side untouched
        EXPECT_EQ(root->second.get(), rightLeaf);
        EXPECT_EQ(rightLeaf->panelIds.size(), 2u);

        EXPECT_EQ(CountLeaves(root.get()), 3);
        EXPECT_EQ(CountTotalPanels(root.get()), 5);
        ExpectNoDuplicatePanels(root.get());
    }

    // Scenario: drag the LAST panel from a leaf → leaf becomes empty → collapse
    TEST(DockScenario, DragLastPanelCollapsesLeaf)
    {
        ID p1 = MakePanelID(1), p2 = MakePanelID(2), p3 = MakePanelID(3);

        auto root = MakeSplit(DockSplitMode::LeftPinned, 220,
                              MakeLeaf({p1}),
                              MakeLeaf({p2, p3}));

        DockNode* leftLeaf = root->first.get();
        DockNode* rightLeaf = root->second.get();

        // Drag p1 (the only panel in leftLeaf) to right leaf's tab bar
        SimulateTransferPanel(p1, leftLeaf);
        EXPECT_TRUE(leftLeaf->panelIds.empty());
        rightLeaf->panelIds.push_back(p1);

        // Collapse should promote rightLeaf as root
        CollapseEmptyLeaves(root);
        EXPECT_TRUE(root->isLeaf);
        EXPECT_EQ(root->panelIds.size(), 3u);
        EXPECT_EQ(CountTotalPanels(root.get()), 3);
    }

    // Scenario: self-split on a leaf with only 1 panel → source becomes empty → collapse
    TEST(DockScenario, SelfSplitSinglePanelCollapsesToNewLeaf)
    {
        ID p1 = MakePanelID(1);

        auto root = MakeLeaf({p1});
        DockNode* leaf = root.get();

        SimulateTransferPanel(p1, leaf);
        EXPECT_TRUE(leaf->panelIds.empty());

        auto* ownerPtr = FindNodeOwner(root, leaf);
        ASSERT_NE(ownerPtr, nullptr);
        SplitLeafAtZone(*ownerPtr, p1, DockDropZone::Bottom);

        // Bottom zone: existing is first, new is second →
        // first=empty_leaf, second=new_leaf(p1)
        EXPECT_FALSE(root->isLeaf);
        EXPECT_EQ(root->second->panelIds.size(), 1u);
        EXPECT_TRUE(root->first->panelIds.empty());

        // Collapse: empty first child → promote second
        CollapseEmptyLeaves(root);
        EXPECT_TRUE(root->isLeaf);
        EXPECT_EQ(root->panelIds.size(), 1u);
        EXPECT_EQ(root->panelIds[0], p1);
    }

    // Scenario: nested split, then split again — verify deep nesting works
    TEST(DockScenario, DoubleNestedSplit)
    {
        ID p1 = MakePanelID(1), p2 = MakePanelID(2), p3 = MakePanelID(3);

        auto root = MakeLeaf({p1, p2, p3});

        // First split: drop p1 on Left
        SimulateTransferPanel(p1, root.get());
        SplitLeafAtZone(root, p1, DockDropZone::Left);
        EXPECT_EQ(CountLeaves(root.get()), 2);
        EXPECT_EQ(CountTotalPanels(root.get()), 3);

        // Now root is HSplit(leaf[p1], leaf[p2,p3])
        // Second split: drop p2 on Top of the right leaf
        DockNode* rightLeaf = root->second.get();
        ASSERT_TRUE(rightLeaf->isLeaf);
        ASSERT_EQ(rightLeaf->panelIds.size(), 2u);

        SimulateTransferPanel(p2, rightLeaf);
        auto* ownerPtr = FindNodeOwner(root, rightLeaf);
        ASSERT_NE(ownerPtr, nullptr);
        SplitLeafAtZone(*ownerPtr, p2, DockDropZone::Top);

        // Now: HSplit(leaf[p1], VSplit(leaf[p2], leaf[p3]))
        EXPECT_EQ(CountLeaves(root.get()), 3);
        EXPECT_EQ(CountTotalPanels(root.get()), 3);
        ExpectNoDuplicatePanels(root.get());

        EXPECT_FALSE(root->second->isLeaf);
        EXPECT_EQ(root->second->mode, DockSplitMode::TopPinned);
        EXPECT_EQ(root->second->first->panelIds.size(), 1u);
        EXPECT_EQ(root->second->first->panelIds[0], p2);
        EXPECT_EQ(root->second->second->panelIds.size(), 1u);
        EXPECT_EQ(root->second->second->panelIds[0], p3);
    }

    // Scenario: FindNodeOwner returns correct owner for deeply nested node
    TEST(DockScenario, FindNodeOwnerDeepNesting)
    {
        ID p1 = MakePanelID(1), p2 = MakePanelID(2), p3 = MakePanelID(3);

        auto root = MakeSplit(DockSplitMode::LeftPinned, 200,
            MakeLeaf({p1}),
            MakeSplit(DockSplitMode::TopPinned, 150,
                MakeLeaf({p2}),
                MakeLeaf({p3})));

        DockNode* deepLeaf = root->second->second.get();
        auto* owner = FindNodeOwner(root, deepLeaf);
        ASSERT_NE(owner, nullptr);
        EXPECT_EQ(owner->get(), deepLeaf);
        EXPECT_EQ(owner, &root->second->second);
    }

    // Scenario: SplitLeafAtZone preserves splitSize parameter
    TEST(DockSplitAtZone, PreservesSplitSize)
    {
        ID p1 = MakePanelID(1), p2 = MakePanelID(2);
        auto root = MakeLeaf({p1});

        SplitLeafAtZone(root, p2, DockDropZone::Left, 150);
        EXPECT_EQ(root->splitSize, 150);

        // Default splitSize
        auto root2 = MakeLeaf({p1});
        SplitLeafAtZone(root2, p2, DockDropZone::Right);
        EXPECT_EQ(root2->splitSize, 200); // default
    }

    // ---- Layout save / restore ----

    namespace
    {
        struct PanelNames
        {
            std::map<std::string, ID> ids;
            std::function<std::string(ID)> keyOf() const {
                return [this](ID id) {
                    for (const auto& [name, value] : ids) if (value == id) return name;
                    return std::string("?");
                };
            }
            std::function<std::optional<ID>(std::string_view)> idOf() const {
                return [this](std::string_view name) -> std::optional<ID> {
                    const auto found = ids.find(std::string(name));
                    if (found == ids.end()) return std::nullopt;
                    return found->second;
                };
            }
        };

        PanelNames Names()
        {
            PanelNames names;
            int seed = 1;
            for (const char* name : {"Hierarchy", "Viewport", "Inspector", "Project", "Diagnostics", "Tasks"})
                names.ids[name] = MakePanelID(seed++);
            return names;
        }

        std::unique_ptr<DockNode> SampleTree(const PanelNames& n)
        {
            return MakeSplit(DockSplitMode::BottomPinned, 260,
                MakeSplit(DockSplitMode::LeftPinned, 300, MakeLeaf({n.ids.at("Hierarchy")}),
                    MakeSplit(DockSplitMode::RightPinned, 360, MakeLeaf({n.ids.at("Viewport")}), MakeLeaf({n.ids.at("Inspector")}))),
                MakeSplit(DockSplitMode::LeftPinned, 520, MakeLeaf({n.ids.at("Project")}),
                    MakeLeaf({n.ids.at("Diagnostics"), n.ids.at("Tasks")})), 240, 140);
        }

        bool SameTree(const DockNode* a, const DockNode* b)
        {
            if (a == nullptr || b == nullptr) return a == b;
            if (a->isLeaf != b->isLeaf) return false;
            if (a->isLeaf) return a->panelIds == b->panelIds && a->selectedTab == b->selectedTab;
            return a->mode == b->mode && a->splitSize == b->splitSize && a->minFirstSize == b->minFirstSize
                && a->minSecondSize == b->minSecondSize && SameTree(a->first.get(), b->first.get())
                && SameTree(a->second.get(), b->second.get());
        }
    }

    TEST(DockLayoutIO, RoundTripsATree)
    {
        auto names = Names();
        auto tree = SampleTree(names);
        tree->second->second->selectedTab = 1;
        const std::string text = SerializeDockTree(*tree, names.keyOf());
        const auto restored = ParseDockTree(text, names.idOf());
        ASSERT_NE(restored, nullptr);
        EXPECT_TRUE(SameTree(tree.get(), restored.get()));
    }

    TEST(DockLayoutIO, DropsPanelsThatNoLongerExistAndCollapses)
    {
        auto names = Names();
        const std::string text = SerializeDockTree(*SampleTree(names), names.keyOf());
        names.ids.erase("Viewport");
        const auto restored = ParseDockTree(text, names.idOf());
        ASSERT_NE(restored, nullptr);
        std::vector<ID> all;
        for (const auto& [name, id] : names.ids) all.push_back(id);
        EXPECT_TRUE(UnplacedPanels(*restored, all).empty());   // every surviving panel is still placed
        EXPECT_FALSE(restored->first->first->isLeaf == false && restored->first->first->first == nullptr);
    }

    TEST(DockLayoutIO, ReportsPanelsAddedSinceTheLayoutWasSaved)
    {
        auto names = Names();
        auto small = MakeSplit(DockSplitMode::LeftPinned, 200, MakeLeaf({names.ids.at("Hierarchy")}), MakeLeaf({names.ids.at("Viewport")}));
        const std::string text = SerializeDockTree(*small, names.keyOf());
        const auto restored = ParseDockTree(text, names.idOf());
        ASSERT_NE(restored, nullptr);
        std::vector<ID> all;
        for (const auto& [name, id] : names.ids) all.push_back(id);
        EXPECT_EQ(UnplacedPanels(*restored, all).size(), 4u);
    }

    TEST(DockLayoutIO, RepeatedKeyIsPlacedOnce)
    {
        auto names = Names();
        const auto restored = ParseDockTree(R"({"tabs":["Viewport","Viewport","Tasks"],"selected":2})", names.idOf());
        ASSERT_NE(restored, nullptr);
        ASSERT_TRUE(restored->isLeaf);
        EXPECT_EQ(restored->panelIds.size(), 2u);
        EXPECT_EQ(restored->selectedTab, 1);   // clamped into the surviving tabs
    }

    // The saved selection names a panel, not a slot: dropping a missing panel
    // in front of it must not shift the selection onto its neighbour.
    TEST(DockLayoutIO, SelectionFollowsThePanelWhenAnEarlierTabIsMissing)
    {
        auto names = Names();
        const auto restored = ParseDockTree(R"({"tabs":["Gone","Viewport","Tasks"],"selected":1})", names.idOf());
        ASSERT_NE(restored, nullptr);
        ASSERT_EQ(restored->panelIds.size(), 2u);
        EXPECT_EQ(restored->panelIds[size_t(restored->selectedTab)], names.ids.at("Viewport"));
    }

    // A missing selected panel behaves like closing that tab: the next tab, or
    // the previous one when it was last.
    TEST(DockLayoutIO, MissingSelectedPanelSelectsItsNeighbour)
    {
        auto names = Names();
        auto next = ParseDockTree(R"({"tabs":["Viewport","Gone","Tasks"],"selected":1})", names.idOf());
        ASSERT_NE(next, nullptr);
        EXPECT_EQ(next->panelIds[size_t(next->selectedTab)], names.ids.at("Tasks"));

        auto prev = ParseDockTree(R"({"tabs":["Viewport","Tasks","Gone"],"selected":2})", names.idOf());
        ASSERT_NE(prev, nullptr);
        EXPECT_EQ(prev->panelIds[size_t(prev->selectedTab)], names.ids.at("Tasks"));
    }

    TEST(DockLayoutIO, RejectsMalformedAndEmptyInput)
    {
        auto names = Names();
        for (const char* bad : {"", "{", "[]", R"({"tabs":["Viewport"]})", R"({"tabs":["Nope"],"selected":0})",
                 R"({"split":"Sideways","size":1,"minFirst":1,"minSecond":1,"first":{"tabs":["Viewport"],"selected":0},"second":{"tabs":["Tasks"],"selected":0}})",
                 R"({"tabs":["Viewport"],"selected":0} trailing)"})
            EXPECT_EQ(ParseDockTree(bad, names.idOf()), nullptr) << bad;
        std::string deep;
        for (int i = 0; i < 40; ++i) deep += R"({"split":"LeftPinned","size":1,"minFirst":1,"minSecond":1,"first":)";
        EXPECT_EQ(ParseDockTree(deep, names.idOf()), nullptr);
    }

    TEST(DockLayoutIO, MutatedTextNeverCrashes)
    {
        auto names = Names();
        const std::string text = SerializeDockTree(*SampleTree(names), names.keyOf());
        std::mt19937 rng(5);
        int accepted = 0;
        for (int round = 0; round < 3000; ++round)
        {
            std::string copy = text;
            for (int e = 0, edits = 1 + static_cast<int>(rng() % 4); e < edits && !copy.empty(); ++e)
            {
                const size_t at = rng() % copy.size();
                if (rng() % 3 == 0) copy[at] = static_cast<char>(rng());
                else if (rng() % 2 == 0) copy.erase(at, 1 + rng() % 6);
                else copy.insert(at, 1, "{}[]\":,0-"[rng() % 9]);
            }
            if (ParseDockTree(copy, names.idOf())) ++accepted;
        }
        EXPECT_LT(accepted, 3000);
    }


    TEST(DockLayoutIO, SignWithoutDigitsIsRejected)
    {
        auto names = Names();
        for (const char* bad : { R"({"tabs":["Viewport"],"selected":-})",
                                 R"({"split":"LeftPinned","size":-,"minFirst":1,"minSecond":1,"first":{"tabs":["Viewport"],"selected":0},"second":{"tabs":["Tasks"],"selected":0}})" })
        {
            std::unique_ptr<DockNode> restored;
            EXPECT_NO_THROW(restored = ParseDockTree(bad, names.idOf())) << bad;
            EXPECT_EQ(restored, nullptr) << bad;
        }
    }


    // ---- Tab drop position ----

    // Two wrapped rows of tabs, 24 high with a 4 px gap: [0 1 2] at y 0, [3 4] at y 28.
    static interstice::DockNode WrappedTabs()
    {
        interstice::DockNode leaf;
        leaf.cachedTabRects = { { 0, 0, 80, 24 }, { 82, 0, 80, 24 }, { 164, 0, 80, 24 },
                                { 0, 28, 80, 24 }, { 82, 28, 80, 24 } };
        return leaf;
    }

    TEST(TabInsertion, OnARowItGoesBetweenTheTabsAroundTheCursor)
    {
        const auto leaf = WrappedTabs();
        EXPECT_EQ(interstice::TabInsertionIndex(leaf, { 10, 10 }), 0);
        EXPECT_EQ(interstice::TabInsertionIndex(leaf, { 100, 10 }), 1);
        EXPECT_EQ(interstice::TabInsertionIndex(leaf, { 230, 10 }), 3);
        EXPECT_EQ(interstice::TabInsertionIndex(leaf, { 100, 40 }), 4);
    }

    // The gap between wrapped rows belongs to the nearest row, not to "after the row
    // above" (which put the drop at the far right of the bar).
    TEST(TabInsertion, TheGapBetweenRowsCountsAsTheNearestRow)
    {
        const auto leaf = WrappedTabs();
        EXPECT_EQ(interstice::TabInsertionIndex(leaf, { 10, 25 }), 0) << "just under row 1";
        EXPECT_EQ(interstice::TabInsertionIndex(leaf, { 10, 27 }), 3) << "just over row 2";
    }

    // Above the first row (the tab bar's top padding) is the first row: dropping over
    // the second tab's top edge goes next to it, not before the first tab.
    TEST(TabInsertion, AboveTheFirstRowCountsAsTheFirstRow)
    {
        const auto leaf = WrappedTabs();
        EXPECT_EQ(interstice::TabInsertionIndex(leaf, { 100, -3 }), 1);
        EXPECT_EQ(interstice::TabInsertionIndex(leaf, { 150, -3 }), 2);
    }

    // Below the last row is the last row: under a tab it goes next to that tab (not to
    // the far right), and past the last tab after it.
    TEST(TabInsertion, BelowTheLastRowCountsAsTheLastRow)
    {
        const auto leaf = WrappedTabs();
        EXPECT_EQ(interstice::TabInsertionIndex(leaf, { 10, 60 }),  3);
        EXPECT_EQ(interstice::TabInsertionIndex(leaf, { 100, 60 }), 4);
        EXPECT_EQ(interstice::TabInsertionIndex(leaf, { 300, 60 }), 5);
    }
}
