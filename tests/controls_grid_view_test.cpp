// Integration tests for BeginGridView / GridViewItem / EndGridView.
// The item element itself is the hit target, so clicks are reliable headless.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_core/gui_id.h>
#include <gui_core/gui_id_manager.h>
#include <gui_core/state/gui_state_manager.h>
#include <gui_controls/subsystems/gui_subsystem_state.h>

namespace tachyon
{
    using GUITest::HeadlessGUIApp;

    namespace
    {
        class ControlsGridViewTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;
            ID gridID{};
            void SetUp() override
            {
                ASSERT_TRUE(app.Initialize());
                gridID = app.idManager.CreateStaticID();
            }
            void TearDown() override { app.Cleanup(); }

            std::vector<ID> EmitGrid(int n, int& selectedIndex,
                                     tachyon::MultiSelectState* multi = nullptr)
            {
                std::vector<ID> ids;
                ids.reserve(n);
                for (int i = 0; i < n; ++i)
                {
                    ID id{};
                    id.value.dynamicParts.mode = IDMode::Static;
                    id.value.dynamicParts.hashValue = 3000 + i;
                    ids.push_back(id);
                }
                app.Frame([&] {
                    app.BeginRootWindow();
                    if (app.BeginGridView(gridID, &selectedIndex,
                                          app.theme.gridViewStyleRef, multi,
                                          /*dragDrop*/ nullptr,
                                          LayoutOverride{}.Size(400, 300)))
                    {
                        for (int i = 0; i < n; ++i)
                            app.GridViewItem(ids[i], "item",
                                             ElementState::None, nullptr,
                                             StyleRef{}, LayoutOverride{}.Size(80, 80));
                        app.EndGridView();
                    }
                    app.EndRootWindow();
                });
                return ids;
            }
        };

        // Dragging an item (as in the demo's files panel) starts a drag, and its visualizer
        // shows a ghost window with the item's icon by the cursor.
        TEST_F(ControlsGridViewTest, DraggingAnItemShowsItsGhost)
        {
            int sel = -1;
            const ID ghostID = app.idManager.CreateStaticID();
            std::vector<ID> ids;
            for (int i = 0; i < 3; ++i)
            {
                ID id{};
                id.value.dynamicParts.mode = IDMode::Static;
                id.value.dynamicParts.hashValue = 3000 + i;
                ids.push_back(id);
            }
            int visualized = 0;
            ItemDragDropConfig dnd;
            dnd.payloadType = 42;
            dnd.getPayload  = [&](ID id) -> uintptr_t { for (size_t i = 0; i < ids.size(); ++i) if (ids[i] == id) return i; return uintptr_t(-1); };
            dnd.onDrop      = [](uintptr_t, ID) {};
            dnd.visualizer  = [&](const DragVisualContext& visual) {
                ++visualized;
                if (app.BeginCursorGhostWindow(ghostID, visual.screenPos))
                    app.AddGhostIcon(app.theme.addIcon);
                app.EndGhostWindow();
            };
            auto draw = [&] {
                app.BeginRootWindow();
                if (app.BeginGridView(gridID, &sel, app.theme.gridViewStyleRef, nullptr, &dnd, LayoutOverride{}.Size(400, 300)))
                {
                    for (const ID id : ids)
                        app.GridViewItem(id, "item", ElementState::None, nullptr, StyleRef{}, LayoutOverride{}.Size(80, 80));
                    app.EndGridView();
                }
                app.EndRootWindow();
            };
            app.Frame(draw);
            const int2 from = app.CenterOf(ids[0]);
            app.InjectMouseMove(from);
            app.InjectMouseDown(from);
            app.Frame(draw);
            app.InjectMouseMove(from + int2{ 120, 160 }, MouseButtons::Left);
            app.Frame(draw);
            app.Frame(draw);

            EXPECT_TRUE(app.guiStateManager.Drag().IsDragging());
            EXPECT_GT(visualized, 0);
            EXPECT_TRUE(app.HasElement(ghostID)) << "the ghost window is declared";

            app.InjectMouseUp(from + int2{ 120, 160 });
            app.Frame(draw);
            app.Frame(draw);
            EXPECT_FALSE(app.guiStateManager.Drag().IsDragging());
        }

