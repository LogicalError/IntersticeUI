// VirtualTable tests: scrolling, keyboard navigation, column drag/sort and
// tree cells. Scroll offsets are read directly from ScrollManagement.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_controls/gui_application.h>
#include <gui_core/state/gui_state_manager.h>
#include <gui_core/state/gui_scroll_management.h>

namespace interstice
{
    using GUITest::HeadlessGUIApp;

    namespace
    {
        // Mirrors the constant in controls_scrollbar.cpp / controls_table_internal.h.
        constexpr int32_t kScrollbarThickness = 12;

        class TableVirtualScrollTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;
            interstice::ID tableID{};

            // 4 columns wider than the body so the H-scrollbar shows up.
            std::vector<interstice::VirtualTableColumn> columns;

            void SetUp() override
            {
                app.Initialize();
                tableID = app.idManager.CreateStaticID();

                columns.clear();
                columns.push_back({ { .header = "A", .initialWidth = 200, .minWidth = 60, .maxWidth = 400 },
                                    [this](int r) { char b[16]; std::snprintf(b, sizeof(b), "A%05d", r); app.Cell(b); } });
                columns.push_back({ { .header = "B", .initialWidth = 200, .minWidth = 60, .maxWidth = 400 },
                                    [this](int r) { char b[16]; std::snprintf(b, sizeof(b), "B%05d", r); app.Cell(b); } });
                columns.push_back({ { .header = "C", .initialWidth = 200, .minWidth = 60, .maxWidth = 400 },
                                    [this](int r) { char b[16]; std::snprintf(b, sizeof(b), "C%05d", r); app.Cell(b); } });
                columns.push_back({ { .header = "D", .initialWidth = 200, .minWidth = 60, .maxWidth = 400 },
                                    [this](int r) { char b[16]; std::snprintf(b, sizeof(b), "D%05d", r); app.Cell(b); } });
            }
            void TearDown() override { app.Cleanup(); }

            // Body container 400×300; columns total ~800 → overflow X.
            // 200 rows × ~24 px → overflow Y.
            void Emit(interstice::TableState& state, int rowCount = 200,
                      int width = 400, int height = 300)
            {
                app.BeginRootWindow();
                std::span<interstice::VirtualTableColumn> span(columns.data(), columns.size());
                app.VirtualTable(tableID, state, span, rowCount, /*rowHeight*/0,
                                 app.theme.tableStyleRef,
                                 LayoutOverride{}.Size(width, height));
                app.EndRootWindow();
            }

            // A panel-sized pane holding a mode bar and a 50k-row table, like the demo's Table panel.
            interstice::ID paneID = interstice::ID::Invalid();
            IntRect EmitInPane(interstice::TableState& state, StyleRef tableStyle, LayoutOverride&& tableLayout)
            {
                if (paneID == interstice::ID::Invalid())
                    paneID = app.idManager.CreateStaticID();
                app.Frame([&] {
                    app.BeginRootWindow();
                    app.BeginArea(paneID, app.theme.emptyVertStyleRef, LayoutOverride{}.Size(530, 400));
                    app.Button(app.idManager.AllocateDynamicID("mode"), "Row", [] {}, LayoutOverride{}.NoGrow().NoShrink());
                    std::span<interstice::VirtualTableColumn> span(columns.data(), columns.size());
                    app.VirtualTable(tableID, state, span, 50000, 0, tableStyle, std::move(tableLayout));
                    app.EndArea();
                    app.EndRootWindow();
                });
                return app.GetElementRect(tableID);
            }

            int2 SectionScroll(interstice::TableState& state, int sectionIdx = 0)
            {
                auto& sm = app.guiStateManager.GetScrollManagement();
                const interstice::ID id = state.SectionBodyID(sectionIdx);
                if (id == interstice::ID::Invalid()) return int2{0,0};
                auto h = sm.GetScrollOffsetHandle(id);
                if (!sm.IsScrollOffsetHandleValid(h)) return int2{0,0};
                return sm.GetScrollOffset(h);
            }

            // Headless skips render, so pin internal.lastViewportH/W and the overflow flags
            // that its custom-draw would set.
            void PinViewport(interstice::TableState& state,
                             bool xOverflow = true, bool yOverflow = true)
            {
                state.internal.lastViewportH = 298;
                state.internal.lastViewportW = 398;
                state.internal.anyHorzOverflow = xOverflow;
                state.internal.anyVertOverflow = yOverflow;
            }
        };

        // ---------------------------------------------------------------
        // Wheel-scrolling all the way down is idempotent: once at max,
        // more wheel input doesn't shift the offset by a row.
        // ---------------------------------------------------------------
        TEST_F(TableVirtualScrollTest, WheelAtBottomDoesNotOscillate)
        {
            interstice::TableState state{};
            auto pin = [&]{ PinViewport(state); };
            pin();
            app.Frame([&]{ Emit(state); pin(); });
            app.Frame([&]{ Emit(state); pin(); });

            const auto rect = app.GetElementRect(tableID);
            const int2 center{ rect.x + rect.w / 2, rect.y + rect.h / 2 };

            // Mash wheel-down enough ticks to reach max.
            for (int i = 0; i < 200; ++i)
            {
                app.InjectMouseMove(center);
                app.InjectMouseScroll(center, int2{ 0, -1 });
                app.Frame([&]{ Emit(state); pin(); });
            }

            const int2 atMax = SectionScroll(state);
            ASSERT_LT(atMax.y, 0) << "expected to be scrolled down to a negative offset";

            // More wheel-down at max must not bounce the offset back up by a row.
            for (int i = 0; i < 5; ++i)
            {
                app.InjectMouseMove(center);
                app.InjectMouseScroll(center, int2{ 0, -1 });
                app.Frame([&]{ Emit(state); pin(); });
                EXPECT_EQ(SectionScroll(state).y, atMax.y)
                    << "wheel-down at max must be idempotent (iteration " << i
                    << "); was " << atMax.y << ", became " << SectionScroll(state).y;
            }
        }

        // ---------------------------------------------------------------
        // A scrollbar drag writes the section body's offset; the virtualized row
        // range must follow it on the next frame, or the viewport flashes empty.
        // ---------------------------------------------------------------
        TEST_F(TableVirtualScrollTest, ScrollbarJumpUpdatesRowWindowSameFrame)
        {
            interstice::TableState state{};
            auto pin = [&]{ PinViewport(state); };
            pin();
            app.Frame([&]{ Emit(state); pin(); });
            app.Frame([&]{ Emit(state); pin(); });

            const interstice::ID sectionID = state.SectionBodyID(0);
            ASSERT_NE(sectionID, interstice::ID::Invalid());
            ASSERT_GT(state.internal.actualRowHeight, 0);

            // Jump the section body to row 100, as a thumb drag does.
            constexpr int targetRow = 100;
            const int32_t jumpY = targetRow * state.internal.actualRowHeight;
            auto& sm = app.guiStateManager.GetScrollManagement();
            sm.SetScrollOffset(sm.GetOrCreateScrollOffsetHandle(sectionID), int2{ 0, -jumpY });

            app.Frame([&]{ Emit(state); pin(); });

            const interstice::ID rowAtTarget = state.RowIDForIndex(targetRow);
            const interstice::ID rowAtTop    = state.RowIDForIndex(0);
            ASSERT_NE(rowAtTarget, interstice::ID::Invalid());

            EXPECT_TRUE(app.HasElement(rowAtTarget))
                << "row " << targetRow << " must be virtualized the same frame the "
                   "scrollbar jumped the section to it (no one-frame empty-content lag)";
            EXPECT_FALSE(app.HasElement(rowAtTop))
                << "row 0 must be outside the window after jumping to row " << targetRow;
        }

        // ---------------------------------------------------------------
        // After wheel-scrolling all the way down, the last row's bottom
        // must clear the H-scrollbar overlay (sit at least
        // kScrollbarThickness above section_inner_bottom).
        // ---------------------------------------------------------------
        TEST_F(TableVirtualScrollTest, LastRowClearsHorizontalScrollbarAtMaxScroll)
        {
            interstice::TableState state{};
            auto pin = [&]{ PinViewport(state); };
            pin();
            app.Frame([&]{ Emit(state); pin(); });
            app.Frame([&]{ Emit(state); pin(); });

            const auto rect = app.GetElementRect(tableID);
            const int2 center{ rect.x + rect.w / 2, rect.y + rect.h / 2 };

            // Scroll past the bottom.
            for (int i = 0; i < 250; ++i)
            {
                app.InjectMouseMove(center);
                app.InjectMouseScroll(center, int2{ 0, -1 });
                app.Frame([&]{ Emit(state); pin(); });
            }

            // After scrolling to the bottom, the last dataset row is emitted.
            const interstice::ID lastRowID = state.RowIDForIndex(state.internal.totalRowCount - 1);
            ASSERT_NE(lastRowID, interstice::ID::Invalid());
            ASSERT_TRUE(app.HasElement(lastRowID));

            const auto lastRowRect = app.GetElementRect(lastRowID);

            const interstice::ID sectionID = state.SectionBodyID(0);
            ASSERT_NE(sectionID, interstice::ID::Invalid());
            auto sectionRef = app.elementStorage.FindElementRefByID(sectionID);
            ASSERT_TRUE(sectionRef.IsValid());

            const auto& sectionRect = app.elementStorage.GetElementRectUnsafe(sectionRef);
            const int32_t sectionInnerBottom = sectionRect.innerRect.y + sectionRect.innerRect.h;
            const int32_t lastRowBottom      = lastRowRect.y + lastRowRect.h;

            EXPECT_LE(lastRowBottom, sectionInnerBottom - kScrollbarThickness)
                << "last row bottom (" << lastRowBottom << ") must clear the H-scrollbar "
                << "(section inner bottom " << sectionInnerBottom
                << ", scrollbar thickness " << kScrollbarThickness << ")";
        }

