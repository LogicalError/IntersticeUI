// Integration tests for Button, Field(bool&), Field(string&) and Label,
// run through the headless harness with the real handlers.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_core/gui_id.h>
#include <gui_core/gui_id_manager.h>
#include <gui_core/state/gui_state_manager.h>
#include <gui_core/elements/gui_element_storage.h>
#include <gui_core/elements/gui_element_flags.h>
#include <gui_core/input/gui_input_enums.h>

#include <cmath>
#include <limits>
#include <string>

namespace GUI
{
    using GUITest::HeadlessGUIApp;

    namespace
    {

        class ControlsBasicTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;

            void SetUp()    override { app.Initialize(); }
            void TearDown() override { app.Cleanup(); }
        };

        // ---------------------------------------------------------------------
        // Button
        // ---------------------------------------------------------------------

        TEST_F(ControlsBasicTest, ButtonRendersAndIsHittable)
        {
            ID buttonID = app.idManager.CreateStaticID();
            int callCount = 0;

            app.Frame([&] {
                app.BeginRootWindow();
                app.Button(buttonID, "Click", [&] { callCount++; });
                app.EndRootWindow();
            });

            ASSERT_TRUE(app.HasElement(buttonID));
            const auto rect = app.GetElementRect(buttonID);
            EXPECT_GT(rect.w, 0);
            EXPECT_GT(rect.h, 0);
            EXPECT_EQ(callCount, 0);  // No interaction yet.
        }

        TEST_F(ControlsBasicTest, ClickingButtonInvokesOnClickExactlyOnce)
        {
            ID buttonID = app.idManager.CreateStaticID();
            int callCount = 0;

            // Frame 1: lay out the button.
            app.Frame([&] {
                app.BeginRootWindow();
                app.Button(buttonID, "Click", [&] { callCount++; });
                app.EndRootWindow();
            });

            // Click in the centre and run the dispatch frame.
            const int2 center = app.CenterOf(buttonID);
            app.InjectMouseMove(center);
            app.InjectMouseDown(center);
            app.InjectMouseUp(center);

            app.Frame([&] {
                app.BeginRootWindow();
                app.Button(buttonID, "Click", [&] { callCount++; });
                app.EndRootWindow();
            });

            EXPECT_EQ(callCount, 1);
        }

        // Press and release can land in the same frame (touchpad taps, hitches);
        // the press's capture must still be released so the next click reaches
        // whatever is under the cursor.
        TEST_F(ControlsBasicTest, SameFrameClickReleasesCapture)
        {
            ID a = app.idManager.CreateStaticID();
            ID b = app.idManager.CreateStaticID();
            int aCount = 0, bCount = 0;
            auto emit = [&] {
                app.BeginRootWindow();
                app.Button(a, "A", [&] { aCount++; });
                app.Button(b, "B", [&] { bCount++; });
                app.EndRootWindow();
            };

            app.Frame(emit);
            app.ClickAt(app.CenterOf(a));
            app.Frame(emit);
            app.Frame(emit);
            EXPECT_EQ(app.guiStateManager.Interaction().GetCaptureID(), ID::Invalid());

            app.ClickAt(app.CenterOf(b));
            app.Frame(emit);
            EXPECT_EQ(aCount, 1);
            EXPECT_EQ(bCount, 1);
        }

        TEST_F(ControlsBasicTest, ClickAtHelperFiresButton)
        {
            ID buttonID = app.idManager.CreateStaticID();
            int callCount = 0;

            app.Frame([&] {
                app.BeginRootWindow();
                app.Button(buttonID, "Click", [&] { callCount++; });
                app.EndRootWindow();
            });

            app.ClickAt(app.CenterOf(buttonID));
            app.Frame([&] {
                app.BeginRootWindow();
                app.Button(buttonID, "Click", [&] { callCount++; });
                app.EndRootWindow();
            });

            EXPECT_EQ(callCount, 1);
        }

