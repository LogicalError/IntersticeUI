// Table-cell paths: Cell(lambda), TreeCell, TableSelectionMode::Cell,
// multi-select, scrollbars and column sizers.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_core/gui_id.h>
#include <gui_core/gui_id_manager.h>
#include <gui_core/input/gui_input_enums.h>

#include <string>
#include <vector>

namespace tachyon
{
    using GUITest::HeadlessGUIApp;

    namespace {

        class TableCellsTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;

            // Three plain columns, wide enough that a click lands in exactly one.
            tachyon::VirtualTableColumn kColumns[3] = {
                { { .header = "A", .initialWidth = 120, .minWidth = 40, .maxWidth = 400 } },
                { { .header = "B", .initialWidth = 120, .minWidth = 40, .maxWidth = 400 } },
                { { .header = "C", .initialWidth = 120, .minWidth = 40, .maxWidth = 400 } },
            };

            tachyon::ID tableID{};

            void SetUp() override
            {
                ASSERT_TRUE(app.Initialize());
                tableID = app.idManager.CreateStaticID();
            }

            void TearDown() override
            {
                app.Cleanup();
            }
        };

        // ---------------------------------------------------------------------
        // Cell(lambda) - child scope for embedded widgets
        // ---------------------------------------------------------------------

        TEST_F(TableCellsTest, CellLambdaEmitsLabelAsChildAndCellIsHittable)
        {
            // The label emitted by Cell(lambda) must lie inside the cell's rect.
            tachyon::TableState state{};
            const tachyon::ID labelID = app.idManager.CreateStaticID();

            auto build = [&] {
                app.BeginRootWindow();
                if (app.BeginTable(tableID, kColumns, state,
                                   LayoutOverride{}.Size(400, 200)))
                {
                    app.BeginRow();
                    app.Cell("first");
                    app.Cell([&] {
                        app.Label(labelID, "embedded");
                    });
                    app.Cell("third");
                    app.EndRow();
                    app.EndTable();
                }
                app.EndRootWindow();
            };
            app.Frame(build);

            ASSERT_TRUE(app.HasElement(labelID));
            const auto labelRect = app.GetElementRect(labelID);
            EXPECT_GT(labelRect.w, 0);
            EXPECT_GT(labelRect.h, 0);

            // The label is inside the middle column's horizontal span.
            const auto tableRect = app.GetElementRect(tableID);
            EXPECT_GE(labelRect.x, tableRect.x);
            EXPECT_LE(labelRect.x + labelRect.w, tableRect.x + tableRect.w);
            // ...and past column A (>120px from the table's left edge).
            EXPECT_GE(labelRect.x, tableRect.x + 120);
        }

        // Tree-cell sizes are defined by the stylesheet (not literals in the cell code): the theme
        // reads them back from the styles' widths / icon sizes.
        TEST_F(TableCellsTest, TreeCellMetricsComeFromTheStylesheet)
        {
            auto layoutOf = [&](tachyon::StyleRef s) { return app.styleManager.GetLayoutStyle(s.layoutStyleIndex); };
            const auto* indent = layoutOf(app.theme.treeTableIndentStyleRef);
            const auto* arrow  = layoutOf(app.theme.treeTableArrowStyleRef);
            const auto* icon   = layoutOf(app.theme.treeTableIconStyleRef);
            const auto* glyph  = layoutOf(app.theme.treeTableArrowGlyphStyleRef);
            ASSERT_TRUE(indent && arrow && icon && glyph);

            EXPECT_EQ(app.theme.treeTableIndent,         indent->layoutData.width);
            EXPECT_EQ(app.theme.treeTableExpanderWidth,  arrow->layoutData.width);
            EXPECT_EQ(app.theme.treeTableIconSize,       int32_t(icon->iconSize));
            EXPECT_EQ(app.theme.treeTableArrowGlyphSize, int32_t(glyph->iconSize));
            EXPECT_GT(app.theme.treeTableIndent, 0);
            EXPECT_GT(app.theme.treeTableExpanderWidth, 0);
        }

        // A table built inside a cell of another table must leave the outer table intact:
        // the cells emitted after it still land in the outer table.
        TEST_F(TableCellsTest, TableInsideACellDoesNotClobberTheOuterTable)
        {
            tachyon::TableState outer{}, inner{};
            const tachyon::ID innerTableID = app.idManager.CreateStaticID();
            const tachyon::ID afterID      = app.idManager.CreateStaticID();
            tachyon::VirtualTableColumn innerCols[1] = {
                { { .header = "I", .initialWidth = 80, .minWidth = 40, .maxWidth = 200 } },
            };

            auto build = [&] {
                app.BeginRootWindow();
                if (app.BeginTable(tableID, kColumns, outer, LayoutOverride{}.Size(400, 200)))
                {
                    app.BeginRow();
                    app.Cell("first");
                    app.Cell([&] {
                        if (app.BeginTable(innerTableID, innerCols, inner, LayoutOverride{}.Size(100, 60)))
                        {
                            app.BeginRow();
                            app.Cell("x");
                            app.EndRow();
                            app.EndTable();
                        }
                    });
                    app.Cell([&] { app.Label(afterID, "after"); });
                    app.EndRow();
                    app.EndTable();
                }
                app.EndRootWindow();
            };
            app.Frame(build);

            EXPECT_TRUE(app.HasElement(innerTableID));
            ASSERT_TRUE(app.HasElement(afterID)) << "a cell after the nested table must still be emitted";
            const auto tableRect = app.GetElementRect(tableID);
            EXPECT_GE(app.GetElementRect(afterID).x, tableRect.x + 240) << "and land in the third column";
        }

