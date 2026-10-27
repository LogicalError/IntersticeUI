// The (fromIndex, toIndex) onReorder reports for various drags. Items use
// fixed sizes so they are hit-testable headless.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_core/elements/gui_element_storage.h>

#include <optional>
#include <utility>
#include <vector>

namespace tachyon
{
    using GUITest::HeadlessGUIApp;

    namespace
    {
        class ReorderableTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;
            tachyon::ID groupID{};
            tachyon::ID itemIDs[4]{};

            std::optional<std::pair<int, int>> lastReorder;
            int reorderCalls = 0;

            void SetUp() override
            {
                ASSERT_TRUE(app.Initialize());
                groupID = app.idManager.CreateStaticID();
                for (auto& id : itemIDs) id = app.idManager.CreateStaticID();
            }
            void TearDown() override { app.Cleanup(); }

            // Vertical group of 4 uniform rows, each with a drag handle.
            void EmitGroup()
            {
                app.BeginRootWindow();
                auto onReorder = [this](int from, int to)
                {
                    lastReorder = { from, to };
                    reorderCalls++;
                };
                if (app.BeginReorderableGroup(groupID, ReorderDirection::Vertical,
                                              onReorder,
                                              tachyon::LayoutOverride{}.Size(300, 400)))
                {
                    for (int i = 0; i < 4; i++)
                    {
                        app.BeginReorderableItem(itemIDs[i],
                                                 tachyon::LayoutOverride{}.Size(280, 50));
                        app.EmitDragHandle();
                        app.EndReorderableItem();
                    }
                    app.EndReorderableGroup();
                }
                app.EndRootWindow();
            }

            tachyon::int2 ThumbCenter(tachyon::ID itemID) const
            {
                const auto itemRef = app.elementStorage.FindElementRefByID(itemID);
                if (!itemRef.IsValid()) return {};
                const auto children = app.elementStorage.GetChildRange(itemRef);
                if (children.Empty()) return {};
                const tachyon::ElementRef thumbRef{ itemRef.depth + 1, children.begin };
                const auto* tr = app.elementStorage.GetElementRectPtr(thumbRef);
                if (!tr) return {};
                return { tr->rect.x + tr->rect.w / 2, tr->rect.y + tr->rect.h / 2 };
            }

            tachyon::IntRect ItemRect(tachyon::ID itemID) const
            {
                return app.GetElementRect(itemID);
            }

            // Press on `srcItem`'s thumb, drag to `dstY`, release. One frame
            // per phase so threshold + edge-crossing logic sees each step.
            void DragItemTo(tachyon::ID srcItem, int dstY)
            {
                const auto thumb = ThumbCenter(srcItem);
                app.InjectMouseMove(thumb);
                app.InjectMouseDown(thumb);
                app.Frame([&] { EmitGroup(); });
                app.InjectMouseMove({ thumb.x, dstY }, tachyon::MouseButtons::Left);
                app.Frame([&] { EmitGroup(); });
                app.InjectMouseUp({ thumb.x, dstY });
                app.Frame([&] { EmitGroup(); });
            }
        };

        TEST_F(ReorderableTest, ItemsRenderInDeclaredOrder)
        {
            app.Frame([&] { EmitGroup(); });
            int prevY = -1;
            for (const auto id : itemIDs)
            {
                const auto r = ItemRect(id);
                EXPECT_GT(r.h, 0);
                EXPECT_GT(r.y, prevY) << "items stack top-to-bottom in declared order";
                prevY = r.y;
            }
        }

        TEST_F(ReorderableTest, DragPastNeighborReordersDown)
        {
            app.Frame([&] { EmitGroup(); });
            // Drag item 0 down past item 1's midpoint -> reorder (0 -> 1).
            const auto row1 = ItemRect(itemIDs[1]);
            DragItemTo(itemIDs[0], row1.y + row1.h * 3 / 4);

            ASSERT_EQ(reorderCalls, 1);
            EXPECT_EQ(lastReorder->first, 0);
            EXPECT_EQ(lastReorder->second, 1);
        }

        TEST_F(ReorderableTest, DragUpwardReordersToEarlierSlot)
        {
            app.Frame([&] { EmitGroup(); });
            // Drag item 2 up past item 0's midpoint -> reorder (2 -> 0).
            const auto row0 = ItemRect(itemIDs[0]);
            DragItemTo(itemIDs[2], row0.y + row0.h / 4);

            ASSERT_EQ(reorderCalls, 1);
            EXPECT_EQ(lastReorder->first, 2);
            EXPECT_EQ(lastReorder->second, 0);
        }

        TEST_F(ReorderableTest, DragBeyondEndClampsToLastSlot)
        {
            app.Frame([&] { EmitGroup(); });
            // Drag item 0 far below the last row -> clamps to the last slot (3).
            const auto last = ItemRect(itemIDs[3]);
            DragItemTo(itemIDs[0], last.y + last.h + 500);

            ASSERT_EQ(reorderCalls, 1);
            EXPECT_EQ(lastReorder->first, 0);
            EXPECT_EQ(lastReorder->second, 3) << "drag past the end lands in the last slot";
        }

