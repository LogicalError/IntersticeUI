// Real-SkribidiTextSystem editor tests: focus a field, type into the live
// editor, and check what reaches the bound string. Rendering goes to a stub
// renderer (see skribidi_test_fixture.h).

#include "skribidi_test_fixture.h"

#include <gui_core/elements/gui_element_storage.h>
#include <gui_core/graphics/gui_custom_draw_context.h>

#include <string>

namespace interstice
{
    using GUITest::SkribidiHeadlessTest;

    namespace
    {
        class SkribidiEditorTest : public SkribidiHeadlessTest
        {
        protected:
            StyleRef    style{};
            ID          field = ID{ .value{ .integer = 900 } };
            std::string bound = "abc";
            bool        modified = false;
            TextID      textId = TextID::Invalid;

            void SetUp() override
            {
                SkribidiHeadlessTest::SetUp();
                style = MakeTextStyle();
            }

            // One frame of a Field(string&): register, then render at `width`.
            void Frame(int32_t width)
            {
                modified = false;
                const ElementState state = (stateManager.TextEditorFocus().GetFocusedID() == field)
                    ? ElementState::Focused : ElementState::None;
                textId = textSystem->RegisterEditableText(field, bound, TextEditBehavior::None,
                                                          style, state, modified);
                const IntRect rect{ 0, 0, width, 20 };
                textSystem->RenderText(textId, rect, rect, IntRect{ 0, 0, 2000, 2000 });
            }

            void Type(std::string_view text)
            {
                for (char c : text)
                    textSystem->OnKeyboard(ID::Invalid(), static_cast<KeyCode>(c), 0,
                                           KeyModifier::None, KeyAction::KeyChar);
            }

            void Focus(ID id)
            {
                stateManager.TextEditorFocus().Set(id);
                textSystem->SetActiveTextEditor(id);
            }
        };

        // A field inside a zoomed and panned view (node graph): the click lands where the
        // field is DRAWN, so the caret goes where the user clicked, not where the
        // unzoomed layout rect would put it.
        TEST_F(SkribidiEditorTest, ClickUnderZoomAndPanPlacesTheCaretWhereDrawn)
        {
            bound = "abcdefgh";
            const IntRect rect{ 0, 0, 200, 20 };
            // Drawn at 2x, shifted by (100, 50): screen = 2 * layout + (100, 50).
            const float3x2 layoutToScreen = float3x2::Translate(100.0f, 50.0f) * float3x2::Scale(2.0f);
            auto zoomedFrame = [&]
            {
                const ElementState state = (stateManager.TextEditorFocus().GetFocusedID() == field)
                    ? ElementState::Focused : ElementState::None;
                textId = textSystem->RegisterEditableText(field, bound, TextEditBehavior::None, style, state, modified);
                textSystem->RenderText(textId, rect, rect, IntRect{ 0, 0, 2000, 2000 }, 2.0f, layoutToScreen.Inverse());
            };
            zoomedFrame();
            Focus(field);
            zoomedFrame();

            // Layout x = 1 (before 'a') is drawn at screen x = 102.
            const int2 click{ 102, 50 + 2 * 10 };
            textSystem->OnMouseButton(ID::Invalid(), click, MouseButtons::Left, KeyModifier::None, MouseAction::MouseDown);
            textSystem->OnMouseButton(ID::Invalid(), click, MouseButtons::Left, KeyModifier::None, MouseAction::MouseUp);
            Type("Z");
            textSystem->OnKeyboard(ID::Invalid(), KeyCode::Return, 0, KeyModifier::None, KeyAction::KeyDown);
            zoomedFrame();

            EXPECT_EQ(bound, "Zabcdefgh");
        }