        // ---------------------------------------------------------------------
        // Sizer double-click - auto-size the column to its widest content.
        // ---------------------------------------------------------------------
        TEST_F(TableCellsTest, SizerDoubleClickSnapsColumnToMaxContentWidth)
        {
            tachyon::TableState state{};
            // The stub text system measures 8px per char: 30 chars = 240 wide.
            tachyon::VirtualTableColumn cols[3] = {
                { { .header = "A", .initialWidth = 80, .minWidth = 40, .maxWidth = 600,
                    .sizing = tachyon::ColumnSizing::Resizable } },
                { { .header = "B", .initialWidth = 100, .minWidth = 40, .maxWidth = 400 } },
                { { .header = "C", .initialWidth = 100, .minWidth = 40, .maxWidth = 400 } },
            };
            auto build = [&] {
                app.BeginRootWindow();
                if (app.BeginTable(tableID, cols, state, LayoutOverride{}.Size(500, 200)))
                {
                    app.BeginRow();
                    app.Cell("very_long_thirty_char_cell_xyz");
                    app.Cell("b"); app.Cell("c");
                    app.EndRow();
                    app.EndTable();
                }
                app.EndRootWindow();
            };
            app.Frame(build);
            // Second frame registers the measured text width the snap reads.
            app.Frame(build);

            ASSERT_NE(cols[0].internal.sizerID, tachyon::ID::Invalid());
            const tachyon::ID sizerID = cols[0].internal.sizerID;
            ASSERT_TRUE(app.HasElement(sizerID));
            // The sizer is clipped by its parent; use its X but the body's Y.
            const auto sizerRect = app.GetElementRect(sizerID);
            const auto bodyRect  = app.GetElementRect(state.internal.bodyID);
            const int2 hit{ sizerRect.x + sizerRect.w / 2,
                            bodyRect.y + bodyRect.h / 2 };

            const int32_t before = state.columnWidths[0];
            app.InjectMouseMove(hit);
            app.InjectMouseDown(hit);
            app.InjectMouseUp(hit);
            app.InjectMouseDown(hit);
            app.InjectMouseUp(hit);
            app.Frame(build);

            EXPECT_NE(state.columnWidths[0], before)
                << "Double-click should snap the column to its content width.";
            EXPECT_GE(state.columnWidths[0], 200)
                << "Snap target should approach the 240px cell content width.";
        }

        // ---------------------------------------------------------------------
        // TreeCell - depth + isOpen registration on the user TableState
        // ---------------------------------------------------------------------

        // ---------------------------------------------------------------------
        // TreeCell scroll - the tree cell and its inner content (indent / arrow /
        // icon / text) must shift by the same amount, on both axes, and only along
        // the scrolled axis.
        // ---------------------------------------------------------------------

        namespace {
            struct ChildRectSnapshot
            {
                tachyon::IntRect              cell{};
                std::vector<tachyon::IntRect> rects;
            };

            // Rows live in the section grid. The tree cell shares rowID with the row
            // backdrop; only the tree cell has children, so filter on that.
            ChildRectSnapshot SnapshotTreeCellByRow(HeadlessGUIApp& app,
                                                    const tachyon::TableState& state,
                                                    tachyon::ID rowID)
            {
                ChildRectSnapshot s{};
                const auto bodyRef = app.elementStorage.FindElementRefByID(state.SectionBodyID(0));
                if (!bodyRef.IsValid()) return s;
                const auto range = app.elementStorage.GetChildRange(bodyRef);
                if (range.Empty()) return s;
                const auto children = app.elementStorage.GetChildrenSpanUnsafe(bodyRef.depth + 1, range);
                for (size_t i = 0; i < children.size(); ++i)
                {
                    const auto& elem = children[i];
                    if (elem.id != rowID) continue;
                    const tachyon::ElementRef ref{ bodyRef.depth + 1,
                                               static_cast<uint32_t>(range.begin + i) };
                    const auto childRange = app.elementStorage.GetChildRange(ref);
                    if (childRange.Empty()) continue; // backdrop has no children, skip
                    const auto* rectPtr = app.elementStorage.GetElementRectPtr(ref);
                    if (!rectPtr) continue;
                    s.cell = rectPtr->rect;
                    auto rects = app.elementStorage.GetChildRectSpanUnsafe(ref.depth + 1, childRange);
                    s.rects.reserve(rects.size());
                    for (const auto& r : rects)
                        s.rects.push_back(r.rect);
                    return s;
                }
                return s;
            }
        }

