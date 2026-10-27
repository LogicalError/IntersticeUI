#include <gtest/gtest.h>

#include <gui_controls/gui_table_helpers.h>
#include <gui_core/gui_base_types.h>
#include <gui_core/style/gui_style.h>

#include <vector>

namespace tachyon
{
    using TableHelpers::VirtualRowRange;

    // ===========================================================================
    // ComputeVirtualRowRange - the visibility window used by VirtualTable to
    // decide which rows to emit this frame given the current scroll offset.
    // ===========================================================================

    class VirtualRowRangeTest : public ::testing::Test {};

    TEST_F(VirtualRowRangeTest, EmptyDatasetProducesEmptyRange)
    {
        auto r = TableHelpers::ComputeVirtualRowRange(/*scrollY*/0,
            /*viewportH*/400,
            /*rowHeight*/20,
            /*totalRowCount*/0,
            /*bufferRows*/5);
        EXPECT_EQ(r.firstVisible, 0);
        EXPECT_EQ(r.lastVisible, 0);
    }

    TEST_F(VirtualRowRangeTest, ZeroRowHeightYieldsEmptyRange)
    {
        // Guard: a 0 rowHeight would otherwise divide-by-zero. The helper must
        // return an empty range instead of exploding.
        auto r = TableHelpers::ComputeVirtualRowRange(0, 400, 0, 1000, 5);
        EXPECT_EQ(r.firstVisible, 0);
        EXPECT_EQ(r.lastVisible, 0);
    }

    TEST_F(VirtualRowRangeTest, FirstFrameZeroViewportStillReturnsBuffer)
    {
        // On the very first frame the table's viewport height hasn't been
        // sampled yet (= 0). The formula should still emit ~kBufferRows worth
        // of rows so layout can size the container.
        const int32_t scrollY = 0;
        const int32_t viewportH = 0;
        const int32_t rowHeight = 20;
        const int totalRows = 50000;
        const int buffer = 5;

        auto r = TableHelpers::ComputeVirtualRowRange(scrollY, viewportH, rowHeight,
            totalRows, buffer);
        EXPECT_EQ(r.firstVisible, 0);
        // (0 + 0)/20 + 1 + 5 = 6.
        EXPECT_EQ(r.lastVisible, 6);
    }

    TEST_F(VirtualRowRangeTest, TopOfListNoScrollRendersLeadingSlice)
    {
        // scrollY=0, viewport=200, rowHeight=20 -> visible rows 0..10.
        // With a 5-row buffer below: last = 200/20 + 1 + 5 = 16.
        // first clamped to 0.
        auto r = TableHelpers::ComputeVirtualRowRange(0, 200, 20, 50000, 5);
        EXPECT_EQ(r.firstVisible, 0);
        EXPECT_EQ(r.lastVisible, 16);
    }

    TEST_F(VirtualRowRangeTest, MidListComputesBothBuffers)
    {
        // scrollY=1000 (row 50), viewport=200 (10 rows), buffer=5.
        // first = 1000/20 - 5 = 45
        // last  = (1000+200)/20 + 1 + 5 = 66
        auto r = TableHelpers::ComputeVirtualRowRange(1000, 200, 20, 50000, 5);
        EXPECT_EQ(r.firstVisible, 45);
        EXPECT_EQ(r.lastVisible, 66);
    }

    TEST_F(VirtualRowRangeTest, BottomOfListClampsLastVisible)
    {
        // 100 rows, each 20px = 2000px total. Viewport 200px.
        // Scrolled to the end: scrollY = 2000 - 200 = 1800.
        // first = 1800/20 - 5 = 85
        // last  = (1800+200)/20 + 1 + 5 = 106, clamped to totalRowCount=100.
        auto r = TableHelpers::ComputeVirtualRowRange(1800, 200, 20, 100, 5);
        EXPECT_EQ(r.firstVisible, 85);
        EXPECT_EQ(r.lastVisible, 100);
    }

    TEST_F(VirtualRowRangeTest, OverScrollClampsToValidBounds)
    {
        // If something asks for a scroll offset past the end, both bounds
        // should clamp to totalRowCount - no rows emitted, no negative counts.
        auto r = TableHelpers::ComputeVirtualRowRange(/*scrollY*/1'000'000,
            /*viewportH*/400,
            /*rowHeight*/20,
            /*totalRowCount*/100,
            /*bufferRows*/5);
        EXPECT_EQ(r.firstVisible, 100);
        EXPECT_EQ(r.lastVisible, 100);
    }

