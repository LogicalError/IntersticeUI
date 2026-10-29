#include <gtest/gtest.h>

#include <gui_core/gui_base_types.h>
#include <gui_core/gui_id.h>
#include <gui_core/gui_id_manager.h>

#include <gui_core/elements/gui_element.h>
#include <gui_core/elements/gui_element_storage.h>

#include <gui_core/input/gui_input_state.h>
#include <gui_core/input/gui_input_enums.h>

#include <gui_core/state/gui_state_manager.h>

#include <gui_core/style/gui_gradient_manager.h>
#include <gui_core/style/gui_style_manager.h>

#include <gui_controls/gui_default_handlers.h>

#include "headless_gui_app.h"

#include <thread>

namespace tachyon
{
    namespace
    {
        // No rect or element: the cursor is in screen space (no ancestor transforms).
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
            s.prevButtons = MouseButtons::None; // LeftPressed() == true
            return s;
        }
        InputState MakeMouseMoveState(int x, int y)
        {
            InputState s{};
            s.screenPos = { x, y };
            s.buttons = MouseButtons::Left;   // still held
            s.prevButtons = MouseButtons::Left;
            return s;
        }
        InputState MakeMouseUpState(int x, int y)
        {
            InputState s{};
            s.screenPos = { x, y };
            s.buttons = MouseButtons::None;
            s.prevButtons = MouseButtons::Left; // LeftReleased() == true
            return s;
        }
        InputState MakeEscapeKeyState()
        {
            InputState s{};
            s.lastKey = KeyCode::Escape;
            s.lastKeyAction = KeyAction::KeyDown;
            return s;
        }
    }

    // ===========================================================================
    // Fixture
    // ===========================================================================

    class SizerHandlerTest : public ::testing::Test
    {
    protected:
        GUIStateManager guiStateManager;
        GradientManager gradientManager{ 256 };
        StyleManager    styleManager{ gradientManager };
        ElementStorage  elementStorage{ styleManager };
        ID sizerID{};

        void SetUp() override
        {
            // Any non-Invalid ID works for the handler.
            sizerID.value.dynamicParts.mode = IDMode::Dynamic;
            sizerID.value.dynamicParts.hashValue = 1;
        }
    };

    // ===========================================================================
    // Horizontal sizer
    // ===========================================================================

    TEST_F(SizerHandlerTest, Horizontal_MouseDownRecordsStartAndCapturesFocus)
    {
        int size = 100;
        auto handler = MakeHorizontalSizerHandler(guiStateManager, elementStorage, size, 50, 300);

        InputState down = MakeMouseDownState(200, 50);
        EXPECT_TRUE(handler(MakeEvent(sizerID, EventType::MouseDown, down)));
        EXPECT_EQ(guiStateManager.Interaction().GetFocusedID(), sizerID);

        InputState up = MakeMouseUpState(200, 50);
        handler(MakeEvent(sizerID, EventType::MouseUp, up));
    }

    TEST_F(SizerHandlerTest, Horizontal_MouseMoveUpdatesSizeAndFiresOnChanged)
    {
        int size = 100;
        int changedCount = 0;
        int lastChanged = -1;
        auto handler = MakeHorizontalSizerHandler(guiStateManager, elementStorage, size, 50, 300,
            [&](int newSize) { changedCount++; lastChanged = newSize; });

        InputState down = MakeMouseDownState(200, 50);
        handler(MakeEvent(sizerID, EventType::MouseDown, down));

        // Drag +30 px on X
        InputState move = MakeMouseMoveState(230, 50);
        EXPECT_TRUE(handler(MakeEvent(sizerID, EventType::MouseMove, move)));
        EXPECT_EQ(size, 130);
        EXPECT_EQ(changedCount, 1);
        EXPECT_EQ(lastChanged, 130);

        // Drag -50 px relative to start
        InputState move2 = MakeMouseMoveState(150, 50);
        EXPECT_TRUE(handler(MakeEvent(sizerID, EventType::MouseMove, move2)));
        EXPECT_EQ(size, 50);
        EXPECT_EQ(changedCount, 2);

        InputState up = MakeMouseUpState(150, 50);
        handler(MakeEvent(sizerID, EventType::MouseUp, up));
    }

    TEST_F(SizerHandlerTest, Horizontal_MouseMoveClampsToMinAndMax)
    {
        int size = 100;
        auto handler = MakeHorizontalSizerHandler(guiStateManager, elementStorage, size, 50, 200);

        InputState down = MakeMouseDownState(200, 50);
        handler(MakeEvent(sizerID, EventType::MouseDown, down));

        // Drag -500 -> clamp to min 50
        InputState moveLow = MakeMouseMoveState(-300, 50);
        handler(MakeEvent(sizerID, EventType::MouseMove, moveLow));
        EXPECT_EQ(size, 50);

        // Drag +500 -> clamp to max 200
        InputState moveHigh = MakeMouseMoveState(700, 50);
        handler(MakeEvent(sizerID, EventType::MouseMove, moveHigh));
        EXPECT_EQ(size, 200);

        InputState up = MakeMouseUpState(700, 50);
        handler(MakeEvent(sizerID, EventType::MouseUp, up));
    }

    TEST_F(SizerHandlerTest, Horizontal_EscapeRestoresStartSizeAndClearsDragging)
    {
        int size = 100;
        ID prevFocus{};
        prevFocus.value.dynamicParts.mode = IDMode::Dynamic;
        prevFocus.value.dynamicParts.hashValue = 999;
        guiStateManager.Interaction().SetFocusID(prevFocus);

        auto handler = MakeHorizontalSizerHandler(guiStateManager, elementStorage, size, 50, 300);

        InputState down = MakeMouseDownState(200, 50);
        handler(MakeEvent(sizerID, EventType::MouseDown, down));
        EXPECT_EQ(guiStateManager.Interaction().GetFocusedID(), sizerID);

        InputState move = MakeMouseMoveState(250, 50);
        handler(MakeEvent(sizerID, EventType::MouseMove, move));
        EXPECT_EQ(size, 150);

        // Escape should revert size and restore previous focus.
        InputState esc = MakeEscapeKeyState();
        EXPECT_TRUE(handler(MakeEvent(sizerID, EventType::KeyDown, esc)));
        EXPECT_EQ(size, 100);
        EXPECT_EQ(guiStateManager.Interaction().GetFocusedID(), prevFocus);

        // A subsequent MouseMove should be ignored (drag cleared).
        InputState move2 = MakeMouseMoveState(300, 50);
        EXPECT_FALSE(handler(MakeEvent(sizerID, EventType::MouseMove, move2)));
        EXPECT_EQ(size, 100);
    }

    TEST_F(SizerHandlerTest, Horizontal_MouseUpCommitsNewSizeAndRestoresFocus)
    {
        int size = 100;
        ID prevFocus{};
        prevFocus.value.dynamicParts.mode = IDMode::Dynamic;
        prevFocus.value.dynamicParts.hashValue = 7;
        guiStateManager.Interaction().SetFocusID(prevFocus);

        auto handler = MakeHorizontalSizerHandler(guiStateManager, elementStorage, size, 50, 300);

        InputState down = MakeMouseDownState(200, 50);
        handler(MakeEvent(sizerID, EventType::MouseDown, down));
        InputState move = MakeMouseMoveState(240, 50);
        handler(MakeEvent(sizerID, EventType::MouseMove, move));
        InputState up = MakeMouseUpState(240, 50);
        EXPECT_TRUE(handler(MakeEvent(sizerID, EventType::MouseUp, up)));

        EXPECT_EQ(size, 140);
        EXPECT_EQ(guiStateManager.Interaction().GetFocusedID(), prevFocus);

        // Subsequent MouseMoves should be ignored.
        InputState move2 = MakeMouseMoveState(500, 50);
        EXPECT_FALSE(handler(MakeEvent(sizerID, EventType::MouseMove, move2)));
        EXPECT_EQ(size, 140);
    }

    TEST_F(SizerHandlerTest, Horizontal_EventsOnDifferentIDIgnoredWhileDragging)
    {
        int size = 100;
        auto handler = MakeHorizontalSizerHandler(guiStateManager, elementStorage, size, 50, 300);

        InputState down = MakeMouseDownState(200, 50);
        handler(MakeEvent(sizerID, EventType::MouseDown, down));

        // Unrelated element id fires MouseMove - handler should not modify size.
        ID otherID{};
        otherID.value.dynamicParts.mode = IDMode::Dynamic;
        otherID.value.dynamicParts.hashValue = 2;

        InputState move = MakeMouseMoveState(500, 50);
        EXPECT_FALSE(handler(MakeEvent(otherID, EventType::MouseMove, move)));
        EXPECT_EQ(size, 100);

        // Original sizer can still continue its drag.
        InputState move2 = MakeMouseMoveState(220, 50);
        EXPECT_TRUE(handler(MakeEvent(sizerID, EventType::MouseMove, move2)));
        EXPECT_EQ(size, 120);

        InputState up = MakeMouseUpState(220, 50);
        handler(MakeEvent(sizerID, EventType::MouseUp, up));
    }

    // Stored sizes are CSS pixels: a drag of N screen pixels at UI scale 2 moves
    // the size by N/2, and an auto-size measured in pixels is converted too.
    TEST_F(SizerHandlerTest, Horizontal_SizeIsInCssPixels)
    {
        int size = 100;
        auto handler = MakeHorizontalSizerHandler(guiStateManager, elementStorage, size, 50, 300,
            nullptr, [&]() -> int { return 250; }, false, /*pixelsPerUnit*/ 2.0f);

        InputState down = MakeMouseDownState(200, 50);
        handler(MakeEvent(sizerID, EventType::MouseDown, down));
        InputState move = MakeMouseMoveState(230, 50);
        EXPECT_TRUE(handler(MakeEvent(sizerID, EventType::MouseMove, move)));
        EXPECT_EQ(size, 115);
        InputState up = MakeMouseUpState(230, 50);
        handler(MakeEvent(sizerID, EventType::MouseUp, up));

        InputState down2 = MakeMouseDownState(230, 50);
        handler(MakeEvent(sizerID, EventType::MouseDown, down2));
        EXPECT_EQ(size, 125) << "250 px auto-size = 125 CSS px";
    }

    TEST_F(SizerHandlerTest, Vertical_SizeIsInCssPixels)
    {
        int size = 100;
        auto handler = MakeVerticalSizerHandler(guiStateManager, elementStorage, size, 30, 400,
            nullptr, nullptr, false, /*pixelsPerUnit*/ 1.5f);

        InputState down = MakeMouseDownState(50, 200);
        handler(MakeEvent(sizerID, EventType::MouseDown, down));
        InputState move = MakeMouseMoveState(50, 230);
        EXPECT_TRUE(handler(MakeEvent(sizerID, EventType::MouseMove, move)));
        EXPECT_EQ(size, 120);
        InputState up = MakeMouseUpState(50, 230);
        handler(MakeEvent(sizerID, EventType::MouseUp, up));
    }

    TEST_F(SizerHandlerTest, Horizontal_DoubleClickInvokesAutoSize)
    {
        int size = 100;
        int autoCalls = 0;
        auto handler = MakeHorizontalSizerHandler(guiStateManager, elementStorage, size, 50, 300,
            /*onChanged*/ nullptr,
            /*onAutoSize*/ [&]() -> int { autoCalls++; return 250; });

        // First click: records click time and starts a drag.
        InputState down1 = MakeMouseDownState(200, 50);
        handler(MakeEvent(sizerID, EventType::MouseDown, down1));
        // Release to end drag before double-clicking.
        InputState up1 = MakeMouseUpState(200, 50);
        handler(MakeEvent(sizerID, EventType::MouseUp, up1));

        // Second click within the 300ms double-click window invokes autoSize.
        InputState down2 = MakeMouseDownState(200, 50);
        EXPECT_TRUE(handler(MakeEvent(sizerID, EventType::MouseDown, down2)));
        EXPECT_EQ(autoCalls, 1);
        EXPECT_EQ(size, 250);

        // autoSize ends the drag: a following MouseMove must not resize.
        InputState move = MakeMouseMoveState(400, 50);
        EXPECT_FALSE(handler(MakeEvent(sizerID, EventType::MouseMove, move)));
        EXPECT_EQ(size, 250);
    }

    // ===========================================================================
    // Vertical sizer (mirrors horizontal)
    // ===========================================================================

    TEST_F(SizerHandlerTest, Vertical_MouseMoveUpdatesSizeOnYAxis)
    {
        int size = 80;
        int changedCount = 0;
        auto handler = MakeVerticalSizerHandler(guiStateManager, elementStorage, size, 30, 400,
            [&](int) { changedCount++; });

        InputState down = MakeMouseDownState(50, 100);
        EXPECT_TRUE(handler(MakeEvent(sizerID, EventType::MouseDown, down)));
        EXPECT_EQ(guiStateManager.Interaction().GetFocusedID(), sizerID);

        // Drag +40 px on Y
        InputState move = MakeMouseMoveState(50, 140);
        EXPECT_TRUE(handler(MakeEvent(sizerID, EventType::MouseMove, move)));
        EXPECT_EQ(size, 120);
        EXPECT_EQ(changedCount, 1);

        // Y movement only - X delta must not affect size.
        InputState moveXOnly = MakeMouseMoveState(500, 140);
        handler(MakeEvent(sizerID, EventType::MouseMove, moveXOnly));
        EXPECT_EQ(size, 120);

        InputState up = MakeMouseUpState(500, 140);
        handler(MakeEvent(sizerID, EventType::MouseUp, up));
    }

    TEST_F(SizerHandlerTest, Vertical_MouseMoveClampsToMinAndMax)
    {
        int size = 100;
        auto handler = MakeVerticalSizerHandler(guiStateManager, elementStorage, size, 50, 200);

        InputState down = MakeMouseDownState(50, 100);
        handler(MakeEvent(sizerID, EventType::MouseDown, down));

        InputState moveLow = MakeMouseMoveState(50, -500);
        handler(MakeEvent(sizerID, EventType::MouseMove, moveLow));
        EXPECT_EQ(size, 50);

        InputState moveHigh = MakeMouseMoveState(50, 900);
        handler(MakeEvent(sizerID, EventType::MouseMove, moveHigh));
        EXPECT_EQ(size, 200);

        InputState up = MakeMouseUpState(50, 900);
        handler(MakeEvent(sizerID, EventType::MouseUp, up));
    }

    TEST_F(SizerHandlerTest, Vertical_EscapeRestoresStartSize)
    {
        int size = 100;
        ID prevFocus{};
        prevFocus.value.dynamicParts.mode = IDMode::Dynamic;
        prevFocus.value.dynamicParts.hashValue = 42;
        guiStateManager.Interaction().SetFocusID(prevFocus);

        auto handler = MakeVerticalSizerHandler(guiStateManager, elementStorage, size, 30, 400);

        InputState down = MakeMouseDownState(50, 100);
        handler(MakeEvent(sizerID, EventType::MouseDown, down));
        InputState move = MakeMouseMoveState(50, 170);
        handler(MakeEvent(sizerID, EventType::MouseMove, move));
        EXPECT_EQ(size, 170);

        InputState esc = MakeEscapeKeyState();
        EXPECT_TRUE(handler(MakeEvent(sizerID, EventType::KeyDown, esc)));
        EXPECT_EQ(size, 100);
        EXPECT_EQ(guiStateManager.Interaction().GetFocusedID(), prevFocus);

        // Drag must be cleared: further MouseMove should do nothing.
        InputState move2 = MakeMouseMoveState(50, 250);
        EXPECT_FALSE(handler(MakeEvent(sizerID, EventType::MouseMove, move2)));
        EXPECT_EQ(size, 100);
    }

    TEST_F(SizerHandlerTest, Vertical_MouseUpCommitsAndRestoresFocus)
    {
        int size = 100;
        ID prevFocus{};
        prevFocus.value.dynamicParts.mode = IDMode::Dynamic;
        prevFocus.value.dynamicParts.hashValue = 7;
        guiStateManager.Interaction().SetFocusID(prevFocus);

        auto handler = MakeVerticalSizerHandler(guiStateManager, elementStorage, size, 30, 400);

        InputState down = MakeMouseDownState(50, 100);
        handler(MakeEvent(sizerID, EventType::MouseDown, down));
        InputState move = MakeMouseMoveState(50, 160);
        handler(MakeEvent(sizerID, EventType::MouseMove, move));
        InputState up = MakeMouseUpState(50, 160);
        EXPECT_TRUE(handler(MakeEvent(sizerID, EventType::MouseUp, up)));

        EXPECT_EQ(size, 160);
        EXPECT_EQ(guiStateManager.Interaction().GetFocusedID(), prevFocus);
    }

    TEST_F(SizerHandlerTest, Vertical_EventsOnDifferentIDIgnoredWhileDragging)
    {
        int size = 100;
        auto handler = MakeVerticalSizerHandler(guiStateManager, elementStorage, size, 30, 400);

        InputState down = MakeMouseDownState(50, 100);
        handler(MakeEvent(sizerID, EventType::MouseDown, down));

        ID otherID{};
        otherID.value.dynamicParts.mode = IDMode::Dynamic;
        otherID.value.dynamicParts.hashValue = 3;

        InputState move = MakeMouseMoveState(50, 400);
        EXPECT_FALSE(handler(MakeEvent(otherID, EventType::MouseMove, move)));
        EXPECT_EQ(size, 100);

        InputState move2 = MakeMouseMoveState(50, 120);
        EXPECT_TRUE(handler(MakeEvent(sizerID, EventType::MouseMove, move2)));
        EXPECT_EQ(size, 120);

        InputState up = MakeMouseUpState(50, 120);
        handler(MakeEvent(sizerID, EventType::MouseUp, up));
    }

    TEST_F(SizerHandlerTest, Vertical_DoubleClickInvokesAutoSize)
    {
        int size = 100;
        int autoCalls = 0;
        auto handler = MakeVerticalSizerHandler(guiStateManager, elementStorage, size, 30, 500,
            nullptr,
            [&]() -> int { autoCalls++; return 330; });

        InputState down1 = MakeMouseDownState(50, 100);
        handler(MakeEvent(sizerID, EventType::MouseDown, down1));
        InputState up1 = MakeMouseUpState(50, 100);
        handler(MakeEvent(sizerID, EventType::MouseUp, up1));

        InputState down2 = MakeMouseDownState(50, 100);
        EXPECT_TRUE(handler(MakeEvent(sizerID, EventType::MouseDown, down2)));
        EXPECT_EQ(autoCalls, 1);
        EXPECT_EQ(size, 330);

        InputState move = MakeMouseMoveState(50, 400);
        EXPECT_FALSE(handler(MakeEvent(sizerID, EventType::MouseMove, move)));
        EXPECT_EQ(size, 330);
    }
    // A sizer in a zoomed container follows the cursor: at 200%, 40 screen pixels are 20
    // pixels of its layout.
    TEST(SizerZoomTest, ASizerFollowsTheCursorWhenZoomed)
    {
        GUITest::HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        const ID containerID = app.idManager.CreateStaticID();
        const ID sizerID     = app.idManager.CreateStaticID();
        int width = 100;
        ElementRef containerRef = ElementRef::Invalid();
        auto frame = [&] {
            app.Frame([&] {
                app.BeginRootWindow({ 800, 600 });
                containerRef = app.RegisterElement(Element{ .id = containerID, .styleRef = app.theme.emptyVertStyleRef },
                                                   LayoutOverride{}.Size(400, 300));
                app.elementStorage.SetContentTransform(containerRef, float3x2::Scale(2.0f));
                ASSERT_EQ(app.elementStorage.BeginChildren(), ElementStorageResult::Success);
                app.RegisterElement(Element{ .id = sizerID,
                                             .eventHandler = MakeHorizontalSizerHandler(app.guiStateManager, app.elementStorage, width, 0, 1000),
                                             .styleRef = app.theme.emptyHorzStyleRef },
                                    LayoutOverride{}.Size(8, 100));
                ASSERT_EQ(app.elementStorage.EndChildren(), ElementStorageResult::Success);
                app.EndRootWindow();
            });
        };
        frame();
        frame();
        // The container scales its content around its inner rect's top-left.
        const int2 pivot = app.elementStorage.GetElementRectUnsafe(containerRef).innerRect.position;
        const IntRect sizer = app.GetElementRect(sizerID);
        const int2 layoutCenter{ sizer.x + sizer.w / 2, sizer.y + sizer.h / 2 };
        const int2 grab{ pivot.x + 2 * (layoutCenter.x - pivot.x), pivot.y + 2 * (layoutCenter.y - pivot.y) };

        app.InjectMouseMove(grab);
        app.InjectMouseDown(grab);
        frame();
        app.InjectMouseMove({ grab.x + 40, grab.y }, MouseButtons::Left);
        frame();
        EXPECT_EQ(width, 120);
        app.InjectMouseUp({ grab.x + 40, grab.y });
        frame();
        app.Cleanup();
    }
}