        // Text drawn by a custom-draw callback in a transformed space (node graph
        // pan/zoom) hit-tests where it is drawn, window offset included.
        TEST_F(SkribidiEditorTest, CustomDrawnEditorHitTestsWhereDrawn)
        {
            bound = "abcdefgh";
            ElementStorage storage{ styleManager, &stateManager.GetElementDataStore() };
            const IntRect rect{ 0, 0, 200, 20 };
            const int2 windowOffset{ 10, 5 };
            // Window-local = 2 * local + (100, 50); screen = window-local + offset.
            const float3x2 localToWindow = float3x2::Translate(100.0f, 50.0f) * float3x2::Scale(2.0f);
            auto customDrawnFrame = [&]
            {
                const ElementState state = (stateManager.TextEditorFocus().GetFocusedID() == field)
                    ? ElementState::Focused : ElementState::None;
                textId = textSystem->RegisterEditableText(field, bound, TextEditBehavior::None, style, state, modified);
                CustomDrawContext ctx{ graphicsContext.Renderer(), storage, ElementRef::Invalid(), rect, rect,
                                       IntRect{ 0, 0, 2000, 2000 }, localToWindow, windowOffset, textSystem.get() };
                ctx.RenderText(textId, rect);
            };
            customDrawnFrame();
            Focus(field);
            customDrawnFrame();

            // Local x = 1 (before 'a') is drawn at screen x = 10 + 100 + 2.
            const int2 click{ 112, 5 + 50 + 2 * 10 };
            textSystem->OnMouseButton(ID::Invalid(), click, MouseButtons::Left, KeyModifier::None, MouseAction::MouseDown);
            textSystem->OnMouseButton(ID::Invalid(), click, MouseButtons::Left, KeyModifier::None, MouseAction::MouseUp);
            Type("Z");
            textSystem->OnKeyboard(ID::Invalid(), KeyCode::Return, 0, KeyModifier::None, KeyAction::KeyDown);
            customDrawnFrame();

            EXPECT_EQ(bound, "Zabcdefgh");
        }

        // The IME candidate window is placed at the caret as drawn on screen.
        TEST_F(SkribidiEditorTest, CaretScreenRectFollowsZoomAndPan)
        {
            const IntRect rect{ 0, 0, 200, 20 };
            // Drawn at 2x, shifted by (100, 50): screen = 2 * layout + (100, 50).
            const float3x2 layoutToScreen = float3x2::Translate(100.0f, 50.0f) * float3x2::Scale(2.0f);
            auto zoomedFrame = [&]
            {
                const ElementState state = (stateManager.TextEditorFocus().GetFocusedID() == field)
                    ? ElementState::Focused : ElementState::None;
                textId = textSystem->RegisterEditableText(field, bound, TextEditBehavior::None, style, state, modified);
                textSystem->RenderText(textId, rect, rect, IntRect{ 0, 0, 2000, 2000 }, 2.0f, layoutToScreen.Inverse());
            };
            EXPECT_FALSE(textSystem->GetCaretScreenRect().has_value()) << "no editor is active yet";
            zoomedFrame();
            Focus(field);
            zoomedFrame();

            const auto caret = textSystem->GetCaretScreenRect();
            ASSERT_TRUE(caret.has_value());
            EXPECT_NEAR(caret->x, 100, 2) << "caret at the field's start, drawn at x = 100";
            EXPECT_NEAR(caret->y, 50, 6) << "the field's top is drawn at y = 50 (ascenders may reach a little above it)";
            EXPECT_GT(caret->h, 20) << "the caret height is zoomed too";
        }

