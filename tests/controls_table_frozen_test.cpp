// Integration tests for the multi-section table layout. Runs of columns sharing
// a `sectionId` form a section; each flexes between 0 and its content width and
// gets its own H scrollbar when squeezed. Y scroll is shared across sections.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_core/gui_id.h>
#include <gui_core/gui_id_manager.h>
#include <gui_core/state/gui_scroll_management.h>

#include <string>
#include <vector>

namespace tachyon
{
    using GUITest::HeadlessGUIApp;

    namespace
    {

        class FrozenTableTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;

            // Five columns split into three sections via sectionId:
            //   [#] | [Name] [Hex] [Multiple] | [Status]
            //   sec 0      sec 1                sec 2
            // The middle section (~308 wide) overflows its 240px area.
            tachyon::VirtualTableColumn kColumns[5] = {
                { .column = { .header = "#",        .initialWidth =  60, .minWidth = 30, .maxWidth = 200, .locked = true }, .sectionId = 0 },
                { .column = { .header = "Name",     .initialWidth = 100, .minWidth = 30, .maxWidth = 400 },                  .sectionId = 1 },
                { .column = { .header = "Hex",      .initialWidth = 100, .minWidth = 30, .maxWidth = 400 },                  .sectionId = 1 },
                { .column = { .header = "Multiple", .initialWidth = 100, .minWidth = 30, .maxWidth = 400 },                  .sectionId = 1 },
                { .column = { .header = "Status",   .initialWidth =  60, .minWidth = 30, .maxWidth = 200, .locked = true }, .sectionId = 2 },
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

            // Render N rows of dummy data inside a container small enough that
            // both axes overflow. Width 400 is ~100 short of the columns'
            // intrinsic 480; height 200 with ~30 rows x 24px overflows Y.
            void EmitFrozen(tachyon::TableState& state, int rowCount = 30,
                            int width = 400, int height = 200)
            {
                app.BeginRootWindow();
                if (app.BeginTable(tableID, kColumns, state,
                                    LayoutOverride{}.Size(CssPx(width), CssPx(height))))
                {
                    for (int r = 0; r < rowCount; r++)
                    {
                        app.BeginRow();
                        char buf[24];
                        std::snprintf(buf, sizeof(buf), "%d", r);          app.Cell(buf);
                        std::snprintf(buf, sizeof(buf), "Row%05d", r);     app.Cell(buf);
                        std::snprintf(buf, sizeof(buf), "0x%04X", r);      app.Cell(buf);
                        std::snprintf(buf, sizeof(buf), "%d×7=%d", r, r*7); app.Cell(buf);
                        app.Cell(r % 7 == 0 ? "ALERT" : "ok");
                        app.EndRow();
                    }
                    app.EndTable();
                }
                app.EndRootWindow();
            }

            // Sizer ID for the boundary after global column `c` (pre-allocated by
            // BeginTable, so safe to read outside a frame).
            tachyon::ID SizerID(int c)
            {
                if (c < 0 || c >= int(std::size(kColumns))) return tachyon::ID::Invalid();
                return kColumns[c].internal.sizerID;
            }

            // Read section[s]'s body scroll offset.
            int2 SectionBodyScroll(const tachyon::TableState& state, size_t s)
            {
                if (s >= size_t(tachyon::TableState::kMaxTableSections)) return int2{0, 0};
                const tachyon::ID id = state.SectionBodyID(int(s));
                if (id == tachyon::ID::Invalid()) return int2{0, 0};
                auto& sm = app.guiStateManager.GetScrollManagement();
                auto handle = sm.GetScrollOffsetHandle(id);
                if (!sm.IsScrollOffsetHandleValid(handle)) return int2{0, 0};
                return sm.GetScrollOffset(handle);
            }
        };

        // -------------------------------------------------------------------
        // Structure
        // -------------------------------------------------------------------

        TEST_F(FrozenTableTest, FrozenModeCreatesThreeBodySections)
        {
            tachyon::TableState state{};
            app.Frame([&] { EmitFrozen(state); });

            ASSERT_EQ(state.internal.sectionCount, 3);
            for (int s = 0; s < 3; s++)
                EXPECT_TRUE(app.HasElement(state.SectionBodyID(s)));
        }

        // -------------------------------------------------------------------
        // Section separators only emit between two adjacent sections; header and
        // body share the section grid, so 1 boundary -> 1 separator.
        // -------------------------------------------------------------------

        // Count elements in storage whose styleRef matches the table separator.
        // Walks every (depth, index) since separators sit at varying depths
        // inside header-row vs body-row scopes.
        int CountSeparators(const HeadlessGUIApp& a)
        {
            const StyleRef sep = a.theme.tableSeparatorStyleRef;
            int count = 0;
            for (uint32_t d = 0; d < a.elementStorage.NumberOfDepthLevels(); d++)
            {
                const uint32_t n = a.elementStorage.DepthSizeOf(d);
                for (uint32_t i = 0; i < n; i++)
                {
                    tachyon::ElementRef ref{ d, i };
                    if (a.elementStorage.IsValidRef(ref)
                        && a.elementStorage.GetElementUnsafe(ref).styleRef == sep)
                        count++;
                }
            }
            return count;
        }

