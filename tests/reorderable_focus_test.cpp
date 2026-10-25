// Focus behaviour of reorderable items' drag thumbs, through the headless frame loop:
//   - Clicking a thumb (or its row) must not strip focus from the focused control.
//   - On mouse-up (after a reorder, a drag back to origin, or a plain click), focus
//     moves to the first IsControl element of the row under the cursor; with no
//     IsControl child, focus is left alone.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_core/elements/gui_element_storage.h>

namespace interstice
{
    using GUITest::HeadlessGUIApp;

    namespace
    {

    class ReorderableFocusTest : public ::testing::Test
    {
    protected:
        HeadlessGUIApp app;

        // Stable across frames so events can target the same elements.
        interstice::ID groupID{};
        interstice::ID itemID0{};
        interstice::ID itemID1{};
        interstice::ID itemID2{};
        interstice::ID buttonID0{};
        interstice::ID buttonID1{};
        interstice::ID buttonID2{};

        // Focused control outside the reorderable; thumb clicks must not steal its focus.
        interstice::ID externalControlID{};

        std::vector<int> reorderHistory; // appended (from, to) on every onReorder.

        void SetUp() override
        {
            app.Initialize();
            groupID           = app.idManager.CreateStaticID();
            itemID0           = app.idManager.CreateStaticID();
            itemID1           = app.idManager.CreateStaticID();
            itemID2           = app.idManager.CreateStaticID();
            buttonID0         = app.idManager.CreateStaticID();
            buttonID1         = app.idManager.CreateStaticID();
            buttonID2         = app.idManager.CreateStaticID();
            externalControlID = app.idManager.CreateStaticID();
        }
        void TearDown() override { app.Cleanup(); }

        // Three rows, each [drag-thumb, Button(IsControl)]. Vertical group so
        // a drag down past row 1's midpoint reorders row 0 -> row 1.
        void EmitGroup()
        {
            app.BeginRootWindow();

            // Out-of-group control we focus to test "don't steal focus".
            app.Button(externalControlID, "Outside", []{},
                       interstice::LayoutOverride{}.Size(120, 24));

            auto onReorder = [this](int from, int to)
            {
                reorderHistory.push_back(from);
                reorderHistory.push_back(to);
            };
            if (app.BeginReorderableGroup(groupID, ReorderDirection::Vertical,
                                          onReorder,
                                          interstice::LayoutOverride{}.Size(300, 200)))
            {
                const interstice::ID itemIDs[3]   = { itemID0, itemID1, itemID2 };
                const interstice::ID buttonIDs[3] = { buttonID0, buttonID1, buttonID2 };
                for (int i = 0; i < 3; i++)
                {
                    app.BeginReorderableItem(itemIDs[i],
                                             interstice::LayoutOverride{}.Size(280, 40));
                    app.EmitDragHandle();
                    app.Button(buttonIDs[i], "Row", []{},
                               interstice::LayoutOverride{}.FlexGrow(1));
                    app.EndReorderableItem();
                }
                app.EndReorderableGroup();
            }
            app.EndRootWindow();
        }

        // The drag thumb is the reorderable item's first child (EmitDragHandle
        // adds it before the row content).
        interstice::int2 ThumbCenterOfItem(interstice::ID itemID) const
        {
            const auto itemRef = app.elementStorage.FindElementRefByID(itemID);
            if (!itemRef.IsValid()) return {};
            const auto children = app.elementStorage.GetChildRange(itemRef);
            if (children.Empty()) return {};
            const interstice::ElementRef thumbRef{ itemRef.depth + 1, children.begin };
            const auto* thumbRect = app.elementStorage.GetElementRectPtr(thumbRef);
            if (!thumbRect) return {};
            const auto& r = thumbRect->rect;
            return { r.x + r.w / 2, r.y + r.h / 2 };
        }

        void FocusExternalControl()
        {
            app.guiStateManager.Interaction().SetFocusID(externalControlID);
            app.guiStateManager.TextEditorFocus().Set(externalControlID);
        }
    };

    // -------------------------------------------------------------------------
    // 1. MouseDown on a thumb must NOT defocus the externally-focused control:
    //    only clicks on another IsControl (or empty space) defocus.
    // -------------------------------------------------------------------------
    TEST_F(ReorderableFocusTest, MouseDownOnThumbPreservesExternalFocus)
    {
        // Frame 1: lay out the tree so we can find the thumb's rect.
        app.Frame([&] { EmitGroup(); });
        const auto thumbCenter = ThumbCenterOfItem(itemID0);
        ASSERT_NE(thumbCenter, interstice::int2{});

        // Park focus on the external control AFTER layout so the next
        // dispatched MouseDown is the thing under test.
        FocusExternalControl();
        ASSERT_EQ(app.guiStateManager.Interaction().GetFocusedID(), externalControlID);

        // Frame 2: cursor moves over the thumb, then MouseDown is dispatched.
        app.InjectMouseMove(thumbCenter);
        app.InjectMouseDown(thumbCenter);
        app.Frame([&] { EmitGroup(); });

        // Only Interaction focus is checked: the stub text system is never a text
        // editor, so TextEditorFocus isn't synced from it.
        EXPECT_EQ(app.guiStateManager.Interaction().GetFocusedID(), externalControlID)
            << "clicking the drag thumb stripped focus from the external control";
    }

