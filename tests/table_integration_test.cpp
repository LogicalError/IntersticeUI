// Integration tests for Table / VirtualTable event handlers: real headless frames,
// so BeginTable's handlers fire and the resulting TableState can be checked.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_core/gui_id.h>
#include <gui_core/gui_id_manager.h>

#include <string>
#include <vector>

namespace interstice
{
    using GUITest::HeadlessGUIApp;

    namespace {

        class TableIntegrationTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;

            // A tiny, stable test dataset. Sortable on all three columns so we can
            // exercise click-to-sort paths and verify the sort-column resolution.
            interstice::VirtualTableColumn kColumns[3] = {
                { {.header = "Name",  .initialWidth = 120, .minWidth = 40, .maxWidth = 400, .sortable = true } },
                { {.header = "Type",  .initialWidth = 100, .minWidth = 40, .maxWidth = 400, .sortable = true } },
                { {.header = "Value", .initialWidth = 140, .minWidth = 40, .maxWidth = 400, .sortable = true } },
            };

            interstice::ID  tableID{};

            void SetUp() override
            {
                app.Initialize();
                // Stable ID for the table across frames — a dynamic id would churn
                // every frame and nuke the persisted selection state.
                tableID = app.idManager.CreateStaticID();
            }

            void TearDown() override
            {
                app.Cleanup();
            }

            // Convenience: render a 3-row, 3-column table. Row IDs are allocated
            // with a fixed seed so selectedRowID stays consistent across frames.
            void EmitSimpleTable(interstice::TableState& state)
            {
                app.BeginRootWindow();
                if (app.BeginTable(tableID, kColumns, state, LayoutOverride{}.Size(400, 200)))
                {
                    static const char* rows[][3] = {
                        { "Position", "float3", "1,2,3" },
                        { "Rotation", "float4", "0,0,0,1" },
                        { "Scale",    "float3", "1,1,1" },
                    };
                    for (int r = 0; r < 3; r++)
                    {
                        app.BeginRow();
                        app.Cell(rows[r][0]);
                        app.Cell(rows[r][1]);
                        app.Cell(rows[r][2]);
                        app.EndRow();
                    }
                    app.EndTable();
                }
                app.EndRootWindow();
            }
        };

        // ---------------------------------------------------------------------------
        // Table construction + layout
        // ---------------------------------------------------------------------------

        TEST_F(TableIntegrationTest, TableContainerIsPlacedInStorage)
        {
            // Driving a single frame is enough to verify the table container and
            // header cells actually land in ElementStorage at sensible rects.
            interstice::TableState state{};
            app.Frame([&] { EmitSimpleTable(state); });
            ASSERT_TRUE(app.HasElement(tableID));
            auto rect = app.GetElementRect(tableID);
            EXPECT_GT(rect.w, 0);
            EXPECT_GT(rect.h, 0);
        }

        // The header section grid and the body grid share their column-gap
        // through every code path that emits sizers (header strip + body row).
        // If the two diverge, sizers in the header land at a different X than
        // the body's, leaving sizers visually offset from the column boundary.
        TEST_F(TableIntegrationTest, HeaderAndBodyGapMatchAcrossThemes)
        {
            const auto* headerLs = app.styleManager.GetLayoutStyle(
                app.theme.tableHeaderSectionStyleRef.layoutStyleIndex);
            const auto* bodyLs = app.styleManager.GetLayoutStyle(
                app.theme.tableScrollableBodyStyleRef.layoutStyleIndex);
            ASSERT_NE(headerLs, nullptr);
            ASSERT_NE(bodyLs, nullptr);
            EXPECT_EQ(headerLs->layoutData.grid.column.gap,
                      bodyLs->layoutData.grid.column.gap);
        }

        // Zero columns must not crash. BeginTable still runs through to
        // EndTable, the section helpers short-circuit on colStart >= colEnd,
        // and no row IDs accumulate.
        TEST_F(TableIntegrationTest, EmptyColumnArrayDoesNotCrash)
        {
            interstice::TableState state{};
            app.Frame([&] {
                app.BeginRootWindow();
                if (app.BeginTable(tableID,
                                   /*columns*/static_cast<interstice::VirtualTableColumn*>(nullptr),
                                   /*columnCount*/0u, state,
                                   LayoutOverride{}.Size(200, 100)))
                    app.EndTable();
                app.EndRootWindow();
            });
            EXPECT_TRUE(app.HasElement(tableID));
            EXPECT_EQ(state.internal.totalRowCount, 0);
        }

        // No BeginRow calls: header still emits, body has no backdrops, and
        // _orderedRowIDs stays empty.
        TEST_F(TableIntegrationTest, ZeroRowsEmitsHeaderButNoBackdrops)
        {
            interstice::TableState state{};
            app.Frame([&] {
                app.BeginRootWindow();
                if (app.BeginTable(tableID, kColumns, state, LayoutOverride{}.Size(400, 200)))
                    app.EndTable();
                app.EndRootWindow();
            });
            EXPECT_TRUE(app.HasElement(tableID));
            EXPECT_EQ(state.internal.totalRowCount, 0);
            EXPECT_TRUE(kColumns[0].internal.headerRef.IsValid());
        }

        TEST_F(TableIntegrationTest, ColumnWidthsLazyInitFromDefaults)
        {
            interstice::TableState state{};
            EXPECT_TRUE(state.columnWidths.empty());
            app.Frame([&] { EmitSimpleTable(state); });
            ASSERT_EQ(state.columnWidths.size(), 3u);
            EXPECT_EQ(state.columnWidths[0], 120);
            EXPECT_EQ(state.columnWidths[1], 100);
            EXPECT_EQ(state.columnWidths[2], 140);
        }