        // ---------------------------------------------------------------
        // Tree-cell variant, shaped like the tree-table panel: column 0 emits a
        // TreeCell and the rest are plain Cells.
        // ---------------------------------------------------------------
        TEST_F(TableVirtualScrollTest, WheelAtBottomDoesNotOscillate_TreeTable)
        {
            // Every row is depth 0, no children, no rename.
            columns.clear();
            columns.push_back({ { .header = "Name", .initialWidth = 240, .minWidth = 80, .maxWidth = 600, .locked = true },
                                [this](int r) {
                                    char b[24]; std::snprintf(b, sizeof(b), "Item%05d", r);
                                    app.TreeCell(b, /*depth*/0, /*isOpen*/nullptr,
                                                 IconID::Invalid, /*ancestorMask*/0,
                                                 /*isLastSibling*/true, /*renameBuffer*/nullptr);
                                } });
            columns.push_back({ { .header = "Size", .initialWidth = 100, .minWidth = 40, .maxWidth = 200 },
                                [this](int r) { char b[16]; std::snprintf(b, sizeof(b), "%dKB", r); app.Cell(b); } });
            columns.push_back({ { .header = "Type", .initialWidth = 120, .minWidth = 40, .maxWidth = 200 },
                                [this](int r) { app.Cell((r % 2) ? "File" : "Folder"); } });

            interstice::TableState state{};
            auto pin = [&]{ PinViewport(state); };
            pin();
            app.Frame([&]{ Emit(state); pin(); });
            app.Frame([&]{ Emit(state); pin(); });

            const auto rect = app.GetElementRect(tableID);
            const int2 center{ rect.x + rect.w / 2, rect.y + rect.h / 2 };

            for (int i = 0; i < 250; ++i)
            {
                app.InjectMouseMove(center);
                app.InjectMouseScroll(center, int2{ 0, -1 });
                app.Frame([&]{ Emit(state); pin(); });
            }

            const int2 atMax = SectionScroll(state);
            ASSERT_LT(atMax.y, 0);

            // Sustained wheel-down at max — verify no oscillation across many frames.
            for (int i = 0; i < 10; ++i)
            {
                app.InjectMouseMove(center);
                app.InjectMouseScroll(center, int2{ 0, -1 });
                app.Frame([&]{ Emit(state); pin(); });
                EXPECT_EQ(SectionScroll(state).y, atMax.y)
                    << "tree-table wheel-down at max must be idempotent (iter " << i << ")";
            }
        }
        // Plain and tree cells must render at the same height, or the spacer math
        // drifts from layout and max-scroll oscillates.
        TEST_F(TableVirtualScrollTest, PlainAndTreeCellsRenderAtSameHeight)
        {
            // Plain table.
            interstice::TableState state{};
            auto pin = [&]{ PinViewport(state); };
            pin();
            app.Frame([&]{ Emit(state); pin(); });
            app.Frame([&]{ Emit(state); pin(); });
            ASSERT_GT(state.internal.totalRowCount, 0);
            const int32_t plainH = app.GetElementRect(state.RowIDForIndex(0)).h;

            // Tree-cell variant.
            columns.clear();
            columns.push_back({ { .header = "Name", .initialWidth = 240, .minWidth = 80, .maxWidth = 600, .locked = true },
                                [this](int r) {
                                    char b[24]; std::snprintf(b, sizeof(b), "Item%05d", r);
                                    app.TreeCell(b, /*depth*/0, /*isOpen*/nullptr,
                                                 IconID::Invalid, /*ancestorMask*/0,
                                                 /*isLastSibling*/true, /*renameBuffer*/nullptr);
                                } });
            columns.push_back({ { .header = "Size", .initialWidth = 100, .minWidth = 40, .maxWidth = 200 },
                                [this](int r) { char b[16]; std::snprintf(b, sizeof(b), "%d", r); app.Cell(b); } });

            interstice::TableState state2{};
            auto pin2 = [&]{ PinViewport(state2); };
            pin2();
            app.Frame([&]{ Emit(state2); pin2(); });
            app.Frame([&]{ Emit(state2); pin2(); });
            ASSERT_GT(state2.internal.totalRowCount, 0);
            const int32_t treeH = app.GetElementRect(state2.RowIDForIndex(0)).h;

            EXPECT_EQ(plainH, treeH)
                << "plain row " << plainH << " px vs tree row " << treeH
                << " px — mixing cell types in one table requires uniform row height";
        }

        // ---------------------------------------------------------------
        // The 12-px H-scrollbar pad at the bottom of the scroll range must only
        // apply when a section overflows X.
        // ---------------------------------------------------------------
        // Section body scroll range (contentH - innerH); needs a laid-out frame.
        static int32_t SectionScrollRange(GUITest::HeadlessGUIApp& app, interstice::ID sectionID)
        {
            auto ref = app.elementStorage.FindElementRefByID(sectionID);
            if (!ref.IsValid()) return 0;
            const auto& r = app.elementStorage.GetElementRectUnsafe(ref);
            return r.contentBounds.h - r.innerRect.h;
        }

        // The V-scrollbar must not be emitted when the table fits vertically
        // (its track would capture input).
        TEST_F(TableVirtualScrollTest, VScrollbarOnlyEmittedWhenContentOverflowsY)
        {
            // No-Y-overflow: pin yOverflow=false. Bar must NOT exist.
            interstice::TableState stateA{};
            auto pinA = [&]{ PinViewport(stateA, /*xOverflow*/true, /*yOverflow*/false); };
            pinA();
            app.Frame([&]{ Emit(stateA); pinA(); });
            app.Frame([&]{ Emit(stateA); pinA(); });
            EXPECT_FALSE(app.HasElement(stateA.internal.vertScrollbarID))
                << "V-scrollbar must not be emitted when content fits vertically";

            // Y-overflow: pin yOverflow=true. Bar must exist.
            interstice::TableState stateB{};
            auto pinB = [&]{ PinViewport(stateB, /*xOverflow*/true, /*yOverflow*/true); };
            pinB();
            app.Frame([&]{ Emit(stateB); pinB(); });
            app.Frame([&]{ Emit(stateB); pinB(); });
            EXPECT_TRUE(app.HasElement(stateB.internal.vertScrollbarID))
                << "V-scrollbar must be emitted when content overflows Y";
        }

        // The H-scrollbar must not be emitted when no section overflows X (its
        // track would eat clicks at the body's bottom).
        TEST_F(TableVirtualScrollTest, HScrollbarOnlyEmittedWhenAnySectionOverflows)
        {
            // No-overflow: pin xOverflow=false. Bar must NOT exist.
            interstice::TableState stateA{};
            auto pinA = [&]{ PinViewport(stateA, /*xOverflow*/false); };
            pinA();
            app.Frame([&]{ Emit(stateA); pinA(); });
            app.Frame([&]{ Emit(stateA); pinA(); });
            EXPECT_FALSE(app.HasElement(stateA.internal.horzScrollbarID))
                << "H-scrollbar must not be emitted when no section overflows X";

            // Overflow: pin xOverflow=true. Bar must exist.
            interstice::TableState stateB{};
            auto pinB = [&]{ PinViewport(stateB, /*xOverflow*/true); };
            pinB();
            app.Frame([&]{ Emit(stateB); pinB(); });
            app.Frame([&]{ Emit(stateB); pinB(); });
            EXPECT_TRUE(app.HasElement(stateB.internal.horzScrollbarID))
                << "H-scrollbar must be emitted when at least one section overflows X";
        }