    // -------------------------------------------------------------------------
    // 2. MouseUp after a click-without-drag must focus the row's first control.
    // -------------------------------------------------------------------------
    TEST_F(ReorderableFocusTest, ClickOnThumbWithoutDragFocusesFirstControl)
    {
        app.Frame([&] { EmitGroup(); });
        const auto thumbCenter = ThumbCenterOfItem(itemID1);
        ASSERT_NE(thumbCenter, interstice::int2{});

        FocusExternalControl();

        // Press, then release on the same spot - under the drag-activation
        // threshold, so the reorderable handler treats this as a click.
        app.InjectMouseMove(thumbCenter);
        app.InjectMouseDown(thumbCenter);
        app.Frame([&] { EmitGroup(); });

        app.InjectMouseUp(thumbCenter);
        app.Frame([&] { EmitGroup(); });

        EXPECT_EQ(app.guiStateManager.Interaction().GetFocusedID(), buttonID1)
            << "click on row 1's thumb should land focus on row 1's button";
        // No reorder fired.
        EXPECT_TRUE(reorderHistory.empty());
    }

    // -------------------------------------------------------------------------
    // 3. MouseUp after a drag that DID reorder must focus the first control of
    //    the row at the destination slot. Dynamic IDs are slot-stable, so the
    //    button ID we assert on (buttonID1) is the one whose slot now contains
    //    the moved data.
    // -------------------------------------------------------------------------
    TEST_F(ReorderableFocusTest, DragWithReorderFocusesFirstControlOfDestinationSlot)
    {
        app.Frame([&] { EmitGroup(); });
        const auto thumbCenter0 = ThumbCenterOfItem(itemID0);
        const auto itemRef1     = app.elementStorage.FindElementRefByID(itemID1);
        ASSERT_NE(thumbCenter0, interstice::int2{});
        ASSERT_TRUE(itemRef1.IsValid());

        // Compute a destination point comfortably past row 1's midpoint so the
        // reorderable's edge-crossing logic moves item 0 into slot 1.
        const auto* row1Rect = app.elementStorage.GetElementRectPtr(itemRef1);
        ASSERT_NE(row1Rect, nullptr);
        const interstice::int2 dropPos{ thumbCenter0.x,
                                 row1Rect->rect.y + row1Rect->rect.h * 3 / 4 };

        FocusExternalControl();

        // Press on row 0's thumb.
        app.InjectMouseMove(thumbCenter0);
        app.InjectMouseDown(thumbCenter0);
        app.Frame([&] { EmitGroup(); });

        // Move past the activation threshold AND past row 1's midpoint to
        // commit the reorder.
        app.InjectMouseMove(dropPos, interstice::MouseButtons::Left);
        app.Frame([&] { EmitGroup(); });

        // Release - fires onReorder and focuses the destination slot's button.
        app.InjectMouseUp(dropPos);
        app.Frame([&] { EmitGroup(); });

        ASSERT_EQ(reorderHistory.size(), 2u) << "expected exactly one onReorder call";
        EXPECT_EQ(reorderHistory[0], 0);
        EXPECT_EQ(reorderHistory[1], 1);

        EXPECT_EQ(app.guiStateManager.Interaction().GetFocusedID(), buttonID1)
            << "after dragging row 0 → slot 1, focus should be on the button at slot 1";
    }

    // -------------------------------------------------------------------------
    // 4. While a row of a plain list is dragged, the group draws the slot it will land
    //    in and the row draws itself lifted; neither draws anything otherwise.
    // -------------------------------------------------------------------------
    TEST_F(ReorderableFocusTest, ADraggedRowShowsItsDropSlot)
    {
        app.Frame([&] { EmitGroup(); });
        const auto thumbCenter0 = ThumbCenterOfItem(itemID0);
        const auto* row1Rect = app.elementStorage.GetElementRectPtr(app.elementStorage.FindElementRefByID(itemID1));
        ASSERT_NE(row1Rect, nullptr);
        auto drawsOf = [&](interstice::ID id) { return app.elementStorage.GetCustomDraw(app.elementStorage.FindElementRefByID(id)) != nullptr; };
        EXPECT_FALSE(drawsOf(groupID));
        EXPECT_FALSE(drawsOf(itemID0));

        app.InjectMouseMove(thumbCenter0);
        app.InjectMouseDown(thumbCenter0);
        app.Frame([&] { EmitGroup(); });
        app.InjectMouseMove({ thumbCenter0.x, row1Rect->rect.y + row1Rect->rect.h * 3 / 4 }, interstice::MouseButtons::Left);
        app.Frame([&] { EmitGroup(); });
        app.Frame([&] { EmitGroup(); });   // the drag shows from the frame after it starts

        EXPECT_TRUE(drawsOf(groupID)) << "the drop slot";
        EXPECT_TRUE(drawsOf(itemID0)) << "the lifted row";

        app.InjectMouseUp(thumbCenter0);
        app.Frame([&] { EmitGroup(); });
        app.Frame([&] { EmitGroup(); });
        EXPECT_FALSE(drawsOf(groupID));
    }

    } // namespace
} // namespace interstice