        // Two sections (no leading single-column section): one boundary ->
        // one separator in storage (header + body share the section grid).
        TEST_F(FrozenTableTest, NoSeparatorWhenLeftFrozenIsZero)
        {
            // Reassign sectionIds so there are exactly two sections:
            //   [Name Hex Multiple] | [Status]   (cols 1..3 then col 4;
            // col 0 also lives in the leading section).
            tachyon::VirtualTableColumn cols[5] = {
                { .column = { .header = "#",        .initialWidth =  60 }, .sectionId = 0 },
                { .column = { .header = "Name",     .initialWidth = 100 }, .sectionId = 0 },
                { .column = { .header = "Hex",      .initialWidth = 100 }, .sectionId = 0 },
                { .column = { .header = "Multiple", .initialWidth = 100 }, .sectionId = 0 },
                { .column = { .header = "Status",   .initialWidth =  60 }, .sectionId = 1 },
            };
            tachyon::TableState state{};
            app.Frame([&] {
                app.BeginRootWindow();
                if (app.BeginTable(tableID, cols, state, LayoutOverride{}.Size(400, 200)))
                {
                    app.BeginRow();
                    for (int c = 0; c < 5; c++) app.Cell("x");
                    app.EndRow();
                    app.EndTable();
                }
                app.EndRootWindow();
            });
            EXPECT_EQ(CountSeparators(app), 1)
                << "One boundary between section 0 and section 1 → 1 separator.";
        }

        // Mirror: leading single-column section + everything else.
        TEST_F(FrozenTableTest, NoSeparatorWhenRightFrozenIsZero)
        {
            tachyon::VirtualTableColumn cols[5] = {
                { .column = { .header = "#",        .initialWidth =  60 }, .sectionId = 0 },
                { .column = { .header = "Name",     .initialWidth = 100 }, .sectionId = 0 },
                { .column = { .header = "Hex",      .initialWidth = 100 }, .sectionId = 1 },
                { .column = { .header = "Multiple", .initialWidth = 100 }, .sectionId = 1 },
                { .column = { .header = "Status",   .initialWidth =  60 }, .sectionId = 1 },
            };
            tachyon::TableState state{};
            app.Frame([&] {
                app.BeginRootWindow();
                if (app.BeginTable(tableID, cols, state, LayoutOverride{}.Size(400, 200)))
                {
                    app.BeginRow();
                    for (int c = 0; c < 5; c++) app.Cell("x");
                    app.EndRow();
                    app.EndTable();
                }
                app.EndRootWindow();
            });
            EXPECT_EQ(CountSeparators(app), 1)
                << "One boundary → 1 separator (header lives inside the section grid).";
        }

        // Three sections (default kColumns layout) - both boundaries non-empty.
        TEST_F(FrozenTableTest, BothSeparatorsEmittedWhenAllSectionsNonEmpty)
        {
            tachyon::TableState state{};
            app.Frame([&] { EmitFrozen(state, /*rowCount*/3); });
            EXPECT_EQ(CountSeparators(app), 2)
                << "Two boundaries → 2 separators.";
        }

        // -------------------------------------------------------------------
        // Sizer ID is shared between header and body, so hover/active state spans
        // both copies.
        // -------------------------------------------------------------------

        TEST_F(FrozenTableTest, SizerIdsAreDeterministicPerColumn)
        {
            tachyon::TableState state{};
            app.Frame([&] { EmitFrozen(state); });

            // Section 1 (cols 1..3) has two interior boundaries, between
            // cols 1|2 and 2|3.
            EXPECT_TRUE(app.HasElement(SizerID(1)));
            EXPECT_TRUE(app.HasElement(SizerID(2)));
        }

        // Sizer X for an interior boundary lines up with that column's
        // header cell right edge - same invariant in every section.
        TEST_F(FrozenTableTest, SizerXAlignsWithHeaderCellRightEdge)
        {
            tachyon::TableState state{};
            app.Frame([&] { EmitFrozen(state); });

            // Boundaries between section-1 cols (1|2 and 2|3).
            for (int c : { 1, 2 })
            {
                const auto sizerID = SizerID(c);
                ASSERT_TRUE(app.HasElement(sizerID));
                const auto sizerRect = app.GetElementRect(sizerID);
                const auto headerRect = app.elementStorage.GetElementRectUnsafe(
                    kColumns[c].internal.headerRef).rect;
                EXPECT_NEAR(sizerRect.x, headerRect.x + headerRect.w, 1)
                    << "Sizer " << c << " must sit at right edge of header cell.";
            }
        }

        // -------------------------------------------------------------------
        // Mouse wheel - every section scrolls naturally
        // -------------------------------------------------------------------