        TEST_F(TableCellsTest, TreeCellInnerContentShiftsWithBodyHorizontalScroll)
        {
            tachyon::TableState state{};
            // Wide columns so the table overflows X inside the 300px viewport.
            tachyon::VirtualTableColumn wideCols[3] = {
                { { .header = "Name", .initialWidth = 240, .minWidth = 80, .maxWidth = 600 } },
                { { .header = "Size", .initialWidth = 100, .minWidth = 40, .maxWidth = 400 } },
                { { .header = "Type", .initialWidth = 200, .minWidth = 40, .maxWidth = 400 } },
            };
            bool branchOpen = true;

            auto build = [&] {
                app.BeginRootWindow();
                if (app.BeginTable(tableID, wideCols, state,
                                   LayoutOverride{}.Size(300, 200)))
                {
                    app.BeginRow();
                    app.TreeCell("a-folder", /*depth*/ 1, &branchOpen, IconID::Invalid);
                    app.Cell("12 KB"); app.Cell("Folder");
                    app.EndRow();
                    app.EndTable();
                }
                app.EndRootWindow();
            };
            app.Frame(build);
            ASSERT_EQ(state.internal.totalRowCount, 1);
            const tachyon::ID rowID = state.RowIDForIndex(0);

            const auto before = SnapshotTreeCellByRow(app, state, rowID);
            ASSERT_GT(before.cell.w, 0);
            ASSERT_FALSE(before.rects.empty())
                << "TreeCell should have inner children (indent / arrow / text)";

            const auto rowCenter = app.CenterOf(rowID);
            app.InjectMouseMove(rowCenter);
            app.InjectMouseScroll(rowCenter, int2{ -3, 0 }); // scroll right
            app.Frame(build);
            app.Frame(build);

            const auto after = SnapshotTreeCellByRow(app, state, rowID);
            const auto& cellBefore = before.cell;
            const auto& cellAfter  = after.cell;
            const auto& childrenBefore = before;
            const auto& childrenAfter  = after;
            const int32_t cellDeltaX = cellAfter.x - cellBefore.x;
            EXPECT_LT(cellDeltaX, 0) << "Body scroll should have moved the cell LEFT.";

            ASSERT_EQ(childrenBefore.rects.size(), childrenAfter.rects.size());
            for (size_t i = 0; i < childrenBefore.rects.size(); ++i)
            {
                const int32_t childDeltaX = childrenAfter.rects[i].x - childrenBefore.rects[i].x;
                EXPECT_EQ(childDeltaX, cellDeltaX)
                    << "TreeCell child #" << i << " (rect "
                    << childrenBefore.rects[i].x << " → " << childrenAfter.rects[i].x
                    << ") should have shifted LEFT by the same amount as the cell ("
                    << cellDeltaX << "), but shifted by " << childDeltaX
                    << " — children getting out of step with their cell is the "
                       "scroll-cascade regression.";
                // Y must NOT shift on a horizontal scroll.
                const int32_t childDeltaY = childrenAfter.rects[i].y - childrenBefore.rects[i].y;
                EXPECT_EQ(childDeltaY, 0)
                    << "Horizontal scroll should not move children on Y. child #" << i;
            }
        }

        TEST_F(TableCellsTest, TreeCellInnerContentShiftsWithBodyVerticalScroll)
        {
            // Many rows so the body overflows Y inside the 100px viewport.
            tachyon::TableState state{};
            tachyon::VirtualTableColumn cols[3] = {
                { { .header = "Name", .initialWidth = 120, .minWidth = 40, .maxWidth = 400 } },
                { { .header = "Size", .initialWidth = 100, .minWidth = 40, .maxWidth = 400 } },
                { { .header = "Type", .initialWidth = 100, .minWidth = 40, .maxWidth = 400 } },
            };
            bool branchOpen = true;

            auto build = [&] {
                app.BeginRootWindow();
                if (app.BeginTable(tableID, cols, state,
                                   LayoutOverride{}.Size(400, 100)))
                {
                    for (int r = 0; r < 30; ++r)
                    {
                        char rowName[32];
                        std::snprintf(rowName, sizeof(rowName), "vscroll_row_%d", r);
                        app.BeginRow();
                        app.TreeCell(rowName, /*depth*/ 1, &branchOpen, IconID::Invalid);
                        app.Cell("12 KB"); app.Cell("Folder");
                        app.EndRow();
                    }
                    app.EndTable();
                }
                app.EndRootWindow();
            };
            app.Frame(build);
            ASSERT_GE(state.internal.totalRowCount, 5);
            // Row 3 stays on-screen after a ~30px scroll (row 0 might not).
            const tachyon::ID rowID = state.RowIDForIndex(3);

            const auto before = SnapshotTreeCellByRow(app, state, rowID);
            ASSERT_GT(before.cell.h, 0);
            ASSERT_FALSE(before.rects.empty());

            // Wheel-scroll at the body's center.
            const auto bodyCenter = app.CenterOf(state.internal.bodyID);
            app.InjectMouseMove(bodyCenter);
            app.InjectMouseScroll(bodyCenter, int2{ 0, -1 }); // scroll down 1 tick (~30px)
            app.Frame(build);
            app.Frame(build);

            const auto after = SnapshotTreeCellByRow(app, state, rowID);
            const auto& cellBefore = before.cell;
            const auto& cellAfter  = after.cell;
            const auto& childrenBefore = before;
            const auto& childrenAfter  = after;
            const int32_t cellDeltaY = cellAfter.y - cellBefore.y;
            EXPECT_LT(cellDeltaY, 0) << "Body Y-scroll should have moved the cell UP.";

            ASSERT_EQ(childrenBefore.rects.size(), childrenAfter.rects.size());
            for (size_t i = 0; i < childrenBefore.rects.size(); ++i)
            {
                const int32_t childDeltaY = childrenAfter.rects[i].y - childrenBefore.rects[i].y;
                EXPECT_EQ(childDeltaY, cellDeltaY)
                    << "TreeCell child #" << i << " should have shifted UP by the "
                       "same amount as the cell (" << cellDeltaY
                    << "), but shifted by " << childDeltaY
                    << ". A double-shifted child (e.g. -2× the cell delta) means "
                       "the Y axis is incorrectly cascading scroll on top of the "
                       "post-shift flex placement — content disappears off-screen.";
                // X must NOT shift on a vertical scroll.
                const int32_t childDeltaX = childrenAfter.rects[i].x - childrenBefore.rects[i].x;
                EXPECT_EQ(childDeltaX, 0)
                    << "Vertical scroll should not move children on X. child #" << i;
            }
        }

