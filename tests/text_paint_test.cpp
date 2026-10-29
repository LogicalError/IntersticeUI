// Text is laid out from its font and metrics alone: a state that only changes the colors keeps
// the layout, and decoration lines are drawn in their own colors. Rendering goes to a stub
// renderer (see skribidi_test_fixture.h).

#include "skribidi_test_fixture.h"

#include <algorithm>
#include <vector>

namespace tachyon
{
    using GUITest::SkribidiHeadlessTest;
    using GlyphDraw = GUITest::StubGUIRenderer::GlyphDraw;

    namespace
    {
        constexpr color4b kRed   { 255, 0, 0, 255 };
        constexpr color4b kGreen { 0, 255, 0, 255 };
        constexpr color4b kBlue  { 0, 0, 255, 255 };
        constexpr color4b kWhite { 255, 255, 255, 255 };

        class TextPaintTest : public SkribidiHeadlessTest
        {
        protected:
            const IntRect rect{ 0, 0, 200, 40 };
            const IntRect clip{ 0, 0, 2000, 2000 };
            const ID      element{ .value{ .integer = 81 } };

            // 14px blue text in every state.
            static Style BaseStyle()
            {
                Style s{};
                s.textLayoutStyle.textWrapping = TextWrap::NoWrap;
                for (StyleState& state : s.states)
                {
                    state.textStyle.fontSize = 14.0f;
                    state.textColor = ColorStyle::Create(kBlue);
                }
                return s;
            }

            TextID Register(StyleRef style, ElementState state, std::string_view text = "Paint")
            {
                const TextID id = textSystem->RegisterText(element, IconID::Invalid, text, style, state);
                EXPECT_NE(id, TextID::Invalid);
                return id;
            }

            std::vector<GlyphDraw> Render(TextID id)
            {
                graphicsContext.Renderer().glyphDraws.clear();
                textSystem->RenderText(id, rect, rect, clip);
                return graphicsContext.Renderer().glyphDraws;
            }

            VisualColorStyleIndex ColorIndex(color4b c) { return styleManager.RegisterColorStyle(ColorStyle::Create(c)); }

            static size_t CountColor(const std::vector<GlyphDraw>& draws, VisualColorStyleIndex color)
            {
                return size_t(std::ranges::count_if(draws, [&](const GlyphDraw& d) { return d.color == color; }));
            }
        };

        TEST_F(TextPaintTest, AStateThatOnlyRecolorsKeepsTheLayout)
        {
            Style s = BaseStyle();
            s.states[ElementState::Hover].textColor = ColorStyle::Create(kRed);
            const StyleRef style = styleManager.Register(s);

            const TextID id = Register(style, ElementState::None);
            const TextMeasurement before = textSystem->GetTextMeasurement(id);
            EXPECT_EQ(CountColor(Render(id), ColorIndex(kBlue)), 5u);
            const uint64_t layouts = textSystem->LayoutsMade();
            ASSERT_GT(layouts, 0u);

            EXPECT_EQ(Register(style, ElementState::Hover), id);
            EXPECT_EQ(textSystem->GetTextMeasurement(id).maxWidth, before.maxWidth);
            EXPECT_EQ(CountColor(Render(id), ColorIndex(kRed)), 5u) << "the colors follow the state";
            EXPECT_EQ(textSystem->LayoutsMade(), layouts) << "laid out again for a color";
        }

        // Drawing at another width lays the text out at that width only; its measurement stays.
        TEST_F(TextPaintTest, AnotherWidthKeepsTheMeasurement)
        {
            const TextID id = Register(styleManager.Register(BaseStyle()), ElementState::None);
            const TextMeasurement measured = textSystem->GetTextMeasurement(id);
            Render(id);
            const uint64_t layouts = textSystem->LayoutsMade();
            EXPECT_EQ(textSystem->GetTextMeasurement(id).maxWidth, measured.maxWidth);
            EXPECT_EQ(textSystem->LayoutsMade(), layouts);

            textSystem->RenderText(id, IntRect{ 0, 0, 120, 40 }, IntRect{ 0, 0, 120, 40 }, clip);
            EXPECT_EQ(textSystem->LayoutsMade(), layouts + 1) << "one layout for the new width";
            EXPECT_EQ(textSystem->GetTextMeasurement(id).maxWidth, measured.maxWidth);
            EXPECT_EQ(textSystem->LayoutsMade(), layouts + 1);
        }