        // A single-line field taller than its text keeps the text vertically centred
        // while it is being edited: drawn, caret and clicks all in the same place.
        TEST_F(SkribidiEditorTest, CentredFieldStaysCentredWhileEditing)
        {
            Style s{};
            s.borderWidths = BoxEdges(0);
            s.padding      = BoxEdges(0);
            s.margin       = BoxEdges(0);
            s.textLayoutStyle.textWrapping      = TextWrap::NoWrap;
            s.textLayoutStyle.verticalAlignment = TextAlign::Center;
            for (int i = 0; i < Style::kStateCount; ++i)
                s.states[i].textStyle.fontSize = 14.0f;
            style = styleManager.Register(s);

            bound = "abcdefgh";
            const IntRect rect{ 0, 0, 200, 60 };
            auto tallFrame = [&]
            {
                const ElementState state = (stateManager.TextEditorFocus().GetFocusedID() == field)
                    ? ElementState::Focused : ElementState::None;
                textId = textSystem->RegisterEditableText(field, bound, TextEditBehavior::None, style, state, modified);
                graphicsContext.Renderer().glyphTop = 1e9f;
                textSystem->RenderText(textId, rect, rect, IntRect{ 0, 0, 2000, 2000 });
                return graphicsContext.Renderer().glyphTop;
            };
            const float staticTop = tallFrame();
            ASSERT_GT(staticTop, 10.0f) << "the unfocused text is centred in the 60px box";
            Focus(field);
            const float editingTop = tallFrame();
            EXPECT_NEAR(editingTop, staticTop, 1.0f) << "editing doesn't move the text";

            const auto caret = textSystem->GetCaretScreenRect();
            ASSERT_TRUE(caret.has_value());
            EXPECT_NEAR(caret->y + caret->h / 2, 30, 4) << "the caret is centred with the text";

            // A click on the text's line, near the end, puts the caret there.
            const int2 click{ 199, 30 };
            textSystem->OnMouseButton(ID::Invalid(), click, MouseButtons::Left, KeyModifier::None, MouseAction::MouseDown);
            textSystem->OnMouseButton(ID::Invalid(), click, MouseButtons::Left, KeyModifier::None, MouseAction::MouseUp);
            Type("Z");
            textSystem->OnKeyboard(ID::Invalid(), KeyCode::Return, 0, KeyModifier::None, KeyAction::KeyDown);
            tallFrame();
            EXPECT_EQ(bound, "abcdefghZ");
        }

        // IME: composition text is drawn at the caret but isn't part of the field's
        // text until the IME commits it (as ordinary text input).
        TEST_F(SkribidiEditorTest, CompositionIsShownButNotCommitted)
        {
            auto& renderer = graphicsContext.Renderer();
            Frame(200);
            Focus(field);
            Frame(200);
            renderer.glyphRight = 0.0f;
            Frame(200);
            const float plain = renderer.glyphRight;

            textSystem->OnTextComposition(ID::Invalid(), "WWW", 3);
            renderer.glyphRight = 0.0f;
            Frame(200);
            EXPECT_GT(renderer.glyphRight, plain + 10.0f) << "the composition isn't drawn";

            textSystem->OnKeyboard(ID::Invalid(), KeyCode::Return, 0, KeyModifier::None, KeyAction::KeyDown);
            Frame(200);
            EXPECT_EQ(bound, "abc") << "composition text leaked into the field";
        }

        TEST_F(SkribidiEditorTest, CommittedCompositionReplacesThePreview)
        {
            Frame(200);
            Focus(field);
            Frame(200);

            textSystem->OnTextComposition(ID::Invalid(), "ka", 2);
            Frame(200);
            textSystem->OnTextComposition(ID::Invalid(), "", 0);
            Type("K");
            textSystem->OnKeyboard(ID::Invalid(), KeyCode::Return, 0, KeyModifier::None, KeyAction::KeyDown);
            Frame(200);

            EXPECT_EQ(bound, "Kabc"); // the caret starts at the front
        }

        // Text input arriving while composing (some IMEs commit before clearing)
        // replaces the preview rather than landing next to it.
        TEST_F(SkribidiEditorTest, TextInputWhileComposingDropsThePreview)
        {
            auto& renderer = graphicsContext.Renderer();
            Frame(200);
            Focus(field);
            Frame(200);

            textSystem->OnTextComposition(ID::Invalid(), "ka", 2);
            Type("K");
            renderer.glyphRight = 0.0f;
            Frame(200);
            const float drawn = renderer.glyphRight;
            textSystem->OnKeyboard(ID::Invalid(), KeyCode::Return, 0, KeyModifier::None, KeyAction::KeyDown);
            Frame(200);
            EXPECT_EQ(bound, "Kabc"); // the caret starts at the front

            Focus(field);
            Frame(200);
            renderer.glyphRight = 0.0f;
            Frame(200);
            EXPECT_NEAR(drawn, renderer.glyphRight, 1.5f) << "the preview was still drawn after the commit";
        }