        TEST_F(TableCellsTest, TreeCellRegistersDepthAndIsOpenByRow)
        {
            tachyon::TableState state{};
            bool branchOpen = true;

            auto build = [&] {
                app.BeginRootWindow();
                if (app.BeginTable(tableID, kColumns, state,
                                   LayoutOverride{}.Size(400, 200)))
                {
                    app.BeginRow();
                    app.TreeCell("branch", /*depth*/ 1, &branchOpen);
                    app.Cell("b"); app.Cell("c");
                    app.EndRow();
                    app.BeginRow();
                    app.TreeCell("leaf", /*depth*/ 2, /*isOpen*/ nullptr);
                    app.Cell("b"); app.Cell("c");
                    app.EndRow();
                    app.EndTable();
                }
                app.EndRootWindow();
            };
            app.Frame(build);

            ASSERT_EQ(state.internal.totalRowCount, 2);
            const tachyon::ID branchID = state.RowIDForIndex(0);
            const tachyon::ID leafID   = state.RowIDForIndex(1);

            // Branch row is registered with its isOpen pointer + depth.
            const auto* branchInfo = state.FindTreeRow(branchID);
            ASSERT_NE(branchInfo, nullptr);
            EXPECT_EQ(branchInfo->isOpen, &branchOpen);
            EXPECT_EQ(branchInfo->depth, 1);

            // Leaf registers too, but with no isOpen pointer.
            const auto* leafInfo = state.FindTreeRow(leafID);
            ASSERT_NE(leafInfo, nullptr);
            EXPECT_EQ(leafInfo->isOpen, nullptr);
            EXPECT_EQ(leafInfo->depth, 2);
        }

        // ---------------------------------------------------------------------
        // TreeCell - row-mode double-click on the cell toggles isOpen.
        //
        // In row mode the click lands on the row backdrop, which handles the toggle.
        // ---------------------------------------------------------------------

        TEST_F(TableCellsTest, TreeCellDoubleClickTogglesIsOpenInRowMode)
        {
            tachyon::TableState state{};
            // Row mode is the default; keep selectionMode untouched.
            bool branchOpen = false;

            auto build = [&] {
                app.BeginRootWindow();
                if (app.BeginTable(tableID, kColumns, state,
                                   LayoutOverride{}.Size(400, 200)))
                {
                    app.BeginRow();
                    app.TreeCell("branch", /*depth*/ 1, &branchOpen);
                    app.Cell("b"); app.Cell("c");
                    app.EndRow();
                    app.EndTable();
                }
                app.EndRootWindow();
            };
            app.Frame(build);

            ASSERT_EQ(state.internal.totalRowCount, 1);
            const tachyon::ID branchID = state.RowIDForIndex(0);

            // Row backdrops carry the rowID - derive the click point from it.
            ASSERT_TRUE(app.HasElement(branchID));
            const auto rowCenter = app.CenterOf(branchID);

            // Two clicks within the 300ms window; the tracker resets after a toggle,
            // so a third click would not flip it again.
            app.InjectMouseMove(rowCenter);
            app.InjectMouseDown(rowCenter);
            app.InjectMouseUp(rowCenter);
            app.InjectMouseDown(rowCenter);
            app.InjectMouseUp(rowCenter);
            app.Frame(build);

            EXPECT_TRUE(branchOpen) << "Two fast clicks on the branch row should "
                                       "have flipped isOpen from false → true.";
        }

        // ---------------------------------------------------------------------
        // Cell-mode selection - clicking a cell writes selectedRowID + selectedColumn
        // ---------------------------------------------------------------------

        TEST_F(TableCellsTest, CellModeClickingCellSelectsRowAndColumn)
        {
            tachyon::TableState state{};
            state.selectionMode = tachyon::TableSelectionMode::Cell;

            auto build = [&] {
                app.BeginRootWindow();
                if (app.BeginTable(tableID, kColumns, state,
                                   LayoutOverride{}.Size(400, 200)))
                {
                    app.BeginRow();
                    app.Cell("col0"); app.Cell("col1"); app.Cell("col2");
                    app.EndRow();
                    app.EndTable();
                }
                app.EndRootWindow();
            };
            app.Frame(build);

            ASSERT_EQ(state.internal.totalRowCount, 1);
            const tachyon::ID rowID = state.RowIDForIndex(0);

            // Middle column: 120px columns, no gaps, so column 1 spans [120, 240).
            const auto rowRect = app.GetElementRect(rowID);
            ASSERT_GT(rowRect.h, 0);
            const tachyon::int2 col1Center{ rowRect.x + 120 + 60, rowRect.y + rowRect.h / 2 };
            app.InjectMouseMove(col1Center);
            app.InjectMouseDown(col1Center);
            app.InjectMouseUp(col1Center);
            app.Frame(build);

            EXPECT_EQ(state.selectedRowID, rowID);
            EXPECT_EQ(state.selectedColumn, 1);
        }

        // ---------------------------------------------------------------------
        // MultiSelect - Ctrl-click toggles individual rows
        // ---------------------------------------------------------------------