        TEST_F(TableVirtualScrollTest, HScrollbarPadOnlyWhenAnySectionOverflows)
        {
            // Same table with and without X overflow.

            // Scenario A: NO X-overflow — flag false, no pad expected.
            interstice::TableState stateA{};
            auto pinA = [&]{ PinViewport(stateA, /*xOverflow*/false); };
            pinA();
            app.Frame([&]{ Emit(stateA); pinA(); });
            app.Frame([&]{ Emit(stateA); pinA(); });
            const int32_t rangeNoOverflow = SectionScrollRange(app, stateA.SectionBodyID(0));
            ASSERT_GT(rangeNoOverflow, 0);

            // Scenario B: X-overflow — flag true, pad expected.
            interstice::TableState stateB{};
            auto pinB = [&]{ PinViewport(stateB, /*xOverflow*/true); };
            pinB();
            app.Frame([&]{ Emit(stateB); pinB(); });
            app.Frame([&]{ Emit(stateB); pinB(); });
            const int32_t rangeOverflow = SectionScrollRange(app, stateB.SectionBodyID(0));
            ASSERT_GT(rangeOverflow, 0);

            // The overflow scenario is exactly kScrollbarThickness taller.
            EXPECT_EQ(rangeOverflow - rangeNoOverflow, kScrollbarThickness)
                << "H-scrollbar pad must add exactly " << kScrollbarThickness
                << " px when overflow=true and 0 when overflow=false. "
                << "noOverflow=" << rangeNoOverflow << " overflow=" << rangeOverflow;
        }

        // ---------------------------------------------------------------
        // Headers must be emitted after body rows so they paint on top of rows
        // scrolled under the sticky header.
        // ---------------------------------------------------------------
        TEST_F(TableVirtualScrollTest, HeadersEmittedAfterBodyRowsForCorrectPaintOrder)
        {
            interstice::TableState state{};
            auto pin = [&]{ PinViewport(state); };
            pin();
            app.Frame([&]{ Emit(state); pin(); });
            app.Frame([&]{ Emit(state); pin(); });

            const interstice::ID sectionID = state.SectionBodyID(0);
            ASSERT_NE(sectionID, interstice::ID::Invalid());

            ASSERT_GT(state.internal.totalRowCount, 0);
            const interstice::ID firstRowID = state.RowIDForIndex(0);
            ASSERT_TRUE(app.HasElement(firstRowID));
            const auto rowRef = app.elementStorage.FindElementRefByID(firstRowID);
            ASSERT_TRUE(rowRef.IsValid());

            const auto sectionRef = app.elementStorage.FindElementRefByID(sectionID);
            ASSERT_TRUE(sectionRef.IsValid());

            ASSERT_FALSE(columns.empty());
            ASSERT_NE(columns[0].internal.headerRef, interstice::ElementRef::Invalid())
                << "header ref must be captured in BeginTable for this check";
            const interstice::ElementRef headerRef = columns[0].internal.headerRef;

            // For siblings, HashCode (depth high, index low) orders by emission.
            EXPECT_GT(headerRef.HashCode(), rowRef.HashCode())
                << "header element must be emitted AFTER row backdrop "
                << "so it paints on top during sticky-Y scroll";
        }

        // ---------------------------------------------------------------
        // theme.tableRowHeight must match the laid-out row height, so the spacer
        // math matches layout.
        // ---------------------------------------------------------------
        TEST_F(TableVirtualScrollTest, ThemeTableRowHeightMatchesActualRendering)
        {
            // Render a normal (non-virtual) cell so layout produces a
            // real row height, then compare against theme.tableRowHeight.
            interstice::TableState state{};
            auto pin = [&]{ PinViewport(state); };
            pin();
            app.Frame([&]{ Emit(state); pin(); });
            app.Frame([&]{ Emit(state); pin(); });

            ASSERT_GT(state.internal.totalRowCount, 0);
            const auto rowRect = app.GetElementRect(state.RowIDForIndex(0));

            EXPECT_GT(app.theme.tableRowHeight, 0)
                << "theme.tableRowHeight must be initialized by ComputeRowMetrics";
            EXPECT_EQ(app.theme.tableRowHeight, rowRect.h)
                << "theme.tableRowHeight (" << app.theme.tableRowHeight
                << ") must match the actual rendered row height ("
                << rowRect.h << ") — drift here is what causes scroll "
                << "oscillation";
        }

        // ---------------------------------------------------------------
        // Column reorder drop-target must respect horizontal scroll: header rects
        // are captured at press time, so a mid-drag X scroll must be compensated.
        // ---------------------------------------------------------------
        TEST_F(TableVirtualScrollTest, ColumnReorderRespectsHorizontalScroll)
        {
            int reorderFrom = -1, reorderTo = -1;
            interstice::TableState state{};
            state.onReorderColumns = [&](int f, int t){ reorderFrom = f; reorderTo = t; };
            auto pin = [&]{ PinViewport(state); };
            pin();
            app.Frame([&]{ Emit(state); pin(); });
            app.Frame([&]{ Emit(state); pin(); });

            const interstice::ID sectionID = state.SectionBodyID(0);
            ASSERT_NE(sectionID, interstice::ID::Invalid());
            auto& sm = app.guiStateManager.GetScrollManagement();
            auto handle = sm.GetOrCreateScrollOffsetHandle(sectionID);

            ASSERT_GE(int(columns.size()), 4);
            const auto col1Ref = columns[1].internal.headerRef;
            const auto col3Ref = columns[3].internal.headerRef;
            const auto col1Rect = app.elementStorage.GetElementRectUnsafe(col1Ref).rect;
            const auto col3Rect = app.elementStorage.GetElementRectUnsafe(col3Ref).rect;
            const int2 col1Center{ col1Rect.x + col1Rect.w / 2, col1Rect.y + col1Rect.h / 2 };
            const int2 col3Center{ col3Rect.x + col3Rect.w / 2, col3Rect.y + col3Rect.h / 2 };

            // One event per Frame: the harness hit-tests once per frame at the last
            // mouse position.

            // Press on col1.
            app.InjectMouseMove(col1Center);
            app.InjectMouseDown(col1Center);
            app.Frame([&]{ Emit(state); pin(); });

            // Move toward col3 (no scroll yet), past the drag threshold.
            app.InjectMouseMove(col3Center, interstice::MouseButtons::Left);
            app.Frame([&]{ Emit(state); pin(); });

            // Scroll the section horizontally mid-drag.
            sm.SetScrollOffset(handle, int2{ -100, 0 });
            app.Frame([&]{ Emit(state); pin(); });

            // Move to where col3 now sits (100 left after the scroll); the drop target
            // must stay col3.
            const auto col3RectS = app.elementStorage.GetElementRectUnsafe(col3Ref).rect;
            const int2 col3CenterS{ col3RectS.x + col3RectS.w / 2, col3RectS.y + col3RectS.h / 2 };
            app.InjectMouseMove(col3CenterS, interstice::MouseButtons::Left);
            app.Frame([&]{ Emit(state); pin(); });

            // Release.
            app.InjectMouseUp(col3CenterS);
            app.Frame([&]{ Emit(state); pin(); });

            EXPECT_EQ(reorderFrom, 1);
            EXPECT_EQ(reorderTo, 3)
                << "drop target must compensate for horizontal scroll "
                << "delta during drag — was " << reorderTo;
        }

        // ---------------------------------------------------------------
        // While reordering, the dragged column emits a per-row backdrop so it isn't
        // transparent over the columns it overlaps: one extra element per visible row.
        // ---------------------------------------------------------------
        TEST_F(TableVirtualScrollTest, DraggedColumnShowsRowColorization)
        {
            // Helper: count immediate children of the section body.
            auto countSectionChildren = [&](interstice::ID sectionID) -> int {
                auto ref = app.elementStorage.FindElementRefByID(sectionID);
                if (!ref.IsValid()) return 0;
                return int(app.elementStorage.GetChildrenSpan(ref).size());
            };

            interstice::TableState state{};
            state.onReorderColumns = [](int, int){};
            auto pin = [&]{ PinViewport(state); };
            pin();
            app.Frame([&]{ Emit(state); pin(); });
            app.Frame([&]{ Emit(state); pin(); });

            const interstice::ID sectionID = state.SectionBodyID(0);
            ASSERT_NE(sectionID, interstice::ID::Invalid());

            const int childCountBeforeDrag = countSectionChildren(sectionID);
            ASSERT_GT(childCountBeforeDrag, 0);

            // Start a drag on col1, one event per Frame (one hit-test per frame).
            ASSERT_GE(int(columns.size()), 2);
            const auto col1Ref = columns[1].internal.headerRef;
            ASSERT_NE(col1Ref, interstice::ElementRef::Invalid());
            const auto col1Rect = app.elementStorage.GetElementRectUnsafe(col1Ref).rect;
            const int2 col1Center{ col1Rect.x + col1Rect.w / 2,
                                   col1Rect.y + col1Rect.h / 2 };

            app.InjectMouseMove(col1Center);
            app.InjectMouseDown(col1Center);
            app.Frame([&]{ Emit(state); pin(); });

            // Move past drag threshold (>4 px) to commit the drag.
            const int2 col1Off{ col1Center.x + 50, col1Center.y };
            app.InjectMouseMove(col1Off, interstice::MouseButtons::Left);
            app.Frame([&]{ Emit(state); pin(); });
            app.Frame([&]{ Emit(state); pin(); });

            const int childCountDuringDrag = countSectionChildren(sectionID);
            const int extraDuringDrag = childCountDuringDrag - childCountBeforeDrag;

            EXPECT_GT(extraDuringDrag, 0)
                << "during drag, extra row-colorization backdrops must be "
                << "emitted (got " << extraDuringDrag << ")";

            // End the drag; the extras disappear the frame after MouseUp.
            app.InjectMouseUp(col1Off);
            app.Frame([&]{ Emit(state); pin(); });
            app.Frame([&]{ Emit(state); pin(); });
            EXPECT_EQ(countSectionChildren(sectionID), childCountBeforeDrag)
                << "drag-column backdrops must NOT persist past drop";
        }