        // The item a drag would drop into stands out (:active); one that refuses it doesn't.
        TEST_F(ControlsGridViewTest, TheItemADragWouldDropIntoIsHighlighted)
        {
            int sel = -1;
            std::vector<ID> ids;
            for (int i = 0; i < 3; ++i)
            {
                ID id{};
                id.value.dynamicParts.mode = IDMode::Static;
                id.value.dynamicParts.hashValue = 3100 + i;
                ids.push_back(id);
            }
            ItemDragDropConfig dnd;
            dnd.payloadType = 43;
            dnd.getPayload  = [&](ID id) -> uintptr_t { for (size_t i = 0; i < ids.size(); ++i) if (ids[i] == id) return i; return uintptr_t(-1); };
            dnd.canDrop     = [&](uintptr_t, ID target) { return target != ids[2]; };   // item 2 refuses
            dnd.onDrop      = [](uintptr_t, ID) {};
            auto draw = [&] {
                app.BeginRootWindow();
                if (app.BeginGridView(gridID, &sel, app.theme.gridViewStyleRef, nullptr, &dnd, LayoutOverride{}.Size(400, 300)))
                {
                    for (const ID id : ids)
                        app.GridViewItem(id, "item", ElementState::None, nullptr, StyleRef{}, LayoutOverride{}.Size(80, 80));
                    app.EndGridView();
                }
                app.EndRootWindow();
            };
            auto state = [&](ID id) { return app.elementStorage.GetElementUnsafe(app.elementStorage.FindElementRefByID(id)).state; };
            app.Frame(draw);
            const int2 from = app.CenterOf(ids[0]);
            app.InjectMouseMove(from);
            app.InjectMouseDown(from);
            app.Frame(draw);
            for (const size_t over : { size_t(1), size_t(2) })
            {
                app.InjectMouseMove(app.CenterOf(ids[over]), MouseButtons::Left);
                app.Frame(draw);
                app.Frame(draw);
                ASSERT_TRUE(app.guiStateManager.Drag().IsDragging());
                const bool highlighted = (state(ids[over]) & ElementState::Active) != ElementState::None;
                EXPECT_EQ(highlighted, over == 1) << "over item " << over;
            }
            app.InjectMouseUp(app.CenterOf(ids[2]));
            app.Frame(draw);
        }

        TEST_F(ControlsGridViewTest, GridRendersAllItems)
        {
            int sel = 0;
            auto ids = EmitGrid(4, sel);
            for (auto id : ids) EXPECT_TRUE(app.HasElement(id));
        }

        TEST_F(ControlsGridViewTest, ClickingItemSetsSelectedIndex)
        {
            int sel = 0;
            auto ids = EmitGrid(4, sel);

            app.ClickAt(app.CenterOf(ids[2]));
            EmitGrid(4, sel);

            EXPECT_EQ(sel, 2);
        }

        TEST_F(ControlsGridViewTest, ClickingDifferentItemUpdatesSelection)
        {
            int sel = 0;
            auto ids = EmitGrid(4, sel);

            app.ClickAt(app.CenterOf(ids[1]));
            EmitGrid(4, sel);
            EXPECT_EQ(sel, 1);

            app.ClickAt(app.CenterOf(ids[3]));
            EmitGrid(4, sel);
            EXPECT_EQ(sel, 3);
        }

        TEST_F(ControlsGridViewTest, RightClickDoesNotSelect)
        {
            int sel = 0;
            auto ids = EmitGrid(4, sel);

            app.ClickAt(app.CenterOf(ids[2]), MouseButtons::Right);
            EmitGrid(4, sel);
            EXPECT_EQ(sel, 0);
        }

        // ---------------------------------------------------------------------
        // Multi-select via MultiSelectState.
        // ---------------------------------------------------------------------

        TEST_F(ControlsGridViewTest, PlainClickWithMultiSelectClearsAndInsertsOne)
        {
            int sel = 0;
            tachyon::MultiSelectState multi{};
            auto ids = EmitGrid(4, sel, &multi);

            multi.selectedIDs = { ids[0], ids[1] };

            app.ClickAt(app.CenterOf(ids[2]));
            EmitGrid(4, sel, &multi);

            EXPECT_EQ(multi.selectedIDs.size(), 1u);
            EXPECT_TRUE(multi.selectedIDs.count(ids[2]));
            EXPECT_EQ(multi.lastClickedID, ids[2]);
        }