        TEST_F(TableCellsTest, MultiSelectCtrlClickTogglesIndividualRows)
        {
            tachyon::TableState state{};
            tachyon::MultiSelectState ms{};
            state.multiSelect = &ms;

            auto build = [&] {
                app.BeginRootWindow();
                if (app.BeginTable(tableID, kColumns, state,
                                   LayoutOverride{}.Size(400, 400)))
                {
                    app.BeginRow();
                    app.Cell("a0"); app.Cell("b0"); app.Cell("c0"); app.EndRow();
                    app.BeginRow();
                    app.Cell("a1"); app.Cell("b1"); app.Cell("c1"); app.EndRow();
                    app.BeginRow();
                    app.Cell("a2"); app.Cell("b2"); app.Cell("c2"); app.EndRow();
                    app.EndTable();
                }
                app.EndRootWindow();
            };
            app.Frame(build);

            ASSERT_EQ(state.internal.totalRowCount, 3);
            const tachyon::ID row0ID = state.RowIDForIndex(0);
            const tachyon::ID row2ID = state.RowIDForIndex(2);

            // Ctrl-click row 0 -> selectedIDs = {row0}
            const auto r0c = app.CenterOf(row0ID);
            app.InjectMouseMove(r0c, MouseButtons::None, KeyModifier::Control);
            app.InjectMouseDown(r0c, MouseButtons::Left, KeyModifier::Control);
            app.InjectMouseUp  (r0c, MouseButtons::Left, KeyModifier::Control);
            app.Frame(build);

            EXPECT_EQ(ms.selectedIDs.size(), 1u);
            EXPECT_EQ(ms.selectedIDs.count(row0ID), 1u);

            // Ctrl-click row 2 -> adds -> {row0, row2}
            const auto r2c = app.CenterOf(row2ID);
            app.InjectMouseMove(r2c, MouseButtons::None, KeyModifier::Control);
            app.InjectMouseDown(r2c, MouseButtons::Left, KeyModifier::Control);
            app.InjectMouseUp  (r2c, MouseButtons::Left, KeyModifier::Control);
            app.Frame(build);

            EXPECT_EQ(ms.selectedIDs.size(), 2u);
            EXPECT_EQ(ms.selectedIDs.count(row0ID), 1u);
            EXPECT_EQ(ms.selectedIDs.count(row2ID), 1u);

            // Ctrl-click row 0 again -> toggles it off -> {row2}
            app.InjectMouseMove(r0c, MouseButtons::None, KeyModifier::Control);
            app.InjectMouseDown(r0c, MouseButtons::Left, KeyModifier::Control);
            app.InjectMouseUp  (r0c, MouseButtons::Left, KeyModifier::Control);
            app.Frame(build);

            EXPECT_EQ(ms.selectedIDs.size(), 1u);
            EXPECT_EQ(ms.selectedIDs.count(row2ID), 1u);
        }

        // ---------------------------------------------------------------------
        // MultiSelect - Shift-click selects the inclusive range from anchor to target
        // ---------------------------------------------------------------------

        TEST_F(TableCellsTest, MultiSelectShiftClickSelectsRange)
        {
            tachyon::TableState state{};
            tachyon::MultiSelectState ms{};
            state.multiSelect = &ms;

            auto build = [&] {
                app.BeginRootWindow();
                if (app.BeginTable(tableID, kColumns, state,
                                   LayoutOverride{}.Size(400, 400)))
                {
                    app.BeginRow();
                    app.Cell("0a"); app.Cell("0b"); app.Cell("0c"); app.EndRow();
                    app.BeginRow();
                    app.Cell("1a"); app.Cell("1b"); app.Cell("1c"); app.EndRow();
                    app.BeginRow();
                    app.Cell("2a"); app.Cell("2b"); app.Cell("2c"); app.EndRow();
                    app.BeginRow();
                    app.Cell("3a"); app.Cell("3b"); app.Cell("3c"); app.EndRow();
                    app.EndTable();
                }
                app.EndRootWindow();
            };
            app.Frame(build);

            ASSERT_EQ(state.internal.totalRowCount, 4);
            const tachyon::ID r0 = state.RowIDForIndex(0);
            const tachyon::ID r1 = state.RowIDForIndex(1);
            const tachyon::ID r2 = state.RowIDForIndex(2);
            const tachyon::ID r3 = state.RowIDForIndex(3);

            // Plain click row 1 establishes the anchor.
            const auto r1c = app.CenterOf(r1);
            app.InjectMouseMove(r1c);
            app.InjectMouseDown(r1c);
            app.InjectMouseUp  (r1c);
            app.Frame(build);
            ASSERT_EQ(ms.lastClickedID, r1);
            ASSERT_EQ(ms.selectedIDs.size(), 1u);

            // Shift-click row 3 -> selects inclusive {r1, r2, r3}.
            const auto r3c = app.CenterOf(r3);
            app.InjectMouseMove(r3c, MouseButtons::None, KeyModifier::Shift);
            app.InjectMouseDown(r3c, MouseButtons::Left, KeyModifier::Shift);
            app.InjectMouseUp  (r3c, MouseButtons::Left, KeyModifier::Shift);
            app.Frame(build);

            EXPECT_EQ(ms.selectedIDs.size(), 3u);
            EXPECT_EQ(ms.selectedIDs.count(r1), 1u);
            EXPECT_EQ(ms.selectedIDs.count(r2), 1u);
            EXPECT_EQ(ms.selectedIDs.count(r3), 1u);
            EXPECT_EQ(ms.selectedIDs.count(r0), 0u);
        }

