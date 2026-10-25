// Unit tests for MakeDragSourceHandler / MakeDropTargetHandler, driven with
// synthetic events.

#include <gtest/gtest.h>

#include <gui_core/gui_id.h>
#include <gui_core/gui_id_manager.h>
#include <gui_core/elements/gui_element.h>
#include <gui_core/state/gui_state_manager.h>
#include <gui_core/input/gui_input_state.h>
#include <gui_core/input/gui_input_enums.h>
#include <gui_controls/gui_default_handlers.h>

#include <any>

namespace interstice
{
    namespace
    {
        ElementEvent MakeEvent(ID id, EventType type, InputState& s)
        {
            return ElementEvent{ id, type, s, nullptr, ElementRef::Invalid() };
        }

        // ===================================================================
        // Drop target - payload type filter, callback routing.
        // ===================================================================

        TEST(DropTargetHandler, AcceptsMatchingPayloadType)
        {
            uintptr_t receivedValue = 0;
            ID receivedSource{};
            auto handler = MakeDropTargetHandler(/*acceptedType*/ 42,
                [&](uintptr_t v, ID src) { receivedValue = v; receivedSource = src; });

            ID dst{}; dst.value.dynamicParts.hashValue = 1;
            ID src{}; src.value.dynamicParts.hashValue = 2;
            InputState s{};
            s.dragPayloadType  = 42;
            s.dragPayloadValue = 0xDEADBEEFu;
            s.dragSourceID     = src;
            EXPECT_TRUE(handler(MakeEvent(dst, EventType::Drop, s)));
            EXPECT_EQ(receivedValue, 0xDEADBEEFu);
            EXPECT_EQ(receivedSource, src);
        }

        TEST(DropTargetHandler, RejectsMismatchedPayloadType)
        {
            int callCount = 0;
            auto handler = MakeDropTargetHandler(/*acceptedType*/ 1,
                [&](uintptr_t, ID) { callCount++; });

            ID dst{}; dst.value.dynamicParts.hashValue = 3;
            InputState s{};
            s.dragPayloadType  = 999; // wrong
            s.dragPayloadValue = 1;
            EXPECT_FALSE(handler(MakeEvent(dst, EventType::Drop, s)));
            EXPECT_EQ(callCount, 0);
        }

        TEST(DropTargetHandler, DragEnterReturnsTrueIfPayloadMatches)
        {
            auto handler = MakeDropTargetHandler(7, nullptr);
            ID dst{}; dst.value.dynamicParts.hashValue = 1;

            InputState ok{}; ok.dragPayloadType = 7;
            EXPECT_TRUE(handler(MakeEvent(dst, EventType::DragEnter, ok)));
            EXPECT_TRUE(handler(MakeEvent(dst, EventType::DragOver, ok)));
            EXPECT_TRUE(handler(MakeEvent(dst, EventType::DragOut, ok)));

            InputState mismatched{}; mismatched.dragPayloadType = 99;
            EXPECT_FALSE(handler(MakeEvent(dst, EventType::DragEnter, mismatched)));
            EXPECT_FALSE(handler(MakeEvent(dst, EventType::DragOver, mismatched)));
            EXPECT_FALSE(handler(MakeEvent(dst, EventType::DragOut, mismatched)));
        }

        TEST(DropTargetHandler, NullCallbackOnDropIsHarmless)
        {
            auto handler = MakeDropTargetHandler(1, nullptr);
            ID dst{}; dst.value.dynamicParts.hashValue = 1;
            InputState s{}; s.dragPayloadType = 1; s.dragPayloadValue = 0;
            EXPECT_TRUE(handler(MakeEvent(dst, EventType::Drop, s)));
        }

        TEST(DropTargetHandler, IgnoresNonDragEvents)
        {
            auto handler = MakeDropTargetHandler(1, nullptr);
            ID dst{}; dst.value.dynamicParts.hashValue = 1;
            InputState s{}; s.dragPayloadType = 1;
            EXPECT_FALSE(handler(MakeEvent(dst, EventType::MouseDown, s)));
            EXPECT_FALSE(handler(MakeEvent(dst, EventType::MouseMove, s)));
            EXPECT_FALSE(handler(MakeEvent(dst, EventType::KeyDown,  s)));
        }