        // ---------------------------------------------------------------
        // The dragged column (cells, header and sizer) must be emitted last in the
        // section body so it paints over every other column's sizer.
        // ---------------------------------------------------------------
        TEST_F(TableVirtualScrollTest, DraggedColumnPaintsOnTopOfSizers)
        {
            interstice::TableState state{};
            state.onReorderColumns = [](int, int){};
            auto pin = [&]{ PinViewport(state); };
            pin();
            app.Frame([&]{ Emit(state); pin(); });
            app.Frame([&]{ Emit(state); pin(); });

            // Start a drag on column 1 and commit (>4 px).
            const auto col1Ref = columns[1].internal.headerRef;
            ASSERT_NE(col1Ref, interstice::ElementRef::Invalid());
            const auto col1Rect = app.elementStorage.GetElementRectUnsafe(col1Ref).rect;
            const int2 col1Center{ col1Rect.x + col1Rect.w / 2, col1Rect.y + col1Rect.h / 2 };
            app.InjectMouseMove(col1Center);
            app.InjectMouseDown(col1Center);
            app.Frame([&]{ Emit(state); pin(); });
            const int2 col1Off{ col1Center.x + 50, col1Center.y };
            app.InjectMouseMove(col1Off, interstice::MouseButtons::Left);
            app.Frame([&]{ Emit(state); pin(); });
            app.Frame([&]{ Emit(state); pin(); });

            // Re-fetch the header ref; the dragged column is re-emitted on top.
            const auto draggedHdrRef = app.elementStorage.FindElementRefByID(
                columns[1].internal.headerCellID);
            ASSERT_TRUE(draggedHdrRef.IsValid());

            // Sizer 0 (non-dragged) must be emitted before the dragged column's header.
            const auto sizer0Ref = app.elementStorage.FindElementRefByID(columns[0].internal.sizerID);
            ASSERT_TRUE(sizer0Ref.IsValid());

            EXPECT_LT(sizer0Ref.HashCode(), draggedHdrRef.HashCode())
                << "non-dragged sizer must be emitted BEFORE dragged column "
                << "stack, otherwise it paints over the visually-shifted "
                << "dragged column";

            app.InjectMouseUp(col1Off);
            app.Frame([&]{ Emit(state); pin(); });
        }

        // ---------------------------------------------------------------
        // Auto-scroll while dragging near the section's edges. Cursor in
        // the right-edge zone scrolls content left (offset.x decreases);
        // cursor in the left-edge zone scrolls content right (offset.x
        // increases toward 0).
        // ---------------------------------------------------------------
        TEST_F(TableVirtualScrollTest, ColumnDragAutoScrollsNearEdges)
        {
            interstice::TableState state{};
            state.onReorderColumns = [](int, int){};
            auto pin = [&]{ PinViewport(state); };
            pin();
            app.Frame([&]{ Emit(state); pin(); });
            app.Frame([&]{ Emit(state); pin(); });

            const interstice::ID sectionID = state.SectionBodyID(0);
            ASSERT_NE(sectionID, interstice::ID::Invalid());
            auto& sm = app.guiStateManager.GetScrollManagement();
            auto handle = sm.GetOrCreateScrollOffsetHandle(sectionID);

            // Start a drag on column 1.
            const auto col1Ref = columns[1].internal.headerRef;
            const auto col1Rect = app.elementStorage.GetElementRectUnsafe(col1Ref).rect;
            const int2 col1Center{ col1Rect.x + col1Rect.w / 2, col1Rect.y + col1Rect.h / 2 };
            app.InjectMouseMove(col1Center);
            app.InjectMouseDown(col1Center);
            app.Frame([&]{ Emit(state); pin(); });

            // Commit drag (>4 px).
            const int2 col1Off{ col1Center.x + 10, col1Center.y };
            app.InjectMouseMove(col1Off, interstice::MouseButtons::Left);
            app.Frame([&]{ Emit(state); pin(); });

            // Move into the 30-px right-edge zone of the body.
            const auto bodyRef = app.elementStorage.FindElementRefByID(sectionID);
            const auto bodyInner = app.elementStorage.GetElementRectUnsafe(bodyRef).innerRect;
            const int2 nearRight{ bodyInner.x + bodyInner.w - 5, col1Center.y };

            const int32_t scrollBefore = sm.GetScrollOffset(handle).x;
            app.InjectMouseMove(nearRight, interstice::MouseButtons::Left);
            app.Frame([&]{ Emit(state); pin(); });
            const int32_t scrollAfterRight = sm.GetScrollOffset(handle).x;
            EXPECT_LT(scrollAfterRight, scrollBefore)
                << "dragging into the right edge zone must scroll content "
                << "LEFT (offset.x decreases)";

            // Now move near LEFT edge — should scroll content right.
            // First scroll the section back so there's room to scroll right.
            sm.SetScrollOffset(handle, int2{ -100, 0 });
            app.Frame([&]{ Emit(state); pin(); });
            const int32_t scrollSet = sm.GetScrollOffset(handle).x;
            const int2 nearLeft{ bodyInner.x + 5, col1Center.y };
            app.InjectMouseMove(nearLeft, interstice::MouseButtons::Left);
            app.Frame([&]{ Emit(state); pin(); });
            const int32_t scrollAfterLeft = sm.GetScrollOffset(handle).x;
            EXPECT_GT(scrollAfterLeft, scrollSet)
                << "dragging into the left edge zone must scroll content "
                << "RIGHT (offset.x increases toward 0)";

            app.InjectMouseUp(nearLeft);
            app.Frame([&]{ Emit(state); pin(); });
        }

        // ---------------------------------------------------------------
        // Keyboard navigation must use the FULL dataset (not just the
        // virtualized window): Home selects row 0 of the dataset, End
        // selects the last row, and Up/Down at the edge of the visible
        // window step into rows that aren't currently emitted.
        // ---------------------------------------------------------------
        TEST_F(TableVirtualScrollTest, KeyboardHomeEndJumpToDatasetBoundsNotVisibleWindow)
        {
            interstice::TableState state{};
            auto pin = [&]{ PinViewport(state); };
            pin();
            app.Frame([&]{ Emit(state); pin(); });
            app.Frame([&]{ Emit(state); pin(); });

            // Click into the table body so it has keyboard focus.
            const auto rect = app.GetElementRect(tableID);
            const int2 center{ rect.x + rect.w / 2, rect.y + rect.h / 2 };
            app.InjectMouseMove(center);
            app.InjectMouseDown(center);
            app.InjectMouseUp(center);
            app.Frame([&]{ Emit(state); pin(); });

            ASSERT_GT(state.internal.totalRowCount, 0)
                << "VirtualTable must publish dataset row count";
            ASSERT_NE(state.internal.rowsRangeID, interstice::ID::Invalid())
                << "VirtualTable must publish the rows-range ID";

            // Scroll so row 0 is outside the virtualized window.
            for (int i = 0; i < 200; ++i)
            {
                app.InjectMouseMove(center);
                app.InjectMouseScroll(center, int2{ 0, -1 });
                app.Frame([&]{ Emit(state); pin(); });
            }

            // Home selects dataset row 0, not the first buffered row.
            app.InjectKey(interstice::KeyCode::Home);
            app.Frame([&]{ Emit(state); pin(); });

            ASSERT_EQ(state.selectedRowID.GetMode(), interstice::IDMode::SubRange);
            EXPECT_EQ(static_cast<int>(state.selectedRowID.value.subRangeParts.index), 0)
                << "Home must select dataset row 0, not the topmost visible row";

            // Press End — selection must jump to row totalRowCount-1.
            app.InjectKey(interstice::KeyCode::End);
            app.Frame([&]{ Emit(state); pin(); });

            ASSERT_EQ(state.selectedRowID.GetMode(), interstice::IDMode::SubRange);
            EXPECT_EQ(static_cast<int>(state.selectedRowID.value.subRangeParts.index),
                      state.internal.totalRowCount - 1)
                << "End must select dataset row N-1, not the bottommost visible row";
        }