        TEST_F(FrozenTableTest, VerticalWheelOverMiddleScrollsY)
        {
            tachyon::TableState state{};
            app.Frame([&] { EmitFrozen(state); });

            // Center of the middle section's body grid.
            const auto midRect = app.GetElementRect(state.SectionBodyID(1));
            const int2 mid{ midRect.x + midRect.w / 2, midRect.y + midRect.h / 2 };

            const int2 before = SectionBodyScroll(state, 1);
            app.InjectMouseMove(mid);
            app.InjectMouseScroll(mid, int2{ 0, -1 }); // scroll DOWN one tick
            app.Frame([&] { EmitFrozen(state); });

            const int2 after = SectionBodyScroll(state, 1);
            EXPECT_LT(after.y, before.y) << "wheel-down should make scroll offset more negative";
        }

        TEST_F(FrozenTableTest, VerticalWheelOverLeftFrozenScrollsY)
        {
            tachyon::TableState state{};
            app.Frame([&] { EmitFrozen(state); });

            const auto leftRect = app.GetElementRect(state.SectionBodyID(0));
            const int2 pos{ leftRect.x + leftRect.w / 2, leftRect.y + leftRect.h / 2 };

            const int2 beforeLeft = SectionBodyScroll(state, 0);
            app.InjectMouseMove(pos);
            app.InjectMouseScroll(pos, int2{ 0, -1 });
            app.Frame([&] { EmitFrozen(state); });

            const int2 afterLeft = SectionBodyScroll(state, 0);
            EXPECT_LT(afterLeft.y, beforeLeft.y);
        }

        // Only two sections: the Y votes tie, so the first section's scroll must still win
        // when it is the one that changed.
        TEST_F(FrozenTableTest, VerticalWheelOverFirstOfTwoSectionsScrollsY)
        {
            tachyon::VirtualTableColumn cols[4] = {
                { .column = { .header = "#",    .initialWidth =  60, .locked = true }, .sectionId = 0 },
                { .column = { .header = "Name", .initialWidth = 100 },                 .sectionId = 1 },
                { .column = { .header = "Hex",  .initialWidth = 100 },                 .sectionId = 1 },
                { .column = { .header = "Val",  .initialWidth = 100 },                 .sectionId = 1 },
            };
            tachyon::TableState state{};
            auto emit = [&] {
                app.BeginRootWindow();
                if (app.BeginTable(tableID, cols, state, LayoutOverride{}.Size(400, 200)))
                {
                    for (int r = 0; r < 30; r++)
                    {
                        app.BeginRow();
                        for (int c = 0; c < 4; c++) app.Cell("x");
                        app.EndRow();
                    }
                    app.EndTable();
                }
                app.EndRootWindow();
            };
            app.Frame(emit);
            ASSERT_EQ(state.internal.sectionCount, 2);

            const auto firstRect = app.GetElementRect(state.SectionBodyID(0));
            const int2 pos{ firstRect.x + firstRect.w / 2, firstRect.y + firstRect.h / 2 };
            app.InjectMouseMove(pos);
            app.InjectMouseScroll(pos, int2{ 0, -1 });
            app.Frame(emit);
            app.Frame(emit);

            EXPECT_LT(SectionBodyScroll(state, 0).y, 0) << "scrolling the first section must stick";
            EXPECT_EQ(SectionBodyScroll(state, 0).y, SectionBodyScroll(state, 1).y) << "and mirror to the second";
        }

        TEST_F(FrozenTableTest, VerticalWheelOverRightFrozenScrollsY)
        {
            tachyon::TableState state{};
            app.Frame([&] { EmitFrozen(state); });

            const auto rightRect = app.GetElementRect(state.SectionBodyID(2));
            const int2 pos{ rightRect.x + rightRect.w / 2, rightRect.y + rightRect.h / 2 };

            const int2 beforeRight = SectionBodyScroll(state, 2);
            app.InjectMouseMove(pos);
            app.InjectMouseScroll(pos, int2{ 0, -1 });
            app.Frame([&] { EmitFrozen(state); });

            const int2 afterRight = SectionBodyScroll(state, 2);
            EXPECT_LT(afterRight.y, beforeRight.y);
        }

        TEST_F(FrozenTableTest, HorizontalWheelOverMiddleScrollsX)
        {
            tachyon::TableState state{};
            app.Frame([&] { EmitFrozen(state); });

            const auto midRect = app.GetElementRect(state.SectionBodyID(1));
            const int2 pos{ midRect.x + midRect.w / 2, midRect.y + midRect.h / 2 };

            const int2 before = SectionBodyScroll(state, 1);
            app.InjectMouseMove(pos);
            app.InjectMouseScroll(pos, int2{ -1, 0 });
            app.Frame([&] { EmitFrozen(state); });

            EXPECT_LT(SectionBodyScroll(state, 1).x, before.x);
        }

        TEST_F(FrozenTableTest, HorizontalWheelOverLeftFrozenStillScrollsX)
        {
            // The wheel scrolls the leading section under the cursor, which fits its
            // content, so X clamps to 0 and must stay valid.
            tachyon::TableState state{};
            app.Frame([&] { EmitFrozen(state); });

            const auto leftRect = app.GetElementRect(state.SectionBodyID(0));
            const int2 pos{ leftRect.x + leftRect.w / 2, leftRect.y + leftRect.h / 2 };

            app.InjectMouseMove(pos);
            app.InjectMouseScroll(pos, int2{ -1, 0 });
            app.Frame([&] { EmitFrozen(state); });

            EXPECT_LE(SectionBodyScroll(state, 0).x, 0);
        }