    TEST_F(VirtualRowRangeTest, NegativeScrollYTreatedAsStart)
    {
        // Scroll offsets below 0 shouldn't be reachable, but the helper must
        // stay safe - first is clamped to 0, last to a small leading slice.
        auto r = TableHelpers::ComputeVirtualRowRange(-500, 200, 20, 1000, 5);
        EXPECT_EQ(r.firstVisible, 0);
        EXPECT_GE(r.lastVisible, 0);
        EXPECT_LE(r.lastVisible, 1000);
    }

    TEST_F(VirtualRowRangeTest, NoBufferRowsProducesTightSlice)
    {
        // Buffer = 0: exactly the viewport slice, inclusive on both ends.
        // scrollY=100, viewport=80, rowHeight=20: visible rows 5..10.
        auto r = TableHelpers::ComputeVirtualRowRange(100, 80, 20, 1000, 0);
        EXPECT_EQ(r.firstVisible, 5);
        EXPECT_EQ(r.lastVisible, 10);  // (100+80)/20 + 1 = 10
    }

    TEST_F(VirtualRowRangeTest, LastVisibleNeverLessThanFirst)
    {
        // Invariant: firstVisible <= lastVisible, always.
        for (int32_t scroll : {0, 100, 10'000, 999'999})
            for (int32_t vh : {0, 50, 400})
                for (int32_t rh : {1, 10, 28})
                    for (int total : {0, 1, 50, 50'000})
                    {
                        auto r = TableHelpers::ComputeVirtualRowRange(scroll, vh, rh, total, 5);
                        EXPECT_LE(r.firstVisible, r.lastVisible)
                            << "scroll=" << scroll << " vh=" << vh
                            << " rh=" << rh << " total=" << total;
                        EXPECT_GE(r.firstVisible, 0);
                        EXPECT_LE(r.lastVisible, total);
                    }
    }

    // ===========================================================================
    // ComputeColumnInsertionIndex - the drag-to-reorder target column, computed
    // via edge-crossing on the snapshot of header rects taken at drag start.
    // ===========================================================================

    class ColumnInsertionIndexTest : public ::testing::Test
    {
    protected:
        // Four columns: 0@[0..100), 1@[100..200), 2@[200..300), 3@[300..400).
        std::vector<IntRect> rects
        {
            IntRect{  0, 0, 100, 24 },
            IntRect{100, 0, 100, 24 },
            IntRect{200, 0, 100, 24 },
            IntRect{300, 0, 100, 24 },
        };
    };

    TEST_F(ColumnInsertionIndexTest, NoMovementReturnsFromCol)
    {
        // Mouse at the center of column 2 (dragged column) -> still column 2.
        int to = TableHelpers::ComputeColumnInsertionIndex(250, /*fromCol*/2, rects);
        EXPECT_EQ(to, 2);
    }

    TEST_F(ColumnInsertionIndexTest, CrossingRightNeighborLeftEdgeSnapsIntoIt)
    {
        // Dragging col 0. Mouse at x=100 (exactly col 1's left edge) -> target 1.
        int to = TableHelpers::ComputeColumnInsertionIndex(100, /*fromCol*/0, rects);
        EXPECT_EQ(to, 1);
    }

    TEST_F(ColumnInsertionIndexTest, CrossingMultipleRightNeighborsCascades)
    {
        // Dragging col 0. Mouse at x=250 (past col 1's AND col 2's left edges).
        int to = TableHelpers::ComputeColumnInsertionIndex(250, 0, rects);
        EXPECT_EQ(to, 2);
    }

    TEST_F(ColumnInsertionIndexTest, DraggingFarRightPinsToLastColumn)
    {
        // Mouse way past the rightmost column -> target = last column index.
        int to = TableHelpers::ComputeColumnInsertionIndex(10'000, 0, rects);
        EXPECT_EQ(to, 3);
    }

    TEST_F(ColumnInsertionIndexTest, CrossingLeftNeighborRightEdgeSnapsIntoIt)
    {
        // Dragging col 3. Mouse at x=299 (inside col 2's right edge = 300).
        int to = TableHelpers::ComputeColumnInsertionIndex(299, /*fromCol*/3, rects);
        EXPECT_EQ(to, 2);
    }

    TEST_F(ColumnInsertionIndexTest, DraggingFarLeftPinsToFirstColumn)
    {
        // Mouse way to the left of the leftmost column -> target = 0.
        int to = TableHelpers::ComputeColumnInsertionIndex(-500, 3, rects);
        EXPECT_EQ(to, 0);
    }

    TEST_F(ColumnInsertionIndexTest, EmptyHeadersYieldsZero)
    {
        std::vector<IntRect> empty;
        int to = TableHelpers::ComputeColumnInsertionIndex(0, 0, empty);
        EXPECT_EQ(to, 0);
    }

    TEST_F(ColumnInsertionIndexTest, OutOfRangeFromColClamps)
    {
        // Defensive: if the caller passes an out-of-range fromCol, the helper
        // clamps rather than reading past the end of headerRects.
        int to = TableHelpers::ComputeColumnInsertionIndex(50, /*fromCol*/99, rects);
        EXPECT_EQ(to, 3);  // clamped to last
        int to2 = TableHelpers::ComputeColumnInsertionIndex(50, /*fromCol*/-2, rects);
        EXPECT_EQ(to2, 0);
    }

    TEST_F(ColumnInsertionIndexTest, MidColumnHoverStaysOnNeighbor)
    {
        // Dragging col 0. Mouse at x=150 (past col 1's left edge 100, not past
        // col 2's left edge 200). Target should be 1, not 2.
        int to = TableHelpers::ComputeColumnInsertionIndex(150, 0, rects);
        EXPECT_EQ(to, 1);
    }

    TEST_F(ColumnInsertionIndexTest, NonUniformColumnWidthsWork)
    {
        // Widths: 50, 200, 50. Columns at [0..50), [50..250), [250..300).
        std::vector<IntRect> nonuniform{
            IntRect{  0, 0,  50, 24 },
            IntRect{ 50, 0, 200, 24 },
            IntRect{250, 0,  50, 24 },
        };
        // Dragging col 0. Mouse at x=49 (one px inside col 0): no neighbor
        // crossing, target = 0.
        EXPECT_EQ(TableHelpers::ComputeColumnInsertionIndex(49, 0, nonuniform), 0);
        // Mouse at x=50 (col 1's left edge): past -> target 1.
        EXPECT_EQ(TableHelpers::ComputeColumnInsertionIndex(50, 0, nonuniform), 1);
        // Mouse at x=249 (inside col 1, before col 2's left): still 1.
        EXPECT_EQ(TableHelpers::ComputeColumnInsertionIndex(249, 0, nonuniform), 1);
        // Mouse at x=250 (col 2's left edge): past -> target 2.
        EXPECT_EQ(TableHelpers::ComputeColumnInsertionIndex(250, 0, nonuniform), 2);
    }

    // Apply a reorder(from, to) to a pair of parallel vectors so columns and
    // their persisted widths stay in lockstep. Semantics match the callback
    // contract used by TableState::onReorderColumns. No-op when from == to.
    template <typename T, typename U>
    inline void RotateColumns(std::vector<T>& a, std::vector<U>& b, int from, int to)
    {
        if (from == to) return;
        if (from < 0 || to < 0) return;
        if (from >= int(a.size()) || to >= int(a.size())) return;
        auto rot = [](auto& v, int f, int t)
            {
                if (f < t) std::rotate(v.begin() + f, v.begin() + f + 1, v.begin() + t + 1);
                else       std::rotate(v.begin() + t, v.begin() + f, v.begin() + f + 1);
            };
        rot(a, from, to);
        if (int(b.size()) == int(a.size()))
            rot(b, from, to);
    }

    // ===========================================================================
    // RotateColumns - the reorder swap applied by onReorderColumns callbacks.
    // Keeps columns and their persisted widths in lockstep.
    // ===========================================================================

    class RotateColumnsTest : public ::testing::Test {};

    TEST_F(RotateColumnsTest, NoOpWhenFromEqualsTo)
    {
        std::vector<int>     cols{ 10, 20, 30, 40 };
        std::vector<int32_t> widths{ 80, 120, 100, 90 };
        RotateColumns(cols, widths, 2, 2);
        EXPECT_EQ(cols, (std::vector<int>    { 10, 20, 30, 40 }));
        EXPECT_EQ(widths, (std::vector<int32_t>{ 80, 120, 100, 90 }));
    }

    TEST_F(RotateColumnsTest, MoveLeftToRightRotatesBoth)
    {
        // Move index 0 to index 2: [A,B,C,D] -> [B,C,A,D]
        std::vector<char>    cols{ 'A','B','C','D' };
        std::vector<int32_t> widths{ 80, 120, 100, 90 };
        RotateColumns(cols, widths, 0, 2);
        EXPECT_EQ(cols, (std::vector<char>   {'B', 'C', 'A', 'D'}));
        EXPECT_EQ(widths, (std::vector<int32_t>{ 120, 100, 80, 90 }));
    }

    TEST_F(RotateColumnsTest, MoveRightToLeftRotatesBoth)
    {
        // Move index 3 to index 1: [A,B,C,D] -> [A,D,B,C]
        std::vector<char>    cols{ 'A','B','C','D' };
        std::vector<int32_t> widths{ 80, 120, 100, 90 };
        RotateColumns(cols, widths, 3, 1);
        EXPECT_EQ(cols, (std::vector<char>   {'A', 'D', 'B', 'C'}));
        EXPECT_EQ(widths, (std::vector<int32_t>{ 80, 90, 120, 100 }));
    }

    TEST_F(RotateColumnsTest, AdjacentSwap)
    {
        std::vector<char>    cols{ 'A','B','C' };
        std::vector<int32_t> widths{ 10, 20, 30 };
        RotateColumns(cols, widths, 0, 1);
        EXPECT_EQ(cols, (std::vector<char>   {'B', 'A', 'C'}));
        EXPECT_EQ(widths, (std::vector<int32_t>{ 20, 10, 30 }));
    }

    TEST_F(RotateColumnsTest, MismatchedSizesLeavesSecondUntouched)
    {
        // If the widths vector is out of sync with columns, only the primary
        // moves - matches the defensive guard in the showcase's callback.
        std::vector<char>    cols{ 'A','B','C' };
        std::vector<int32_t> widths{ 10, 20 };  // shorter
        RotateColumns(cols, widths, 0, 2);
        EXPECT_EQ(cols, (std::vector<char>   {'B', 'C', 'A'}));
        EXPECT_EQ(widths, (std::vector<int32_t>{ 10, 20 }));  // unchanged
    }

    TEST_F(RotateColumnsTest, OutOfRangeIndicesAreNoOp)
    {
        std::vector<char>    cols{ 'A','B','C' };
        std::vector<int32_t> widths{ 10, 20, 30 };
        RotateColumns(cols, widths, 0, 5);
        EXPECT_EQ(cols, (std::vector<char>   {'A', 'B', 'C'}));
        EXPECT_EQ(widths, (std::vector<int32_t>{ 10, 20, 30 }));
        RotateColumns(cols, widths, -1, 0);
        EXPECT_EQ(cols, (std::vector<char>   {'A', 'B', 'C'}));
    }

    // ===========================================================================
    // Integration: repeatedly applying reorders converges where expected.
    // This mirrors a user dragging a column across multiple slots.
    // ===========================================================================

    TEST(TableReorderIntegration, SequentialMovesPreserveColumnWidthsAlignment)
    {
        std::vector<char>    cols{ 'A','B','C','D','E' };
        std::vector<int32_t> widths{ 10, 20, 30, 40, 50 };

        // Move A -> end.
        RotateColumns(cols, widths, 0, 4);
        EXPECT_EQ(cols, (std::vector<char>   {'B', 'C', 'D', 'E', 'A'}));
        EXPECT_EQ(widths, (std::vector<int32_t>{ 20, 30, 40, 50, 10 }));

        // Move A (now at index 4) back to index 0.
        RotateColumns(cols, widths, 4, 0);
        EXPECT_EQ(cols, (std::vector<char>   {'A', 'B', 'C', 'D', 'E'}));
        EXPECT_EQ(widths, (std::vector<int32_t>{ 10, 20, 30, 40, 50 }));
    }

}