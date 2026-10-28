// text-shadow: the shadow passes the real text system draws under text, icons and a
// focused field's text. Rendering goes to a stub renderer (see skribidi_test_fixture.h).

#include "skribidi_test_fixture.h"

#include <gui_core/text/skribidi/internal/gui_skribidi_context.h>

#include <cstring>
#include <fstream>
#include <regex>
#include <sstream>
#include <initializer_list>
#include <vector>

namespace tachyon
{
    using GUITest::SkribidiHeadlessTest;
    using GlyphDraw = GUITest::StubGUIRenderer::GlyphDraw;

    namespace
    {
        const char* kIcon = R"(<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><circle cx="12" cy="12" r="10" fill="#000"/></svg>)";

        constexpr color4b kRed  { 255, 0, 0, 255 };
        constexpr color4b kBlue { 0, 0, 255, 255 };

        class TextShadowTest : public SkribidiHeadlessTest
        {
        protected:
            const IntRect rect{ 0, 0, 200, 40 };
            const IntRect clip{ 0, 0, 2000, 2000 };

            // A 14px text style with `shadows` in the states `stateSlot` covers.
            StyleRef ShadowedStyle(std::initializer_list<BoxShadow> shadows, int stateSlot = -1)
            {
                Style s{};
                s.textLayoutStyle.textWrapping = TextWrap::NoWrap;
                for (int i = 0; i < Style::kStateCount; ++i)
                {
                    s.states[i].textStyle.fontSize = 14.0f;
                    if (stateSlot >= 0 && i != stateSlot)
                        continue;
                    int layer = 0;
                    for (const BoxShadow& b : shadows)
                        s.states[i].textShadow[layer++] = b;
                }
                return styleManager.Register(s);
            }

            std::vector<GlyphDraw>& Draws() { return graphicsContext.Renderer().glyphDraws; }

            std::vector<GlyphDraw> Render(StyleRef style, std::string_view text, ElementState state = ElementState::None)
            {
                const TextID id = textSystem->RegisterText(ID{ .value{ .integer = 77 } }, IconID::Invalid, text, style, state);
                EXPECT_NE(id, TextID::Invalid);
                Draws().clear();
                textSystem->RenderText(id, rect, rect, clip);
                return Draws();
            }

            VisualColorStyleIndex ColorIndex(color4b c) { return styleManager.RegisterColorStyle(ColorStyle::Create(c)); }

            static float2 Center(const GlyphDraw& d) { return float2{ d.pos.x + d.size.x * 0.5f, d.pos.y + d.size.y * 0.5f }; }
        };

        // The glyph shader decodes the atlas' distance field with the scale it was made with, and
        // fades a shadow out where the field ends.
        TEST(TextShadowAtlasTest, TheShaderReadsTheDistanceFieldItIsGiven)
        {
            std::ifstream file("shaders/glyph_common.glsl");
            ASSERT_TRUE(file) << "run from workdirectory/";
            std::stringstream source;
            source << file.rdbuf();
            const std::string glsl = source.str();

            std::smatch m;
            ASSERT_TRUE(std::regex_search(glsl, m, std::regex(R"(kSdfPerTexel\s*=\s*([0-9.]+)\s*/\s*255\.0)")));
            EXPECT_FLOAT_EQ(std::stof(m[1].str()), SkribidiContext::kSdfDistanceScale);
            ASSERT_TRUE(std::regex_search(glsl, m, std::regex(R"(kSdfReachTexels\s*=\s*([0-9.]+))")));
            // The quad stops a texel short of the padding; the fade ends half a texel before that.
            EXPECT_FLOAT_EQ(std::stof(m[1].str()), float(SkribidiContext::kSdfPadding - 1) - 0.5f);
            // The field's 8 bits each way cover the reach.
            EXPECT_LE(float(SkribidiContext::kSdfPadding - 1) * SkribidiContext::kSdfDistanceScale, 128.0f);
        }

        TEST_F(TextShadowTest, TextWithoutShadowsIsDrawnOnce)
        {
            const auto draws = Render(ShadowedStyle({}), "Glow");
            ASSERT_EQ(draws.size(), 4u);
            for (const GlyphDraw& d : draws)
                EXPECT_LT(d.shadowBlur, 0.0f);
        }

        // Every shadow is drawn under the text, the last first; each moved by its offset and
        // drawn in its color with its blur.
        TEST_F(TextShadowTest, EachShadowIsDrawnUnderTheText)
        {
            const auto draws = Render(ShadowedStyle({ BoxShadow{ .x = 2, .y = 3, .blur = 4, .color = kRed },
                                                      BoxShadow{ .x = -1, .color = kBlue } }), "Glow");
            ASSERT_EQ(draws.size(), 12u);
            const std::span<const GlyphDraw> second(&draws[0], 4), first(&draws[4], 4), text(&draws[8], 4);
            for (size_t i = 0; i < 4; ++i)
            {
                EXPECT_EQ(second[i].shadowBlur, 0.0f);
                EXPECT_EQ(second[i].color, ColorIndex(kBlue));
                EXPECT_NEAR(Center(second[i]).x, Center(text[i]).x - 1.0f, 0.01f);
                EXPECT_NEAR(Center(second[i]).y, Center(text[i]).y, 0.01f);

                EXPECT_EQ(first[i].shadowBlur, 4.0f);
                EXPECT_EQ(first[i].color, ColorIndex(kRed));
                EXPECT_NEAR(Center(first[i]).x, Center(text[i]).x + 2.0f, 0.01f);
                EXPECT_NEAR(Center(first[i]).y, Center(text[i]).y + 3.0f, 0.01f);

                EXPECT_LT(text[i].shadowBlur, 0.0f);
                EXPECT_NE(text[i].color, ColorIndex(kRed));
            }
        }

