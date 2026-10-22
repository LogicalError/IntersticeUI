// Unit tests for the flat-list tree helpers (gui_tree_utils.h) used by the
// hierarchy and tree-table panels.

#include <gtest/gtest.h>

#include <gui_controls/gui_tree_utils.h>

#include <string>
#include <vector>

namespace GUI
{
    namespace {

        struct TestRow
        {
            std::string name;
            int         depth            = 0;
            int         skipCount        = 0;
            bool        isOpen           = false;
            uint32_t    ancestorLineMask = 0;
            bool        isLastSibling    = false;
        };

        using DropPos = GUI::TreeDropPosition;

        // A(0)
        //   B(1)
        //     C(2)
        //     D(2)
        //   E(1)
        // F(0)
        std::vector<TestRow> MakeTree()
        {
            return {
                { "A", 0 }, { "B", 1 }, { "C", 2 },
                { "D", 2 }, { "E", 1 }, { "F", 0 },
            };
        }

        std::vector<std::string> Names(const std::vector<TestRow>& rows)
        {
            std::vector<std::string> out;
            for (const auto& r : rows) out.push_back(r.name);
            return out;
        }

        TEST(TreeUtilsTest, RebuildIndexComputesSkipCounts)
        {
            auto rows = MakeTree();
            TreeUtils::RebuildTreeIndex(rows);
            EXPECT_EQ(rows[0].skipCount, 4); // A spans B,C,D,E
            EXPECT_EQ(rows[1].skipCount, 2); // B spans C,D
            EXPECT_EQ(rows[2].skipCount, 0);
            EXPECT_EQ(rows[3].skipCount, 0);
            EXPECT_EQ(rows[4].skipCount, 0);
            EXPECT_EQ(rows[5].skipCount, 0);
        }

        TEST(TreeUtilsTest, RebuildIndexComputesAncestorLineMask)
        {
            auto rows = MakeTree();
            TreeUtils::RebuildTreeIndex(rows);
            // C and D draw a depth-0 guide column because their depth-1
            // ancestor (B) has a later sibling (E).
            EXPECT_EQ(rows[2].ancestorLineMask, 1u);
            EXPECT_EQ(rows[3].ancestorLineMask, 1u);
            // Depth-0/1 rows have no ancestor guide columns here.
            EXPECT_EQ(rows[0].ancestorLineMask, 0u);
            EXPECT_EQ(rows[1].ancestorLineMask, 0u);
            EXPECT_EQ(rows[4].ancestorLineMask, 0u);
        }

        TEST(TreeUtilsTest, RebuildIndexMarksLastSiblings)
        {
            auto rows = MakeTree();
            TreeUtils::RebuildTreeIndex(rows);
            EXPECT_FALSE(rows[0].isLastSibling); // A has sibling F
            EXPECT_FALSE(rows[1].isLastSibling); // B has sibling E
            EXPECT_FALSE(rows[2].isLastSibling); // C has sibling D
            EXPECT_TRUE(rows[3].isLastSibling);  // D
            EXPECT_TRUE(rows[4].isLastSibling);  // E
            EXPECT_TRUE(rows[5].isLastSibling);  // F
        }

        TEST(TreeUtilsTest, ReorderBeforeInsertsAtTarget)
        {
            auto rows = MakeTree();
            TreeUtils::RebuildTreeIndex(rows);
            // Move F before B → F becomes A's first child position-wise but
            // keeps B's depth (reparent to depth 1).
            const int newIdx = TreeUtils::ReorderTreeRow(rows, 5, 1, DropPos::Before);
            EXPECT_EQ(newIdx, 1);
            EXPECT_EQ(Names(rows), (std::vector<std::string>{ "A", "F", "B", "C", "D", "E" }));
            EXPECT_EQ(rows[1].depth, 1);
        }