        // -------------------------------------------------------------------
        // Mirror - vertical scroll propagates Y to every section's body
        // -------------------------------------------------------------------

        TEST_F(FrozenTableTest, VerticalScrollIsMirroredToFrozenSides)
        {
            tachyon::TableState state{};
            app.Frame([&] { EmitFrozen(state); });

            const auto midRect = app.GetElementRect(state.SectionBodyID(1));
            const int2 pos{ midRect.x + midRect.w / 2, midRect.y + midRect.h / 2 };

            app.InjectMouseMove(pos);
            app.InjectMouseScroll(pos, int2{ 0, -2 });
            app.Frame([&] { EmitFrozen(state); });
            // Second frame runs the mirror against the post-wheel state.
            app.Frame([&] { EmitFrozen(state); });

            const int2 m = SectionBodyScroll(state, 1);
            const int2 l = SectionBodyScroll(state, 0);
            const int2 r = SectionBodyScroll(state, 2);
            EXPECT_LT(m.y, 0);
            EXPECT_EQ(l.y, m.y);
            EXPECT_EQ(r.y, m.y);
        }

        // Header cells share the body's scrolling grid, so this only checks that the
        // body section scrolls on a horizontal wheel.
        TEST_F(FrozenTableTest, MiddleSectionScrollsHorizontally)
        {
            tachyon::TableState state{};
            app.Frame([&] { EmitFrozen(state); });

            const auto midRect = app.GetElementRect(state.SectionBodyID(1));
            const int2 pos{ midRect.x + midRect.w / 2, midRect.y + midRect.h / 2 };

            app.InjectMouseMove(pos);
            app.InjectMouseScroll(pos, int2{ -2, 0 });
            app.Frame([&] { EmitFrozen(state); });
            app.Frame([&] { EmitFrozen(state); });

            const int2 m = SectionBodyScroll(state, 1);
            EXPECT_LT(m.x, 0);
        }

        // -------------------------------------------------------------------
        // Sizer drag - from the header AND from the body
        // -------------------------------------------------------------------

        // A fresh press sets activeID without capture, so the drag's MouseMove must
        // land while the button is still held. One frame per phase:
        //   Frame N:   Move(from) + Down(from) - activeID := sizer
        //   Frame N+1: Move(to, Left held)     - handler updates width
        //   Frame N+2: Up(to)                  - drag teardown
        static void DragSizer(HeadlessGUIApp& app, tachyon::TableState& state,
                              tachyon::int2 from, tachyon::int2 to,
                              std::function<void()> emitFn)
        {
            app.InjectMouseMove(from);
            app.InjectMouseDown(from);
            app.Frame(emitFn);

            app.InjectMouseMove(to, tachyon::MouseButtons::Left);
            app.Frame(emitFn);

            app.InjectMouseUp(to);
            app.Frame(emitFn);
            (void)state;
        }

        TEST_F(FrozenTableTest, SizerDragFromHeaderResizesColumn)
        {
            tachyon::TableState state{};
            app.Frame([&] { EmitFrozen(state); });

            const tachyon::ID sizerID = SizerID(1);
            ASSERT_TRUE(app.HasElement(sizerID));

            // Header cells are sticky-pinned at the top of the section body
            // grid, so a Y a few pixels into the body rect lands on the
            // header copy of the sizer.
            const int beforeWidth = state.columnWidths[1];
            const auto sizerRect  = app.GetElementRect(sizerID);
            const auto midBodyRect = app.GetElementRect(state.SectionBodyID(1));
            const int hdrY = midBodyRect.y + 4;
            const int2 from{ sizerRect.x + sizerRect.w / 2, hdrY };
            const int2 to  { from.x + 30, from.y };

            DragSizer(app, state, from, to, [&] { EmitFrozen(state); });
            EXPECT_EQ(state.columnWidths[1], beforeWidth + 30);
        }

        TEST_F(FrozenTableTest, SizerDragFromBodyResizesColumn)
        {
            tachyon::TableState state{};
            app.Frame([&] { EmitFrozen(state); });

            // Same ID as the header copy; a body-level Y hits the body copy.
            const tachyon::ID sizerID = SizerID(2);  // Hex|Multiple boundary
            ASSERT_TRUE(app.HasElement(sizerID));

            const int beforeWidth = state.columnWidths[2];
            const auto sizerRect = app.GetElementRect(sizerID);
            const auto midRect   = app.GetElementRect(state.SectionBodyID(1));
            const int bodyY = midRect.y + midRect.h / 2;
            const int2 from{ sizerRect.x + sizerRect.w / 2, bodyY };
            const int2 to  { from.x - 25, from.y };

            DragSizer(app, state, from, to, [&] { EmitFrozen(state); });
            EXPECT_EQ(state.columnWidths[2], beforeWidth - 25);
        }