        TEST_F(ControlsGridViewTest, SingleCtrlClickInsertsItem)
        {
            int sel = 0;
            tachyon::MultiSelectState multi{};
            auto ids = EmitGrid(4, sel, &multi);

            app.ClickAt(app.CenterOf(ids[1]), MouseButtons::Left, KeyModifier::Control);
            EmitGrid(4, sel, &multi);

            EXPECT_TRUE(multi.selectedIDs.count(ids[1]));
            EXPECT_EQ(multi.lastClickedID, ids[1]);
        }

        TEST_F(ControlsGridViewTest, CtrlClickAccumulatesAcrossClicks)
        {
            int sel = 0;
            tachyon::MultiSelectState multi{};
            auto ids = EmitGrid(4, sel, &multi);

            app.ClickAt(app.CenterOf(ids[0]), MouseButtons::Left, KeyModifier::Control);
            EmitGrid(4, sel, &multi);
            ASSERT_TRUE(multi.selectedIDs.count(ids[0]));

            app.ClickAt(app.CenterOf(ids[2]), MouseButtons::Left, KeyModifier::Control);
            EmitGrid(4, sel, &multi);

            EXPECT_TRUE(multi.selectedIDs.count(ids[0]))
                << "ids[0] should remain after second Ctrl+Click on a different item";
            EXPECT_TRUE(multi.selectedIDs.count(ids[2]))
                << "ids[2] should be added by Ctrl+Click";
            EXPECT_EQ(multi.selectedIDs.size(), 2u);
        }

        TEST_F(ControlsGridViewTest, ShiftClickExtendsRangeFromAnchor)
        {
            int sel = 0;
            tachyon::MultiSelectState multi{};
            auto ids = EmitGrid(5, sel, &multi);

            // Anchor at item 1.
            app.ClickAt(app.CenterOf(ids[1]));
            EmitGrid(5, sel, &multi);
            ASSERT_EQ(multi.lastClickedID, ids[1]);

            // Shift+Click 3 -> range {1, 2, 3}.
            app.ClickAt(app.CenterOf(ids[3]), MouseButtons::Left, KeyModifier::Shift);
            EmitGrid(5, sel, &multi);

            EXPECT_TRUE(multi.selectedIDs.count(ids[1]));
            EXPECT_TRUE(multi.selectedIDs.count(ids[2]));
            EXPECT_TRUE(multi.selectedIDs.count(ids[3]));
            EXPECT_FALSE(multi.selectedIDs.count(ids[0]));
            EXPECT_FALSE(multi.selectedIDs.count(ids[4]));
            EXPECT_EQ(multi.lastClickedID, ids[1]) << "Anchor should be preserved on Shift+Click";
        }

        // ---------------------------------------------------------------------
        // Inline rename (renameBuffer): F2 opens an editor, typing keeps it
        // open, and Enter / Escape commit / discard the edit.
        // ---------------------------------------------------------------------

        class GridViewRenameTest : public ControlsGridViewTest
        {
        protected:
            tachyon::MultiSelectState multi{};
            std::vector<std::string> names = { "alpha", "beta", "gamma", "delta" };
            std::vector<ID> ids;

            void SetUp() override
            {
                ControlsGridViewTest::SetUp();
                for (int i = 0; i < 4; ++i)
                {
                    ID id{};
                    id.value.dynamicParts.mode = IDMode::Static;
                    id.value.dynamicParts.hashValue = 4000 + i;
                    ids.push_back(id);
                }
            }

            void EmitFrame()
            {
                app.Frame([&] {
                    app.BeginRootWindow();
                    if (app.BeginGridView(gridID, nullptr, app.theme.gridViewStyleRef, &multi,
                                          /*dragDrop*/ nullptr, LayoutOverride{}.Size(400, 300)))
                    {
                        for (int i = 0; i < 4; ++i)
                            app.GridViewItem(ids[i], names[i],
                                             ElementState::None, &names[i],
                                             StyleRef{}, LayoutOverride{}.Size(80, 80));
                        app.EndGridView();
                    }
                    app.EndRootWindow();
                });
            }