        // Up/Down at the buffer boundary must step into rows outside the
        // virtualized window.
        TEST_F(TableVirtualScrollTest, KeyboardUpStepsBeyondVirtualizedTopBuffer)
        {
            interstice::TableState state{};
            auto pin = [&]{ PinViewport(state); };
            pin();
            app.Frame([&]{ Emit(state); pin(); });
            app.Frame([&]{ Emit(state); pin(); });

            const auto rect = app.GetElementRect(tableID);
            const int2 center{ rect.x + rect.w / 2, rect.y + rect.h / 2 };
            app.InjectMouseMove(center);
            app.InjectMouseDown(center);
            app.InjectMouseUp(center);
            app.Frame([&]{ Emit(state); pin(); });

            // Scroll deep into the dataset so row 0 isn't even buffered.
            for (int i = 0; i < 200; ++i)
            {
                app.InjectMouseMove(center);
                app.InjectMouseScroll(center, int2{ 0, -1 });
                app.Frame([&]{ Emit(state); pin(); });
            }

            // Select a row outside the window and press Up: selection steps to index - 1.
            ASSERT_GT(state.internal.totalRowCount, 50);
            const int startIdx = 50;
            state.selectedRowID = state.RowIDForIndex(startIdx);

            app.InjectKey(interstice::KeyCode::Up);
            app.Frame([&]{ Emit(state); pin(); });

            const int newIdx = static_cast<int>(state.selectedRowID.value.subRangeParts.index);
            EXPECT_EQ(newIdx, startIdx - 1)
                << "Up must step to the dataset row above regardless of "
                << "the virtualization window (was " << startIdx
                << ", became " << newIdx << ")";
        }

        // ---------------------------------------------------------------
        // Sizers must stay in the element store at any vertical scroll position.
        // ---------------------------------------------------------------
        TEST_F(TableVirtualScrollTest, SizersStayPresentWhenScrolling)
        {
            interstice::TableState state{};
            auto pin = [&]{ PinViewport(state); };
            pin();
            app.Frame([&]{ Emit(state); pin(); });
            app.Frame([&]{ Emit(state); pin(); });

            // Sanity: every column-boundary sizer exists at the top.
            for (size_t c = 0; c + 1 < columns.size(); ++c)
            {
                ASSERT_NE(columns[c].internal.sizerID, interstice::ID::Invalid()) << "col " << c;
                EXPECT_TRUE(app.HasElement(columns[c].internal.sizerID))
                    << "sizer at boundary " << c << " missing at top of scroll";
            }

            // Scroll deep into the table.
            const auto rect = app.GetElementRect(tableID);
            const int2 center{ rect.x + rect.w / 2, rect.y + rect.h / 2 };
            for (int i = 0; i < 200; ++i)
            {
                app.InjectMouseMove(center);
                app.InjectMouseScroll(center, int2{ 0, -1 });
                app.Frame([&]{ Emit(state); pin(); });
            }

            // After scrolling, sizers MUST still be present.
            for (size_t c = 0; c + 1 < columns.size(); ++c)
                EXPECT_TRUE(app.HasElement(columns[c].internal.sizerID))
                    << "sizer at boundary " << c
                    << " missing after scrolling deep into the table";
        }

        // ---------------------------------------------------------------
        // End must scroll the last row fully into view, its bottom clear of the
        // H-scrollbar overlay.
        // ---------------------------------------------------------------
        TEST_F(TableVirtualScrollTest, KeyboardEndFullyShowsLastRowAboveHScrollbar)
        {
            interstice::TableState state{};
            auto pin = [&]{ PinViewport(state); };
            pin();
            app.Frame([&]{ Emit(state); pin(); });
            app.Frame([&]{ Emit(state); pin(); });

            // Click into the table to give it focus.
            const auto rect = app.GetElementRect(tableID);
            const int2 center{ rect.x + rect.w / 2, rect.y + rect.h / 2 };
            app.InjectMouseMove(center);
            app.InjectMouseDown(center);
            app.InjectMouseUp(center);
            app.Frame([&]{ Emit(state); pin(); });

            app.InjectKey(interstice::KeyCode::End);
            // Three frames: dispatch sets the scroll, it propagates, then the new range
            // is virtualized and the last row emitted.
            app.Frame([&]{ Emit(state); pin(); });
            app.Frame([&]{ Emit(state); pin(); });
            app.Frame([&]{ Emit(state); pin(); });

            // The last row must clear the 12-px H-scrollbar strip.
            const interstice::ID lastRowID = state.RowIDForIndex(state.internal.totalRowCount - 1);
            ASSERT_TRUE(app.HasElement(lastRowID))
                << "End must bring the last dataset row into view";

            const auto lastRect = app.GetElementRect(lastRowID);
            const interstice::ID sectionID = state.SectionBodyID(0);
            ASSERT_NE(sectionID, interstice::ID::Invalid());
            auto sectionRef = app.elementStorage.FindElementRefByID(sectionID);
            ASSERT_TRUE(sectionRef.IsValid());
            const auto& sectionRect = app.elementStorage.GetElementRectUnsafe(sectionRef);
            const int32_t sectionInnerBottom = sectionRect.innerRect.y + sectionRect.innerRect.h;
            const int32_t lastRowBottom = lastRect.y + lastRect.h;

            EXPECT_LE(lastRowBottom, sectionInnerBottom - kScrollbarThickness)
                << "End must scroll so the last row's bottom is above the "
                << "H-scrollbar overlay (row bot=" << lastRowBottom
                << " section inner bot=" << sectionInnerBottom << ")";
        }

        // ---------------------------------------------------------------
        // Pressing Up must scroll so the new selected row's TOP clears
        // the sticky header — otherwise the header paints over the
        // row's top edge and the user can't see what's selected.
        // ---------------------------------------------------------------
        TEST_F(TableVirtualScrollTest, KeyboardUpScrollsSelectedRowBelowStickyHeader)
        {
            interstice::TableState state{};
            auto pin = [&]{ PinViewport(state); };
            pin();
            app.Frame([&]{ Emit(state); pin(); });
            app.Frame([&]{ Emit(state); pin(); });

            const auto rect = app.GetElementRect(tableID);
            const int2 center{ rect.x + rect.w / 2, rect.y + rect.h / 2 };
            app.InjectMouseMove(center);
            app.InjectMouseDown(center);
            app.InjectMouseUp(center);
            app.Frame([&]{ Emit(state); pin(); });

            // Scroll deep into the table, then press End so we're at the
            // bottom (selection on the last row, fully scrolled down).
            for (int i = 0; i < 200; ++i)
            {
                app.InjectMouseMove(center);
                app.InjectMouseScroll(center, int2{ 0, -1 });
                app.Frame([&]{ Emit(state); pin(); });
            }
            app.InjectKey(interstice::KeyCode::End);
            app.Frame([&]{ Emit(state); pin(); });
            app.Frame([&]{ Emit(state); pin(); });

            // Press Up until the selection reaches the top; it must sit below the header.
            for (int i = 0; i < 50; ++i)
            {
                app.InjectKey(interstice::KeyCode::Up);
                app.Frame([&]{ Emit(state); pin(); });
            }
            app.Frame([&]{ Emit(state); pin(); });

            const interstice::ID sel = state.selectedRowID;
            ASSERT_TRUE(app.HasElement(sel)) << "selected row must be emitted after Up navigation";
            const auto selRect = app.GetElementRect(sel);
            const interstice::ID sectionID = state.SectionBodyID(0);
            auto sectionRef = app.elementStorage.FindElementRefByID(sectionID);
            ASSERT_TRUE(sectionRef.IsValid());
            const auto& sectionRect = app.elementStorage.GetElementRectUnsafe(sectionRef);

            // The header's bottom (gridRow 0) is where body rows start.
            const int32_t headerBottom = sectionRect.innerRect.y + state.internal.actualRowHeight;
            EXPECT_GE(selRect.y, headerBottom)
                << "Up-navigated selected row's top (" << selRect.y
                << ") must be at or below the sticky header bottom ("
                << headerBottom << ")";
        }