        TEST_F(TextPaintTest, AStateWithAnotherFontLaysTheTextOutAgain)
        {
            Style s = BaseStyle();
            s.states[ElementState::Hover].textStyle.fontSize = 28.0f;
            const StyleRef style = styleManager.Register(s);

            const TextID id = Register(style, ElementState::None);
            const int32_t width = textSystem->GetTextMeasurement(id).maxWidth;
            const uint64_t layouts = textSystem->LayoutsMade();

            Register(style, ElementState::Hover);
            EXPECT_GT(textSystem->GetTextMeasurement(id).maxWidth, width);
            EXPECT_GT(textSystem->LayoutsMade(), layouts);
        }

        // TextLayoutStyle comes with the layout style, so two styles with the same text but
        // another alignment still lay the text out again.
        TEST_F(TextPaintTest, AnotherAlignmentLaysTheTextOutAgain)
        {
            Style start = BaseStyle();
            Style end   = BaseStyle();
            end.textLayoutStyle.horizontalAlignment = TextAlign::End;
            const StyleRef startRef = styleManager.Register(start);
            const StyleRef endRef   = styleManager.Register(end);
            ASSERT_EQ(startRef.visualTextStyleIndex, endRef.visualTextStyleIndex);
            ASSERT_NE(startRef.layoutStyleIndex, endRef.layoutStyleIndex);

            const TextID id = Register(startRef, ElementState::None);
            const float startX = Render(id).front().pos.x;
            const uint64_t layouts = textSystem->LayoutsMade();

            Register(endRef, ElementState::None);
            EXPECT_GT(Render(id).front().pos.x, startX + 50.0f) << "drawn at the end of the box";
            EXPECT_GT(textSystem->LayoutsMade(), layouts);
        }

        TEST_F(TextPaintTest, ADecorationIsDrawnInTheTextColorUnlessItHasItsOwn)
        {
            Style s = BaseStyle();
            for (StyleState& state : s.states)
                state.textStyle.underLine.thickness = 2;
            s.states[ElementState::Hover].decorationColors[size_t(DecorationLine::Under)] = ColorStyle::Create(kGreen);
            const StyleRef style = styleManager.Register(s);

            const TextID id = Register(style, ElementState::None);
            const std::vector<GlyphDraw> plain = Render(id);
            const size_t underline = plain.size() - 5;
            ASSERT_GT(underline, 0u) << "no underline drawn";
            EXPECT_EQ(CountColor(plain, ColorIndex(kBlue)), plain.size());
            const uint64_t layouts = textSystem->LayoutsMade();

            Register(style, ElementState::Hover);
            const std::vector<GlyphDraw> hovered = Render(id);
            ASSERT_EQ(hovered.size(), plain.size());
            EXPECT_EQ(CountColor(hovered, ColorIndex(kGreen)), underline);
            EXPECT_EQ(CountColor(hovered, ColorIndex(kBlue)), 5u);
            EXPECT_EQ(textSystem->LayoutsMade(), layouts) << "laid out again for a decoration color";
        }

        // Each line takes its own color, the ones under the text and the one over it.
        TEST_F(TextPaintTest, EachDecorationLineHasItsColor)
        {
            Style s = BaseStyle();
            for (StyleState& state : s.states)
            {
                state.textStyle.underLine.thickness   = 2;
                state.textStyle.throughLine.thickness = 2;
                state.textStyle.overLine.thickness    = 2;
                state.decorationColors[size_t(DecorationLine::Under)]   = ColorStyle::Create(kGreen);
                state.decorationColors[size_t(DecorationLine::Through)] = ColorStyle::Create(kRed);
                state.decorationColors[size_t(DecorationLine::Over)]    = ColorStyle::Create(kWhite);
            }
            const std::vector<GlyphDraw> draws = Render(Register(styleManager.Register(s), ElementState::None));
            ASSERT_GT(CountColor(draws, ColorIndex(kGreen)), 0u);
            ASSERT_GT(CountColor(draws, ColorIndex(kRed)), 0u);
            ASSERT_GT(CountColor(draws, ColorIndex(kWhite)), 0u);
            EXPECT_EQ(CountColor(draws, ColorIndex(kBlue)), 5u);

            float underTop = 0.0f, throughTop = 0.0f, overTop = 0.0f;
            for (const GlyphDraw& d : draws)
            {
                if (d.color == ColorIndex(kGreen)) underTop   = d.pos.y;
                if (d.color == ColorIndex(kRed))   throughTop = d.pos.y;
                if (d.color == ColorIndex(kWhite)) overTop    = d.pos.y;
            }
            EXPECT_GT(underTop, throughTop) << "the underline is under the line through";
            EXPECT_GT(throughTop, overTop) << "the overline is over the line through";
        }