        // Column widths are CSS pixels: they follow a live UI scale change.
        TEST_F(TableIntegrationTest, ColumnWidthsFollowTheUIScale)
        {
            interstice::TableState state{};
            app.Frame([&] { EmitSimpleTable(state); });
            app.Frame([&] { EmitSimpleTable(state); });
            auto headerWidth = [&] {
                return app.GetElementRect(app.elementStorage.GetElementUnsafe(kColumns[0].internal.headerRef).id).w;
            };
            const int w1 = headerWidth();
            ASSERT_GT(w1, 0);

            app.SetUIScale(2.0f);
            app.Frame([&] { EmitSimpleTable(state); });
            app.Frame([&] { EmitSimpleTable(state); });
            EXPECT_EQ(state.columnWidths[0], 120) << "stored in CSS pixels";
            EXPECT_EQ(headerWidth(), 2 * w1);
        }

        TEST_F(TableIntegrationTest, SortColumnResolvesToFirstSortableWhenUnset)
        {
            // Default sortColumn is -1. BeginTable should auto-resolve it to the
            // first sortable column (here, col 0).
            interstice::TableState state{};
            EXPECT_EQ(state.sortColumn, -1);
            app.Frame([&] { EmitSimpleTable(state); });
            EXPECT_EQ(state.sortColumn, 0);
            EXPECT_FALSE(state.sortDescending);
        }

        TEST_F(TableIntegrationTest, SortColumnClampsToSortableWhenOutOfRange)
        {
            interstice::TableState state{};
            state.sortColumn = 99;  // absurd value
            app.Frame([&] { EmitSimpleTable(state); });
            // Clamped back to the first sortable column.
            EXPECT_EQ(state.sortColumn, 0);
        }

        // ---------------------------------------------------------------------------
        // Sort — clicking a header with `sortable` set toggles sort state.
        // ---------------------------------------------------------------------------

        TEST_F(TableIntegrationTest, ClickingDifferentColumnHeaderChangesSortColumn)
        {
            interstice::TableState state{};
            // Frame 1 establishes the table (default sort = column 0 asc).
            app.Frame([&] { EmitSimpleTable(state); });
            ASSERT_EQ(state.sortColumn, 0);

            // Move mouse over column 2's header and click. Header row sits at
            // the top of the table container; column 2 starts at 120+100 = 220
            // from the table's left edge. Add a little padding offset.
            auto tableRect = app.GetElementRect(tableID);
            interstice::int2 col2HeaderPos{ tableRect.x + 120 + 100 + 20, tableRect.y + 8 };

            app.InjectMouseMove(col2HeaderPos);
            app.InjectMouseDown(col2HeaderPos);
            app.Frame([&] { EmitSimpleTable(state); });

            EXPECT_EQ(state.sortColumn, 2);
            EXPECT_FALSE(state.sortDescending);
        }

        TEST_F(TableIntegrationTest, ClickingSameColumnHeaderTogglesDirection)
        {
            interstice::TableState state{};
            app.Frame([&] { EmitSimpleTable(state); });
            ASSERT_EQ(state.sortColumn, 0);
            ASSERT_FALSE(state.sortDescending);

            // Helper: click-and-release on column 0's header. Two distinct events
            // (Down + Up) so each click is actually a fresh press (LeftPressed()
            // needs prevButtons == None ≠ buttons == Left).
            auto tableRect = app.GetElementRect(tableID);
            interstice::int2 col0HeaderPos{ tableRect.x + 40, tableRect.y + 8 };
            auto clickAtCol0 = [&] {
                app.InjectMouseMove(col0HeaderPos);
                app.InjectMouseDown(col0HeaderPos);
                app.InjectMouseUp(col0HeaderPos);
                };

            // Click once → descending.
            clickAtCol0();
            app.Frame([&] { EmitSimpleTable(state); });
            EXPECT_EQ(state.sortColumn, 0);
            EXPECT_TRUE(state.sortDescending);

            // Click again → ascending.
            clickAtCol0();
            app.Frame([&] { EmitSimpleTable(state); });
            EXPECT_EQ(state.sortColumn, 0);
            EXPECT_FALSE(state.sortDescending);
        }

        // ---------------------------------------------------------------------------
        // Row selection — clicking a row backdrop sets selectedRowID.
        // ---------------------------------------------------------------------------

        TEST_F(TableIntegrationTest, ClickingRowSelectsIt)
        {
            interstice::TableState state{};
            app.Frame([&] { EmitSimpleTable(state); });
            EXPECT_EQ(state.selectedRowID, interstice::ID::Invalid());

            // Row IDs are positional sub-range IDs; unsorted, visual == dataset.
            ASSERT_EQ(state.internal.totalRowCount, 3);
            const interstice::ID row1ID = state.RowIDForIndex(1);

            // Derive the hit position from the row backdrop's actual rect — row
            // heights depend on font metrics so hard-coding a y-offset is fragile.
            auto row1Center = app.CenterOf(row1ID);
            app.InjectMouseMove(row1Center);
            app.InjectMouseDown(row1Center);
            app.InjectMouseUp(row1Center);
            app.Frame([&] { EmitSimpleTable(state); });

            EXPECT_EQ(state.selectedRowID, row1ID);
        }

        // ---------------------------------------------------------------------------
        // Column reorder — dragging a header past another column's edge fires
        // onReorderColumns(from, to).
        // ---------------------------------------------------------------------------