        // ---------------------------------------------------------------
        // Pressing Down to the bottom-visible row must scroll so the
        // row's bottom clears the H-scrollbar overlay.
        // ---------------------------------------------------------------
        TEST_F(TableVirtualScrollTest, KeyboardDownScrollsLastVisibleRowAboveHScrollbar)
        {
            interstice::TableState state{};
            auto pin = [&]{ PinViewport(state); };
            pin();
            app.Frame([&]{ Emit(state); pin(); });
            app.Frame([&]{ Emit(state); pin(); });

            const auto rect = app.GetElementRect(tableID);
            const int2 center{ rect.x + rect.w / 2, rect.y + rect.h / 2 };
            app.InjectMouseMove(center);
            app.InjectMouseDown(center);
            app.InjectMouseUp(center);
            app.Frame([&]{ Emit(state); pin(); });

            // Press Down until the selection reaches the bottom of the window.
            for (int i = 0; i < 60; ++i)
            {
                app.InjectKey(interstice::KeyCode::Down);
                app.Frame([&]{ Emit(state); pin(); });
            }
            app.Frame([&]{ Emit(state); pin(); });

            const interstice::ID sel = state.selectedRowID;
            ASSERT_TRUE(app.HasElement(sel));
            const auto selRect = app.GetElementRect(sel);
            const interstice::ID sectionID = state.SectionBodyID(0);
            auto sectionRef = app.elementStorage.FindElementRefByID(sectionID);
            ASSERT_TRUE(sectionRef.IsValid());
            const auto& sectionRect = app.elementStorage.GetElementRectUnsafe(sectionRef);
            const int32_t sectionInnerBottom = sectionRect.innerRect.y + sectionRect.innerRect.h;
            const int32_t selRowBottom = selRect.y + selRect.h;

            EXPECT_LE(selRowBottom, sectionInnerBottom - kScrollbarThickness)
                << "Down-navigated selected row's bottom (" << selRowBottom
                << ") must clear the H-scrollbar overlay (section bot="
                << sectionInnerBottom << ", scrollbar=" << kScrollbarThickness << ")";
        }

        // ---------------------------------------------------------------
        // PageDown must not land on a row partially under the H-scrollbar overlay.
        // ---------------------------------------------------------------
        TEST_F(TableVirtualScrollTest, KeyboardPageDownLandsAboveHScrollbar)
        {
            interstice::TableState state{};
            auto pin = [&]{ PinViewport(state); };
            pin();
            app.Frame([&]{ Emit(state); pin(); });
            app.Frame([&]{ Emit(state); pin(); });

            // Click into the table to give it focus.
            const auto rect = app.GetElementRect(tableID);
            const int2 center{ rect.x + rect.w / 2, rect.y + rect.h / 2 };
            app.InjectMouseMove(center);
            app.InjectMouseDown(center);
            app.InjectMouseUp(center);
            app.Frame([&]{ Emit(state); pin(); });

            // One PageDown from the top lands on the last fully visible row.
            app.InjectKey(interstice::KeyCode::PageDown);
            app.Frame([&]{ Emit(state); pin(); });
            app.Frame([&]{ Emit(state); pin(); });

            const interstice::ID sel = state.selectedRowID;
            ASSERT_TRUE(app.HasElement(sel))
                << "PageDown's target row must be in the emit window";

            const auto selRect = app.GetElementRect(sel);
            const interstice::ID sectionID = state.SectionBodyID(0);
            auto sectionRef = app.elementStorage.FindElementRefByID(sectionID);
            ASSERT_TRUE(sectionRef.IsValid());
            const auto& sectionRect = app.elementStorage.GetElementRectUnsafe(sectionRef);
            const int32_t sectionInnerBottom = sectionRect.innerRect.y + sectionRect.innerRect.h;
            const int32_t selRowBottom = selRect.y + selRect.h;

            EXPECT_LE(selRowBottom, sectionInnerBottom - kScrollbarThickness)
                << "PageDown must land selection on a row whose bottom is "
                << "above the H-scrollbar overlay (row bot=" << selRowBottom
                << ", section inner bot=" << sectionInnerBottom << ")";
        }

        // ---------------------------------------------------------------
        // The keyboard handler must derive H overflow from layout, not from
        // internal.anyHorzOverflow: that flag is set at render, after input dispatch.
        // ---------------------------------------------------------------
        TEST_F(TableVirtualScrollTest, KeyboardPageDownClearsHScrollbarWithoutPinningOverflowFlag)
        {
            interstice::TableState state{};
            // Pin only the viewport size; the overflow flag stays false as at dispatch.
            auto pin = [&]{
                state.internal.lastViewportH = 298;
                state.internal.lastViewportW = 398;
            };
            pin();
            app.Frame([&]{ Emit(state); pin(); });
            app.Frame([&]{ Emit(state); pin(); });

            // Click into the table to give it focus.
            const auto rect = app.GetElementRect(tableID);
            const int2 center{ rect.x + rect.w / 2, rect.y + rect.h / 2 };
            app.InjectMouseMove(center);
            app.InjectMouseDown(center);
            app.InjectMouseUp(center);
            app.Frame([&]{ Emit(state); pin(); });

            ASSERT_FALSE(state.internal.anyHorzOverflow)
                << "test setup must NOT pin the flag — that's what makes "
                << "this faithful to production input-dispatch timing";

            // PageDown from top.
            app.InjectKey(interstice::KeyCode::PageDown);
            app.Frame([&]{ Emit(state); pin(); });
            app.Frame([&]{ Emit(state); pin(); });

            const interstice::ID sel = state.selectedRowID;
            ASSERT_TRUE(app.HasElement(sel));
            const auto selRect = app.GetElementRect(sel);
            const interstice::ID sectionID = state.SectionBodyID(0);
            auto sectionRef = app.elementStorage.FindElementRefByID(sectionID);
            ASSERT_TRUE(sectionRef.IsValid());
            const auto& sectionRect = app.elementStorage.GetElementRectUnsafe(sectionRef);

            // The 800-wide columns overflow the 398-px viewport on X.
            ASSERT_GT(sectionRect.contentBounds.w, sectionRect.innerRect.w)
                << "test setup must overflow X";

            const int32_t sectionInnerBottom = sectionRect.innerRect.y + sectionRect.innerRect.h;
            const int32_t selRowBottom = selRect.y + selRect.h;
            EXPECT_LE(selRowBottom, sectionInnerBottom - kScrollbarThickness)
                << "PageDown must land on a row clear of the H-scrollbar "
                << "even when state.internal.anyHorzOverflow is stale (production "
                << "input-dispatch order). Row bot=" << selRowBottom
                << " section inner bot=" << sectionInnerBottom;
        }

        // ---------------------------------------------------------------
        // Dragging the sorted column carries the sort indicator with it: the reorder
        // permutes sortColumn too.
        // ---------------------------------------------------------------
        TEST_F(TableVirtualScrollTest, SortIndicatorFollowsDraggedColumn)
        {
            int reorderFrom = -1, reorderTo = -1;
            interstice::TableState state{};
            // The caller rotates its columns vector on reorder.
            state.onReorderColumns = [&](int f, int t){
                reorderFrom = f; reorderTo = t;
                if (f == t) return;
                if (f < t) std::rotate(columns.begin() + f,
                                       columns.begin() + f + 1,
                                       columns.begin() + t + 1);
                else       std::rotate(columns.begin() + t,
                                       columns.begin() + f,
                                       columns.begin() + f + 1);
            };

            // Sorted by column 1, the column that will be dragged.
            state.sortColumn = 1;
            state.sortDescending = true;

            auto pin = [&]{ PinViewport(state); };
            pin();
            app.Frame([&]{ Emit(state); pin(); });
            app.Frame([&]{ Emit(state); pin(); });

            ASSERT_GE(int(columns.size()), 4);
            const auto col1Ref = columns[1].internal.headerRef;
            const auto col3Ref = columns[3].internal.headerRef;
            const auto col1Rect = app.elementStorage.GetElementRectUnsafe(col1Ref).rect;
            const auto col3Rect = app.elementStorage.GetElementRectUnsafe(col3Ref).rect;
            const int2 col1Center{ col1Rect.x + col1Rect.w / 2, col1Rect.y + col1Rect.h / 2 };
            const int2 col3Center{ col3Rect.x + col3Rect.w / 2, col3Rect.y + col3Rect.h / 2 };

            app.InjectMouseMove(col1Center);
            app.InjectMouseDown(col1Center);
            app.Frame([&]{ Emit(state); pin(); });
            app.InjectMouseMove(col3Center, interstice::MouseButtons::Left);
            app.Frame([&]{ Emit(state); pin(); });
            app.InjectMouseUp(col3Center);
            app.Frame([&]{ Emit(state); pin(); });

            ASSERT_EQ(reorderFrom, 1);
            ASSERT_EQ(reorderTo, 3) << "test sanity: drag should resolve to col 3";
            EXPECT_EQ(state.sortColumn, 3)
                << "sortColumn must track the dragged column to its new "
                << "slot — was " << state.sortColumn;
            EXPECT_TRUE(state.sortDescending)
                << "direction must persist across reorder";
        }