        // The body sizer stays at a stable Y inside the visible region while the body
        // scrolls vertically, so it remains clickable.
        TEST_F(FrozenTableTest, SizerStaysHittableAfterVerticalScroll)
        {
            tachyon::TableState state{};
            app.Frame([&] { EmitFrozen(state); });

            // Scroll ~half the content height directly, independent of wheel geometry.
            auto& sm = app.guiStateManager.GetScrollManagement();
            auto h = sm.GetOrCreateScrollOffsetHandle(state.SectionBodyID(1));
            sm.SetScrollOffset(h, int2{ 0, -200 });
            app.Frame([&] { EmitFrozen(state); });
            app.Frame([&] { EmitFrozen(state); });

            // Had the sizer scrolled with the body, this click would land on a cell.
            const tachyon::ID sizerID = SizerID(1);
            ASSERT_TRUE(app.HasElement(sizerID));
            const int beforeWidth = state.columnWidths[1];
            const auto sizerRect = app.GetElementRect(sizerID);
            const auto midRect   = app.GetElementRect(state.SectionBodyID(1));
            const int bodyY = midRect.y + midRect.h / 2;
            const int2 from{ sizerRect.x + sizerRect.w / 2, bodyY };
            const int2 to  { from.x + 20, from.y };
            DragSizer(app, state, from, to, [&] { EmitFrozen(state); });

            EXPECT_EQ(state.columnWidths[1], beforeWidth + 20);
        }

        // ----------------------------------------------------------------
        // Resizable + Stretch columns INSIDE a multi-column section
        // ----------------------------------------------------------------
        //
        // Sections may contain multiple columns. Inside a section:
        //   - Resizable columns (sizing == Resizable) get a draggable
        //     sizer at internal boundaries.
        //   - Stretch columns (sizing == Stretch) get a 1fr track instead
        //     of Fixed. Their boundary becomes a 1px separator (matching
        //     emitSectionSizers' rule that Stretch columns stop sizers).

        TEST_F(FrozenTableTest, ResizableColumnInsideLeftFrozenSection)
        {
            // 3 sections: [L0 L1] | [M0 M1] | [R0]. Boundary 0 (between
            // cols 0 and 1) lives ENTIRELY inside the leading section
            // and should be a draggable sizer.
            tachyon::VirtualTableColumn cols[5] = {
                { .column = { .header = "L0", .initialWidth = 60, .minWidth = 30, .maxWidth = 200, .locked = true }, .sectionId = 0 },
                { .column = { .header = "L1", .initialWidth = 60, .minWidth = 30, .maxWidth = 200, .locked = true }, .sectionId = 0 },
                { .column = { .header = "M0", .initialWidth = 100, .minWidth = 30, .maxWidth = 400 },                 .sectionId = 1 },
                { .column = { .header = "M1", .initialWidth = 100, .minWidth = 30, .maxWidth = 400 },                 .sectionId = 1 },
                { .column = { .header = "R0", .initialWidth = 60, .minWidth = 30, .maxWidth = 200, .locked = true }, .sectionId = 2 },
            };
            tachyon::TableState state{};

            auto emit = [&] {
                app.BeginRootWindow();
                if (app.BeginTable(tableID, cols, state, LayoutOverride{}.Size(500, 200)))
                {
                    for (int r = 0; r < 5; r++)
                    {
                        app.BeginRow();
                        for (int c = 0; c < 5; c++)
                        {
                            char cb[8]; std::snprintf(cb, sizeof(cb), "%d-%d", r, c);
                            app.Cell(cb);
                        }
                        app.EndRow();
                    }
                    app.EndTable();
                }
                app.EndRootWindow();
            };
            app.Frame(emit);

            // The sizer between L0 and L1 should be present.
            const tachyon::ID sizerID = cols[0].internal.sizerID;
            ASSERT_TRUE(app.HasElement(sizerID));

            const int beforeWidth = state.columnWidths[0];
            const auto sizerRect  = app.GetElementRect(sizerID);
            const auto leftBody   = app.GetElementRect(state.SectionBodyID(0));
            const int hdrY = leftBody.y + 4;
            const int2 from{ sizerRect.x + sizerRect.w / 2, hdrY };
            const int2 to  { from.x + 20, from.y };

            DragSizer(app, state, from, to, emit);

            EXPECT_EQ(state.columnWidths[0], beforeWidth + 20);
        }