            // Select item 1 and start its rename via F2; settle until the
            // editor holds focus.
            void StartRenameOnItem1()
            {
                EmitFrame();
                app.ClickAt(app.CenterOf(ids[1]));
                EmitFrame();
                ASSERT_EQ(multi.lastClickedID, ids[1]);
                app.InjectKey(KeyCode::F2);
                EmitFrame();
                ASSERT_TRUE(app.m_renameSub->isActive);
                ASSERT_EQ(app.m_renameSub->targetID, ids[1]);
                EmitFrame(); // editor emitted, takes focus
                ASSERT_EQ(app.guiStateManager.Interaction().GetFocusedID(), ids[1]);
            }
        };

        TEST_F(GridViewRenameTest, TypingKeepsEditorOpenAndEnterCommits)
        {
            StartRenameOnItem1();

            app.InjectText("x");
            EmitFrame();
            EXPECT_TRUE(app.m_renameSub->isActive)
                << "typing must not close the rename editor";
            EXPECT_EQ(names[1], "beta") << "no commit yet — the name is unchanged";

            app.InjectText("y");
            EmitFrame();
            EXPECT_TRUE(app.m_renameSub->isActive);

            app.InjectKey(KeyCode::Return);
            EmitFrame();
            EXPECT_FALSE(app.m_renameSub->isActive) << "Enter commits and ends the rename";
            EXPECT_EQ(names[1], "betaxy");
        }

        // A second grid view in the same frame must not wipe the first one's
        // rename lookups (end-of-frame F2 handling needs them).
        TEST_F(GridViewRenameTest, SecondGridInFrameKeepsFirstGridRenameable)
        {
            ID otherGrid = app.idManager.CreateStaticID();
            ID otherItem = app.idManager.CreateStaticID();
            int otherSel = -1;
            auto emitBoth = [&] {
                app.Frame([&] {
                    app.BeginRootWindow();
                    if (app.BeginGridView(gridID, nullptr, app.theme.gridViewStyleRef, &multi,
                                          /*dragDrop*/ nullptr, LayoutOverride{}.Size(400, 300)))
                    {
                        for (int i = 0; i < 4; ++i)
                            app.GridViewItem(ids[i], names[i],
                                             ElementState::None, &names[i],
                                             StyleRef{}, LayoutOverride{}.Size(80, 80));
                        app.EndGridView();
                    }
                    if (app.BeginGridView(otherGrid, &otherSel, nullptr, nullptr, LayoutOverride{}.Size(400, 100)))
                    {
                        app.GridViewItem(otherItem, "other",
                                         ElementState::None, nullptr, StyleRef{}, LayoutOverride{}.Size(80, 80));
                        app.EndGridView();
                    }
                    app.EndRootWindow();
                });
            };

            emitBoth();
            app.ClickAt(app.CenterOf(ids[1]));
            emitBoth();
            ASSERT_EQ(multi.lastClickedID, ids[1]);
            app.InjectKey(KeyCode::F2);
            emitBoth();
            EXPECT_TRUE(app.m_renameSub->isActive);
            EXPECT_EQ(app.m_renameSub->targetID, ids[1]);
        }

        TEST_F(GridViewRenameTest, EscapeCancelsWithoutCommit)
        {
            StartRenameOnItem1();

            app.InjectText("x");
            EmitFrame();
            ASSERT_TRUE(app.m_renameSub->isActive);

            app.InjectKey(KeyCode::Escape);
            EmitFrame();
            EXPECT_FALSE(app.m_renameSub->isActive);
            EXPECT_EQ(names[1], "beta") << "Escape must discard the edit";
        }

        // ---------------------------------------------------------------------
        // VirtualGridView viewport tracking - files-panel shaped structure.
        // ---------------------------------------------------------------------

        // Mirrors the Files panel: fixed outer pane -> split with a fixed left column
        // -> grid pane -> VirtualGridView with stretched cells. Shrinking the pane must
        // shrink the viewport; a content-derived min width would lock it at the old size.
        class GridViewShrinkTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;
            tachyon::GridViewVirtualState vstate{};
            int paneW = 600;

            void SetUp() override { ASSERT_TRUE(app.Initialize()); }
            void TearDown() override { app.Cleanup(); }

            // Mirror production's post-layout pass (the harness skips it) so
            // VirtualGridView's viewport capture actually runs.
            void FirePostLayout()
            {
                for (const auto& entry : app.elementStorage.GetPostLayoutEntries())
                {
                    const auto* er = app.elementStorage.GetElementRectPtr(entry.ownerRef);
                    if (!er) continue;
                    PostLayoutContext plCtx{ app.elementStorage, entry.ownerRef,
                                             er->rect, er->innerRect, er->contentTransform };
                    entry.callback(plCtx);
                }
            }

