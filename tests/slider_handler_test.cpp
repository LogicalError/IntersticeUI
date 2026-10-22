#include <gtest/gtest.h>

#include <gui_core/gui_base_types.h>
#include <gui_core/gui_id.h>

#include <gui_core/elements/gui_element.h>
#include <gui_core/elements/gui_element_storage.h>

#include <gui_core/input/gui_input_state.h>
#include <gui_core/input/gui_input_enums.h>

#include <gui_core/state/gui_state_manager.h>

#include <gui_core/style/gui_gradient_manager.h>
#include <gui_core/style/gui_style_manager.h>

#include <gui_controls/gui_default_handlers.h>

namespace GUI
{

    namespace
    {
        ElementEvent MakeEvent(ID id, EventType type, InputState& state)
        {
            return ElementEvent{
                /*id*/         id,
                /*type*/       type,
                /*state*/      state,
                /*rect*/       nullptr,
                /*elementRef*/ ElementRef::Invalid(),
            };
        }

        InputState MakeMouseDownState(int x, int y)
        {
            InputState s{};
            s.screenPos = { x, y };
            s.buttons = MouseButtons::Left;
            s.prevButtons = MouseButtons::None;
            return s;
        }
        InputState MakeMouseMoveState(int x, int y)
        {
            InputState s{};
            s.screenPos = { x, y };
            s.buttons = MouseButtons::Left;
            s.prevButtons = MouseButtons::Left;
            return s;
        }
        InputState MakeMouseUpState(int x, int y)
        {
            InputState s{};
            s.screenPos = { x, y };
            s.buttons = MouseButtons::None;
            s.prevButtons = MouseButtons::Left;
            return s;
        }
        InputState MakeKeyState(KeyCode key)
        {
            InputState s{};
            s.lastKey = key;
            s.lastKeyAction = KeyAction::KeyDown;
            return s;
        }
    }

    class SliderHandlerTest : public ::testing::Test
    {
    protected:
        GUIStateManager guiStateManager;
        GradientManager gradientManager{256};
        StyleManager styleManager{gradientManager};
        ElementStorage elementStorage{styleManager};
        ID sliderID{};

        void SetUp() override
        {
            sliderID.value.dynamicParts.mode = IDMode::Dynamic;
            sliderID.value.dynamicParts.hashValue = 1;
        }
    };

    // =============================================================================
    // Default [0, 1] range
    // =============================================================================

    TEST_F(SliderHandlerTest, DefaultRange_MouseDragInterpolatesZeroToOne)
    {
        float v = 0.0f;
        auto handler = MakeSliderHandler(guiStateManager, elementStorage, &v, /*elementWidth*/100);

        InputState down = MakeMouseDownState(0, 0);
        handler(MakeEvent(sliderID, EventType::MouseDown, down));

        // +50 px on a 100-wide element = +0.5 across a [0,1] range.
        InputState move = MakeMouseMoveState(50, 0);
        handler(MakeEvent(sliderID, EventType::MouseMove, move));
        EXPECT_FLOAT_EQ(v, 0.5f);

        // Drag past the max clamps to 1.0.
        InputState moveBeyond = MakeMouseMoveState(500, 0);
        handler(MakeEvent(sliderID, EventType::MouseMove, moveBeyond));
        EXPECT_FLOAT_EQ(v, 1.0f);

        InputState up = MakeMouseUpState(500, 0);
        handler(MakeEvent(sliderID, EventType::MouseUp, up));
    }

    // =============================================================================
    // Custom range [0, 100]
    // =============================================================================