        TEST(TreeUtilsTest, ReorderAfterSkipsTargetSubtree)
        {
            auto rows = MakeTree();
            TreeUtils::RebuildTreeIndex(rows);
            // Move F after B → lands past B's whole subtree (C,D), before E.
            const int newIdx = TreeUtils::ReorderTreeRow(rows, 5, 1, DropPos::After);
            EXPECT_EQ(newIdx, 4);
            EXPECT_EQ(Names(rows), (std::vector<std::string>{ "A", "B", "C", "D", "F", "E" }));
            EXPECT_EQ(rows[4].depth, 1);
        }

        TEST(TreeUtilsTest, ReorderInsideBecomesFirstChild)
        {
            auto rows = MakeTree();
            TreeUtils::RebuildTreeIndex(rows);
            const int newIdx = TreeUtils::ReorderTreeRow(rows, 5, 1, DropPos::Inside);
            EXPECT_EQ(newIdx, 2);
            EXPECT_EQ(Names(rows), (std::vector<std::string>{ "A", "B", "F", "C", "D", "E" }));
            EXPECT_EQ(rows[2].depth, 2);
        }

        TEST(TreeUtilsTest, ReorderInsideLastBecomesLastChild)
        {
            auto rows = MakeTree();
            TreeUtils::RebuildTreeIndex(rows);
            const int newIdx = TreeUtils::ReorderTreeRow(rows, 5, 1, DropPos::InsideLast);
            EXPECT_EQ(newIdx, 4);
            EXPECT_EQ(Names(rows), (std::vector<std::string>{ "A", "B", "C", "D", "F", "E" }));
            EXPECT_EQ(rows[4].depth, 2);
        }

        TEST(TreeUtilsTest, ReorderInsideOpensParent)
        {
            auto rows = MakeTree();
            TreeUtils::RebuildTreeIndex(rows);
            ASSERT_FALSE(rows[1].isOpen);
            TreeUtils::ReorderTreeRow(rows, 5, 1, DropPos::Inside);
            // B (still at index 1) was opened so the drop is visible.
            EXPECT_TRUE(rows[1].isOpen);
        }

        TEST(TreeUtilsTest, ReorderRejectsSelfAndDescendantDrop)
        {
            auto rows = MakeTree();
            TreeUtils::RebuildTreeIndex(rows);
            const auto before = Names(rows);
            EXPECT_EQ(TreeUtils::ReorderTreeRow(rows, 1, 1, DropPos::Before), -1); // self
            EXPECT_EQ(TreeUtils::ReorderTreeRow(rows, 1, 2, DropPos::Before), -1); // own child
            EXPECT_EQ(TreeUtils::ReorderTreeRow(rows, 0, 4, DropPos::After),  -1); // own descendant
            EXPECT_EQ(TreeUtils::ReorderTreeRow(rows, -1, 2, DropPos::Before), -1);
            EXPECT_EQ(TreeUtils::ReorderTreeRow(rows, 1, 99, DropPos::Before), -1);
            EXPECT_EQ(Names(rows), before);
        }

        TEST(TreeUtilsTest, ReorderReparentShiftsSubtreeDepths)
        {
            auto rows = MakeTree();
            TreeUtils::RebuildTreeIndex(rows);
            // Move B's subtree (B,C,D) after F → B reparents to depth 0 and
            // its children shift by the same delta.
            const int newIdx = TreeUtils::ReorderTreeRow(rows, 1, 5, DropPos::After);
            EXPECT_EQ(newIdx, 3);
            EXPECT_EQ(Names(rows), (std::vector<std::string>{ "A", "E", "F", "B", "C", "D" }));
            EXPECT_EQ(rows[3].depth, 0);
            EXPECT_EQ(rows[4].depth, 1);
            EXPECT_EQ(rows[5].depth, 1);
        }

        TEST(TreeUtilsTest, ReorderReturnsAdjustedIndexWhenMovingForward)
        {
            auto rows = MakeTree();
            TreeUtils::RebuildTreeIndex(rows);
            // Moving forward past the erased slice: insertAt is adjusted by
            // the slice size, so the returned index addresses the moved row.
            const int newIdx = TreeUtils::ReorderTreeRow(rows, 1, 5, DropPos::Before);
            ASSERT_GE(newIdx, 0);
            EXPECT_EQ(rows[size_t(newIdx)].name, "B");
        }

    } // namespace
}