        // The dragged item vanishes (its group stops being emitted) before the button is
        // released, so no handler ever sees the MouseUp. The drag must not stay latched.
        TEST_F(ReorderableTest, DragDoesNotStayStuckWhenTheReleaseIsNeverDelivered)
        {
            app.Frame([&] { EmitGroup(); });
            const int restingY = ItemRect(itemIDs[0]).y;

            const auto thumb = ThumbCenter(itemIDs[0]);
            app.InjectMouseMove(thumb);
            app.InjectMouseDown(thumb);
            app.Frame([&] { EmitGroup(); });
            app.InjectMouseMove({ thumb.x, thumb.y + 20 }, tachyon::MouseButtons::Left);
            app.Frame([&] { EmitGroup(); });
            app.Frame([&] { EmitGroup(); }); // the offset is applied on the frame after the move is dispatched
            ASSERT_NE(ItemRect(itemIDs[0]).y, restingY) << "precondition: the drag is displacing the item";

            app.Frame([&] { app.BeginRootWindow(); app.EndRootWindow(); }); // group gone mid-drag
            app.InjectMouseUp({ thumb.x, thumb.y + 20 });
            app.Frame([&] { app.BeginRootWindow(); app.EndRootWindow(); });
            app.Frame([&] { EmitGroup(); });

            EXPECT_EQ(ItemRect(itemIDs[0]).y, restingY) << "a finished drag must not keep displacing the item";
        }

        TEST_F(ReorderableTest, SubThresholdReleaseDoesNotReorder)
        {
            app.Frame([&] { EmitGroup(); });
            const auto thumb = ThumbCenter(itemIDs[1]);
            // Press and release within the 4px activation threshold.
            app.InjectMouseMove(thumb);
            app.InjectMouseDown(thumb);
            app.Frame([&] { EmitGroup(); });
            app.InjectMouseMove({ thumb.x + 2, thumb.y + 2 }, tachyon::MouseButtons::Left);
            app.Frame([&] { EmitGroup(); });
            app.InjectMouseUp({ thumb.x + 2, thumb.y + 2 });
            app.Frame([&] { EmitGroup(); });

            EXPECT_EQ(reorderCalls, 0) << "a click under the drag threshold must not reorder";
        }

        TEST_F(ReorderableTest, DropInOriginalSlotDoesNotReorder)
        {
            app.Frame([&] { EmitGroup(); });
            // Drag item 1 down a little but release still within its own row ->
            // no slot change, no callback.
            const auto row1 = ItemRect(itemIDs[1]);
            DragItemTo(itemIDs[1], row1.y + row1.h / 2 + 6);

            EXPECT_EQ(reorderCalls, 0)
                << "releasing in the source slot should not fire onReorder";
        }

        // ---- Wrapped (multi-line) groups, as used by tab bars ----

        class WrappedReorderableTest : public ReorderableTest
        {
        protected:
            // One row of 4 fixed-width items in a wrapping horizontal group.
            void EmitWrapped()
            {
                app.BeginRootWindow();
                auto onReorder = [this](int from, int to)
                {
                    lastReorder = { from, to };
                    reorderCalls++;
                };
                if (app.BeginReorderableGroup(groupID, ReorderDirection::Horizontal, /*isWrapped*/ true,
                                              onReorder, tachyon::LayoutOverride{}.Size(600, 200)))
                {
                    for (int i = 0; i < 4; i++)
                    {
                        app.BeginReorderableItem(itemIDs[i], tachyon::LayoutOverride{}.Size(100, 40));
                        app.EmitDragHandle();
                        app.EndReorderableItem();
                    }
                    app.EndReorderableGroup();
                }
                app.EndRootWindow();
            }

            void DragToX(tachyon::ID srcItem, int dstX)
            {
                const auto thumb = ThumbCenter(srcItem);
                app.InjectMouseMove(thumb);
                app.InjectMouseDown(thumb);
                app.Frame([&] { EmitWrapped(); });
                app.InjectMouseMove({ dstX, thumb.y }, tachyon::MouseButtons::Left);
                app.Frame([&] { EmitWrapped(); });
                app.InjectMouseUp({ dstX, thumb.y });
                app.Frame([&] { EmitWrapped(); });
            }

            int LeftQuarter(int i) const  { const auto r = ItemRect(itemIDs[i]); return r.x + r.w / 4; }
            int RightQuarter(int i) const { const auto r = ItemRect(itemIDs[i]); return r.x + 3 * r.w / 4; }
        };

        TEST_F(WrappedReorderableTest, DropOnOwnLeftHalfDoesNotReorder)
        {
            app.Frame([&] { EmitWrapped(); });
            const auto r = ItemRect(itemIDs[2]);
            ASSERT_EQ(r.y, ItemRect(itemIDs[0]).y) << "all items on one row";
            // The handle sits at the item's left edge; land well past the drag
            // threshold but still in the item's own left half.
            DragToX(itemIDs[2], r.x + r.w * 2 / 5);
            EXPECT_EQ(reorderCalls, 0);
        }

        TEST_F(WrappedReorderableTest, LeftwardDropLandsWhereTheCursorIs)
        {
            app.Frame([&] { EmitWrapped(); });
            DragToX(itemIDs[2], RightQuarter(0));
            ASSERT_TRUE(lastReorder.has_value());
            EXPECT_EQ(*lastReorder, std::make_pair(2, 1)) << "right half of item 0 = after item 0";

            lastReorder.reset();
            app.Frame([&] { EmitWrapped(); });
            DragToX(itemIDs[2], LeftQuarter(0));
            ASSERT_TRUE(lastReorder.has_value());
            EXPECT_EQ(*lastReorder, std::make_pair(2, 0));
        }

        TEST_F(WrappedReorderableTest, RightwardDropLandsWhereTheCursorIs)
        {
            app.Frame([&] { EmitWrapped(); });
            DragToX(itemIDs[0], RightQuarter(3));
            ASSERT_TRUE(lastReorder.has_value());
            EXPECT_EQ(*lastReorder, std::make_pair(0, 3));

            lastReorder.reset();
            app.Frame([&] { EmitWrapped(); });
            DragToX(itemIDs[0], LeftQuarter(2));
            ASSERT_TRUE(lastReorder.has_value());
            EXPECT_EQ(*lastReorder, std::make_pair(0, 1)) << "left half of item 2 = before item 2";
        }

    } // namespace
}