        // The shift anchor stays where the range started; arrow keys move from
        // the row last clicked (the end of the range), not from the anchor.
        TEST_F(TableCellsTest, ArrowAfterShiftClickMovesFromRangeEnd)
        {
            tachyon::TableState state{};
            tachyon::MultiSelectState ms{};
            state.multiSelect = &ms;

            auto build = [&] {
                app.BeginRootWindow();
                if (app.BeginTable(tableID, kColumns, state,
                                   LayoutOverride{}.Size(400, 400)))
                {
                    for (int r = 0; r < 6; ++r)
                    {
                        app.BeginRow();
                        app.Cell("a"); app.Cell("b"); app.Cell("c");
                        app.EndRow();
                    }
                    app.EndTable();
                }
                app.EndRootWindow();
            };
            app.Frame(build);
            const tachyon::ID r0 = state.RowIDForIndex(0);
            const tachyon::ID r1 = state.RowIDForIndex(1);
            const tachyon::ID r3 = state.RowIDForIndex(3);
            const tachyon::ID r4 = state.RowIDForIndex(4);

            const auto r1c = app.CenterOf(r1);
            app.InjectMouseMove(r1c);
            app.InjectMouseDown(r1c);
            app.InjectMouseUp  (r1c);
            app.Frame(build);

            const auto r3c = app.CenterOf(r3);
            app.InjectMouseMove(r3c, MouseButtons::None, KeyModifier::Shift);
            app.InjectMouseDown(r3c, MouseButtons::Left, KeyModifier::Shift);
            app.InjectMouseUp  (r3c, MouseButtons::Left, KeyModifier::Shift);
            app.Frame(build);
            ASSERT_EQ(ms.selectedIDs.size(), 3u);
            EXPECT_EQ(ms.lastClickedID, r1) << "the anchor stays at the range start";

            app.InjectKey(tachyon::KeyCode::Down);
            app.Frame(build);
            EXPECT_EQ(state.selectedRowID, r4);
            EXPECT_EQ(ms.selectedIDs.size(), 1u);
            EXPECT_EQ(ms.selectedIDs.count(r4), 1u);

            // A shift-click after keyboard navigation extends from the new anchor.
            const auto r0c = app.CenterOf(r0);
            app.InjectMouseMove(r0c, MouseButtons::None, KeyModifier::Shift);
            app.InjectMouseDown(r0c, MouseButtons::Left, KeyModifier::Shift);
            app.InjectMouseUp  (r0c, MouseButtons::Left, KeyModifier::Shift);
            app.Frame(build);
            EXPECT_EQ(ms.selectedIDs.size(), 5u) << "rows 0..4";
        }

        // ---------------------------------------------------------------------
        // Standard table - headers are sticky-pinned INSIDE the section grid,
        // so a horizontal scroll moves header and cells together natively
        // (there is no separate header strip to mirror).
        // ---------------------------------------------------------------------
        TEST_F(TableCellsTest, StandardTableHeaderScrollsWithBodyX)
        {
            tachyon::TableState state{};
            tachyon::VirtualTableColumn wideCols[3] = {
                { { .header = "Name", .initialWidth = 240, .minWidth = 80, .maxWidth = 600 } },
                { { .header = "Size", .initialWidth = 100, .minWidth = 40, .maxWidth = 400 } },
                { { .header = "Type", .initialWidth = 200, .minWidth = 40, .maxWidth = 400 } },
            };

            auto build = [&] {
                app.BeginRootWindow();
                if (app.BeginTable(tableID, wideCols, state,
                                   LayoutOverride{}.Size(300, 200)))
                {
                    app.BeginRow();
                    app.Cell("a"); app.Cell("b"); app.Cell("c"); app.EndRow();
                    app.EndTable();
                }
                app.EndRootWindow();
            };
            app.Frame(build);
            const auto headerXBefore = app.elementStorage.GetElementRectUnsafe(
                wideCols[0].internal.headerRef).rect.x;

            // Scroll the table horizontally (wheel routes X to the section
            // grid under the cursor).
            const auto bodyCenter = app.CenterOf(state.SectionBodyID(0));
            app.InjectMouseMove(bodyCenter);
            app.InjectMouseScroll(bodyCenter, int2{ -3, 0 });
            app.Frame(build);
            app.Frame(build);

            auto& sm = app.guiStateManager.GetScrollManagement();
            const auto sectionHandle = sm.GetScrollOffsetHandle(state.SectionBodyID(0));
            ASSERT_TRUE(sm.IsScrollOffsetHandleValid(sectionHandle));
            EXPECT_LT(sm.GetScrollOffset(sectionHandle).x, 0)
                << "Section grid should have scrolled X negative.";

            // The header cell lives inside the same grid, so its laid-out X
            // shifts left with the scroll.
            const auto headerXAfter = app.elementStorage.GetElementRectUnsafe(
                wideCols[0].internal.headerRef).rect.x;
            EXPECT_LT(headerXAfter, headerXBefore)
                << "Header cell must shift with the section's X scroll.";
        }

        // ---------------------------------------------------------------------
        // Standard table - H scrollbar drag writes the body's X offset.
        // ---------------------------------------------------------------------
        TEST_F(TableCellsTest, StandardTableHScrollbarDragWritesXOffset)
        {
            tachyon::TableState state{};
            tachyon::VirtualTableColumn wideCols[3] = {
                { { .header = "Name", .initialWidth = 240, .minWidth = 80, .maxWidth = 600 } },
                { { .header = "Size", .initialWidth = 100, .minWidth = 40, .maxWidth = 400 } },
                { { .header = "Type", .initialWidth = 200, .minWidth = 40, .maxWidth = 400 } },
            };

            // Headless skips render, so force the overflow flags and viewport each
            // build (the prologue resets them every frame).
            auto build = [&] {
                state.internal.lastViewportW = 300;
                state.internal.lastViewportH = 200;
                state.internal.anyHorzOverflow = state.internal.anyVertOverflow = true;
                app.BeginRootWindow();
                if (app.BeginTable(tableID, wideCols, state,
                                   LayoutOverride{}.Size(300, 200)))
                {
                    app.BeginRow();
                    app.Cell("a"); app.Cell("b"); app.Cell("c"); app.EndRow();
                    app.EndTable();
                }
                app.EndRootWindow();
            };
            app.Frame(build);
            app.Frame(build);
            ASSERT_TRUE(app.HasElement(state.internal.horzScrollbarID));

            const auto sbRect   = app.GetElementRect(state.internal.horzScrollbarID);
            const auto bodyRect = app.GetElementRect(state.SectionBodyID(0));
            const int trackLeft  = std::max(sbRect.x, bodyRect.x) + 5;
            const int trackRight = std::min(sbRect.x + sbRect.w, bodyRect.x + bodyRect.w) - 5;
            const int2 from{ trackLeft,  sbRect.y + sbRect.h / 2 };
            const int2 to  { trackRight, from.y };

            // X scroll lives on the section grid, not the outer wrapper.
            auto& sm = app.guiStateManager.GetScrollManagement();
            const auto handle = sm.GetOrCreateScrollOffsetHandle(state.SectionBodyID(0));
            const int2 before = sm.GetScrollOffset(handle);

            app.DragFrom(from, to);
            app.Frame(build);

            EXPECT_LT(sm.GetScrollOffset(handle).x, before.x)
                << "Drag-right should make section X offset more negative.";
        }