        // ---------------------------------------------------------------
        // Widths belong to columns too: after a reorder each column keeps
        // its own width (the caller only rotates its column array).
        // ---------------------------------------------------------------
        TEST_F(TableVirtualScrollTest, ColumnWidthsFollowDraggedColumn)
        {
            interstice::TableState state{};
            state.onReorderColumns = [&](int f, int t){
                if (f == t) return;
                if (f < t) std::rotate(columns.begin() + f, columns.begin() + f + 1, columns.begin() + t + 1);
                else       std::rotate(columns.begin() + t, columns.begin() + f, columns.begin() + f + 1);
            };

            auto pin = [&]{ PinViewport(state); };
            pin();
            app.Frame([&]{ Emit(state); pin(); });
            app.Frame([&]{ Emit(state); pin(); });

            ASSERT_GE(int(columns.size()), 4);
            ASSERT_EQ(state.columnWidths.size(), columns.size());
            for (size_t i = 0; i < state.columnWidths.size(); ++i)
                state.columnWidths[i] = 100 + 10 * int(i);   // distinct per column
            const std::vector<int> before(state.columnWidths.begin(), state.columnWidths.end());
            app.Frame([&]{ Emit(state); pin(); });

            const auto col1Rect = app.elementStorage.GetElementRectUnsafe(columns[1].internal.headerRef).rect;
            const auto col3Rect = app.elementStorage.GetElementRectUnsafe(columns[3].internal.headerRef).rect;
            const int2 col1Center{ col1Rect.x + col1Rect.w / 2, col1Rect.y + col1Rect.h / 2 };
            const int2 col3Center{ col3Rect.x + col3Rect.w / 2, col3Rect.y + col3Rect.h / 2 };

            app.InjectMouseMove(col1Center);
            app.InjectMouseDown(col1Center);
            app.Frame([&]{ Emit(state); pin(); });
            app.InjectMouseMove(col3Center, interstice::MouseButtons::Left);
            app.Frame([&]{ Emit(state); pin(); });
            app.InjectMouseUp(col3Center);
            app.Frame([&]{ Emit(state); pin(); });

            // Column 1 moved to slot 3; columns 2 and 3 shifted left.
            EXPECT_EQ(state.columnWidths[0], before[0]);
            EXPECT_EQ(state.columnWidths[1], before[2]);
            EXPECT_EQ(state.columnWidths[2], before[3]);
            EXPECT_EQ(state.columnWidths[3], before[1]);
        }