        TEST_F(SkribidiEditorTest, EnterCommitsTyping)
        {
            Frame(200);
            Focus(field);
            Frame(200);
            Type("XY");

            textSystem->OnKeyboard(ID::Invalid(), KeyCode::Return, 0, KeyModifier::None, KeyAction::KeyDown);
            Frame(200);

            EXPECT_TRUE(modified);
            EXPECT_EQ(bound.size(), 5u) << bound;
        }

        // Resizing the field (window or splitter drag) while typing must not
        // throw away the uncommitted text.
        TEST_F(SkribidiEditorTest, WidthChangeKeepsUncommittedTyping)
        {
            Frame(200);
            Focus(field);
            Frame(200);
            Type("XY");

            Frame(260);   // the field got wider
            textSystem->OnKeyboard(ID::Invalid(), KeyCode::Return, 0, KeyModifier::None, KeyAction::KeyDown);
            Frame(260);

            EXPECT_TRUE(modified);
            EXPECT_EQ(bound.size(), 5u) << bound;
            EXPECT_NE(bound.find("XY"), std::string::npos) << bound;
        }

        // Shift+Backspace deletes like Backspace (users hit it mid-capital).
        TEST_F(SkribidiEditorTest, ShiftBackspaceDeletesLikeBackspace)
        {
            Frame(200);
            Focus(field);
            Frame(200);
            Type("XY");

            textSystem->OnKeyboard(ID::Invalid(), KeyCode::Backspace, 0, KeyModifier::Shift, KeyAction::KeyDown);
            textSystem->OnKeyboard(ID::Invalid(), KeyCode::Return, 0, KeyModifier::None, KeyAction::KeyDown);
            Frame(200);

            EXPECT_EQ(bound.size(), 4u) << bound;
            EXPECT_EQ(bound.find("XY"), std::string::npos) << bound;
            EXPECT_NE(bound.find('X'), std::string::npos) << bound;
        }

        // Clicking elsewhere commits the typed text; the field must report it.
        TEST_F(SkribidiEditorTest, BlurReportsTheCommittedChange)
        {
            Frame(200);
            Focus(field);
            Frame(200);
            Type("XY");

            Focus(ID::Invalid());
            Frame(200);

            EXPECT_NE(bound.find("XY"), std::string::npos) << bound;
            EXPECT_TRUE(modified) << "the bound text changed, so the field must report it";
        }

        // A numeric field left by clicking elsewhere keeps (and reports) the typed value.
        TEST_F(SkribidiEditorTest, NumericBlurCommitsTheValue)
        {
            NumericValue value{};
            value.value_int32 = 1;
            auto numericFrame = [&] {
                modified = false;
                const ElementState state = (stateManager.TextEditorFocus().GetFocusedID() == field)
                    ? ElementState::Focused : ElementState::None;
                textId = textSystem->RegisterEditableNumber(field, NumericType::TypeInt32, value,
                                                            TextEditBehavior::None, style, state, modified);
                const IntRect rect{ 0, 0, 200, 20 };
                textSystem->RenderText(textId, rect, rect, IntRect{ 0, 0, 2000, 2000 });
            };

            numericFrame();
            Focus(field);
            numericFrame();
            Type("2");

            Focus(ID::Invalid());
            numericFrame();

            EXPECT_TRUE(modified);
            EXPECT_TRUE(value.value_int32 == 21 || value.value_int32 == 12) << value.value_int32;
        }