            void Frame()
            {
                app.Frame([&] {
                    app.BeginRootWindow();
                    app.BeginArea(app.idManager.AllocateDynamicID("outerPane"),
                                  app.theme.emptyVertStyleRef,
                                  LayoutOverride{}.Size(CssPx(paneW), 400));
                    app.BeginHorizontal(app.idManager.AllocateDynamicID("split"),
                                        app.theme.flexFillRowStyleRef,
                                        LayoutOverride::Auto().FlexGrow(1));
                    app.BeginArea(app.idManager.AllocateDynamicID("tree"),
                                  app.theme.emptyVertStyleRef,
                                  LayoutOverride{}.FixedWidth(180).NoGrow());
                    app.EndArea();
                    app.BeginVertical(app.idManager.AllocateDynamicID("gridPane"),
                                      app.theme.flexFillColStyleRef,
                                      LayoutOverride::Auto().FlexGrow(1).MinHeight(0));
                    app.VirtualGridView(app.idManager.AllocateDynamicID("grid"), vstate,
                                        /*itemCount*/5, tachyon::int2{ 80, 80 },
                                        [&](int, tachyon::ID itemID) {
                                            const int w = std::max(80, int(vstate.cellStretchedW));
                                            app.GridViewItem(itemID, "file_with_a_long_name.txt", ElementState::None,
                                                             nullptr,
                                                             StyleRef{}, LayoutOverride{}.Size(CssPx(w), 80));
                                        },
                                        nullptr, nullptr, nullptr, {}, {},
                                        LayoutOverride{}.FlexGrow(1));
                    app.EndVertical();
                    app.EndHorizontal();
                    app.EndArea();
                    app.EndRootWindow();
                });
                FirePostLayout();
            }
        };

        TEST_F(GridViewShrinkTest, ViewportTracksPaneShrinkWithUnderfilledGrid)
        {
            // Settle at the wide size (viewport capture lags construction by a frame).
            for (int i = 0; i < 3; ++i) Frame();
            ASSERT_GT(vstate.lastViewportW, 300) << "grid should occupy the wide pane";

            // Shrink the pane by half; give the feedback loop ample frames.
            paneW = 300;
            for (int i = 0; i < 8; ++i) Frame();

            EXPECT_LE(vstate.lastViewportW, 300 - 180 + 40)
                << "grid viewport must shrink with the pane; stretched cells must "
                   "not wedge the width at the old size";
        }

        // BeginGridView: Down moves one laid-out row, even when the row's gaps
        // leave room for one fewer item than innerWidth / itemWidth suggests.
        TEST_F(ControlsGridViewTest, DownMovesOneLaidOutRow)
        {
            std::vector<ID> items(12);
            for (auto& id : items) id = app.idManager.CreateStaticID();
            int selected = 0;
            auto emit = [&] {
                app.Frame([&] {
                    app.BeginRootWindow();
                    app.BeginArea(app.idManager.AllocateDynamicID("pane"), app.theme.emptyVertStyleRef,
                                  LayoutOverride{}.Size(401, 400));
                    if (app.BeginGridView(gridID, &selected, nullptr, nullptr, LayoutOverride{}.FlexGrow(1)))
                    {
                        for (auto& id : items)
                            app.GridViewItem(id, "item", ElementState::None, nullptr,
                                             StyleRef{}, LayoutOverride{}.Size(100, 40));
                        app.EndGridView();
                    }
                    app.EndArea();
                    app.EndRootWindow();
                });
            };
            emit();
            emit();
            int perRow = 0;
            while (perRow < int(items.size()) &&
                   app.GetElementRect(items[size_t(perRow)]).y == app.GetElementRect(items[0]).y) ++perRow;
            ASSERT_GT(perRow, 0);
            ASSERT_LT(perRow, 4) << "test setup: the gaps must cost a column";

            app.guiStateManager.Interaction().SetFocusID(gridID);
            app.InjectKey(KeyCode::Down);
            emit();
            EXPECT_EQ(selected, perRow);
        }

        // ---------------------------------------------------------------------
        // VirtualGridView keyboard navigation and Shift-range selection.
        // ---------------------------------------------------------------------

        // A slow second click on a selected item of a virtual grid (the files panel's) opens
        // its rename; a press that became a drag, dropped back on the item, doesn't.
        class VirtualGridRenameTest : public GridViewShrinkTest
        {
        protected:
            tachyon::ID gridID{};
            tachyon::MultiSelectState multi{};
            std::vector<std::string> names = { "alpha", "beta", "gamma", "delta" };
            std::vector<tachyon::ID> itemIDs = std::vector<tachyon::ID>(4, tachyon::ID::Invalid());
            ItemDragDropConfig* dragDrop = nullptr;

            void SetUp() override
            {
                GridViewShrinkTest::SetUp();
                gridID = app.idManager.CreateStaticID();
            }

            void GridFrame()
            {
                app.Frame([&] {
                    app.BeginRootWindow();
                    app.BeginArea(app.idManager.AllocateDynamicID("gridPane"), app.theme.emptyVertStyleRef,
                                  LayoutOverride{}.Size(420, 220));
                    app.VirtualGridView(gridID, vstate, int(names.size()), tachyon::int2{ 100, 50 },
                                        [&](int idx, tachyon::ID itemID) {
                                            itemIDs[size_t(idx)] = itemID;
                                            app.GridViewItem(itemID, names[size_t(idx)], ElementState::None,
                                                             &names[size_t(idx)], StyleRef{}, LayoutOverride{}.Size(100, 50));
                                        },
                                        nullptr, &multi, dragDrop, {}, {}, LayoutOverride{}.FlexGrow(1).MinHeight(0));
                    app.EndArea();
                    app.EndRootWindow();
                });
                FirePostLayout();
            }
        };

        TEST_F(VirtualGridRenameTest, ASlowSecondClickStartsARename)
        {
            GridFrame();
            GridFrame();
            app.ClickAt(app.CenterOf(itemIDs[1]));
            GridFrame();
            vstate.internal.itemClickTracker.lastTimeNanos -= 800000000;   // the tracker reads the real clock
            app.ClickAt(app.CenterOf(itemIDs[1]));
            GridFrame();
            GridFrame();
            EXPECT_TRUE(app.m_renameSub->isActive);
            EXPECT_TRUE(app.IsRenaming());
        }

        TEST_F(VirtualGridRenameTest, DraggingAnItemOntoItselfDoesNotStartARename)
        {
            ItemDragDropConfig dnd;
            dnd.payloadType = 44;
            dnd.getPayload  = [](tachyon::ID) -> uintptr_t { return 0; };
            dnd.onDrop      = [](uintptr_t, tachyon::ID) {};
            dragDrop = &dnd;
            GridFrame();
            GridFrame();
            const int2 at = app.CenterOf(itemIDs[1]);
            app.ClickAt(at);
            GridFrame();
            vstate.internal.itemClickTracker.lastTimeNanos -= 800000000;   // the tracker reads the real clock
            app.InjectMouseMove(at);
            app.InjectMouseDown(at);               // a slow second press: arms the rename
            GridFrame();
            app.InjectMouseMove(at + int2{ 150, 120 }, MouseButtons::Left);
            GridFrame();
            GridFrame();
            ASSERT_TRUE(app.guiStateManager.Drag().IsDragging());
            app.InjectMouseMove(at, MouseButtons::Left);
            GridFrame();
            app.InjectMouseUp(at);                 // dropped on itself
            GridFrame();
            GridFrame();
            EXPECT_FALSE(app.m_renameSub->isActive);
        }

        class VirtualGridKeyboardTest : public GridViewShrinkTest
        {
        protected:
            static constexpr int kItems = 120;
            tachyon::ID gridID{};
            int selected = 0;
            tachyon::MultiSelectState multi{};
            bool useMulti = false;
            std::vector<tachyon::ID> itemIDs = std::vector<tachyon::ID>(kItems, tachyon::ID::Invalid());

            void SetUp() override
            {
                GridViewShrinkTest::SetUp();
                gridID = app.idManager.CreateStaticID();
            }

            // A 420px-wide viewport: 4 columns of 100+4 fit, but only 3 once the
            // 12px scrollbar gutter is reserved (the grid overflows vertically).
            void GridFrame()
            {
                app.Frame([&] {
                    app.BeginRootWindow();
                    app.BeginArea(app.idManager.AllocateDynamicID("gridPane"),
                                  app.theme.emptyVertStyleRef,
                                  LayoutOverride{}.Size(420, 220));
                    app.VirtualGridView(gridID, vstate, kItems, tachyon::int2{ 100, 50 },
                                        [&](int idx, tachyon::ID itemID) {
                                            itemIDs[size_t(idx)] = itemID;
                                            app.GridViewItem(itemID, "item",
                                                             ElementState::None, nullptr,
                                                             StyleRef{}, LayoutOverride{}.Size(100, 50));
                                        },
                                        useMulti ? nullptr : &selected,
                                        useMulti ? &multi : nullptr,
                                        nullptr, {}, {},
                                        LayoutOverride{}.FlexGrow(1).MinHeight(0));
                    app.EndArea();
                    app.EndRootWindow();
                });
                FirePostLayout();
            }

            int ItemsInFirstRow() const
            {
                const int y0 = app.GetElementRect(itemIDs[0]).y;
                int n = 0;
                while (n < kItems && app.HasElement(itemIDs[size_t(n)]) &&
                       app.GetElementRect(itemIDs[size_t(n)]).y == y0) ++n;
                return n;
            }

            void Press(tachyon::KeyCode key)
            {
                app.guiStateManager.Interaction().SetFocusID(gridID);
                app.InjectKey(key);
                GridFrame();
                GridFrame();
            }
        };

        TEST_F(VirtualGridKeyboardTest, DownMovesExactlyOneLaidOutRow)
        {
            for (int i = 0; i < 3; ++i) GridFrame();
            const int cols = ItemsInFirstRow();
            ASSERT_GT(cols, 0);
            selected = 0;
            Press(tachyon::KeyCode::Down);
            EXPECT_EQ(selected, cols) << "Down must land straight below, not diagonally";
        }

        TEST_F(VirtualGridKeyboardTest, KeyboardSelectionScrollsIntoView)
        {
            for (int i = 0; i < 3; ++i) GridFrame();
            selected = 0;
            for (int i = 0; i < 12; ++i)
                Press(tachyon::KeyCode::Down);

            ASSERT_GT(selected, 20);
            ASSERT_TRUE(app.HasElement(itemIDs[size_t(selected)])) << "selected item was virtualized away";
            const auto item = app.GetElementRect(itemIDs[size_t(selected)]);
            const auto grid = app.GetElementRect(gridID);
            EXPECT_GE(item.y, grid.y);
            EXPECT_LE(item.y + item.h, grid.y + grid.h);
        }

        TEST_F(VirtualGridKeyboardTest, ShiftClickExtendsFromAnOffScreenAnchor)
        {
            useMulti = true;
            for (int i = 0; i < 3; ++i) GridFrame();
            app.ClickAt(app.CenterOf(itemIDs[0]));
            GridFrame();
            ASSERT_EQ(multi.lastClickedID, itemIDs[0]);

            // Scroll far enough that item 0 is no longer emitted.
            auto& sm = app.guiStateManager.GetScrollManagement();
            sm.SetScrollOffset(sm.GetOrCreateScrollOffsetHandle(gridID), tachyon::int2{ 0, -1200 });
            GridFrame();
            GridFrame();
            ASSERT_FALSE(app.HasElement(itemIDs[0]));

            // An item fully inside the viewport (buffer rows sit outside it).
            const auto grid = app.GetElementRect(gridID);
            int target = -1;
            for (int i = kItems - 1; i >= 0 && target < 0; --i)
            {
                if (!app.HasElement(itemIDs[size_t(i)])) continue;
                const auto r = app.GetElementRect(itemIDs[size_t(i)]);
                if (r.y >= grid.y && r.y + r.h <= grid.y + grid.h) target = i;
            }
            ASSERT_GT(target, 0);
            const auto c = app.CenterOf(itemIDs[size_t(target)]);
            app.InjectMouseMove(c, MouseButtons::None, KeyModifier::Shift);
            app.InjectMouseDown(c, MouseButtons::Left, KeyModifier::Shift);
            app.InjectMouseUp  (c, MouseButtons::Left, KeyModifier::Shift);
            GridFrame();

            EXPECT_EQ(multi.selectedIDs.size(), size_t(target + 1));
            EXPECT_EQ(multi.selectedIDs.count(itemIDs[0]), 1u);
        }

    } // namespace
} // namespace tachyon