        TEST_F(ControlsBasicTest, ButtonReturnsTrueOnFrameAfterDispatch)
        {
            // m_clickID is set during end-of-frame dispatch, so Button() returns true
            // on the frame after the click.
            ID buttonID = app.idManager.CreateStaticID();

            auto draw = [&](bool& out) {
                app.BeginRootWindow();
                out = app.Button(buttonID, "B", [] {});
                app.EndRootWindow();
            };
            bool ret = false;
            app.Frame([&] { draw(ret); });
            EXPECT_FALSE(ret);

            app.ClickAt(app.CenterOf(buttonID));
            app.Frame([&] { draw(ret); });
            // The click dispatches at the end of this frame; construction sees none yet.
            EXPECT_FALSE(ret);

            app.Frame([&] { draw(ret); });
            EXPECT_TRUE(ret);

            app.Frame([&] { draw(ret); });
            EXPECT_FALSE(ret);  // Cleared after one frame.
        }

        TEST_F(ControlsBasicTest, ClickOutsideButtonDoesNotInvokeCallback)
        {
            ID buttonID = app.idManager.CreateStaticID();
            int callCount = 0;

            app.Frame([&] {
                app.BeginRootWindow();
                app.Button(buttonID, "B", [&] { callCount++; });
                app.EndRootWindow();
            });

            // Click well clear of the button (top-left corner of the window).
            app.ClickAt({ 4, 4 });
            app.Frame([&] {
                app.BeginRootWindow();
                app.Button(buttonID, "B", [&] { callCount++; });
                app.EndRootWindow();
            });
            EXPECT_EQ(callCount, 0);
        }

        TEST_F(ControlsBasicTest, ButtonDoesNotFireOnHoverAlone)
        {
            ID buttonID = app.idManager.CreateStaticID();
            int callCount = 0;

            app.Frame([&] {
                app.BeginRootWindow();
                app.Button(buttonID, "B", [&] { callCount++; });
                app.EndRootWindow();
            });

            // Move the cursor over the button without pressing.
            app.InjectMouseMove(app.CenterOf(buttonID));
            app.Frame([&] {
                app.BeginRootWindow();
                app.Button(buttonID, "B", [&] { callCount++; });
                app.EndRootWindow();
            });
            EXPECT_EQ(callCount, 0);
        }

        TEST_F(ControlsBasicTest, RightClickOnButtonDoesNotInvokeCallback)
        {
            ID buttonID = app.idManager.CreateStaticID();
            int callCount = 0;

            app.Frame([&] {
                app.BeginRootWindow();
                app.Button(buttonID, "B", [&] { callCount++; });
                app.EndRootWindow();
            });

            // MakeButtonHandler only reacts to LeftPressed — Right should be ignored.
            app.ClickAt(app.CenterOf(buttonID), MouseButtons::Right);
            app.Frame([&] {
                app.BeginRootWindow();
                app.Button(buttonID, "B", [&] { callCount++; });
                app.EndRootWindow();
            });
            EXPECT_EQ(callCount, 0);
        }

        TEST_F(ControlsBasicTest, MultipleClicksFireMultipleCallbacks)
        {
            ID buttonID = app.idManager.CreateStaticID();
            int callCount = 0;

            auto draw = [&] {
                app.BeginRootWindow();
                app.Button(buttonID, "B", [&] { callCount++; });
                app.EndRootWindow();
            };
            app.Frame(draw);

            const int2 center = app.CenterOf(buttonID);
            for (int i = 0; i < 3; ++i)
            {
                app.ClickAt(center);
                app.Frame(draw);
            }
            EXPECT_EQ(callCount, 3);
        }

        TEST_F(ControlsBasicTest, IndeterminateButtonStillRendersButContextChanges)
        {
            // Buttons stay interactive inside BeginIndeterminate.
            ID buttonID = app.idManager.CreateStaticID();
            int callCount = 0;

            app.Frame([&] {
                app.BeginRootWindow();
                app.BeginIndeterminate();
                app.Button(buttonID, "B", [&] { callCount++; });
                app.EndIndeterminate();
                app.EndRootWindow();
            });

            ASSERT_TRUE(app.HasElement(buttonID));

            app.ClickAt(app.CenterOf(buttonID));
            app.Frame([&] {
                app.BeginRootWindow();
                app.BeginIndeterminate();
                app.Button(buttonID, "B", [&] { callCount++; });
                app.EndIndeterminate();
                app.EndRootWindow();
            });
            EXPECT_EQ(callCount, 1);
        }

        // ---------------------------------------------------------------------
        // Field(bool&) — Toggle
        // ---------------------------------------------------------------------