        TEST_F(TableIntegrationTest, DraggingHeaderFiresReorderCallback)
        {
            interstice::TableState state{};
            struct { int from = -1, to = -1; int calls = 0; } seen;
            state.onReorderColumns = [&](int f, int t) { seen.from = f; seen.to = t; seen.calls++; };

            // Frame 1: establish table, header rects become available.
            app.Frame([&] { EmitSimpleTable(state); });

            // Press on column 0 header (x in [0,120)).
            auto tableRect = app.GetElementRect(tableID);
            interstice::int2 start{ tableRect.x + 40, tableRect.y + 8 };
            app.InjectMouseMove(start);
            app.InjectMouseDown(start);
            app.Frame([&] { EmitSimpleTable(state); });

            // Drag past column 1's left edge (x=120) by a wide margin so the
            // drag-threshold is crossed and the edge-crossing logic targets col 2.
            interstice::int2 dragTo{ tableRect.x + 250, tableRect.y + 8 };
            app.InjectMouseMove(dragTo, MouseButtons::Left);
            app.Frame([&] { EmitSimpleTable(state); });

            // Release — should fire onReorderColumns(0, target).
            app.InjectMouseUp(dragTo);
            app.Frame([&] { EmitSimpleTable(state); });

            EXPECT_EQ(seen.calls, 1);
            EXPECT_EQ(seen.from, 0);
            EXPECT_GT(seen.to, 0);   // moved past its original slot
        }

        TEST_F(TableIntegrationTest, ClickWithoutDragStillTriggersSort)
        {
            // When onReorderColumns is set, a simple click (no drag) on a sortable
            // header still toggles sort state — the threshold check distinguishes
            // the gestures.
            interstice::TableState state{};
            bool reorderCalled = false;
            state.onReorderColumns = [&](int, int) { reorderCalled = true; };

            app.Frame([&] { EmitSimpleTable(state); });
            ASSERT_EQ(state.sortColumn, 0);

            auto tableRect = app.GetElementRect(tableID);
            interstice::int2 click{ tableRect.x + 40 + 120 + 10, tableRect.y + 8 }; // column 1
            app.InjectMouseMove(click);
            app.InjectMouseDown(click);
            app.InjectMouseUp(click);  // no mouse move → below threshold
            app.Frame([&] { EmitSimpleTable(state); });

            EXPECT_EQ(state.sortColumn, 1);
            EXPECT_FALSE(reorderCalled);
        }

        TEST_F(TableIntegrationTest, ActiveColumnDragShiftsNeighboursToMakeRoom)
        {
            // Dragging column 0 past column 2 shifts columns 1 and 2 left by
            // (draggedWidth + column-gap) to open the drop slot, as in
            // BeginReorderableGroup. Checked on the final element rects.
            interstice::TableState state{};
            state.onReorderColumns = [](int, int) {};  // enable reorder path

            app.Frame([&] { EmitSimpleTable(state); });
            ASSERT_TRUE(kColumns[1].internal.headerRef.IsValid());
            const auto col1HeaderID_before = app.elementStorage.GetElementUnsafe(
                kColumns[1].internal.headerRef).id;
            const auto col1X_before = app.GetElementRect(col1HeaderID_before).x;

            // Start drag on column 0.
            auto tableRect = app.GetElementRect(tableID);
            interstice::int2 start{ tableRect.x + 40, tableRect.y + 8 };
            app.InjectMouseMove(start);
            app.InjectMouseDown(start);
            app.Frame([&] { EmitSimpleTable(state); });

            // Drag far to the right — well past column 1's and column 2's left
            // edges so the target column is the last one. One Frame commits the
            // drag state in dispatch; the NEXT frame's construction reads it and
            // applies the preview offsets before layout runs.
            app.InjectMouseMove({ tableRect.x + 350, tableRect.y + 8 }, MouseButtons::Left);
            app.Frame([&] { EmitSimpleTable(state); });
            app.Frame([&] { EmitSimpleTable(state); });

            const auto col1HeaderID = app.elementStorage.GetElementUnsafe(
                kColumns[1].internal.headerRef).id;
            const int32_t col1X_during = app.GetElementRect(col1HeaderID).x;

            EXPECT_LT(col1X_during, col1X_before)
                << "Column 1 should have shifted LEFT to make room for the "
                "dragged column. Before=" << col1X_before
                << " during=" << col1X_during;

            // Release — next frame should snap back to natural positions.
            app.InjectMouseUp({ tableRect.x + 350, tableRect.y + 8 });
            app.Frame([&] { EmitSimpleTable(state); });
        }