        TEST_F(FrozenTableTest, StretchColumnInsideFrozenSectionEmitsSeparatorNotSizer)
        {
            // 3 sections where the leading [L0 L1] section's L0 is Stretch, so the
            // L0|L1 boundary is a 1px separator, not a sizer.
            tachyon::VirtualTableColumn cols[5] = {
                { .column = { .header = "L0", .initialWidth = 60, .minWidth = 30, .maxWidth = 200,
                              .locked = true, .sizing = tachyon::ColumnSizing::Stretch }, .sectionId = 0 },
                { .column = { .header = "L1", .initialWidth = 60, .minWidth = 30, .maxWidth = 200, .locked = true }, .sectionId = 0 },
                { .column = { .header = "M0", .initialWidth = 100, .minWidth = 30, .maxWidth = 400 },                 .sectionId = 1 },
                { .column = { .header = "M1", .initialWidth = 100, .minWidth = 30, .maxWidth = 400 },                 .sectionId = 1 },
                { .column = { .header = "R0", .initialWidth = 60, .minWidth = 30, .maxWidth = 200, .locked = true }, .sectionId = 2 },
            };
            tachyon::TableState state{};

            app.Frame([&] {
                app.BeginRootWindow();
                if (app.BeginTable(tableID, cols, state, LayoutOverride{}.Size(500, 200)))
                {
                    app.BeginRow();
                    for (int c = 0; c < 5; c++) app.Cell("x");
                    app.EndRow();
                    app.EndTable();
                }
                app.EndRootWindow();
            });

            // Boundary between L0|L1 is c=0. With L0 as Stretch the
            // boundary emits as a 1px non-draggable separator (sharing the
            // sizer ID, but rendered narrower and with no event handler).
            ASSERT_TRUE(app.HasElement(cols[0].internal.sizerID));
            const auto rect = app.GetElementRect(cols[0].internal.sizerID);
            EXPECT_EQ(rect.w, 1)
                << "Stretch column boundary should be a 1px non-draggable separator";
        }

        // With both scrollbars visible, each scrollbar's element rect is shortened to
        // stop at the corner, so neither bar nor thumb extends into it.
        TEST_F(FrozenTableTest, BothScrollbarsEndAtCornerWithoutOverlap)
        {
            tachyon::TableState state{};
            // Run an unprimed frame so the section bodies get laid out.
            app.Frame([&] { EmitFrozen(state); });
            // Headless skips render, so feed the rightmost section's real size into
            // internal.lastViewportW/H (used to trim each bar by the corner).
            const auto rightFrame1 = app.GetElementRect(state.SectionBodyID(state.internal.sectionCount - 1));
            state.internal.lastViewportW = rightFrame1.w;
            state.internal.lastViewportH = rightFrame1.h;
            // Force the overflow flags each build (the prologue resets them).
            app.Frame([&] {
                state.internal.anyHorzOverflow = state.internal.anyVertOverflow = true;
                EmitFrozen(state);
            });

            ASSERT_TRUE(app.HasElement(state.internal.horzScrollbarID));
            ASSERT_TRUE(app.HasElement(state.internal.vertScrollbarID));

            const auto rightSection = app.GetElementRect(state.SectionBodyID(state.internal.sectionCount - 1));
            const auto vRect        = app.GetElementRect(state.internal.vertScrollbarID);
            const auto hRect        = app.GetElementRect(state.internal.horzScrollbarID);

            constexpr int kThick = 12;
            // V scrollbar's bottom must not reach the body's bottom by
            // (at least) the corner thickness.
            EXPECT_LE(vRect.y + vRect.h, rightSection.y + rightSection.h - kThick + 1)
                << "vertical scrollbar should end at the top of the corner";
            // H scrollbar's right must not reach the body's right by
            // (at least) the corner thickness.
            EXPECT_LE(hRect.x + hRect.w, rightSection.x + rightSection.w - kThick + 1)
                << "horizontal scrollbar should end at the left of the corner";
        }

        // When BOTH scrollbars are visible a corner-cap element gets
        // emitted at the bottom-right (rightmost section).
        TEST_F(FrozenTableTest, ScrollbarCornerMaskExistsWhenBothScrollbarsVisible)
        {
            tachyon::TableState state{};
            // Headless skips render, so force both overflow flags.
            auto build = [&] {
                state.internal.anyHorzOverflow = state.internal.anyVertOverflow = true;
                EmitFrozen(state);
            };
            app.Frame(build);
            app.Frame(build);

            ASSERT_TRUE(app.HasElement(state.internal.horzScrollbarID));
            ASSERT_TRUE(app.HasElement(state.internal.vertScrollbarID));

            // The shared V/H scrollbars live on the rightmost section.
            const auto rightSection = app.GetElementRect(state.SectionBodyID(state.internal.sectionCount - 1));
            const auto vRect        = app.GetElementRect(state.internal.vertScrollbarID);
            const auto hRect        = app.GetElementRect(state.internal.horzScrollbarID);
            // Both scrollbar rects exist with non-zero size (track is huge,
            // clipped to body).
            EXPECT_GT(vRect.h, 0);
            EXPECT_GT(hRect.w, 0);
            // Sanity: vert scrollbar lives near body's right edge.
            EXPECT_GE(vRect.x + vRect.w, rightSection.x + rightSection.w - 16);
            // Sanity: horz scrollbar lives near body's bottom edge.
            EXPECT_GE(hRect.y + hRect.h, rightSection.y + rightSection.h - 16);
        }

        // -------------------------------------------------------------------
        // Scrollbar drag - both axes
        // -------------------------------------------------------------------

