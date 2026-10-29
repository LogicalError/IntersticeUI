// PointerDrag, the press-then-drag state every control's drag is built on, and the shared
// click-or-drag and double-click rules.

#include <gtest/gtest.h>

#include <gui_core/elements/gui_element.h>
#include <gui_core/elements/gui_element_storage.h>
#include <gui_core/input/gui_input_manager.h>
#include <gui_core/input/gui_pointer_drag.h>
#include <gui_core/state/gui_drag_drop_state.h>
#include <gui_core/state/gui_state_manager.h>
#include <gui_core/style/gui_gradient_manager.h>
#include <gui_core/style/gui_style_manager.h>

#include <gui_controls/gui_widget_state_types.h>

#include <thread>

namespace tachyon
{
    namespace
    {
        InputState Pointer(int2 pos, MouseButtons buttons, MouseButtons prevButtons)
        {
            InputState s{};
            s.screenPos   = pos;
            s.buttons     = buttons;
            s.prevButtons = prevButtons;
            return s;
        }

        class PointerDragTest : public ::testing::Test
        {
        protected:
            GradientManager gradientManager{ 16 };
            StyleManager    styleManager{ gradientManager };
            ElementStorage  storage{ styleManager };
            ID              a = ID{ .value{ .integer = 11 } };
            ID              b = ID{ .value{ .integer = 12 } };

            // No rect or element: the cursor is in screen space.
            ElementEvent Event(ID id, EventType type, const InputState& s)
            {
                return ElementEvent{ .id = id, .type = type, .state = s, .rect = nullptr, .elementRef = ElementRef::Invalid() };
            }
            bool Press(PointerDrag& d, ID id, int2 pos, MouseButtons button = MouseButtons::Left, bool atOnce = false)
            {
                const InputState s = Pointer(pos, button, MouseButtons::None);
                return d.Press(Event(id, EventType::MouseDown, s), storage, button, atOnce);
            }
            bool Move(PointerDrag& d, ID id, int2 pos, int32_t distance = 4, MouseButtons held = MouseButtons::Left)
            {
                const InputState s = Pointer(pos, held, held);
                return d.Move(Event(id, EventType::MouseMove, s), distance);
            }
            PointerDrag::Ended Release(PointerDrag& d, ID id, int2 pos, MouseButtons button = MouseButtons::Left)
            {
                const InputState s = Pointer(pos, MouseButtons::None, button);
                return d.Release(Event(id, EventType::MouseUp, s));
            }
        };

        TEST(DragTravelTest, ADragIsTheDistanceInAStraightLine)
        {
            EXPECT_FALSE(IsDragTravel({ 0, 0 }, 4));
            EXPECT_FALSE(IsDragTravel({ 3, 0 }, 4));
            EXPECT_TRUE (IsDragTravel({ 4, 0 }, 4));
            EXPECT_TRUE (IsDragTravel({ 0, -4 }, 4));
            EXPECT_TRUE (IsDragTravel({ 3, 3 }, 4)) << "4.24 pixels diagonally";
            EXPECT_FALSE(IsDragTravel({ 2, 3 }, 4)) << "3.6 pixels diagonally";
            EXPECT_TRUE (IsDragTravel({ 0, 0 }, 0)) << "no distance: a drag at once";
            EXPECT_TRUE (IsDragTravel({ 40000, 40000 }, 50000)) << "no overflow";
        }

        TEST(DragTravelTest, OnlyEscapeGoingDownCancels)
        {
            InputState s{};
            s.lastKey = KeyCode::Escape;
            s.lastKeyAction = KeyAction::KeyDown;
            EXPECT_TRUE(IsDragCancelKey(s));
            s.lastKeyAction = KeyAction::KeyUp;
            EXPECT_FALSE(IsDragCancelKey(s));
            s.lastKey = KeyCode::Return;
            s.lastKeyAction = KeyAction::KeyDown;
            EXPECT_FALSE(IsDragCancelKey(s));
        }