    TEST_F(SliderHandlerTest, CustomRange_MouseDragScalesAcrossFullRange)
    {
        float v = 0.0f;
        auto handler = MakeSliderHandler(guiStateManager, elementStorage, &v, /*width*/100,
            /*onChanged*/ nullptr,
            /*minVal*/ 0.0f, /*maxVal*/ 100.0f);

        InputState down = MakeMouseDownState(0, 0);
        handler(MakeEvent(sliderID, EventType::MouseDown, down));

        // +25 px on a 100-wide element = +25% of 100 = +25.
        InputState move = MakeMouseMoveState(25, 0);
        handler(MakeEvent(sliderID, EventType::MouseMove, move));
        EXPECT_FLOAT_EQ(v, 25.0f);

        // +100 px = +100 units, clamped to maxVal.
        InputState moveMax = MakeMouseMoveState(150, 0);
        handler(MakeEvent(sliderID, EventType::MouseMove, moveMax));
        EXPECT_FLOAT_EQ(v, 100.0f);

        // Negative delta clamps to minVal.
        InputState moveMin = MakeMouseMoveState(-500, 0);
        handler(MakeEvent(sliderID, EventType::MouseMove, moveMin));
        EXPECT_FLOAT_EQ(v, 0.0f);

        InputState up = MakeMouseUpState(-500, 0);
        handler(MakeEvent(sliderID, EventType::MouseUp, up));
    }

    TEST_F(SliderHandlerTest, CustomRange_NonZeroMinVal)
    {
        float v = 10.0f;
        auto handler = MakeSliderHandler(guiStateManager, elementStorage, &v, /*width*/100,
            nullptr, /*minVal*/10.0f, /*maxVal*/20.0f);

        InputState down = MakeMouseDownState(0, 0);
        handler(MakeEvent(sliderID, EventType::MouseDown, down));

        // +50 px on width 100, range = 10 → delta = 5.
        InputState move = MakeMouseMoveState(50, 0);
        handler(MakeEvent(sliderID, EventType::MouseMove, move));
        EXPECT_FLOAT_EQ(v, 15.0f);

        // Drag hard negative: clamps to minVal (10), NOT 0.
        InputState moveLow = MakeMouseMoveState(-500, 0);
        handler(MakeEvent(sliderID, EventType::MouseMove, moveLow));
        EXPECT_FLOAT_EQ(v, 10.0f);

        InputState up = MakeMouseUpState(-500, 0);
        handler(MakeEvent(sliderID, EventType::MouseUp, up));
    }

    // =============================================================================
    // Keyboard step scales with the range
    // =============================================================================

    TEST_F(SliderHandlerTest, CustomRange_ArrowKeyStepScalesToRange)
    {
        float v = 50.0f;
        auto handler = MakeSliderHandler(guiStateManager, elementStorage, &v, /*width*/100,
            nullptr, /*minVal*/0.0f, /*maxVal*/100.0f);

        // Focus the slider so the keyboard path runs.
        guiStateManager.Interaction().SetFocusID(sliderID);

        // Arrow key step = range * 0.01 = 1.0.
        InputState right = MakeKeyState(KeyCode::Right);
        EXPECT_TRUE(handler(MakeEvent(sliderID, EventType::KeyDown, right)));
        EXPECT_FLOAT_EQ(v, 51.0f);

        InputState left = MakeKeyState(KeyCode::Left);
        handler(MakeEvent(sliderID, EventType::KeyDown, left));
        EXPECT_FLOAT_EQ(v, 50.0f);

        // PageUp step = range * 0.1 = 10.0.
        InputState pgUp = MakeKeyState(KeyCode::PageUp);
        handler(MakeEvent(sliderID, EventType::KeyDown, pgUp));
        EXPECT_FLOAT_EQ(v, 60.0f);

        InputState pgDn = MakeKeyState(KeyCode::PageDown);
        handler(MakeEvent(sliderID, EventType::KeyDown, pgDn));
        EXPECT_FLOAT_EQ(v, 50.0f);
    }

    TEST_F(SliderHandlerTest, CustomRange_HomeAndEndJumpToBounds)
    {
        float v = 50.0f;
        auto handler = MakeSliderHandler(guiStateManager, elementStorage, &v, /*width*/100,
            nullptr, /*minVal*/0.0f, /*maxVal*/100.0f);

        guiStateManager.Interaction().SetFocusID(sliderID);

        InputState end = MakeKeyState(KeyCode::End);
        handler(MakeEvent(sliderID, EventType::KeyDown, end));
        EXPECT_FLOAT_EQ(v, 100.0f);

        InputState home = MakeKeyState(KeyCode::Home);
        handler(MakeEvent(sliderID, EventType::KeyDown, home));
        EXPECT_FLOAT_EQ(v, 0.0f);
    }