        // A field being edited draws its decorations in their colors too.
        TEST_F(TextPaintTest, AFocusedFieldsDecorationHasItsColor)
        {
            Style s = BaseStyle();
            for (StyleState& state : s.states)
            {
                state.textStyle.underLine.thickness = 2;
                state.textStyle.overLine.thickness  = 2;
                state.decorationColors[size_t(DecorationLine::Under)] = ColorStyle::Create(kGreen);
                state.decorationColors[size_t(DecorationLine::Over)]  = ColorStyle::Create(kWhite);
            }
            const StyleRef style = styleManager.Register(s);
            const ID field{ .value{ .integer = 82 } };
            std::string bound = "abc";
            bool modified = false;

            auto& focus = stateManager.TextEditorFocus();
            focus.Set(field);
            textSystem->SwitchTextEditor(focus.GetPrevFocusedID(), field);
            focus.Commit();

            std::vector<GlyphDraw> draws;
            for (int frame = 0; frame < 2; ++frame)
                draws = Render(textSystem->RegisterEditableText(field, bound, TextEditBehavior::None, style, ElementState::Focused, modified));
            EXPECT_EQ(CountColor(draws, ColorIndex(kBlue)), 3u);
            EXPECT_GT(CountColor(draws, ColorIndex(kGreen)), 0u);
            EXPECT_GT(CountColor(draws, ColorIndex(kWhite)), 0u);
        }

        // A link run is drawn in its link color, the rest in the text color.
        TEST_F(TextPaintTest, ALinkIsDrawnInItsColor)
        {
            const StyleRef style = styleManager.Register(BaseStyle());
            std::vector<ContentRun> runs{
                { .type = ContentRun::Text, .text = "ab" },
                { .type = ContentRun::Text, .text = "cde", .contentId = 1, .isLink = true,
                  .linkStyle = { .color = kGreen, .hoverColor = kRed } },
            };
            const std::vector<GlyphDraw> draws = Render(textSystem->RegisterRichText(element, runs, style, ElementState::None));
            EXPECT_EQ(CountColor(draws, ColorIndex(kBlue)), 2u);
            EXPECT_EQ(CountColor(draws, ColorIndex(kGreen)), 3u);
        }

        // So it is in selectable text, focused or not.
        TEST_F(TextPaintTest, ASelectableLinkIsDrawnInItsColor)
        {
            const StyleRef style = styleManager.Register(BaseStyle());
            std::vector<ContentRun> runs{
                { .type = ContentRun::Text, .text = "ab" },
                { .type = ContentRun::Text, .text = "cde", .contentId = 1, .isLink = true,
                  .linkStyle = { .color = kGreen, .hoverColor = kRed } },
            };
            const ID field{ .value{ .integer = 83 } };
            bool modified = false;
            auto render = [&] {
                return Render(textSystem->RegisterEditableRichText(field, runs, TextEditBehavior::ReadOnly, style,
                                                                   ElementState::None, modified));
            };
            std::vector<GlyphDraw> draws = render();
            EXPECT_EQ(CountColor(draws, ColorIndex(kBlue)), 2u);
            EXPECT_EQ(CountColor(draws, ColorIndex(kGreen)), 3u);

            auto& focus = stateManager.TextEditorFocus();
            focus.Set(field);
            textSystem->SwitchTextEditor(focus.GetPrevFocusedID(), field);
            focus.Commit();
            for (int frame = 0; frame < 2; ++frame)
                draws = render();
            EXPECT_EQ(CountColor(draws, ColorIndex(kBlue)), 2u) << "focused";
            EXPECT_EQ(CountColor(draws, ColorIndex(kGreen)), 3u) << "focused";
        }
    }
}