        TEST_F(FrozenTableTest, VerticalScrollbarDragWritesYOffset)
        {
            tachyon::TableState state{};
            // Headless skips render: set _lastViewport{W,H} and the overflow flags each
            // build (the prologue resets them).
            auto build = [&] {
                state.internal.lastViewportW = 200;
                state.internal.lastViewportH = 150;
                state.internal.anyHorzOverflow = state.internal.anyVertOverflow = true;
                EmitFrozen(state);
            };
            app.Frame(build);
            app.Frame(build);
            ASSERT_TRUE(app.HasElement(state.internal.vertScrollbarID))
                << "vertical scrollbar should be emitted once content overflows Y";

            const auto sbRect       = app.GetElementRect(state.internal.vertScrollbarID);
            const auto rightSection = app.GetElementRect(state.SectionBodyID(state.internal.sectionCount - 1));
            // The scrollbar rect spans kAxisSpan; clip the drag into the body's visible Y.
            const int trackTop    = std::max(sbRect.y, rightSection.y) + 5;
            const int trackBottom = std::min(sbRect.y + sbRect.h, rightSection.y + rightSection.h) - 5;
            const int2 from{ sbRect.x + sbRect.w / 2, trackTop };
            const int2 to  { from.x, trackBottom };

            const int2 before = SectionBodyScroll(state, size_t(state.internal.sectionCount - 1));
            app.DragFrom(from, to);
            app.Frame(build);

            const int2 after = SectionBodyScroll(state, size_t(state.internal.sectionCount - 1));
            EXPECT_LT(after.y, before.y) << "drag-down should move offset more negative";
        }

        TEST_F(FrozenTableTest, HorizontalScrollbarDragWritesXOffset)
        {
            tachyon::TableState state{};
            auto build = [&] {
                state.internal.lastViewportW = 200;
                state.internal.lastViewportH = 150;
                state.internal.anyHorzOverflow = state.internal.anyVertOverflow = true;
                EmitFrozen(state);
            };
            app.Frame(build);
            app.Frame(build);
            ASSERT_TRUE(app.HasElement(state.internal.horzScrollbarID))
                << "horizontal scrollbar should be emitted once content overflows X";

            const auto sbRect       = app.GetElementRect(state.internal.horzScrollbarID);
            const auto rightSection = app.GetElementRect(state.SectionBodyID(state.internal.sectionCount - 1));
            const int trackLeft  = std::max(sbRect.x, rightSection.x) + 5;
            const int trackRight = std::min(sbRect.x + sbRect.w, rightSection.x + rightSection.w) - 5;
            const int2 from{ trackLeft, sbRect.y + sbRect.h / 2 };
            const int2 to  { trackRight, from.y };

            const size_t lastSection = size_t(state.internal.sectionCount - 1);
            const int2 before = SectionBodyScroll(state, lastSection);
            app.DragFrom(from, to);
            app.Frame(build);

            EXPECT_LT(SectionBodyScroll(state, lastSection).x, before.x);
        }

    } // namespace
} // namespace tachyon

namespace tachyon
{
    namespace
    {
        // Mirrors the showcase's multi-section table (panel_split_scroll.cpp):
        // [# Type] fit-content | [Name Hex Multiple] scrollable | [Tag Status],
        // laid out in a 530x400 panel.
        class DemoSplitTableTest : public ::testing::Test
        {
        protected:
            // The showcase's configuration: whether Tag/Status are fit-content, and
            // whether the table fills its panel (console-panel pattern).
            static constexpr bool kTagStatusFitContent = true;
            static constexpr bool kTableFillsPanel     = true;

            GUITest::HeadlessGUIApp app;
            tachyon::TableState state{};
            std::vector<tachyon::VirtualTableColumn> cols;
            static constexpr int kPaneW = 530, kPaneH = 400;
            static constexpr int kRows  = 50;   // taller than the panel, within 16-bit layout coordinates

            void SetUp() override
            {
                ASSERT_TRUE(app.Initialize());
                auto cell = [this](const char* t) { return [this, t](int) { app.Cell(t); }; };
                cols = {
                    { .column = { .header = "#",        .initialWidth =  60, .minWidth = 40, .maxWidth = 160, .locked = true }, .generateCell = cell("0"),   .sectionId = 0, .sectionSizing = SectionSizing::FitContent },
                    { .column = { .header = "Type",     .initialWidth =  80, .minWidth = 40, .maxWidth = 200, .locked = true }, .generateCell = cell("int"), .sectionId = 0 },
                    { .column = { .header = "Name",     .initialWidth = 160, .minWidth = 60, .maxWidth = 400 },                  .generateCell = cell("Row"), .sectionId = 1 },
                    { .column = { .header = "Hex",      .initialWidth = 120, .minWidth = 60, .maxWidth = 200 },                  .generateCell = cell("0x"),  .sectionId = 1 },
                    { .column = { .header = "Multiple", .initialWidth = 140, .minWidth = 60, .maxWidth = 400 },                  .generateCell = cell("m"),   .sectionId = 1 },
                    { .column = { .header = "Tag",      .initialWidth =  60, .minWidth = 40, .maxWidth = 200, .locked = true, .sizing = ColumnSizing::Stretch }, .generateCell = cell("A"), .sectionId = 2,
                      .sectionSizing = kTagStatusFitContent ? SectionSizing::FitContent : SectionSizing::Scrollable },
                    { .column = { .header = "Status",   .initialWidth =  80, .minWidth = 60, .maxWidth = 200, .locked = true }, .generateCell = cell("ok"),  .sectionId = 2 },
                };
            }
            void TearDown() override { app.Cleanup(); }