        TEST_F(ControlsBasicTest, ToggleFieldFlipsValueOnClick)
        {
            ID toggleID = app.idManager.CreateStaticID();
            bool value = false;

            auto draw = [&] {
                app.BeginRootWindow();
                app.Field(toggleID, value, "Enabled");
                app.EndRootWindow();
            };
            app.Frame(draw);
            ASSERT_FALSE(value);

            app.ClickAt(app.CenterOf(toggleID));
            app.Frame(draw);
            EXPECT_TRUE(value);
        }

        TEST_F(ControlsBasicTest, TogglingTwiceReturnsToOriginalValue)
        {
            ID toggleID = app.idManager.CreateStaticID();
            bool value = false;

            auto draw = [&] {
                app.BeginRootWindow();
                app.Field(toggleID, value, "Enabled");
                app.EndRootWindow();
            };
            app.Frame(draw);

            const int2 center = app.CenterOf(toggleID);
            app.ClickAt(center); app.Frame(draw);
            ASSERT_TRUE(value);
            app.ClickAt(center); app.Frame(draw);
            EXPECT_FALSE(value);
        }

        TEST_F(ControlsBasicTest, ToggleFieldReturnsModifiedOnePostDispatchFrame)
        {
            // Field(bool&) returns true on the frame after the click is dispatched.
            ID toggleID = app.idManager.CreateStaticID();
            bool value = false;
            bool modified = false;

            auto draw = [&] {
                app.BeginRootWindow();
                modified = app.Field(toggleID, value, "T");
                app.EndRootWindow();
            };
            app.Frame(draw);
            EXPECT_FALSE(modified);

            app.ClickAt(app.CenterOf(toggleID));
            app.Frame(draw);     // dispatch happens here; modified read pre-dispatch
            EXPECT_FALSE(modified);

            app.Frame(draw);     // first frame after dispatch sees the click
            EXPECT_TRUE(modified);

            app.Frame(draw);     // cleared again
            EXPECT_FALSE(modified);
        }

        TEST_F(ControlsBasicTest, ClickOutsideToggleLeavesValueUnchanged)
        {
            ID toggleID = app.idManager.CreateStaticID();
            bool value = true;

            auto draw = [&] {
                app.BeginRootWindow();
                app.Field(toggleID, value, "T");
                app.EndRootWindow();
            };
            app.Frame(draw);

            app.ClickAt({ 4, 4 });  // far from the toggle
            app.Frame(draw);
            EXPECT_TRUE(value);
        }

        // ---------------------------------------------------------------------
        // Field(string&) — Text edit. StubTextSystem routes typed characters into
        // the bound string via RegisterEditableText.
        // ---------------------------------------------------------------------

        TEST_F(ControlsBasicTest, TextFieldRegistersAsEditableElement)
        {
            ID fieldID = app.idManager.CreateStaticID();
            std::string text = "hello";

            app.Frame([&] {
                app.BeginRootWindow();
                app.Field(fieldID, text);
                app.EndRootWindow();
            });

            ASSERT_TRUE(app.HasElement(fieldID));
            EXPECT_TRUE(app.ownedTextSystem.IsTextEditor(fieldID));
        }

        TEST_F(ControlsBasicTest, ClickingTextFieldFocusesIt)
        {
            ID fieldID = app.idManager.CreateStaticID();
            std::string text = "hi";

            auto draw = [&] {
                app.BeginRootWindow();
                app.Field(fieldID, text);
                app.EndRootWindow();
            };
            app.Frame(draw);

            app.ClickAt(app.CenterOf(fieldID));
            app.Frame(draw);

            EXPECT_EQ(app.guiStateManager.Interaction().GetFocusedID(), fieldID);
        }

        TEST_F(ControlsBasicTest, TypingAfterFocusAppendsToBoundString)
        {
            ID fieldID = app.idManager.CreateStaticID();
            std::string text = "";

            auto draw = [&] {
                app.BeginRootWindow();
                app.Field(fieldID, text);
                app.EndRootWindow();
            };
            app.Frame(draw);

            // Click to focus, then frame to settle SetActiveTextEditor.
            app.ClickAt(app.CenterOf(fieldID));
            app.Frame(draw);

            app.InjectText("abc");
            app.Frame(draw);

            EXPECT_EQ(text, "abc");
        }

