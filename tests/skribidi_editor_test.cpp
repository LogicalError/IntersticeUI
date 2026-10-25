// Real-SkribidiTextSystem editor tests: focus a field, type into the live
// editor, and check what reaches the bound string. Rendering goes to a stub
// renderer (see skribidi_test_fixture.h).

#include "skribidi_test_fixture.h"

#include <gui_core/elements/gui_element_storage.h>
#include <gui_core/graphics/gui_custom_draw_context.h>
#include <gui_core/input/gui_undo_history.h>

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
            UndoHistory history;

            void SetUp() override
            {
                SkribidiHeadlessTest::SetUp();
                style = MakeTextStyle();
                textSystem->SetUndoHistory(&history);
            }

            void Key(KeyCode key, KeyModifier modifiers = KeyModifier::None)
            {
                textSystem->OnKeyboard(ID::Invalid(), key, 0, modifiers, KeyAction::KeyDown);
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
                auto& focus = stateManager.TextEditorFocus();
                focus.Set(id);
                textSystem->SwitchTextEditor(focus.GetPrevFocusedID(), id);
                focus.Commit();
            }

            // What the input manager does after dispatch when something moved the text focus.
            void SettleFocus()
            {
                auto& focus = stateManager.TextEditorFocus();
                if (focus.GetPrevFocusedID() != focus.GetFocusedID())
                {
                    textSystem->SwitchTextEditor(focus.GetPrevFocusedID(), focus.GetFocusedID());
                    if (focus.GetFocusedID() != ID::Invalid())
                        textSystem->RequestSelectAll(focus.GetFocusedID());
                }
                focus.Commit();
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
            int32_t value = 1;
            auto numericFrame = [&] {
                modified = false;
                const ElementState state = (stateManager.TextEditorFocus().GetFocusedID() == field)
                    ? ElementState::Focused : ElementState::None;
                textId = textSystem->RegisterEditableValue(field, value,
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
            EXPECT_TRUE(value == 21 || value == 12) << value;
        }

        // A whole-number field takes digits only: the letter and the dot aren't typed.
        TEST_F(SkribidiEditorTest, IntFieldRejectsLettersAndDots)
        {
            int32_t value = 1;
            auto numericFrame = [&] {
                modified = false;
                const ElementState state = (stateManager.TextEditorFocus().GetFocusedID() == field)
                    ? ElementState::Focused : ElementState::None;
                textId = textSystem->RegisterEditableValue(field, value,
                                                            TextEditBehavior::None, style, state, modified);
                const IntRect rect{ 0, 0, 200, 20 };
                textSystem->RenderText(textId, rect, rect, IntRect{ 0, 0, 2000, 2000 });
            };

            numericFrame();
            Focus(field);
            numericFrame();
            Key(KeyCode::End);
            Type("x.2");
            Key(KeyCode::Return);
            numericFrame();

            EXPECT_EQ(value, 12);
        }

        // A double field takes a typed decimal point (the key filter let it through for
        // floats only).
        TEST_F(SkribidiEditorTest, DoubleFieldTakesATypedDot)
        {
            double value = 1.0;
            auto numericFrame = [&] {
                modified = false;
                const ElementState state = (stateManager.TextEditorFocus().GetFocusedID() == field)
                    ? ElementState::Focused : ElementState::None;
                textId = textSystem->RegisterEditableValue(field, value, TextEditBehavior::None, style, state, modified);
                const IntRect rect{ 0, 0, 200, 20 };
                textSystem->RenderText(textId, rect, rect, IntRect{ 0, 0, 2000, 2000 });
            };

            numericFrame();
            Focus(field);
            numericFrame();
            Key(KeyCode::End);
            Type(".5");
            Key(KeyCode::Return);
            numericFrame();

            EXPECT_DOUBLE_EQ(value, 1.5);
        }

        // A field for a type of the app's own, through a converter object: it formats and
        // parses the value, and its validator limits what can be typed.
        struct Version { int major = 0, minor = 0; };
        struct VersionConverter : ITextValidator
        {
            std::string Format(const Version& v, bool /*editing*/) const { return std::to_string(v.major) + "." + std::to_string(v.minor); }
            bool Parse(std::string_view text, Version& v) const
            {
                const size_t dot = text.find('.');
                if (dot == std::string_view::npos || dot == 0 || dot + 1 == text.size())
                    return false;
                v = { std::stoi(std::string(text.substr(0, dot))), std::stoi(std::string(text.substr(dot + 1))) };
                return true;
            }
            bool AcceptsCharacter(uint32_t c) const override { return (c >= '0' && c <= '9') || c == '.'; }
        };

        TEST_F(SkribidiEditorTest, AConverterEditsAnyType)
        {
            static const VersionConverter converter;
            Version value{ 1, 2 };
            auto versionFrame = [&] {
                modified = false;
                const ElementState state = (stateManager.TextEditorFocus().GetFocusedID() == field)
                    ? ElementState::Focused : ElementState::None;
                textId = textSystem->RegisterEditableValue(field, value, TextEditBehavior::None, style, state, modified, converter);
                const IntRect rect{ 0, 0, 200, 20 };
                textSystem->RenderText(textId, rect, rect, IntRect{ 0, 0, 2000, 2000 });
            };

            versionFrame();
            EXPECT_EQ(textSystem->GetText(textId), "1.2");
            Focus(field);
            versionFrame();
            Key(KeyCode::End);
            Type("x3");   // the letter isn't taken
            Key(KeyCode::Return);
            versionFrame();

            EXPECT_TRUE(modified);
            EXPECT_EQ(value.major, 1);
            EXPECT_EQ(value.minor, 23);
        }

        // Text that doesn't parse leaves the value as it was, and isn't reported.
        TEST_F(SkribidiEditorTest, AnEditThatDoesNotParseKeepsTheValue)
        {
            static const VersionConverter converter;
            Version value{ 1, 2 };
            auto versionFrame = [&] {
                modified = false;
                const ElementState state = (stateManager.TextEditorFocus().GetFocusedID() == field)
                    ? ElementState::Focused : ElementState::None;
                textId = textSystem->RegisterEditableValue(field, value, TextEditBehavior::None, style, state, modified, converter);
                const IntRect rect{ 0, 0, 200, 20 };
                textSystem->RenderText(textId, rect, rect, IntRect{ 0, 0, 2000, 2000 });
            };

            versionFrame();
            Focus(field);
            versionFrame();
            Key(KeyCode::End);
            Key(KeyCode::Backspace);   // "1."
            Key(KeyCode::Return);
            versionFrame();

            EXPECT_FALSE(modified);
            EXPECT_EQ(value.major, 1);
            EXPECT_EQ(value.minor, 2);
        }

        // Number fields go through the app's undo history like text fields.
        TEST_F(SkribidiEditorTest, UndoRestoresANumber)
        {
            int32_t value = 5;
            auto numericFrame = [&] {
                modified = false;
                const ElementState state = (stateManager.TextEditorFocus().GetFocusedID() == field)
                    ? ElementState::Focused : ElementState::None;
                textId = textSystem->RegisterEditableValue(field, value, TextEditBehavior::None, style, state, modified);
                const IntRect rect{ 0, 0, 200, 20 };
                textSystem->RenderText(textId, rect, rect, IntRect{ 0, 0, 2000, 2000 });
            };

            numericFrame();
            Focus(field);
            numericFrame();
            Key(KeyCode::End);
            Type("7");
            Focus(ID::Invalid());
            numericFrame();
            ASSERT_EQ(value, 57);

            history.Undo();
            numericFrame();
            EXPECT_TRUE(modified);
            EXPECT_EQ(value, 5);
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
            float value = 0.0f;
            auto numericFrame = [&] {
                modified = false;
                const ElementState state = (stateManager.TextEditorFocus().GetFocusedID() == field)
                    ? ElementState::Focused : ElementState::None;
                textId = textSystem->RegisterEditableValue(field, value,
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

            EXPECT_FLOAT_EQ(value, 100000.0f);
        }

        // A field displaying exponent notation stays editable: typing inside
        // "1e+10" must not be rejected.
        TEST_F(SkribidiEditorTest, FloatFieldShowingAnExponentIsEditable)
        {
            double value = 1e10;
            auto numericFrame = [&] {
                modified = false;
                const ElementState state = (stateManager.TextEditorFocus().GetFocusedID() == field)
                    ? ElementState::Focused : ElementState::None;
                textId = textSystem->RegisterEditableValue(field, value,
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

            EXPECT_DOUBLE_EQ(value, 2.1e11);
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

        // A selection in a focused field shows in the focused selection color; with the
        // app in the background (no active window) in the unfocused one
        // (:not(:focus)::selection), so it's drawn without the Focused state.
        TEST_F(SkribidiEditorTest, TheSelectionDimsWithTheAppInTheBackground)
        {
            auto& renderer = graphicsContext.Renderer();
            Frame(200);
            Focus(field);
            textSystem->RequestSelectAll(field);
            Frame(200);
            auto drawnState = [&] { return uint32_t(renderer.lastBoxStyle % uint32_t(Style::kStateCount)); };

            stateManager.Interaction().SetActiveWindowID(ID{ .value{ .integer = 77 } });
            renderer.lastBoxStyle = 0;
            Frame(200);
            ASSERT_NE(renderer.lastBoxStyle, 0u) << "test setup: the selection was drawn";
            EXPECT_NE(drawnState() & uint32_t(ElementState::Focused), 0u);

            stateManager.Interaction().SetActiveWindowID(ID::Invalid());
            renderer.lastBoxStyle = 0;
            Frame(200);
            ASSERT_NE(renderer.lastBoxStyle, 0u);
            EXPECT_EQ(drawnState() & uint32_t(ElementState::Focused), 0u);
            EXPECT_NE(drawnState() & uint32_t(ElementState::Selection), 0u);
        }

        // A selection over several lines shows each line's break: every selected line but
        // the last reaches a little (its break's 4 px) past its last glyph.
        TEST_F(SkribidiEditorTest, ASelectedLineBreakShows)
        {
            auto& renderer = graphicsContext.Renderer();
            bound = "ab\nab";
            auto multiline = [&]
            {
                modified = false;
                const ElementState state = (stateManager.TextEditorFocus().GetFocusedID() == field)
                    ? ElementState::Focused : ElementState::None;
                textId = textSystem->RegisterEditableText(field, bound, TextEditBehavior::Multiline, style, state, modified);
                const IntRect rect{ 0, 0, 200, 60 };
                textSystem->RenderText(textId, rect, rect, IntRect{ 0, 0, 2000, 2000 });
            };
            multiline();
            Focus(field);
            textSystem->RequestSelectAll(field);
            multiline();
            renderer.boxes.clear();
            multiline();
            ASSERT_EQ(renderer.boxes.size(), 2u) << "a selection box per line";
            const IntRect first = renderer.boxes[0].y < renderer.boxes[1].y ? renderer.boxes[0] : renderer.boxes[1];
            const IntRect last  = renderer.boxes[0].y < renderer.boxes[1].y ? renderer.boxes[1] : renderer.boxes[0];
            EXPECT_GT(first.w, last.w) << "the first line's break is selected too";
            EXPECT_LE(first.w, last.w + 8) << "by about a space's width, not more";
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
                    const TextID id = textSystem->RegisterEditableValue(rows[i], items[i], TextEditBehavior::None, style, state, changed);
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

        // A committed edit is a step in the app's history: undo gives the field its old text
        // back, redo the new.
        TEST_F(SkribidiEditorTest, ACommittedEditIsAnUndoStep)
        {
            Frame(200);
            Focus(field);
            Frame(200);
            Type("XY");
            Key(KeyCode::Return);
            Frame(200);
            const std::string edited = bound;
            ASSERT_NE(edited, "abc");
            ASSERT_EQ(history.UndoCount(), 1u);

            Focus(ID::Invalid());
            Frame(200);
            history.Undo();
            Frame(200);
            EXPECT_EQ(bound, "abc");
            EXPECT_TRUE(modified) << "the undo changed the bound text, so the field must report it";

            history.Redo();
            Frame(200);
            EXPECT_EQ(bound, edited);
        }

        // An unchanged commit (Enter without typing, or a blur) adds no step.
        TEST_F(SkribidiEditorTest, AnUnchangedCommitIsNoUndoStep)
        {
            Frame(200);
            Focus(field);
            Frame(200);
            Key(KeyCode::Return);
            Frame(200);
            Focus(ID::Invalid());
            Frame(200);
            EXPECT_FALSE(history.CanUndo());
        }

        // The commits in one field while it keeps the focus are one step; after it loses
        // the focus, the next edit is a step of its own.
        TEST_F(SkribidiEditorTest, EditsMergeWhileTheFieldKeepsTheFocus)
        {
            Frame(200);
            Focus(field);
            Frame(200);
            Type("X");
            Key(KeyCode::Return);
            Frame(200);
            Type("Y");
            Key(KeyCode::Return);
            Frame(200);
            EXPECT_EQ(history.UndoCount(), 1u);
            const std::string firstVisit = bound;

            Focus(ID::Invalid());
            Frame(200);
            Focus(field);
            Frame(200);
            Type("Z");
            Focus(ID::Invalid());   // committed by the blur
            Frame(200);
            EXPECT_EQ(history.UndoCount(), 2u);

            history.Undo();
            Frame(200);
            EXPECT_EQ(bound, firstVisit);
            history.Undo();
            Frame(200);
            EXPECT_EQ(bound, "abc");
        }

        // Undo while the field is focused replaces the editor's text too, so the field
        // doesn't show (or commit back) the undone text.
        TEST_F(SkribidiEditorTest, UndoReachesTheFocusedField)
        {
            Frame(200);
            Focus(field);
            Frame(200);
            Type("XY");
            Key(KeyCode::Return);
            Frame(200);
            ASSERT_NE(bound, "abc");

            history.Undo();
            Frame(200);
            Frame(200);
            EXPECT_EQ(bound, "abc");

            Focus(ID::Invalid());   // the blur must not write the undone text back
            Frame(200);
            EXPECT_EQ(bound, "abc");
            EXPECT_EQ(history.UndoCount(), 0u);
            EXPECT_EQ(history.RedoCount(), 1u) << "the blur pushed a step and dropped the redo";
        }

        // Two fields, A over B: drag A's selected text into B.
        class SkribidiTextMoveTest : public SkribidiEditorTest
        {
        protected:
            const ID    fieldA = ID{ .value{ .integer = 930 } };
            const ID    fieldB = ID{ .value{ .integer = 931 } };
            std::string textA = "hello";
            std::string textB = "world";

            void TwoFields()
            {
                const ID ids[2] = { fieldA, fieldB };
                std::string* texts[2] = { &textA, &textB };
                for (int i = 0; i < 2; i++)
                {
                    bool changed = false;
                    const ElementState state = (stateManager.TextEditorFocus().GetFocusedID() == ids[i])
                        ? ElementState::Focused : ElementState::None;
                    const TextID id = textSystem->RegisterEditableText(ids[i], *texts[i], TextEditBehavior::None, style, state, changed);
                    const IntRect rect{ 0, 30 * i, 200, 20 };
                    textSystem->RenderText(id, rect, rect, IntRect{ 0, 0, 2000, 2000 });
                }
            }

            // Selects all of A and drops it at the end of B.
            void DragAIntoB()
            {
                TwoFields();
                Focus(fieldA);
                TwoFields();
                Key(KeyCode::KeyA, KeyModifier::Control);
                TwoFields();

                const int2 onSelection{ 8, 10 }, overB{ 190, 40 };
                textSystem->OnMouseButton(ID::Invalid(), onSelection, MouseButtons::Left, KeyModifier::None, MouseAction::MouseDown);
                textSystem->OnMouseMove(ID::Invalid(), int2{ 8, 30 }, MouseButtons::Left, KeyModifier::None);
                textSystem->OnMouseMove(ID::Invalid(), overB, MouseButtons::Left, KeyModifier::None);
                textSystem->OnMouseButton(ID::Invalid(), overB, MouseButtons::Left, KeyModifier::None, MouseAction::MouseUp);
                TwoFields();
                TwoFields();
            }
        };

        TEST_F(SkribidiTextMoveTest, DraggingTextMovesItIntoTheOtherField)
        {
            DragAIntoB();
            EXPECT_EQ(textA, "");
            EXPECT_EQ(textB, "worldhello");
            EXPECT_EQ(stateManager.TextEditorFocus().GetFocusedID(), fieldB) << "the target takes the focus";
            EXPECT_EQ(history.UndoCount(), 1u) << "the move is one step";
            EXPECT_EQ(history.UndoDescription(), "Move text");
        }

        // #1: undo after the move restored only the target.
        TEST_F(SkribidiTextMoveTest, UndoingAMoveRestoresBothFields)
        {
            DragAIntoB();
            ASSERT_EQ(textB, "worldhello");

            history.Undo();
            TwoFields();
            TwoFields();
            EXPECT_EQ(textA, "hello");
            EXPECT_EQ(textB, "world");

            history.Redo();
            TwoFields();
            TwoFields();
            EXPECT_EQ(textA, "");
            EXPECT_EQ(textB, "worldhello");
        }

        // Ctrl+Z in the target right after the drop undoes the whole move: the paste isn't
        // a step in the target's own typing history.
        TEST_F(SkribidiTextMoveTest, CtrlZInTheTargetUndoesTheMove)
        {
            DragAIntoB();
            ASSERT_EQ(textB, "worldhello");

            Key(KeyCode::KeyZ, KeyModifier::Control);
            SettleFocus();
            TwoFields();
            TwoFields();
            EXPECT_EQ(textA, "hello");
            EXPECT_EQ(textB, "world");

            Key(KeyCode::KeyY, KeyModifier::Control);   // in the source, which has the focus now
            SettleFocus();
            TwoFields();
            TwoFields();
            EXPECT_EQ(textA, "");
            EXPECT_EQ(textB, "worldhello");
        }

        // Undoing the move gives the focus back to the field the text came from; redoing it
        // to the one it went to.
        TEST_F(SkribidiTextMoveTest, UndoingAMoveFocusesTheSource)
        {
            DragAIntoB();
            ASSERT_EQ(stateManager.TextEditorFocus().GetFocusedID(), fieldB);

            Key(KeyCode::KeyZ, KeyModifier::Control);
            EXPECT_EQ(stateManager.TextEditorFocus().GetFocusedID(), fieldA);
            EXPECT_EQ(stateManager.Interaction().GetFocusedID(), fieldA);
            SettleFocus();
            TwoFields();
            TwoFields();
            EXPECT_EQ(textA, "hello");
            EXPECT_EQ(textB, "world");

            // The focused source takes typing, and the undo didn't select all of it.
            Key(KeyCode::End);
            Type("!");
            Key(KeyCode::Return);
            TwoFields();
            EXPECT_EQ(textA, "hello!");
        }

        TEST_F(SkribidiTextMoveTest, RedoingAMoveFocusesTheTarget)
        {
            DragAIntoB();
            history.Undo();
            SettleFocus();
            TwoFields();
            ASSERT_EQ(stateManager.TextEditorFocus().GetFocusedID(), fieldA);

            history.Redo();
            EXPECT_EQ(stateManager.TextEditorFocus().GetFocusedID(), fieldB);
            SettleFocus();
            TwoFields();
            TwoFields();
            EXPECT_EQ(textA, "");
            EXPECT_EQ(textB, "worldhello");
        }

        // Undoing an edit after leaving the field focuses it again.
        TEST_F(SkribidiEditorTest, UndoFocusesTheEditedField)
        {
            Frame(200);
            Focus(field);
            Frame(200);
            Type("XY");
            Focus(ID::Invalid());
            Frame(200);
            ASSERT_NE(bound, "abc");

            history.Undo();
            EXPECT_EQ(stateManager.TextEditorFocus().GetFocusedID(), field);
            SettleFocus();
            Frame(200);
            Frame(200);
            EXPECT_EQ(bound, "abc");
            Key(KeyCode::End);
            Type("Q");
            Key(KeyCode::Return);
            Frame(200);
            EXPECT_EQ(bound, "abcQ") << "the refocused field took the typing, without selecting all first";
        }

        // Undoing typing puts the caret where the typing began; redoing it, after the typing.
        TEST_F(SkribidiEditorTest, UndoAndRedoPutTheCaretAtTheChange)
        {
            auto edit = [&] {
                Frame(200);
                Focus(field);
                Frame(200);
                Key(KeyCode::Home);
                Key(KeyCode::Right);
                Type("XY");
                Focus(ID::Invalid());
                Frame(200);
            };
            edit();
            ASSERT_EQ(bound, "aXYbc");

            history.Undo();
            SettleFocus();
            Frame(200);
            Frame(200);
            ASSERT_EQ(bound, "abc");
            history.Redo();
            SettleFocus();
            Frame(200);
            Frame(200);
            ASSERT_EQ(bound, "aXYbc");
            Type("Q");
            Key(KeyCode::Return);
            Frame(200);
            EXPECT_EQ(bound, "aXYQbc") << "redo: the caret goes after the typing";

            bound = "abc";
            history.Clear();
            Focus(ID::Invalid());
            edit();
            history.Undo();
            SettleFocus();
            Frame(200);
            Frame(200);
            Type("Q");
            Key(KeyCode::Return);
            Frame(200);
            EXPECT_EQ(bound, "aQbc") << "undo: the caret goes where the typing began";
        }

        // Typing at both ends in one visit is one step; its undo gives back the caret as it was
        // before the first of it (not a selection of everything in between).
        TEST_F(SkribidiEditorTest, UndoRestoresTheSelectionFromBeforeTheEdit)
        {
            Frame(200);
            Focus(field);
            Frame(200);
            Key(KeyCode::Home);
            Type("X");
            Key(KeyCode::End);
            Type("Y");
            Focus(ID::Invalid());
            Frame(200);
            ASSERT_EQ(bound, "XabcY");
            ASSERT_EQ(history.UndoCount(), 1u);

            history.Undo();
            SettleFocus();
            Frame(200);
            Frame(200);
            ASSERT_EQ(bound, "abc");
            Type("Q");
            Key(KeyCode::Return);
            Frame(200);
            EXPECT_EQ(bound, "Qabc");
        }

        // A selection that was replaced comes back as that selection, direction included:
        // Shift+Right after the undo extends it from the same anchor.
        TEST_F(SkribidiEditorTest, UndoRestoresABackwardSelection)
        {
            bound = "abcd";
            Frame(200);
            Focus(field);
            Frame(200);
            Key(KeyCode::End);
            Key(KeyCode::Left);
            Key(KeyCode::Left, KeyModifier::Shift);
            Key(KeyCode::Left, KeyModifier::Shift);   // "bc" selected, caret before b
            Type("Z");
            Focus(ID::Invalid());
            Frame(200);
            ASSERT_EQ(bound, "aZd");

            history.Undo();
            SettleFocus();
            Frame(200);
            Frame(200);
            ASSERT_EQ(bound, "abcd");
            Key(KeyCode::Right, KeyModifier::Shift);   // caret moves to after b: only "c" stays selected
            Type("Q");
            Key(KeyCode::Return);
            Frame(200);
            EXPECT_EQ(bound, "abQd");
        }

        // Undoing text that replaced a selection selects the text it brings back.
        TEST_F(SkribidiEditorTest, UndoSelectsTheReplacedText)
        {
            Frame(200);
            Focus(field);
            Frame(200);
            Key(KeyCode::KeyA, KeyModifier::Control);
            Type("Z");
            Focus(ID::Invalid());
            Frame(200);
            ASSERT_EQ(bound, "Z");

            history.Undo();
            SettleFocus();
            Frame(200);
            Frame(200);
            ASSERT_EQ(bound, "abc");
            Type("Q");   // replaces the selection
            Key(KeyCode::Return);
            Frame(200);
            EXPECT_EQ(bound, "Q");
        }

        // The caret position counts characters, not UTF-8 bytes.
        TEST_F(SkribidiEditorTest, UndoCaretCountsCharacters)
        {
            bound = "\xC3\xA9\xC3\xA9\xC3\xA9";   // three e-acutes
            Frame(200);
            Focus(field);
            Frame(200);
            Key(KeyCode::Home);
            Key(KeyCode::Right);
            Type("X");
            Focus(ID::Invalid());
            Frame(200);
            ASSERT_EQ(bound, "\xC3\xA9" "X" "\xC3\xA9\xC3\xA9");

            history.Undo();
            SettleFocus();
            Frame(200);
            Frame(200);
            Type("Q");
            Key(KeyCode::Return);
            Frame(200);
            EXPECT_EQ(bound, "\xC3\xA9" "Q" "\xC3\xA9\xC3\xA9");
        }

        // While dragging over another field, the source shows no insertion caret; over its
        // own text (off the selection) it does.
        TEST_F(SkribidiTextMoveTest, TheSourceShowsNoDropCaretWhileDraggingElsewhere)
        {
            TwoFields();
            Focus(fieldA);
            TwoFields();
            Key(KeyCode::KeyA, KeyModifier::Control);
            TwoFields();

            auto& renderer = graphicsContext.Renderer();
            auto linesInA = [&] {
                int n = 0;
                for (const int2& p : renderer.lineStarts)
                    if (p.y >= -10 && p.y < 25) n++;
                return n;
            };
            auto dragFrame = [&](int2 mouse) {
                textSystem->OnMouseMove(ID::Invalid(), mouse, MouseButtons::Left, KeyModifier::None);
                textSystem->BeginFrame(mouse, MouseButtons::Left, 0);
                renderer.lineStarts.clear();
                TwoFields();
            };

            textSystem->OnMouseButton(ID::Invalid(), int2{ 8, 10 }, MouseButtons::Left, KeyModifier::None, MouseAction::MouseDown);
            dragFrame(int2{ 8, 30 });     // past the drag threshold, between the fields
            dragFrame(int2{ 190, 40 });   // over B
            EXPECT_EQ(linesInA(), 0) << "the source drew a drop caret while the mouse is over the other field";

            dragFrame(int2{ 190, 10 });   // over A, right of its (selected) text
            EXPECT_GT(linesInA(), 0) << "test setup: over its own text, the source shows where it would drop";

            textSystem->OnMouseButton(ID::Invalid(), int2{ 190, 10 }, MouseButtons::Left, KeyModifier::None, MouseAction::MouseUp);
        }

        // Undoing a move selects the moved text in the source again; redoing it puts the
        // caret after the dropped text.
        TEST_F(SkribidiTextMoveTest, UndoingAMoveSelectsTheMovedText)
        {
            DragAIntoB();
            Key(KeyCode::KeyZ, KeyModifier::Control);
            SettleFocus();
            TwoFields();
            TwoFields();
            ASSERT_EQ(textA, "hello");
            Type("Q");   // replaces the selection
            Key(KeyCode::Return);
            TwoFields();
            EXPECT_EQ(textA, "Q");
        }

        // A drop selects the dropped text in its new field, so typing replaces it.
        TEST_F(SkribidiTextMoveTest, TheDroppedTextIsSelected)
        {
            textB = "wo";
            DragAIntoB();
            ASSERT_EQ(textB, "wohello");
            Type("Q");
            Key(KeyCode::Return);
            TwoFields();
            EXPECT_EQ(textB, "woQ");
        }

        // The same for a drop inside the field the text came from.
        TEST_F(SkribidiEditorTest, TextMovedWithinAFieldIsSelected)
        {
            bound = "abcdef";
            Frame(200);
            Focus(field);
            Frame(200);
            Key(KeyCode::Home);
            Key(KeyCode::Right, KeyModifier::Shift);
            Key(KeyCode::Right, KeyModifier::Shift);   // "ab"
            Frame(200);

            const int2 onSelection{ 4, 10 }, pastTheEnd{ 190, 10 };
            textSystem->OnMouseButton(ID::Invalid(), onSelection, MouseButtons::Left, KeyModifier::None, MouseAction::MouseDown);
            textSystem->OnMouseMove(ID::Invalid(), int2{ 40, 10 }, MouseButtons::Left, KeyModifier::None);
            textSystem->OnMouseMove(ID::Invalid(), pastTheEnd, MouseButtons::Left, KeyModifier::None);
            textSystem->OnMouseButton(ID::Invalid(), pastTheEnd, MouseButtons::Left, KeyModifier::None, MouseAction::MouseUp);
            Type("Q");
            Key(KeyCode::Return);
            Frame(200);
            EXPECT_EQ(bound, "cdefQ");
        }

        // Redo gives the dropped text back selected, as the drop left it.
        TEST_F(SkribidiTextMoveTest, RedoingAMoveSelectsTheDroppedText)
        {
            textB = "wo";
            DragAIntoB();
            ASSERT_EQ(textB, "wohello");
            history.Undo();
            SettleFocus();
            TwoFields();
            history.Redo();
            SettleFocus();
            TwoFields();
            TwoFields();
            Type("!");
            Key(KeyCode::Return);
            TwoFields();
            EXPECT_EQ(textB, "wo!");
        }

        // Typing in the target after the drop, then leaving it: the typing is a step after
        // the move, and undoing both gets back to the start.
        TEST_F(SkribidiTextMoveTest, TypingAfterTheMoveIsAStepOfItsOwn)
        {
            DragAIntoB();
            Key(KeyCode::End);   // the caret is left at the drop point
            Type("!");
            Focus(ID::Invalid());
            TwoFields();
            EXPECT_EQ(textB, "worldhello!");
            EXPECT_EQ(history.UndoCount(), 2u);

            history.Undo();
            TwoFields();
            EXPECT_EQ(textB, "worldhello");
            EXPECT_EQ(textA, "");
            history.Undo();
            TwoFields();
            EXPECT_EQ(textA, "hello");
            EXPECT_EQ(textB, "world");
        }
    }
}