    TEST_F(SliderHandlerTest, CustomRange_ArrowKeyRespectsMaxClamp)
    {
        float v = 99.8f;
        auto handler = MakeSliderHandler(guiStateManager, elementStorage, &v, /*width*/100,
            nullptr, /*minVal*/0.0f, /*maxVal*/100.0f);

        guiStateManager.Interaction().SetFocusID(sliderID);

        // Step is 1.0; 99.8 + 1.0 = 100.8 → clamped to 100.0.
        InputState right = MakeKeyState(KeyCode::Right);
        handler(MakeEvent(sliderID, EventType::KeyDown, right));
        EXPECT_FLOAT_EQ(v, 100.0f);
    }

    // =============================================================================
    // Null-pointer safety and inverted range
    // =============================================================================

    TEST_F(SliderHandlerTest, NullValuePointerIsNoOp)
    {
        auto handler = MakeSliderHandler(guiStateManager, elementStorage, nullptr, 100);
        InputState down = MakeMouseDownState(0, 0);
        EXPECT_FALSE(handler(MakeEvent(sliderID, EventType::MouseDown, down)));
    }

    // =============================================================================
    // Click vs drag: a clean click opens text-edit; sub-threshold movement still counts
    // as a click.
    // =============================================================================

    TEST_F(SliderHandlerTest, CleanClickOpensEditModeAndLeavesValueUnchanged)
    {
        float v = 0.5f;
        auto handler = MakeSliderHandler(guiStateManager, elementStorage, &v, /*elementWidth*/100);

        // Down @ 50, Up @ 50 — no movement → click semantics.
        InputState down = MakeMouseDownState(50, 0);
        handler(MakeEvent(sliderID, EventType::MouseDown, down));
        EXPECT_FLOAT_EQ(v, 0.5f) << "MouseDown alone must not move the value";

        InputState up = MakeMouseUpState(50, 0);
        handler(MakeEvent(sliderID, EventType::MouseUp, up));

        EXPECT_FLOAT_EQ(v, 0.5f) << "Clean click must not change the value";

        auto* state = guiStateManager.GetElementDataStore().Get<SliderState>(sliderID);
        ASSERT_NE(state, nullptr);
        EXPECT_TRUE(state->editMode) << "Clean click should arm edit mode";
    }

    TEST_F(SliderHandlerTest, SubThresholdNudgeStillCountsAsClick)
    {
        float v = 0.5f;
        auto handler = MakeSliderHandler(guiStateManager, elementStorage, &v, /*elementWidth*/100);

        // Down @ 50, Move @ 51 (delta=1 < 3px threshold), Up @ 51 → still a click.
        InputState down = MakeMouseDownState(50, 0);
        handler(MakeEvent(sliderID, EventType::MouseDown, down));

        InputState move = MakeMouseMoveState(51, 0);
        handler(MakeEvent(sliderID, EventType::MouseMove, move));
        EXPECT_FLOAT_EQ(v, 0.5f) << "Sub-threshold drag must not move the value";

        InputState up = MakeMouseUpState(51, 0);
        handler(MakeEvent(sliderID, EventType::MouseUp, up));

        EXPECT_FLOAT_EQ(v, 0.5f);
        auto* state = guiStateManager.GetElementDataStore().Get<SliderState>(sliderID);
        ASSERT_NE(state, nullptr);
        EXPECT_TRUE(state->editMode);
    }

    TEST_F(SliderHandlerTest, InvertedRangeIsSwappedInternally)
    {
        // Caller mistakenly passes max < min — handler swaps them rather than
        // producing a degenerate clamp range.
        float v = 0.0f;
        auto handler = MakeSliderHandler(guiStateManager, elementStorage, &v, /*width*/100,
            nullptr, /*min*/100.0f, /*max*/0.0f);

        InputState down = MakeMouseDownState(0, 0);
        handler(MakeEvent(sliderID, EventType::MouseDown, down));

        InputState move = MakeMouseMoveState(50, 0);
        handler(MakeEvent(sliderID, EventType::MouseMove, move));
        // After swap, range is [0, 100]; start value 0 + (50/100 * 100) = 50.
        EXPECT_FLOAT_EQ(v, 50.0f);

        InputState up = MakeMouseUpState(50, 0);
        handler(MakeEvent(sliderID, EventType::MouseUp, up));
    }
}