        TEST_F(ControlsBasicTest, BackspaceDeletesLastCharacter)
        {
            ID fieldID = app.idManager.CreateStaticID();
            std::string text = "hello";

            auto draw = [&] {
                app.BeginRootWindow();
                app.Field(fieldID, text);
                app.EndRootWindow();
            };
            app.Frame(draw);

            app.ClickAt(app.CenterOf(fieldID));
            app.Frame(draw);
            app.InjectKey(KeyCode::Backspace, KeyAction::KeyDown);
            app.Frame(draw);

            EXPECT_EQ(text, "hell");
        }

        TEST_F(ControlsBasicTest, TypingWithoutFocusDoesNotMutateString)
        {
            ID fieldID = app.idManager.CreateStaticID();
            std::string text = "untouched";

            auto draw = [&] {
                app.BeginRootWindow();
                app.Field(fieldID, text);
                app.EndRootWindow();
            };
            app.Frame(draw);

            // No click → no focus → no active text editor.
            app.InjectText("xyz");
            app.Frame(draw);

            EXPECT_EQ(text, "untouched");
        }

        TEST_F(ControlsBasicTest, IndeterminateTextFieldIsReadOnlyPlaceholder)
        {
            // Under BeginIndeterminate, Field(string&) only renders the placeholder.
            ID fieldID = app.idManager.CreateStaticID();
            std::string text = "real";

            app.Frame([&] {
                app.BeginRootWindow();
                app.BeginIndeterminate();
                app.Field(fieldID, text);
                app.EndIndeterminate();
                app.EndRootWindow();
            });

            EXPECT_FALSE(app.ownedTextSystem.IsTextEditor(fieldID));
        }

        // Focusing (e.g. tabbing into) a numeric field that holds NaN/Inf must
        // not mutate the bound value — only a real edit may change it.
        TEST_F(ControlsBasicTest, FocusingNonFiniteNumericFieldDoesNotChangeValue)
        {
            ID id = app.idManager.CreateStaticID();
            // Call through the base interface — the derived stub's non-template
            // override otherwise hides the templated convenience wrapper.
            ITextSystem& ts = app.ownedTextSystem;
            for (float v0 : { std::numeric_limits<float>::quiet_NaN(),
                              std::numeric_limits<float>::infinity(),
                              -std::numeric_limits<float>::infinity() })
            {
                float value = v0;
                bool modified = false;
                ts.RegisterEditableNumber<float>(
                    id, value, TextEditBehavior::ContinuousUpdate,
                    app.theme.controls->textbox, ElementState::Focused, modified);
                EXPECT_FALSE(modified);
                EXPECT_FALSE(std::isfinite(value)); // value left untouched
            }
        }

        // ---------------------------------------------------------------------
        // Label
        // ---------------------------------------------------------------------

        TEST_F(ControlsBasicTest, LabelRegistersWithIsLabelFlag)
        {
            ID labelID = app.idManager.CreateStaticID();

            app.Frame([&] {
                app.BeginRootWindow();
                app.Label(labelID, "Name");
                app.EndRootWindow();
            });

            auto ref = app.elementStorage.FindElementRefByID(labelID);
            ASSERT_TRUE(ref.IsValid());
            const auto& el = app.elementStorage.GetElementUnsafe(ref);
            EXPECT_NE(el.flags & ElementFlags::IsLabel, ElementFlags::None);
        }

        TEST_F(ControlsBasicTest, LabelRectIsNonEmptyAfterLayout)
        {
            ID labelID = app.idManager.CreateStaticID();

            app.Frame([&] {
                app.BeginRootWindow();
                app.Label(labelID, "Some label text");
                app.EndRootWindow();
            });

            const auto rect = app.GetElementRect(labelID);
            EXPECT_GT(rect.w, 0);
            EXPECT_GT(rect.h, 0);
        }

        TEST_F(ControlsBasicTest, HoveringLabelMarksItHovered)
        {
            ID labelID = app.idManager.CreateStaticID();

            auto draw = [&] {
                app.BeginRootWindow();
                app.Label(labelID, "Hover me");
                app.EndRootWindow();
            };
            app.Frame(draw);

            app.InjectMouseMove(app.CenterOf(labelID));
            app.Frame(draw);

            const auto state = app.guiStateManager.GetCurrentState(labelID);
            EXPECT_NE(state & ElementState::Hover, ElementState::None);
        }

    } // namespace
} // namespace GUI