        // A shadow's quad has all the distance field there is for its blur; the text's own quad
        // leaves out the part only a shadow needs. Same atlas texels per pixel for both.
        TEST_F(TextShadowTest, TheTextLeavesOutThePaddingOnlyShadowsNeed)
        {
            const auto draws = Render(ShadowedStyle({ BoxShadow{ .blur = 4, .color = kRed } }), "W");
            ASSERT_EQ(draws.size(), 2u);
            const GlyphDraw& shadow = draws[0];
            const GlyphDraw& text   = draws[1];
            EXPECT_FLOAT_EQ(text.uvMin.x - shadow.uvMin.x, 9.0f);
            EXPECT_FLOAT_EQ(text.uvMin.y - shadow.uvMin.y, 9.0f);
            EXPECT_FLOAT_EQ(shadow.uvMax.x - text.uvMax.x, 9.0f);
            EXPECT_FLOAT_EQ(shadow.uvMax.y - text.uvMax.y, 9.0f);
            EXPECT_NEAR(text.size.x / (text.uvMax.x - text.uvMin.x), shadow.size.x / (shadow.uvMax.x - shadow.uvMin.x), 1e-4f);
            EXPECT_NEAR(Center(text).x, Center(shadow).x, 0.01f);
            EXPECT_NEAR(Center(text).y, Center(shadow).y, 0.01f);
        }

        TEST_F(TextShadowTest, ShadowsFollowTheState)
        {
            const StyleRef style = ShadowedStyle({ BoxShadow{ .blur = 3, .color = kRed } }, Style::kHover);
            EXPECT_EQ(Render(style, "ab", ElementState::None).size(), 2u);
            EXPECT_EQ(Render(style, "ab", ElementState::Hover).size(), 4u);
            EXPECT_EQ(Render(style, "ab", ElementState::None).size(), 2u);
        }

        // An element's icon, beside its text or on its own, has the text's shadows.
        TEST_F(TextShadowTest, IconsAreShadowedLikeText)
        {
            const IconID icon = textSystem->RegisterIconFromSVG("shadow-test-dot", kIcon, std::strlen(kIcon));
            ASSERT_NE(icon, IconID::Invalid);
            const StyleRef style = ShadowedStyle({ BoxShadow{ .x = 1, .blur = 2, .color = kRed } });

            const TextID id = textSystem->RegisterText(ID{ .value{ .integer = 78 } }, icon, "a", style, ElementState::None);
            Draws().clear();
            textSystem->RenderText(id, rect, rect, clip);
            ASSERT_EQ(Draws().size(), 4u) << "the text and its icon, each with a shadow";
            EXPECT_GE(Draws()[0].shadowBlur, 0.0f);
            EXPECT_GE(Draws()[1].shadowBlur, 0.0f);
            EXPECT_LT(Draws()[2].shadowBlur, 0.0f);
            EXPECT_LT(Draws()[3].shadowBlur, 0.0f);

            const VisualTextStyle* text = styleManager.GetVisualTextStyle(style.visualTextStyleIndex, ElementState::None);
            Draws().clear();
            textSystem->RenderIcon(icon, IntRect{ 0, 0, 16, 16 }, clip, text->iconColorIndex, text->textShadows);
            ASSERT_EQ(Draws().size(), 2u);
            EXPECT_EQ(Draws()[0].shadowBlur, 2.0f);
            EXPECT_EQ(Draws()[0].color, ColorIndex(kRed));
            EXPECT_NEAR(Center(Draws()[0]).x, Center(Draws()[1]).x + 1.0f, 0.01f);
            EXPECT_LT(Draws()[1].shadowBlur, 0.0f);
            EXPECT_EQ(Draws()[1].color, text->iconColorIndex);

            Draws().clear();
            textSystem->RenderIcon(icon, IntRect{ 0, 0, 16, 16 }, clip, text->iconColorIndex);
            EXPECT_EQ(Draws().size(), 1u) << "no shadows given, none drawn";
        }

        // A field being edited keeps its shadows.
        TEST_F(TextShadowTest, AFocusedFieldIsShadowed)
        {
            const StyleRef style = ShadowedStyle({ BoxShadow{ .y = 1, .blur = 2, .color = kRed } });
            const ID field{ .value{ .integer = 79 } };
            std::string bound = "abc";
            bool modified = false;

            auto& focus = stateManager.TextEditorFocus();
            focus.Set(field);
            textSystem->SwitchTextEditor(focus.GetPrevFocusedID(), field);
            focus.Commit();

            for (int frame = 0; frame < 2; ++frame)
            {
                const TextID id = textSystem->RegisterEditableText(field, bound, TextEditBehavior::None, style, ElementState::Focused, modified);
                Draws().clear();
                textSystem->RenderText(id, rect, rect, clip);
            }
            ASSERT_EQ(Draws().size(), 6u);
            for (size_t i = 0; i < 3; ++i)
            {
                EXPECT_EQ(Draws()[i].shadowBlur, 2.0f);
                EXPECT_EQ(Draws()[i].color, ColorIndex(kRed));
                EXPECT_NEAR(Center(Draws()[i]).y, Center(Draws()[i + 3]).y + 1.0f, 0.01f);
                EXPECT_LT(Draws()[i + 3].shadowBlur, 0.0f);
            }
        }
    }
}