        // ===================================================================
        // Drag source - primes a drag in GUIStateManager.Drag() on MouseDown
        // and reacts to Left-button MouseMove for drag-update.
        // ===================================================================

        TEST(DragSourceHandler, MouseDownPrimesDrag_PromotedAfterThresholdMove)
        {
            // MouseDown makes the drag pending; m_draggedID stays Invalid until a
            // Left-held move passes the threshold.
            GUIStateManager sm{};
            int payloadFnCalls = 0;
            auto handler = MakeDragSourceHandler(sm, /*payloadType*/ 11,
                [&]() -> uintptr_t { payloadFnCalls++; return 0xABCDu; },
                [&]() -> std::any { return std::any{ std::string("payload") }; },
                /*sourceVisualizer*/ nullptr);

            ID src{}; src.value.dynamicParts.hashValue = 5;
            InputState down{};
            down.buttons     = MouseButtons::Left;
            down.prevButtons = MouseButtons::None;
            down.screenPos   = { 10, 20 };
            EXPECT_TRUE(handler(MakeEvent(src, EventType::MouseDown, down)));
            EXPECT_EQ(payloadFnCalls, 1);
            // Still pending - the drag has not been promoted yet.
            EXPECT_EQ(sm.Drag().GetDraggedID(), ID::Invalid());
            EXPECT_FALSE(sm.Drag().IsDragging());

            // Move past the 4 px threshold.
            InputState move{};
            move.buttons   = MouseButtons::Left;
            move.screenPos = { 30, 20 }; // 20 px right, well past 4 px threshold
            EXPECT_TRUE(handler(MakeEvent(src, EventType::MouseMove, move)));
            EXPECT_EQ(sm.Drag().GetDraggedID(), src);
            EXPECT_TRUE(sm.Drag().IsDragging());
        }

        TEST(DragSourceHandler, ReleasingButtonBeforeThresholdCancelsDrag)
        {
            GUIStateManager sm{};
            auto handler = MakeDragSourceHandler(sm, 1,
                []() -> uintptr_t { return 0; }, []() { return std::any{}; }, nullptr);
            ID src{}; src.value.dynamicParts.hashValue = 9;

            InputState down{};
            down.buttons     = MouseButtons::Left;
            down.prevButtons = MouseButtons::None;
            handler(MakeEvent(src, EventType::MouseDown, down));

            // Move with NO button held - UpdateFromInput should cancel.
            InputState move{};
            move.buttons = MouseButtons::None;
            handler(MakeEvent(src, EventType::MouseMove, move));
            EXPECT_FALSE(sm.Drag().IsDragging());
        }

        TEST(DragSourceHandler, RightMouseDoesNotPrimeDrag)
        {
            GUIStateManager sm{};
            auto handler = MakeDragSourceHandler(sm, 1,
                []() -> uintptr_t { return 0; },
                []() { return std::any{}; },
                nullptr);

            ID src{}; src.value.dynamicParts.hashValue = 6;
            InputState s{};
            s.buttons = MouseButtons::Right;
            EXPECT_FALSE(handler(MakeEvent(src, EventType::MouseDown, s)));
            EXPECT_EQ(sm.Drag().GetDraggedID(), ID::Invalid());
        }

        TEST(DragSourceHandler, MouseMoveWithoutLeftDoesNotUpdateDrag)
        {
            GUIStateManager sm{};
            auto handler = MakeDragSourceHandler(sm, 1,
                []() -> uintptr_t { return 0; }, []() { return std::any{}; }, nullptr);

            ID src{}; src.value.dynamicParts.hashValue = 7;
            InputState s{};
            s.buttons = MouseButtons::None;
            EXPECT_FALSE(handler(MakeEvent(src, EventType::MouseMove, s)));
        }
    }
} // namespace interstice