        TEST_F(PointerDragTest, APressReleasedUnderTheDistanceIsAClick)
        {
            PointerDrag d;
            ASSERT_TRUE(Press(d, a, { 100, 100 }));
            EXPECT_TRUE(d.IsPressed(a));
            EXPECT_FALSE(d.IsPressed(b));
            EXPECT_FALSE(Move(d, a, { 102, 102 }));
            EXPECT_FALSE(d.IsDragging(a));
            EXPECT_EQ(Release(d, a, { 102, 102 }), PointerDrag::Ended::Click);
            EXPECT_FALSE(d.IsHeld());
        }

        TEST_F(PointerDragTest, PastTheDistanceItIsADragUntilReleased)
        {
            PointerDrag d;
            ASSERT_TRUE(Press(d, a, { 100, 100 }));
            EXPECT_TRUE(Move(d, a, { 104, 100 })) << "the move that reaches the distance is a drag move";
            EXPECT_TRUE(d.IsDragging(a));
            EXPECT_TRUE(Move(d, a, { 100, 100 })) << "back at the press it stays a drag";
            EXPECT_EQ(d.ScreenDelta(Pointer({ 90, 130 }, MouseButtons::Left, MouseButtons::Left)), (int2{ -10, 30 }));
            EXPECT_EQ(Release(d, a, { 100, 100 }), PointerDrag::Ended::Drag);
            EXPECT_FALSE(d.IsHeld());
        }

        TEST_F(PointerDragTest, APressAtOnceIsADragFromTheStart)
        {
            PointerDrag d;
            ASSERT_TRUE(Press(d, a, { 10, 10 }, MouseButtons::Left, /*atOnce*/ true));
            EXPECT_TRUE(d.IsDragging(a));
            EXPECT_TRUE(Move(d, a, { 10, 10 }));
            EXPECT_EQ(Release(d, a, { 10, 10 }), PointerDrag::Ended::Drag);
        }

        TEST_F(PointerDragTest, OnlyItsButtonAndElementCount)
        {
            PointerDrag left;
            const InputState rightDown = Pointer({ 0, 0 }, MouseButtons::Right, MouseButtons::None);
            EXPECT_FALSE(left.Press(Event(a, EventType::MouseDown, rightDown), storage, MouseButtons::Left));
            EXPECT_FALSE(left.IsHeld());

            const InputState held = Pointer({ 0, 0 }, MouseButtons::Left, MouseButtons::Left);
            EXPECT_FALSE(left.Press(Event(a, EventType::MouseDown, held), storage)) << "a button already down isn't a press";

            ASSERT_TRUE(Press(left, a, { 0, 0 }));
            EXPECT_FALSE(Move(left, b, { 50, 0 })) << "another element's move";
            EXPECT_FALSE(left.dragging);
            EXPECT_FALSE(Move(left, a, { 50, 0 }, 4, MouseButtons::Right)) << "a move without its button";
            EXPECT_EQ(Release(left, b, { 0, 0 }), PointerDrag::Ended::No);
            const InputState rightUp = Pointer({ 0, 0 }, MouseButtons::Left, MouseButtons::Left | MouseButtons::Right);
            EXPECT_EQ(left.Release(Event(a, EventType::MouseUp, rightUp)), PointerDrag::Ended::No) << "another button's release, its own still held";
            EXPECT_TRUE(left.IsPressed(a));
        }

        TEST_F(PointerDragTest, ARightPressDragsWithTheRightButton)
        {
            PointerDrag d;
            ASSERT_TRUE(Press(d, a, { 0, 0 }, MouseButtons::Right));
            EXPECT_TRUE(Move(d, a, { 0, 9 }, 4, MouseButtons::Right));
            EXPECT_EQ(Release(d, a, { 0, 9 }, MouseButtons::Right), PointerDrag::Ended::Drag);
        }