        // A field using a registered font (TextStyle::fontId) must keep that font
        // when it gains focus and switches to the live editor.
        TEST_F(SkribidiEditorTest, FocusedEditorKeepsTheFieldsFont)
        {
            const std::string_view faces[] = { "fonts/Noto-otf/NotoSerifDisplay-Regular.otf" };
            const FontID serif = textSystem->RegisterFontFamily("Noto Serif Display", faces);
            ASSERT_NE(serif, FontID::Invalid);
            Style s{};
            s.borderWidths = BoxEdges(0);
            s.padding      = BoxEdges(0);
            s.margin       = BoxEdges(0);
            for (int i = 0; i < Style::kStateCount; ++i)
            {
                s.states[i].textStyle.fontSize = 14.0f;
                s.states[i].textStyle.fontId   = serif;
            }
            style = styleManager.Register(s);
            bound = "WWWWWWWW mmmmmmmm iiiiiiii";

            auto& renderer = graphicsContext.Renderer();
            renderer.glyphRight = 0.0f;
            Frame(400);
            const float unfocused = renderer.glyphRight;
            ASSERT_GT(unfocused, 0.0f);

            Focus(field);
            renderer.glyphRight = 0.0f;
            Frame(400);
            EXPECT_NEAR(renderer.glyphRight, unfocused, 1.5f) << "the focused editor fell back to another font";
        }

        // Typing exponent notation into a float field gives that value.
        TEST_F(SkribidiEditorTest, FloatFieldAcceptsTypedExponent)
        {
            NumericValue value{};
            value.value_float = 0.0f;
            auto numericFrame = [&] {
                modified = false;
                const ElementState state = (stateManager.TextEditorFocus().GetFocusedID() == field)
                    ? ElementState::Focused : ElementState::None;
                textId = textSystem->RegisterEditableNumber(field, NumericType::TypeFloat, value,
                                                            TextEditBehavior::None, style, state, modified);
                const IntRect rect{ 0, 0, 200, 20 };
                textSystem->RenderText(textId, rect, rect, IntRect{ 0, 0, 2000, 2000 });
            };

            numericFrame();
            Focus(field);
            numericFrame();
            // Replace the "0" with typed text.
            textSystem->OnKeyboard(ID::Invalid(), KeyCode::Backspace, 0, KeyModifier::None, KeyAction::KeyDown);
            textSystem->OnKeyboard(ID::Invalid(), KeyCode::Delete, 0, KeyModifier::None, KeyAction::KeyDown);
            Type("1e5");
            textSystem->OnKeyboard(ID::Invalid(), KeyCode::Return, 0, KeyModifier::None, KeyAction::KeyDown);
            numericFrame();

            EXPECT_FLOAT_EQ(value.value_float, 100000.0f);
        }

        // A field displaying exponent notation stays editable: typing inside
        // "1e+10" must not be rejected.
        TEST_F(SkribidiEditorTest, FloatFieldShowingAnExponentIsEditable)
        {
            NumericValue value{};
            value.value_double = 1e10;
            auto numericFrame = [&] {
                modified = false;
                const ElementState state = (stateManager.TextEditorFocus().GetFocusedID() == field)
                    ? ElementState::Focused : ElementState::None;
                textId = textSystem->RegisterEditableNumber(field, NumericType::TypeDouble, value,
                                                            TextEditBehavior::None, style, state, modified);
                const IntRect rect{ 0, 0, 200, 20 };
                textSystem->RenderText(textId, rect, rect, IntRect{ 0, 0, 2000, 2000 });
            };

            numericFrame();
            Focus(field);
            numericFrame();
            textSystem->OnKeyboard(ID::Invalid(), KeyCode::Home, 0, KeyModifier::None, KeyAction::KeyDown);
            Type("2");
            textSystem->OnKeyboard(ID::Invalid(), KeyCode::Return, 0, KeyModifier::None, KeyAction::KeyDown);
            numericFrame();

            EXPECT_DOUBLE_EQ(value.value_double, 2.1e11);
        }