        // ---------------------------------------------------------------------
        // Standard table - V scrollbar drag writes the body's Y offset.
        // ---------------------------------------------------------------------
        TEST_F(TableCellsTest, StandardTableVScrollbarDragWritesYOffset)
        {
            tachyon::TableState state{};
            auto build = [&] {
                state.internal.lastViewportW = 400;
                state.internal.lastViewportH = 100;
                state.internal.anyHorzOverflow = state.internal.anyVertOverflow = true;
                app.BeginRootWindow();
                if (app.BeginTable(tableID, kColumns, state,
                                   LayoutOverride{}.Size(400, 100)))
                {
                    for (int r = 0; r < 30; ++r)
                    {
                        app.BeginRow();
                        app.Cell("a"); app.Cell("b"); app.Cell("c"); app.EndRow();
                    }
                    app.EndTable();
                }
                app.EndRootWindow();
            };
            app.Frame(build);
            app.Frame(build);
            ASSERT_TRUE(app.HasElement(state.internal.vertScrollbarID));

            const auto sbRect   = app.GetElementRect(state.internal.vertScrollbarID);
            const auto bodyRect = app.GetElementRect(state.SectionBodyID(0));
            const int trackTop    = std::max(sbRect.y, bodyRect.y) + 5;
            const int trackBottom = std::min(sbRect.y + sbRect.h, bodyRect.y + bodyRect.h) - 5;
            const int2 from{ sbRect.x + sbRect.w / 2, trackTop };
            const int2 to  { from.x,                  trackBottom };

            // The drag writes every section body's Y handle.
            auto& sm = app.guiStateManager.GetScrollManagement();
            const auto handle = sm.GetOrCreateScrollOffsetHandle(state.SectionBodyID(0));
            const int2 before = sm.GetScrollOffset(handle);

            app.DragFrom(from, to);
            app.Frame(build);

            EXPECT_LT(sm.GetScrollOffset(handle).y, before.y)
                << "Drag-down should make section Y offset more negative.";
        }

        // ---------------------------------------------------------------------
        // Standard table - with both scrollbars visible, a 12x12 corner cap sits at
        // the body's bottom-right inside corner.
        // ---------------------------------------------------------------------
        TEST_F(TableCellsTest, StandardTableBothScrollbarsEmitCornerCap)
        {
            tachyon::TableState state{};

            auto build = [&] {
                app.BeginRootWindow();
                if (app.BeginTable(tableID, kColumns, state,
                                   LayoutOverride{}.Size(200, 100)))
                {
                    for (int r = 0; r < 5; ++r)
                    {
                        app.BeginRow();
                        app.Cell("a"); app.Cell("b"); app.Cell("c"); app.EndRow();
                    }
                    app.EndTable();
                }
                app.EndRootWindow();
            };
            // First frame: layout body so we can read its actual size.
            app.Frame(build);
            const auto bodyRect0 = app.GetElementRect(state.internal.bodyID);
            ASSERT_GT(bodyRect0.h, 12) << "body must be taller than corner thickness";

            // Headless skips render: force the overflow flags and use the real body
            // size so the scrollbars shorten to fit.
            app.Frame([&] {
                state.internal.lastViewportW = bodyRect0.w;
                state.internal.lastViewportH = bodyRect0.h;
                state.internal.anyHorzOverflow = state.internal.anyVertOverflow = true;
                build();
            });

            ASSERT_TRUE(app.HasElement(state.internal.vertScrollbarID));
            ASSERT_TRUE(app.HasElement(state.internal.horzScrollbarID));
            const auto bodyRect = app.GetElementRect(state.internal.bodyID);
            const auto vRect    = app.GetElementRect(state.internal.vertScrollbarID);
            const auto hRect    = app.GetElementRect(state.internal.horzScrollbarID);
            constexpr int kThick = 12;
            EXPECT_LE(vRect.y + vRect.h, bodyRect.y + bodyRect.h - kThick + 1)
                << "Vertical scrollbar should end at the top of the corner.";
            EXPECT_LE(hRect.x + hRect.w, bodyRect.x + bodyRect.w - kThick + 1)
                << "Horizontal scrollbar should end at the left of the corner.";
        }