        TEST_F(PointerDragTest, CancelDropsOnlyItsElementsPress)
        {
            PointerDrag d;
            ASSERT_TRUE(Press(d, a, { 0, 0 }));
            EXPECT_FALSE(d.Cancel(b));
            EXPECT_TRUE(d.IsPressed(a));
            EXPECT_TRUE(d.Cancel(a));
            EXPECT_FALSE(d.IsHeld());
            EXPECT_FALSE(Move(d, a, { 40, 0 })) << "the moves after a cancel are ignored";
            EXPECT_EQ(Release(d, a, { 40, 0 }), PointerDrag::Ended::No) << "and so is the release";
            EXPECT_FALSE(d.Cancel(a));
        }

        TEST_F(PointerDragTest, WithoutAnElementTheLocalDeltaIsTheScreenDelta)
        {
            PointerDrag d;
            ASSERT_TRUE(Press(d, a, { 30, 40 }));
            EXPECT_EQ(d.pressLocal, (int2{ 30, 40 }));
            const InputState s = Pointer({ 45, 20 }, MouseButtons::Left, MouseButtons::Left);
            EXPECT_EQ(d.LocalDelta(Event(a, EventType::MouseMove, s), storage), (int2{ 15, -20 }));
        }

        TEST(DragAbandonedTest, ADragIsLeftBehindOnlyWithItsButtonUpAndNoReleaseQueued)
        {
            InputManager    input;
            EXPECT_TRUE(input.IsDragAbandoned()) << "nothing held";

            // The buttons passed are the ones held after the event.
            input.OnMouseButton(ID::Invalid(), { 0, 0 }, MouseButtons::Left, KeyModifier::None, MouseAction::MouseDown);
            EXPECT_FALSE(input.IsDragAbandoned()) << "the button is down";
            EXPECT_TRUE(input.IsDragAbandoned(MouseButtons::Right)) << "another button";

            input.OnMouseButton(ID::Invalid(), { 0, 0 }, MouseButtons::None, KeyModifier::None, MouseAction::MouseUp);
            EXPECT_FALSE(input.IsDragAbandoned()) << "its release is queued for this frame";
        }

        TEST(DoubleClickTest, TheSecondClickOnTheSameItemInTimeIsADoubleClick)
        {
            const float before = DoubleClickMilliseconds();
            SetDoubleClickMilliseconds(200.0f);
            EXPECT_EQ(DoubleClickMilliseconds(), 200.0f);
            SetDoubleClickMilliseconds(0.0f);
            EXPECT_EQ(DoubleClickMilliseconds(), 200.0f) << "a platform without a time keeps the last one";

            DoubleClickTracker clicks;
            const ID a{ .value{ .integer = 1 } }, b{ .value{ .integer = 2 } };
            EXPECT_FALSE(clicks.IsDoubleClick(a));
            EXPECT_TRUE(clicks.IsDoubleClick(a));
            EXPECT_FALSE(clicks.IsDoubleClick(a)) << "a third click starts over";
            EXPECT_FALSE(clicks.IsDoubleClick(b)) << "another item";
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            EXPECT_FALSE(clicks.IsDoubleClick(b)) << "too late";

            SetDoubleClickMilliseconds(before);
        }
        // A drag and drop starts by the same rule as every other drag: a diagonal 3, 3 is
        // 4.2 pixels, past the 4.
        TEST(DragTravelTest, ADragAndDropStartsByTheSameRule)
        {
            const ID source{ .value{ .integer = 21 } };
            auto held = [](int2 pos) { InputState s{}; s.screenPos = pos; s.buttons = s.prevButtons = MouseButtons::Left; return s; };

            DragDropState drag;
            drag.SetStartDistance(4);
            drag.Begin(source, 1, 0, { 100, 100 });
            drag.UpdateFromInput(held({ 102, 103 }));
            EXPECT_FALSE(drag.IsDragging()) << "3.6 pixels";
            drag.UpdateFromInput(held({ 103, 103 }));
            EXPECT_TRUE(drag.IsDragging());
            EXPECT_EQ(drag.GetDraggedID(), source);
        }
    }
}