        // A theme reload clears the text cache; a field that stays focused across
        // it must keep taking input.
        TEST_F(SkribidiEditorTest, FocusedFieldKeepsWorkingAfterClearCache)
        {
            // Earlier texts give the field a high text ID; after the clear it
            // restarts low, as in the app after a session of registrations.
            for (uint64_t i = 1; i <= 5; ++i)
                (void)textSystem->RegisterText(ID{ .value{ .integer = 2000 + i } }, IconID::Invalid,
                                               "label", style, ElementState::None);
            Frame(200);
            Focus(field);
            Frame(200);

            textSystem->ClearCache();
            Frame(200);
            Type("XY");
            textSystem->OnKeyboard(ID::Invalid(), KeyCode::Return, 0, KeyModifier::None, KeyAction::KeyDown);
            Frame(200);

            EXPECT_NE(bound.find("XY"), std::string::npos) << bound;
        }

        // A list reordered under a focused row: the rows keep their IDs (by position), so
        // when focus moves on, the old row's editor must not write its text over the item
        // that now sits there.
        TEST_F(SkribidiEditorTest, AnUneditedFieldDoesNotCommitOnBlur)
        {
            const ID rows[3] = { ID{ .value{ .integer = 910 } }, ID{ .value{ .integer = 911 } }, ID{ .value{ .integer = 912 } } };
            std::string items[3] = { "a", "b", "c" };
            auto listFrame = [&]
            {
                for (int i = 0; i < 3; i++)
                {
                    bool changed = false;
                    const ElementState state = (stateManager.TextEditorFocus().GetFocusedID() == rows[i])
                        ? ElementState::Focused : ElementState::None;
                    const TextID id = textSystem->RegisterEditableText(rows[i], items[i], TextEditBehavior::None, style, state, changed);
                    const IntRect rect{ 0, 20 * i, 200, 20 };
                    textSystem->RenderText(id, rect, rect, IntRect{ 0, 0, 2000, 2000 });
                }
            };
            listFrame();
            Focus(rows[2]);
            listFrame();
            listFrame();

            // The last item moves to the front; focus goes to the row it landed in.
            items[0] = "c"; items[1] = "a"; items[2] = "b";
            Focus(rows[0]);
            listFrame();
            listFrame();

            EXPECT_EQ(items[0], "c");
            EXPECT_EQ(items[1], "a");
            EXPECT_EQ(items[2], "b") << "the blurred row wrote its old text over the moved item";
        }

        // The same for numbers.
        TEST_F(SkribidiEditorTest, AnUneditedNumberDoesNotCommitOnBlur)
        {
            const ID rows[3] = { ID{ .value{ .integer = 920 } }, ID{ .value{ .integer = 921 } }, ID{ .value{ .integer = 922 } } };
            float items[3] = { 1.0f, 2.0f, 3.0f };
            auto listFrame = [&]
            {
                for (int i = 0; i < 3; i++)
                {
                    bool changed = false;
                    const ElementState state = (stateManager.TextEditorFocus().GetFocusedID() == rows[i])
                        ? ElementState::Focused : ElementState::None;
                    NumericValue v{};
                    const NumericType type = CopyNumericTypeTo(items[i], v);
                    const TextID id = textSystem->RegisterEditableNumber(rows[i], type, v, TextEditBehavior::None, style, state, changed);
                    if (changed) CopyNumericTypeTo(type, v, items[i]);
                    const IntRect rect{ 0, 20 * i, 200, 20 };
                    textSystem->RenderText(id, rect, rect, IntRect{ 0, 0, 2000, 2000 });
                }
            };
            listFrame();
            Focus(rows[2]);
            listFrame();
            listFrame();

            items[0] = 3.0f; items[1] = 1.0f; items[2] = 2.0f;
            Focus(rows[0]);
            listFrame();
            listFrame();

            EXPECT_EQ(items[0], 3.0f);
            EXPECT_EQ(items[1], 1.0f);
            EXPECT_EQ(items[2], 2.0f) << "the blurred row wrote its old number over the moved item";
        }
    }
}