        // ---------------------------------------------------------------------
        // Sizer X invariant - the sizer for boundary c sits at the right edge of
        // header cell c.
        // ---------------------------------------------------------------------
        TEST_F(TableCellsTest, SizerXAlignsWithHeaderCellRightEdgeStandard)
        {
            tachyon::TableState state{};
            // The last column always stretches in standard mode, so only sizers before
            // it can be pinned: with 3 columns, just sizer 0.
            tachyon::VirtualTableColumn cols[3] = {
                { { .header = "A", .initialWidth = 100, .minWidth = 40,
                    .sizing = tachyon::ColumnSizing::Resizable } },
                { { .header = "B", .initialWidth = 100, .minWidth = 40,
                    .sizing = tachyon::ColumnSizing::Resizable } },
                { { .header = "C", .initialWidth = 100, .minWidth = 40 } },
            };
            auto build = [&] {
                app.BeginRootWindow();
                // Exactly the columns' total width, so the stretch on col 2 absorbs nothing.
                if (app.BeginTable(tableID, cols, state, LayoutOverride{}.Size(300, 200)))
                {
                    app.BeginRow();
                    app.Cell("a"); app.Cell("b"); app.Cell("c");
                    app.EndRow();
                    app.EndTable();
                }
                app.EndRootWindow();
            };
            app.Frame(build);

            const auto sizerID = cols[0].internal.sizerID;
            ASSERT_TRUE(app.HasElement(sizerID));
            const auto sizerRect = app.GetElementRect(sizerID);
            const auto headerRect = app.elementStorage.GetElementRectUnsafe(
                cols[0].internal.headerRef).rect;
            EXPECT_NEAR(sizerRect.x, headerRect.x + headerRect.w, 1)
                << "Sizer 0 must sit at right edge of header cell 0 "
                << "(sizer.x=" << sizerRect.x
                << " header.right=" << (headerRect.x + headerRect.w) << ")";
        }

        // The sizer is anchored to a header cell and must track its post-scroll rect.
        TEST_F(TableCellsTest, SizerXFollowsHorizontalScroll)
        {
            tachyon::TableState state{};
            tachyon::VirtualTableColumn wideCols[3] = {
                { { .header = "Name", .initialWidth = 240, .minWidth = 80, .maxWidth = 600,
                    .sizing = tachyon::ColumnSizing::Resizable } },
                { { .header = "Size", .initialWidth = 100, .minWidth = 40, .maxWidth = 400,
                    .sizing = tachyon::ColumnSizing::Resizable } },
                { { .header = "Type", .initialWidth = 200, .minWidth = 40, .maxWidth = 400 } },
            };
            auto build = [&] {
                app.BeginRootWindow();
                if (app.BeginTable(tableID, wideCols, state, LayoutOverride{}.Size(300, 200)))
                {
                    app.BeginRow();
                    app.Cell("a"); app.Cell("b"); app.Cell("c"); app.EndRow();
                    app.EndTable();
                }
                app.EndRootWindow();
            };
            app.Frame(build);

            // Scroll the body horizontally.
            const auto bodyCenter = app.CenterOf(state.internal.bodyID);
            app.InjectMouseMove(bodyCenter);
            app.InjectMouseScroll(bodyCenter, int2{ -3, 0 });
            app.Frame(build);
            app.Frame(build); // settle the scroll

            const auto sizerRect  = app.GetElementRect(wideCols[0].internal.sizerID);
            const auto headerRect = app.elementStorage.GetElementRectUnsafe(
                wideCols[0].internal.headerRef).rect;
            EXPECT_NEAR(sizerRect.x, headerRect.x + headerRect.w, 1)
                << "After scrolling, sizer must still track header cell right edge";
        }

        // Same invariant with a Stretch column left of the boundary.
        TEST_F(TableCellsTest, SizerXAlignsAfterStretchColumn)
        {
            tachyon::TableState state{};
            tachyon::VirtualTableColumn cols[3] = {
                { { .header = "A", .initialWidth = 100, .minWidth = 40,
                    .sizing = tachyon::ColumnSizing::Stretch } },
                { { .header = "B", .initialWidth = 100, .minWidth = 40 } },
                { { .header = "C", .initialWidth = 100, .minWidth = 40 } },
            };
            auto build = [&] {
                app.BeginRootWindow();
                if (app.BeginTable(tableID, cols, state, LayoutOverride{}.Size(500, 200)))
                {
                    app.BeginRow();
                    app.Cell("a"); app.Cell("b"); app.Cell("c");
                    app.EndRow();
                    app.EndTable();
                }
                app.EndRootWindow();
            };
            app.Frame(build);

            // Sizer 0 tracks Stretch column A's right edge, including its 1fr share.
            const auto sizerRect  = app.GetElementRect(cols[0].internal.sizerID);
            const auto headerRect = app.elementStorage.GetElementRectUnsafe(
                cols[0].internal.headerRef).rect;
            EXPECT_NEAR(sizerRect.x, headerRect.x + headerRect.w, 1);
        }

        // ---------------------------------------------------------------------
        // Standard-mode body section has a post-layout callback attached: it
        // samples ElementInnerRect into TableState::internal.lastViewportH/W and the
        // overflow flags, which VirtualTable consumes on the next frame.
        // ---------------------------------------------------------------------
        TEST_F(TableCellsTest, StandardBodyHasViewportSamplingPostLayout)
        {
            tachyon::TableState state{};
            auto build = [&] {
                app.BeginRootWindow();
                if (app.BeginTable(tableID, kColumns, state, LayoutOverride{}.Size(400, 200)))
                {
                    app.BeginRow();
                    app.Cell("a"); app.Cell("b"); app.Cell("c"); app.EndRow();
                    app.EndTable();
                }
                app.EndRootWindow();
            };
            app.Frame(build);

            const auto bodyRef = app.elementStorage.FindElementRefByID(state.SectionBodyID(0));
            ASSERT_TRUE(bodyRef.IsValid());
            bool found = false;
            for (const auto& entry : app.elementStorage.GetPostLayoutEntries())
                if (entry.ownerRef == bodyRef) { found = true; break; }
            EXPECT_TRUE(found)
                << "Section body must carry a post-layout sampler for its viewport size.";
        }

    } // namespace
} // namespace tachyon
