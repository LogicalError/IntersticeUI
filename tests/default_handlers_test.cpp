// Direct unit tests for handler factories in gui_default_handlers.h that
// aren't otherwise covered:
//   - MakeButtonHandler   (button click + Space/Enter activation)
//   - GUI::Chain          (composing handlers)

#include <gtest/gtest.h>

#include <gui_core/gui_id.h>
#include <gui_core/gui_id_manager.h>
#include <gui_core/elements/gui_element.h>
#include <gui_core/state/gui_state_manager.h>
#include <gui_core/input/gui_input_state.h>
#include <gui_core/input/gui_input_enums.h>
#include <gui_controls/gui_default_handlers.h>

namespace GUI
{
    namespace
    {
        ElementEvent MakeEvent(ID id, EventType type, InputState& s)
        {
            return ElementEvent{ id, type, s, nullptr, ElementRef::Invalid() };
        }

        // ===================================================================
        // MakeButtonHandler
        // ===================================================================

        TEST(ButtonHandler, FiresOnClickOnLeftMouseDown)
        {
            GUIStateManager sm{};
            int n = 0;
            auto handler = MakeButtonHandler(sm, [&] { n++; });

            ID id{}; id.value.dynamicParts.hashValue = 1;
            InputState s{};
            s.buttons     = MouseButtons::Left;
            s.prevButtons = MouseButtons::None;
            EXPECT_TRUE(handler(MakeEvent(id, EventType::MouseDown, s)));
            EXPECT_EQ(n, 1);
        }

        TEST(ButtonHandler, IgnoresRightMouseDown)
        {
            GUIStateManager sm{};
            int n = 0;
            auto handler = MakeButtonHandler(sm, [&] { n++; });
            ID id{}; id.value.dynamicParts.hashValue = 2;
            InputState s{};
            s.buttons = MouseButtons::Right;
            EXPECT_FALSE(handler(MakeEvent(id, EventType::MouseDown, s)));
            EXPECT_EQ(n, 0);
        }

        TEST(ButtonHandler, IgnoresMouseUpAndMove)
        {
            GUIStateManager sm{};
            int n = 0;
            auto handler = MakeButtonHandler(sm, [&] { n++; });
            ID id{}; id.value.dynamicParts.hashValue = 3;
            InputState s{};
            s.buttons     = MouseButtons::None;
            s.prevButtons = MouseButtons::Left;
            EXPECT_FALSE(handler(MakeEvent(id, EventType::MouseUp, s)));
            EXPECT_FALSE(handler(MakeEvent(id, EventType::MouseMove, s)));
            EXPECT_EQ(n, 0);
        }

        TEST(ButtonHandler, MouseDownSetsFocusAndClickIDs)
        {
            GUIStateManager sm{};
            ID id{}; id.value.dynamicParts.hashValue = 4;
            auto handler = MakeButtonHandler(sm, []{});

            InputState s{};
            s.buttons = MouseButtons::Left; s.prevButtons = MouseButtons::None;
            handler(MakeEvent(id, EventType::MouseDown, s));

            EXPECT_EQ(sm.Interaction().GetFocusedID(), id);
            EXPECT_TRUE(sm.Interaction().HasBeenClicked(id));
        }

        TEST(ButtonHandler, NullCallbackIsHarmlessOnClick)
        {
            GUIStateManager sm{};
            auto handler = MakeButtonHandler(sm, nullptr);
            ID id{}; id.value.dynamicParts.hashValue = 5;
            InputState s{}; s.buttons = MouseButtons::Left; s.prevButtons = MouseButtons::None;
            EXPECT_NO_THROW(handler(MakeEvent(id, EventType::MouseDown, s)));
        }

        // ===================================================================
        // GUI::Chain — composing two handlers.
        // ===================================================================

        TEST(ChainHandler, FirstHandlerShortCircuits)
        {
            int aCalls = 0, bCalls = 0;
            auto a = [&](ElementEvent&&) -> bool { aCalls++; return true; };
            auto b = [&](ElementEvent&&) -> bool { bCalls++; return true; };
            auto chained = Chain(a, b);

            ID id{}; id.value.dynamicParts.hashValue = 1;
            InputState s{};
            EXPECT_TRUE(chained(MakeEvent(id, EventType::MouseDown, s)));
            EXPECT_EQ(aCalls, 1);
            EXPECT_EQ(bCalls, 0);
        }

        TEST(ChainHandler, FallsThroughWhenFirstReturnsFalse)
        {
            int aCalls = 0, bCalls = 0;
            auto a = [&](ElementEvent&&) -> bool { aCalls++; return false; };
            auto b = [&](ElementEvent&&) -> bool { bCalls++; return true; };
            auto chained = Chain(a, b);

            ID id{}; id.value.dynamicParts.hashValue = 1;
            InputState s{};
            EXPECT_TRUE(chained(MakeEvent(id, EventType::MouseDown, s)));
            EXPECT_EQ(aCalls, 1);
            EXPECT_EQ(bCalls, 1);
        }

    } // namespace
} // namespace GUI