            void Frame()
            {
                app.Frame([&] {
                    app.BeginRootWindow();
                    app.BeginArea(app.idManager.AllocateDynamicID("pane"), app.theme.emptyVertStyleRef,
                                  LayoutOverride{}.Size(kPaneW, kPaneH));
                    std::span<tachyon::VirtualTableColumn> sp(cols.data(), cols.size());
                    const tachyon::ID tableID = app.idManager.AllocateDynamicID("multiSectionTable");
                    if (kTableFillsPanel)
                    {
                        app.BeginHorizontal(app.idManager.AllocateDynamicID("splitScrollFill"),
                                            app.theme.flexFillRowStyleRef, LayoutOverride::Auto().FlexGrow(1));
                        app.VirtualTable(tableID, state, sp, kRows, 0, app.theme.tableFillStyleRef, LayoutOverride{});
                        app.EndHorizontal();
                    }
                    else
                        app.VirtualTable(tableID, state, sp, kRows, 0, app.theme.tableStyleRef, LayoutOverride{}.NoGrow().NoShrink());
                    app.EndArea();
                    app.EndRootWindow();
                });
                for (const auto& entry : app.elementStorage.GetPostLayoutEntries())
                {
                    const auto* er = app.elementStorage.GetElementRectPtr(entry.ownerRef);
                    if (!er) continue;
                    PostLayoutContext plCtx{ app.elementStorage, entry.ownerRef, er->rect, er->innerRect, er->contentTransform };
                    entry.callback(plCtx);
                }
            }

            void Settle() { for (int i = 0; i < 4; ++i) Frame(); }

            tachyon::IntRect Section(int s) const { return app.GetElementRect(state.SectionBodyID(s)); }

            // Horizontal scrollbar tracks whose left edge lies in section `s`.
            std::vector<std::pair<tachyon::ID, tachyon::IntRect>> HorzTracksIn(int s) const
            {
                const auto sec = Section(s);
                std::vector<std::pair<tachyon::ID, tachyon::IntRect>> out;
                for (uint32_t d = 0; d < app.elementStorage.NumberOfDepthLevels(); d++)
                    for (uint32_t i = 0; i < app.elementStorage.DepthSizeOf(d); i++)
                    {
                        tachyon::ElementRef e{ d, i };
                        if (!app.elementStorage.IsValidRef(e)) continue;
                        const auto& el = app.elementStorage.GetElementUnsafe(e);
                        if (el.styleRef != app.theme.scrollbarHorizontalTrackStyleRef) continue;
                        const auto* rp = app.elementStorage.GetElementRectPtr(e);
                        if (rp && rp->rect.x >= sec.x && rp->rect.x < sec.x + sec.w) out.push_back({ el.id, rp->rect });
                    }
                return out;
            }
        };

        // The frozen right-hand columns keep their width while the middle
        // section's columns are resized.
        TEST_F(DemoSplitTableTest, FrozenRightSectionKeepsItsWidth)
        {
            Settle();
            ASSERT_EQ(state.internal.sectionCount, 3);
            const int32_t before = Section(2).w;
            state.columnWidths[3] += 150;   // widen Hex
            Settle();
            EXPECT_EQ(Section(2).w, before);
        }

        // The overflowing middle section's horizontal scrollbar is inside the visible panel.
        TEST_F(DemoSplitTableTest, MiddleSectionScrollbarIsVisible)
        {
            Settle();
            state.columnWidths[3] += 150;   // make the middle section overflow
            Settle();
            const auto tracks = HorzTracksIn(1);
            ASSERT_EQ(tracks.size(), 1u) << "the overflowing middle section has no horizontal scrollbar";
            EXPECT_LE(tracks[0].second.y + tracks[0].second.h, 10 + kPaneH) << "scrollbar is below the visible panel";
        }

        // The middle section's track is laid out across the full axis span and
        // clipped by its section: hovering the next section's bottom strip must
        // not land on it.
        TEST_F(DemoSplitTableTest, MiddleScrollbarDoesNotTakeTheNextSectionsInput)
        {
            Settle();
            state.columnWidths[3] += 150;
            Settle();
            const auto tracks = HorzTracksIn(1);
            ASSERT_EQ(tracks.size(), 1u);
            ASSERT_GT(tracks[0].second.x + tracks[0].second.w, Section(2).x) << "test setup: the track spans past its section";

            const auto right = Section(2);
            const int2 point{ right.x + right.w / 2, tracks[0].second.y + tracks[0].second.h / 2 };
            app.InjectMouseMove(point);
            Frame();
            EXPECT_NE(app.guiStateManager.Interaction().GetHoveredID(), tracks[0].first);
        }
    }
}