        // ---------------------------------------------------------------
        // Click-to-sort builds a visual-order permutation from sortAscending;
        // selection is keyed by dataset row, so it follows the row.
        // ---------------------------------------------------------------
        // followTail selects the row shown at the bottom — under a sort that is
        // not the last dataset row — and with multi-select the selection must
        // land in the multi-select set (that is what row highlighting reads).
        TEST_F(TableVirtualScrollTest, FollowTailSelectsTheBottomRowWhenSortedAndMultiSelect)
        {
            interstice::TableState state{};
            interstice::MultiSelectState multi{};
            state.multiSelect = &multi;
            state.followTail  = true;
            state.sortColumn  = 0;
            state.sortDescending = true;

            // Keys ascending by dataset index, sorted descending: the bottom
            // visual row holds dataset row 0.
            const int kRows = 50;
            std::vector<interstice::VirtualTableColumn> cols;
            cols.push_back({
                .column = { .header = "V", .initialWidth = 200, .minWidth = 60, .maxWidth = 400 },
                .generateCell  = [this](int r) { char b[16]; std::snprintf(b, sizeof(b), "%d", r); app.Cell(b); },
                .sortAscending = [](int a, int b) { return a < b; },
            });
            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow();
                    std::span<interstice::VirtualTableColumn> sp(cols.data(), cols.size());
                    app.VirtualTable(tableID, state, sp, kRows, 0, app.theme.tableStyleRef,
                                     LayoutOverride{}.Size(400, 300));
                    app.EndRootWindow();
                });
                for (const auto& entry : app.elementStorage.GetPostLayoutEntries())
                {
                    const auto* er = app.elementStorage.GetElementRectPtr(entry.ownerRef);
                    if (!er) continue;
                    PostLayoutContext plCtx{ app.elementStorage, entry.ownerRef,
                                             er->rect, er->innerRect, er->contentTransform };
                    entry.callback(plCtx);
                }
            };
            frame();
            frame();

            ASSERT_TRUE(state.internal.rowOrder);
            ASSERT_EQ(state.VisualToDataset(kRows - 1), 0) << "test setup: bottom row is dataset 0";
            const interstice::ID bottom = state.RowIDForIndex(0);
            EXPECT_EQ(state.selectedRowID, bottom);
            EXPECT_EQ(multi.selectedIDs.size(), 1u);
            EXPECT_EQ(multi.selectedIDs.count(bottom), 1u);
            EXPECT_EQ(multi.lastClickedID, bottom);
        }

        // ---- Stale state when the table shrinks ----

        // A selection past the new row count is dropped; it must not come back
        // (onto whatever data row then sits there) when rows are added again.
        TEST_F(TableVirtualScrollTest, SelectionPastTheEndIsDroppedWhenRowsShrink)
        {
            interstice::TableState state{};
            interstice::MultiSelectState multi{};
            auto pin = [&]{ PinViewport(state); };
            app.Frame([&]{ Emit(state, 50); pin(); });
            state.selectedRowID = state.RowIDForIndex(40);
            multi.selectedIDs   = { state.RowIDForIndex(5), state.RowIDForIndex(40) };
            state.multiSelect   = &multi;

            app.Frame([&]{ Emit(state, 10); pin(); });
            EXPECT_EQ(state.selectedRowID, interstice::ID::Invalid());
            EXPECT_EQ(multi.selectedIDs.size(), 1u) << "row 5 still exists; row 40 does not";

            app.Frame([&]{ Emit(state, 50); pin(); });
            EXPECT_EQ(state.selectedRowID, interstice::ID::Invalid());
        }

        // Begin a column drag on column `col` (press + move past the threshold).
        static void BeginColumnDrag(HeadlessGUIApp& app, interstice::TableState& state,
                                    std::vector<interstice::VirtualTableColumn>& columns, int col,
                                    const std::function<void()>& emit)
        {
            const auto r = app.elementStorage.GetElementRectUnsafe(columns[size_t(col)].internal.headerRef).rect;
            const int2 c{ r.x + r.w / 2, r.y + r.h / 2 };
            app.InjectMouseMove(c);
            app.InjectMouseDown(c);
            app.Frame(emit);
            app.InjectMouseMove(int2{ c.x - 40, c.y }, interstice::MouseButtons::Left);
            app.Frame(emit);
            (void)state;
        }

        TEST_F(TableVirtualScrollTest, ColumnDragIsCancelledWhenItsColumnDisappears)
        {
            interstice::TableState state{};
            state.onReorderColumns = [](int, int) {};
            auto pin  = [&]{ PinViewport(state); };
            auto emit = [&]{ Emit(state); pin(); };
            app.Frame(emit);
            app.Frame(emit);
            BeginColumnDrag(app, state, columns, 1, emit);
            ASSERT_TRUE(app.m_columnDrag.active);

            columns.resize(1);   // the dragged column (and the ones after it) went away
            app.Frame(emit);
            EXPECT_FALSE(app.m_columnDrag.active);
        }

        TEST_F(TableVirtualScrollTest, ColumnDragIsCancelledAfterTheTableWasHidden)
        {
            interstice::TableState state{};
            state.onReorderColumns = [](int, int) {};
            auto pin  = [&]{ PinViewport(state); };
            auto emit = [&]{ Emit(state); pin(); };
            app.Frame(emit);
            app.Frame(emit);
            BeginColumnDrag(app, state, columns, 1, emit);
            ASSERT_TRUE(app.m_columnDrag.active);

            // The table is not emitted while the button is released elsewhere.
            auto empty = [&]{ app.BeginRootWindow(); app.EndRootWindow(); };
            app.InjectMouseUp(int2{ 5, 5 });
            app.Frame(empty);
            app.Frame(empty);

            app.Frame(emit);
            EXPECT_FALSE(app.m_columnDrag.active);
        }

        TEST_F(TableVirtualScrollTest, SortAscendingBuildsPermutationAndKeepsSelection)
        {
            interstice::TableState state{};

            // Dataset whose index order isn't its sorted order.
            std::vector<int> data = { 30, 10, 50, 20, 40 };

            std::vector<interstice::VirtualTableColumn> sortedColumns;
            sortedColumns.push_back({
                .column = { .header = "V", .initialWidth = 200, .minWidth = 60, .maxWidth = 400 },
                .generateCell  = [this, &data](int r) {
                    char b[16]; std::snprintf(b, sizeof(b), "%d", data[r]); app.Cell(b);
                },
                .sortAscending = [&data](int a, int b) { return data[a] < data[b]; },
            });

            auto emitSorted = [&]{
                app.BeginRootWindow();
                std::span<interstice::VirtualTableColumn> sp(sortedColumns.data(), sortedColumns.size());
                app.VirtualTable(tableID, state, sp, int(data.size()), 0,
                                 app.theme.tableStyleRef,
                                 LayoutOverride{}.Size(400, 300));
                app.EndRootWindow();
            };

            // Frame 1: no sort. Permutation should stay null.
            app.Frame([&]{ emitSorted(); });
            EXPECT_FALSE(state.internal.rowOrder)
                << "no active sort -> no permutation allocated";
            EXPECT_EQ(state.VisualToDataset(2), 2) << "identity translation when unsorted";
            EXPECT_EQ(state.DatasetToVisual(2), 2);

            // Select the data row whose value is 50 (dataset index 2).
            state.selectedRowID = state.RowIDForIndex(2);
            ASSERT_NE(state.selectedRowID, interstice::ID::Invalid());

            // Frame 2: sort ascending. Visual order [1, 3, 0, 4, 2]: position 4 holds
            // dataset row 2 (value 50).
            state.sortColumn = 0;
            state.sortDescending = false;
            app.Frame([&]{ emitSorted(); });
            ASSERT_TRUE(state.internal.rowOrder) << "sort active -> permutation built";
            ASSERT_EQ(state.internal.rowOrder->size(), data.size());

            const std::vector<int> expectedAsc = { 1, 3, 0, 4, 2 };
            EXPECT_EQ(*state.internal.rowOrder, expectedAsc)
                << "stable ascending sort by value";

            // Selection identity must follow the data row.
            EXPECT_EQ(state.selectedRowID, state.RowIDForIndex(2))
                << "selectedRowID is dataset-keyed; must stay on row 2";
            EXPECT_EQ(state.DatasetToVisual(2), 4)
                << "row 2 (value 50) is now at visual position 4";

            // Flip to descending — same lambda, the widget swaps args.
            state.sortDescending = true;
            app.Frame([&]{ emitSorted(); });
            const std::vector<int> expectedDesc = { 2, 4, 0, 3, 1 };
            EXPECT_EQ(*state.internal.rowOrder, expectedDesc) << "descending = swap args";
            EXPECT_EQ(state.DatasetToVisual(2), 0)
                << "row 2 (value 50, the largest) is now at visual position 0";

            // Drop the sort — permutation goes away.
            state.sortColumn = -1;
            app.Frame([&]{ emitSorted(); });
            EXPECT_FALSE(state.internal.rowOrder) << "no active sort -> permutation released";
        }

        // ---------------------------------------------------------------
        // A sortable column without a sortAscending lambda builds no permutation
        // (the caller sorts externally); internal.rowOrder stays null.
        // ---------------------------------------------------------------
        TEST_F(TableVirtualScrollTest, ColumnWithoutSortLambdaDoesNotBuildPermutation)
        {
            interstice::TableState state{};

            std::vector<interstice::VirtualTableColumn> noLambdaCols;
            noLambdaCols.push_back({
                .column = { .header = "X", .initialWidth = 200, .minWidth = 60, .maxWidth = 400, .sortable = true },
                .generateCell = [this](int r) { char b[16]; std::snprintf(b, sizeof(b), "%d", r); app.Cell(b); },
                // sortAscending intentionally omitted
            });

            state.sortColumn = 0;
            state.sortDescending = true;
            app.Frame([&]{
                app.BeginRootWindow();
                std::span<interstice::VirtualTableColumn> sp(noLambdaCols.data(), noLambdaCols.size());
                app.VirtualTable(tableID, state, sp, 10, 0,
                                 app.theme.tableStyleRef,
                                 LayoutOverride{}.Size(400, 300));
                app.EndRootWindow();
            });

            EXPECT_FALSE(state.internal.rowOrder)
                << "sortable=true alone (without sortAscending) doesn't build a perm";
            EXPECT_EQ(state.VisualToDataset(7), 7) << "identity translation";
        }

        // ---------------------------------------------------------------
        // Tree-cell parity with tree-list:
        //   - hasChildren is set on TreeRowInfo when isOpen != nullptr
        //   - Double-clicking the icon toggles isOpen
        // ---------------------------------------------------------------
        TEST_F(TableVirtualScrollTest, TreeCellPopulatesHasChildrenForBranches)
        {
            bool branchOpen = true;
            std::vector<interstice::VirtualTableColumn> cols;
            cols.push_back({ { .header = "Name", .initialWidth = 240, .minWidth = 80, .maxWidth = 600 },
                             [&](int r) {
                                 if (r == 0)
                                     app.TreeCell("Branch", /*depth*/0, &branchOpen,
                                                  IconID::Invalid, 0, true, nullptr);
                                 else
                                     app.TreeCell("Leaf", /*depth*/1, /*isOpen*/nullptr,
                                                  IconID::Invalid, 0, true, nullptr);
                             } });

            interstice::TableState state{};
            auto pin = [&]{ PinViewport(state, /*xOverflow*/false, /*yOverflow*/false); };
            auto emit = [&]{
                app.BeginRootWindow();
                std::span<interstice::VirtualTableColumn> sp(cols.data(), cols.size());
                app.VirtualTable(tableID, state, sp, 2, 0,
                                 app.theme.tableStyleRef,
                                 LayoutOverride{}.Size(400, 200));
                app.EndRootWindow();
            };
            pin();
            app.Frame([&]{ emit(); pin(); });
            app.Frame([&]{ emit(); pin(); });

            const interstice::ID branchID = state.RowIDForIndex(0);
            const interstice::ID leafID   = state.RowIDForIndex(1);
            const auto* branchInfo = state.FindTreeRow(branchID);
            const auto* leafInfo   = state.FindTreeRow(leafID);
            ASSERT_NE(branchInfo, nullptr);
            ASSERT_NE(leafInfo,   nullptr);
            EXPECT_TRUE(branchInfo->hasChildren) << "branch with isOpen != null sets hasChildren";
            EXPECT_FALSE(leafInfo->hasChildren)  << "leaf with isOpen == null leaves hasChildren false";
        }

        TEST_F(TableVirtualScrollTest, TreeCellIconDoubleClickTogglesBranch)
        {
            bool branchOpen = false;
            std::vector<interstice::VirtualTableColumn> cols;
            cols.push_back({ { .header = "Name", .initialWidth = 240, .minWidth = 80, .maxWidth = 600 },
                             [&](int r) {
                                 (void)r;
                                 // A valid icon so the hittable icon element is emitted.
                                 app.TreeCell("Branch", /*depth*/0, &branchOpen,
                                              app.theme.sortArrowDownIcon, 0, true, nullptr);
                             } });

            interstice::TableState state{};
            auto pin = [&]{ PinViewport(state, false, false); };
            auto emit = [&]{
                app.BeginRootWindow();
                std::span<interstice::VirtualTableColumn> sp(cols.data(), cols.size());
                app.VirtualTable(tableID, state, sp, 1, 0,
                                 app.theme.tableStyleRef,
                                 LayoutOverride{}.Size(400, 200));
                app.EndRootWindow();
            };
            pin();
            app.Frame([&]{ emit(); pin(); });
            app.Frame([&]{ emit(); pin(); });

            // Icon x: depth-0 indent (0) + arrow (20) + ~8 px into the icon.
            const interstice::ID rowID = state.RowIDForIndex(0);
            const auto rowRect = app.GetElementRect(rowID);
            const int2 iconCenter{ rowRect.x + 20 + 8, rowRect.y + rowRect.h / 2 };

            // First click on the icon; the second, within 300 ms, toggles it.
            app.ClickAt(iconCenter);
            app.Frame([&]{ emit(); pin(); });
            ASSERT_FALSE(branchOpen) << "single click must not toggle";

            app.ClickAt(iconCenter);
            app.Frame([&]{ emit(); pin(); });
            EXPECT_TRUE(branchOpen) << "double-click on icon toggles isOpen";
        }

    
        // 50k rows sized to their content would be over a million pixels tall, past the
        // 16-bit layout range, and not show at all (the demo's Table panel did that).
        // Filling its panel, the table stays inside it and scrolls.
        TEST_F(TableVirtualScrollTest, AManyRowTableFillingItsPanelStaysInsideIt)
        {
            interstice::TableState state;
            const IntRect table = EmitInPane(state, app.theme.tableFillStyleRef, LayoutOverride::Auto().FlexGrow(1).MinHeight(0));
            const IntRect pane  = app.GetElementRect(paneID);
            EXPECT_GT(table.h, 100);
            EXPECT_GE(table.y, pane.y);
            EXPECT_LE(table.y + table.h, pane.y + pane.h);
        }
} // namespace
} // namespace interstice