        TEST_F(TableIntegrationTest, LockedColumnRefusesToInitiateDrag)
        {
            // A locked column doesn't participate in drag-to-reorder: clicking
            // and dragging its header must not fire onReorderColumns no matter
            // how far the user drags. Click-to-sort on a sortable locked column
            // still works.
            interstice::TableState state{};
            int reorderCalls = 0;
            state.onReorderColumns = [&](int, int) { reorderCalls++; };

            interstice::VirtualTableColumn cols[] = {
                { {.header = "Pinned", .initialWidth = 120, .sortable = true, .locked = true } },
                { {.header = "B",      .initialWidth = 100, .sortable = true } },
                { {.header = "C",      .initialWidth = 100, .sortable = true } },
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

            // Drag the locked column far to the right — should be inert for
            // reorder purposes.
            auto tableRect = app.GetElementRect(tableID);
            interstice::int2 start{ tableRect.x + 40, tableRect.y + 8 };
            app.InjectMouseMove(start);
            app.InjectMouseDown(start);
            app.Frame(build);
            app.InjectMouseMove({ tableRect.x + 400, tableRect.y + 8 }, MouseButtons::Left);
            app.Frame(build);
            app.InjectMouseUp({ tableRect.x + 400, tableRect.y + 8 });
            app.Frame(build);

            EXPECT_EQ(reorderCalls, 0);
        }

        TEST_F(TableIntegrationTest, LockedColumnAtFrontPreventsOthersFromMovingInFront)
        {
            // Locked column at index 0 acts as a left barrier: dragging columns
            // to its left should clamp the insertion index to 1, never 0.
            interstice::TableState state{};
            std::pair<int, int> lastReorder{ -1, -1 };
            state.onReorderColumns = [&](int f, int t) { lastReorder = { f, t }; };

            interstice::VirtualTableColumn cols[] = {
                { {.header = "Pinned", .initialWidth = 80,  .locked = true } },
                { {.header = "B",      .initialWidth = 100 } },
                { {.header = "C",      .initialWidth = 100 } },
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

            // Drag column 2 ("C") as far left as possible — should stop at 1,
            // not 0 (the locked pinned column keeps the left edge fixed).
            auto tableRect = app.GetElementRect(tableID);
            // Column 2 starts after 80 + gap + 100 + gap = ~188 from table edge.
            interstice::int2 start{ tableRect.x + 220, tableRect.y + 8 };
            app.InjectMouseMove(start);
            app.InjectMouseDown(start);
            app.Frame(build);
            app.InjectMouseMove({ tableRect.x - 100, tableRect.y + 8 }, MouseButtons::Left);
            app.Frame(build);
            app.InjectMouseUp({ tableRect.x - 100, tableRect.y + 8 });
            app.Frame(build);

            EXPECT_EQ(lastReorder.first, 2);
            EXPECT_EQ(lastReorder.second, 1)
                << "Dragged column should be clamped past the locked column at "
                "index 0 — target=1 is as far left as it can go.";
        }

        TEST_F(TableIntegrationTest, LockedColumnAtEndPreventsOthersFromMovingPast)
        {
            interstice::TableState state{};
            std::pair<int, int> lastReorder{ -1, -1 };
            state.onReorderColumns = [&](int f, int t) { lastReorder = { f, t }; };

            interstice::VirtualTableColumn cols[] = {
                { {.header = "A",      .initialWidth = 100 } },
                { {.header = "B",      .initialWidth = 100 } },
                { {.header = "Pinned", .initialWidth = 80,  .locked = true } },
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

            // Drag column 0 ("A") all the way to the right — should stop at 1,
            // not 2 (the locked column pins the right edge).
            auto tableRect = app.GetElementRect(tableID);
            interstice::int2 start{ tableRect.x + 40, tableRect.y + 8 };
            app.InjectMouseMove(start);
            app.InjectMouseDown(start);
            app.Frame(build);
            app.InjectMouseMove({ tableRect.x + 600, tableRect.y + 8 }, MouseButtons::Left);
            app.Frame(build);
            app.InjectMouseUp({ tableRect.x + 600, tableRect.y + 8 });
            app.Frame(build);

            EXPECT_EQ(lastReorder.first, 0);
            EXPECT_EQ(lastReorder.second, 1)
                << "Dragged column should not cross the locked column at index 2 "
                "— target=1 is as far right as it can go.";
        }

        TEST_F(TableIntegrationTest, DroppingInOriginalSlotDoesNotFireCallback)
        {
            // If the drag never crosses another column's edge (target == from),
            // onReorderColumns should be skipped even though a real drag happened.
            interstice::TableState state{};
            int reorderCalls = 0;
            state.onReorderColumns = [&](int, int) { reorderCalls++; };

            app.Frame([&] { EmitSimpleTable(state); });

            auto tableRect = app.GetElementRect(tableID);
            interstice::int2 start{ tableRect.x + 40, tableRect.y + 8 };
            app.InjectMouseMove(start);
            app.InjectMouseDown(start);
            app.Frame([&] { EmitSimpleTable(state); });

            // Commit to drag via a small right-ward motion — past the 4px threshold
            // but not past column 1's left edge (at x=120 from the table's origin).
            interstice::int2 nudge{ tableRect.x + 50, tableRect.y + 8 };
            app.InjectMouseMove(nudge, MouseButtons::Left);
            app.Frame([&] { EmitSimpleTable(state); });

            app.InjectMouseUp(nudge);
            app.Frame([&] { EmitSimpleTable(state); });

            EXPECT_EQ(reorderCalls, 0);
        }

        TEST_F(TableIntegrationTest, EscapeCancelsActiveColumnDrag)
        {
            // During a column drag the header holds mouse capture; Escape reaches it
            // through keyboard-to-capture routing, without moving focus.
            interstice::TableState state{};
            int reorderCalls = 0;
            state.onReorderColumns = [&](int, int) { reorderCalls++; };

            app.Frame([&] { EmitSimpleTable(state); });

            auto tableRect = app.GetElementRect(tableID);
            interstice::int2 start{ tableRect.x + 40, tableRect.y + 8 };
            app.InjectMouseMove(start);
            app.InjectMouseDown(start);
            app.Frame([&] { EmitSimpleTable(state); });

            // Commit to a drag far to the right so on a MouseUp we WOULD reorder.
            app.InjectMouseMove({ tableRect.x + 300, tableRect.y + 8 }, MouseButtons::Left);
            app.Frame([&] { EmitSimpleTable(state); });

            // Escape — should cancel the drag AND release capture.
            app.InjectKey(KeyCode::Escape);
            app.Frame([&] { EmitSimpleTable(state); });

            // Subsequent MouseUp must NOT trigger the reorder callback — the drag
            // was cancelled, not completed.
            app.InjectMouseUp({ tableRect.x + 300, tableRect.y + 8 });
            app.Frame([&] { EmitSimpleTable(state); });

            EXPECT_EQ(reorderCalls, 0);
        }

        // ---------------------------------------------------------------------------
        // VirtualTable — the virtual row slice is driven by state.internal.lastViewportH and
        // the persistent scroll offset. Without any scroll, only leading rows render.
        // ---------------------------------------------------------------------------

        TEST_F(TableIntegrationTest, VirtualTableEmitsOnlyVisibleRows)
        {
            interstice::TableState state{};
            int cellsEmitted = 0;

            interstice::VirtualTableColumn vcols[] = {
                { {.header = "#",     .initialWidth = 80 },
                  [&](int rowIdx) { char b[16]; std::snprintf(b, sizeof(b), "%d", rowIdx); app.Cell(b); cellsEmitted++; } },
                { {.header = "Name",  .initialWidth = 160 },
                  [&](int rowIdx) { (void)rowIdx; app.Cell("row"); cellsEmitted++; } },
            };

            const int totalRows = 50'000;
            app.Frame([&] {
                app.BeginRootWindow();
                app.VirtualTable(tableID, state, vcols, totalRows,
                    /*rowHeight*/ 28,
                    /*style*/ StyleRef{}, LayoutOverride{}.Size(400, 200));
                app.EndRootWindow();
                });

            // Far less than 50 000 rows × 2 cols. On frame 1 viewportH is still 0
            // so only a small kBufferRows slice is emitted (6 rows × 2 cols = 12).
            EXPECT_LT(cellsEmitted, 50);
            EXPECT_GT(cellsEmitted, 0);
        }

        TEST_F(TableIntegrationTest, VirtualTableBodyHeightIsStableAcrossFrames)
        {
            // A VirtualTable body's intrinsic height is enormous (50 000 rows of
            // spacer). With flex-basis honoured its rect stays the same across frames
            // instead of oscillating with the sampled viewport height.
            interstice::TableState state{};
            state.internal.lastViewportH = 400;

            interstice::VirtualTableColumn vcols[] = {
                { {.header = "A", .initialWidth = 80 },
                  [&](int rowIdx) { (void)rowIdx; app.Cell("a"); } },
                { {.header = "B", .initialWidth = 80 },
                  [&](int rowIdx) { (void)rowIdx; app.Cell("b"); } },
            };

            auto build = [&] {
                app.BeginRootWindow();
                app.VirtualTable(tableID, state, vcols, /*totalRows*/50'000,
                    /*rowHeight*/28, StyleRef{},
                    LayoutOverride{}.Size(400, 400));
                app.EndRootWindow();
                };

            app.Frame(build);
            const auto bodyRect1 = app.GetElementRect(state.internal.bodyID);
            app.Frame(build);
            const auto bodyRect2 = app.GetElementRect(state.internal.bodyID);
            app.Frame(build);
            const auto bodyRect3 = app.GetElementRect(state.internal.bodyID);

            // Height must be stable (not drifting with viewport-sampling feedback).
            EXPECT_EQ(bodyRect1.h, bodyRect2.h);
            EXPECT_EQ(bodyRect2.h, bodyRect3.h);
            // Sanity: body height must be a SMALL fraction of the virtual content
            // (50 000 × 28 = 1.4M px). If flex-basis isn't honoured the body would
            // report ≥ 1M px here.
            EXPECT_LT(bodyRect1.h, 500);
            EXPECT_GT(bodyRect1.h, 0);
        }

        TEST_F(TableIntegrationTest, LastColumnExpandsToFillLeftoverWidth)
        {
            // In a table wider than its fixed columns, the last (Fractional) column is
            // at least its initialWidth and absorbs the leftover width.
            interstice::TableState state{};
            // Three columns with configured widths summing to 80+100+120 = 300.
            // Table width 600 → 300px of leftover width should go to the last col.
            interstice::VirtualTableColumn cols[] = {
                { {.header = "A", .initialWidth = 80 } },
                { {.header = "B", .initialWidth = 100 } },
                { {.header = "C", .initialWidth = 120 } },
            };

            app.Frame([&] {
                app.BeginRootWindow();
                if (app.BeginTable(tableID, cols, state, LayoutOverride{}.Size(600, 200)))
                {
                    app.BeginRow();
                    app.Cell("a");
                    app.Cell("b");
                    app.Cell("c");
                    app.EndRow();
                    app.EndTable();
                }
                app.EndRootWindow();
                });

            ASSERT_TRUE(cols[2].internal.headerRef.IsValid());
            const auto lastHeaderID = app.elementStorage.GetElementUnsafe(
                cols[2].internal.headerRef).id;
            const auto lastHeaderRect = app.GetElementRect(lastHeaderID);

            // The last column should be at least its configured initialWidth
            // (120) and visibly wider — it took the leftover width.
            EXPECT_GE(lastHeaderRect.w, 120);
            EXPECT_GT(lastHeaderRect.w, 120)
                << "Last column should absorb leftover width, not just fit its "
                "configured initialWidth.";
        }

        TEST_F(TableIntegrationTest, LastColumnNeverShrinksBelowConfiguredWidth)
        {
            // Symmetric: even when the table is NARROWER than the sum of fixed
            // columns, the last column must not collapse — it uses its configured
            // width as a floor and the outer wrapper handles horizontal overflow.
            interstice::TableState state{};
            interstice::VirtualTableColumn cols[] = {
                { {.header = "A", .initialWidth = 200 } },
                { {.header = "B", .initialWidth = 200 } },
                { {.header = "C", .initialWidth = 200 } },
            };

            // Table width 400 — less than sum of 600.
            app.Frame([&] {
                app.BeginRootWindow();
                if (app.BeginTable(tableID, cols, state, LayoutOverride{}.Size(400, 200)))
                {
                    app.BeginRow();
                    app.Cell("a"); app.Cell("b"); app.Cell("c");
                    app.EndRow();
                    app.EndTable();
                }
                app.EndRootWindow();
                });

            ASSERT_TRUE(cols[2].internal.headerRef.IsValid());
            const auto lastHeaderID = app.elementStorage.GetElementUnsafe(
                cols[2].internal.headerRef).id;
            const auto lastHeaderRect = app.GetElementRect(lastHeaderID);
            EXPECT_GE(lastHeaderRect.w, 200)
                << "Last column should not shrink below its configured width "
                "even when the table is narrower than all columns combined.";
        }

        TEST_F(TableIntegrationTest, VirtualTableHeaderKeepsVisibleHeightUnderHugeBody)
        {
            // The header section must not shrink: the body's huge intrinsic height
            // (50 000 spacer rows) would otherwise squeeze the header to ~0px.
            interstice::TableState state{};
            state.internal.lastViewportH = 400;

            interstice::VirtualTableColumn vcols[] = {
                { {.header = "A", .initialWidth = 80 },
                  [&](int rowIdx) { (void)rowIdx; app.Cell("a"); } },
                { {.header = "B", .initialWidth = 80 },
                  [&](int rowIdx) { (void)rowIdx; app.Cell("b"); } },
            };

            auto build = [&] {
                app.BeginRootWindow();
                app.VirtualTable(tableID, state, vcols, /*totalRows*/50'000,
                    /*rowHeight*/28, StyleRef{},
                    LayoutOverride{}.Size(400, 400));
                app.EndRootWindow();
                };

            app.Frame(build);

            ASSERT_TRUE(vcols[0].internal.headerRef.IsValid());
            const auto headerID = app.elementStorage.GetElementUnsafe(
                vcols[0].internal.headerRef).id;
            const auto headerRect = app.GetElementRect(headerID);
            EXPECT_GT(headerRect.h, 8)
                << "Header collapsed under flex-shrink pressure from the "
                "huge virtual body. Header section needs flex-shrink:0.";
            EXPECT_GT(headerRect.w, 0);
        }

        TEST_F(TableIntegrationTest, StickyHeaderPinsTopWhenScrolled)
        {
            // Header and body are sibling grids, so scrolling the body leaves the
            // header's Y unchanged.
            interstice::TableState state{};
            state.internal.lastViewportH = 400;

            interstice::VirtualTableColumn vcols[] = {
                { {.header = "A", .initialWidth = 80 },
                  [&](int rowIdx) { (void)rowIdx; app.Cell("a"); } },
                { {.header = "B", .initialWidth = 80 },
                  [&](int rowIdx) { (void)rowIdx; app.Cell("b"); } },
            };

            auto build = [&] {
                app.BeginRootWindow();
                app.VirtualTable(tableID, state, vcols, /*totalRows*/500,
                    /*rowHeight*/28, StyleRef{},
                    LayoutOverride{}.Size(400, 400));
                app.EndRootWindow();
                };

            // Frame 1: lay out at scroll 0 so the body id exists.
            app.Frame(build);

            const auto headerAID0 = !vcols[0].internal.headerRef.IsValid()
                ? interstice::ID::Invalid()
                : app.elementStorage.GetElementUnsafe(vcols[0].internal.headerRef).id;
            ASSERT_NE(headerAID0, interstice::ID::Invalid());
            ASSERT_NE(state.internal.bodyID, interstice::ID::Invalid());
            const auto headerY_unscrolled = app.GetElementRect(headerAID0).y;

            // Scroll the body, which holds overflow:auto (not the outer wrapper).
            auto handle = app.guiStateManager.GetScrollManagement().GetOrCreateScrollOffsetHandle(state.internal.bodyID);
            app.guiStateManager.GetScrollManagement().SetScrollOffset(handle, interstice::int2{ 0, -100 });

            // Frame 2: body rows shift up; header stays.
            app.Frame(build);

            const auto headerAID = !vcols[0].internal.headerRef.IsValid()
                ? interstice::ID::Invalid()
                : app.elementStorage.GetElementUnsafe(vcols[0].internal.headerRef).id;
            ASSERT_NE(headerAID, interstice::ID::Invalid());
            const auto headerY_scrolled = app.GetElementRect(headerAID).y;

            EXPECT_NEAR(headerY_scrolled, headerY_unscrolled, 1)
                << "Sticky header should not shift when the table scrolls. "
                << "Unscrolled y=" << headerY_unscrolled
                << " scrolled y=" << headerY_scrolled;
        }

        // A table squeezed to nothing for a few frames (a sibling taking all the
        // room) keeps its scroll position for when it comes back.
        TEST_F(TableIntegrationTest, SqueezedTableKeepsItsScrollPosition)
        {
            interstice::TableState state{};
            interstice::VirtualTableColumn vcols[] = {
                { {.header = "A", .initialWidth = 80 }, [&](int) { app.Cell("a"); } },
            };
            int siblingHeight = 100;
            auto build = [&] {
                app.BeginRootWindow();
                app.BeginVertical(app.theme.flexFillColStyleRef, LayoutOverride{}.Size(400, 400));
                app.BeginVertical(LayoutOverride{}.Height(siblingHeight).NoShrink());
                app.EndVertical();
                app.BeginHorizontal(app.theme.flexFillRowStyleRef, LayoutOverride{}.FlexGrow(1));
                app.VirtualTable(tableID, state, vcols, /*totalRows*/500,
                    /*rowHeight*/28, StyleRef{}, LayoutOverride{}.FlexGrow(1));
                app.EndHorizontal();
                app.EndVertical();
                app.EndRootWindow();
            };
            app.Frame(build);
            app.Frame(build);
            ASSERT_NE(state.internal.bodyID, interstice::ID::Invalid());
            auto& sm = app.guiStateManager.GetScrollManagement();
            sm.SetScrollOffset(sm.GetOrCreateScrollOffsetHandle(state.SectionBodyID(0)), interstice::int2{ 0, -100 });
            app.Frame(build);
            ASSERT_EQ(sm.GetScrollOffset(sm.GetScrollOffsetHandle(state.internal.bodyID)).y, -100) << "scrolled before the squeeze";

            siblingHeight = 400;
            app.Frame(build);
            app.Frame(build);
            siblingHeight = 100;
            app.Frame(build);
            app.Frame(build);

            const auto handle = sm.GetScrollOffsetHandle(state.internal.bodyID);
            ASSERT_TRUE(sm.IsScrollOffsetHandleValid(handle));
            EXPECT_EQ(sm.GetScrollOffset(handle).y, -100);
        }

        // Without a theme-measured row height the table measures its cell style,
        // rather than estimating from the font size.
        TEST_F(TableIntegrationTest, RowHeightFallbackIsMeasured)
        {
            const TextID probe = app.textSystem->RegisterText(interstice::ID::Invalid(), "Mg", app.theme.tableCellStyleRef, interstice::ElementState::None);
            int32_t expected = app.textSystem->GetTextMeasurement(probe).minHeight;
            if (const auto* ls = app.styleManager.GetLayoutStyle(app.theme.tableCellStyleRef.layoutStyleIndex))
                expected += ls->padding.Vertical() + ls->borderWidths.Vertical();

            app.theme.tableRowHeight = 0;
            interstice::TableState state{};
            interstice::VirtualTableColumn vcols[] = {
                { {.header = "A", .initialWidth = 80 }, [&](int) { app.Cell("a"); } },
            };
            app.Frame([&] {
                app.BeginRootWindow();
                app.VirtualTable(tableID, state, vcols, 10, /*rowHeight*/0, StyleRef{}, LayoutOverride{}.Size(400, 200));
                app.EndRootWindow();
            });
            EXPECT_EQ(state.internal.actualRowHeight, expected);
        }

        TEST_F(TableIntegrationTest, VirtualTableSliceExpandsWhenViewportHeightKnown)
        {
            // The production viewport-height measurement happens in a custom-draw
            // callback that only runs during RENDER. Our headless harness stops at
            // layout, so we simulate the sampled height directly on the TableState
            // and verify VirtualTable uses it on the next frame.
            interstice::TableState state{};
            int cellsEmitted = 0;
            int* counter = &cellsEmitted;

            interstice::VirtualTableColumn vcols[] = {
                { {.header = "#",     .initialWidth = 80 },
                  [&](int rowIdx) { (void)rowIdx; app.Cell("n"); (*counter)++; } },
            };

            auto build = [&] {
                app.BeginRootWindow();
                app.VirtualTable(tableID, state, vcols, 
                    /*totalRows*/50'000, /*rowHeight*/28,
                    StyleRef{}, LayoutOverride{}.Size(400, 400));
                app.EndRootWindow();
                };

            // Frame 1: viewport height not yet known → leading buffer only.
            cellsEmitted = 0;
            app.Frame(build);
            const int frame1Cells = cellsEmitted;

            // Simulate the custom-draw hook sampling the container's viewport.
            state.internal.lastViewportH = 400;

            // Frame 2 now has a viewport → should emit far more rows.
            cellsEmitted = 0;
            app.Frame(build);
            const int frame2Cells = cellsEmitted;

            EXPECT_GT(frame2Cells, frame1Cells);
            // Sanity: still a tiny fraction of the full 50 000 rows.
            EXPECT_LT(frame2Cells, 200);
        }

        // ---------------------------------------------------------------------------
        // Keyboard navigation — Down arrow moves selection to the next row.
        // ---------------------------------------------------------------------------

        TEST_F(TableIntegrationTest, DownArrowAdvancesSelectedRow)
        {
            interstice::TableState state{};
            app.Frame([&] { EmitSimpleTable(state); });
            ASSERT_EQ(state.internal.totalRowCount, 3);

            // Click row 0 to establish selection + focus on the table container.
            // Hit position comes from the row backdrop's actual rect — row
            // heights depend on font metrics so a hard-coded y-offset is fragile.
            auto row0Pos = app.CenterOf(state.RowIDForIndex(0));
            app.InjectMouseMove(row0Pos);
            app.InjectMouseDown(row0Pos);
            app.InjectMouseUp(row0Pos);
            app.Frame([&] { EmitSimpleTable(state); });
            ASSERT_EQ(state.selectedRowID, state.RowIDForIndex(0));

            // Press Down — should advance to row 1.
            app.InjectKey(KeyCode::Down);
            app.Frame([&] { EmitSimpleTable(state); });
            EXPECT_EQ(state.selectedRowID, state.RowIDForIndex(1));

            // Down again → row 2.
            app.InjectKey(KeyCode::Down);
            app.Frame([&] { EmitSimpleTable(state); });
            EXPECT_EQ(state.selectedRowID, state.RowIDForIndex(2));

            // Down at the bottom is clamped — still row 2.
            app.InjectKey(KeyCode::Down);
            app.Frame([&] { EmitSimpleTable(state); });
            EXPECT_EQ(state.selectedRowID, state.RowIDForIndex(2));
        }

        TEST_F(TableIntegrationTest, HomeAndEndJumpToBoundaryRows)
        {
            interstice::TableState state{};
            app.Frame([&] { EmitSimpleTable(state); });
            ASSERT_EQ(state.internal.totalRowCount, 3);

            // Click to focus the table.
            auto row1Pos = app.CenterOf(state.RowIDForIndex(1));
            app.InjectMouseMove(row1Pos);
            app.InjectMouseDown(row1Pos);
            app.InjectMouseUp(row1Pos);
            app.Frame([&] { EmitSimpleTable(state); });

            app.InjectKey(KeyCode::End);
            app.Frame([&] { EmitSimpleTable(state); });
            EXPECT_EQ(state.selectedRowID, state.RowIDForIndex(state.internal.totalRowCount - 1));

            app.InjectKey(KeyCode::Home);
            app.Frame([&] { EmitSimpleTable(state); });
            EXPECT_EQ(state.selectedRowID, state.RowIDForIndex(0));
        }

        // ---------------------------------------------------------------------------
        // Keyboard navigation — Page Up / Page Down jump by the visible row count.
        // The handler reads the body's inner rect and walks _orderedRowIDs to find
        // which rows fully fit; cursor advances/retreats by that count.
        // ---------------------------------------------------------------------------

        // 30 rows × 3 cols, 200px tall body — wide enough for clicks to land.
        // Row count is chosen so PageDown advances by more than one but the
        // table still fits in a few pages.
        struct PagingTable
        {
            HeadlessGUIApp& app;
            interstice::ID tableID;
            interstice::VirtualTableColumn* cols;
            size_t colCount;
            int rowCount;
            void emit(interstice::TableState& state) const
            {
                app.BeginRootWindow();
                if (app.BeginTable(tableID, cols, colCount, state,
                                   LayoutOverride{}.Size(400, 200)))
                {
                    for (int r = 0; r < rowCount; r++)
                    {
                        app.BeginRow();
                        app.Cell("a"); app.Cell("b"); app.Cell("c");
                        app.EndRow();
                    }
                    app.EndTable();
                }
                app.EndRootWindow();
            }
        };

        TEST_F(TableIntegrationTest, PageDownAdvancesByVisibleRowCount)
        {
            interstice::TableState state{};
            PagingTable t{ app, tableID, kColumns, 3, 30 };
            auto build = [&] { t.emit(state); };
            app.Frame(build);
            // Headless never renders, so simulate the render-time viewport
            // sampling that drives rows-per-page (same as the slice tests).
            state.internal.lastViewportH = 200;

            // Click row 0 to focus the container.
            const interstice::ID firstID = state.RowIDForIndex(0);
            const auto firstCenter = app.CenterOf(firstID);
            app.InjectMouseMove(firstCenter);
            app.InjectMouseDown(firstCenter);
            app.InjectMouseUp(firstCenter);
            app.Frame(build);
            ASSERT_EQ(state.selectedRowID, firstID);

            const int beforeIdx = 0;
            app.InjectKey(KeyCode::PageDown);
            app.Frame(build);

            int afterIdx = -1;
            for (int i = 0; i < state.internal.totalRowCount; i++)
                if (state.RowIDForIndex(i) == state.selectedRowID) { afterIdx = i; break; }
            EXPECT_GT(afterIdx, beforeIdx)
                << "PageDown should advance the cursor at least one row.";
        }

        TEST_F(TableIntegrationTest, PageUpRetreatsByVisibleRowCount)
        {
            interstice::TableState state{};
            PagingTable t{ app, tableID, kColumns, 3, 30 };
            auto build = [&] { t.emit(state); };
            app.Frame(build);
            state.internal.lastViewportH = 200;   // simulate render-time sampling

            // Click row 0 to give the container focus. Rows past the viewport
            // aren't hit-testable; set cursor state directly to "near the
            // bottom" instead of clicking it.
            const interstice::ID firstID = state.RowIDForIndex(0);
            const auto firstCenter = app.CenterOf(firstID);
            app.InjectMouseMove(firstCenter);
            app.InjectMouseDown(firstCenter);
            app.InjectMouseUp(firstCenter);
            app.Frame(build);
            const int beforeIdx = state.internal.totalRowCount - 1;
            state.selectedRowID = state.RowIDForIndex(beforeIdx);

            app.InjectKey(KeyCode::PageUp);
            app.Frame(build);

            int afterIdx = -1;
            for (int i = 0; i < state.internal.totalRowCount; i++)
                if (state.RowIDForIndex(i) == state.selectedRowID) { afterIdx = i; break; }
            EXPECT_LT(afterIdx, beforeIdx)
                << "PageUp should retreat the cursor at least one row.";
        }

        TEST_F(TableIntegrationTest, PageDownStopsAtLastRow)
        {
            interstice::TableState state{};
            PagingTable t{ app, tableID, kColumns, 3, 30 };
            auto build = [&] { t.emit(state); };
            app.Frame(build);
            state.internal.lastViewportH = 200;   // simulate render-time sampling

            // Click first row to focus the container, then point the cursor
            // at the last row by hand — the last row is offscreen so a click
            // there won't dispatch.
            const interstice::ID firstID = state.RowIDForIndex(0);
            const auto firstCenter = app.CenterOf(firstID);
            app.InjectMouseMove(firstCenter);
            app.InjectMouseDown(firstCenter);
            app.InjectMouseUp(firstCenter);
            app.Frame(build);
            const interstice::ID lastID = state.RowIDForIndex(state.internal.totalRowCount - 1);
            state.selectedRowID = lastID;

            app.InjectKey(KeyCode::PageDown);
            app.Frame(build);
            EXPECT_EQ(state.selectedRowID, lastID);
        }

        TEST_F(TableIntegrationTest, PageUpStopsAtFirstRow)
        {
            interstice::TableState state{};
            PagingTable t{ app, tableID, kColumns, 3, 30 };
            auto build = [&] { t.emit(state); };
            app.Frame(build);
            state.internal.lastViewportH = 200;   // simulate render-time sampling

            const interstice::ID firstID = state.RowIDForIndex(0);
            const auto firstCenter = app.CenterOf(firstID);
            app.InjectMouseMove(firstCenter);
            app.InjectMouseDown(firstCenter);
            app.InjectMouseUp(firstCenter);
            app.Frame(build);
            ASSERT_EQ(state.selectedRowID, firstID);

            app.InjectKey(KeyCode::PageUp);
            app.Frame(build);
            EXPECT_EQ(state.selectedRowID, firstID);
        }

    } // namespace
}