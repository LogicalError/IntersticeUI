#include <gtest/gtest.h>
#include <gui_core/style/gui_style_sheet.h>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace tachyon
{

    // ============================================================================
    // Helpers
    // ============================================================================

    static color4b RGB(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
    {
        return { r, g, b, a };
    }

    static bool GradientColorEq(const Gradient& g, int stopIdx, const color4b& expected)
    {
        if (stopIdx >= g.stopCount) return false;
        color4b c = g.stops[stopIdx].GetColor4b();
        return c == expected;
    }

    // ============================================================================
    // Basic parsing
    // ============================================================================

    TEST(StyleSheet, EmptyInput)
    {
        auto s = ParseStyleSheet("");
        EXPECT_FALSE(s.HasErrors());
        EXPECT_TRUE(s.styles.empty());
    }

    TEST(StyleSheet, Comments)
    {
        auto s = ParseStyleSheet(R"(
            /* block comment */
            // line comment
            .foo { color: white; } // trailing
        )");
        EXPECT_FALSE(s.HasErrors());
        EXPECT_NE(s.GetStyle("foo"), nullptr);
    }

    TEST(StyleSheet, StyleWithAndWithoutLeadingDot)
    {
        auto s = ParseStyleSheet(R"(
            .with-dot { color: white; }
            without-dot { color: white; }
        )");
        EXPECT_NE(s.GetStyle("with-dot"),    nullptr);
        EXPECT_NE(s.GetStyle("without-dot"), nullptr);
    }

    TEST(StyleSheet, MultipleStyles)
    {
        auto s = ParseStyleSheet(R"(
            .a { color: white; }
            .b { color: black; }
            .c { color: red; }
        )");
        EXPECT_EQ(s.styles.size(), 3u);
        EXPECT_NE(s.GetStyle("a"), nullptr);
        EXPECT_NE(s.GetStyle("b"), nullptr);
        EXPECT_NE(s.GetStyle("c"), nullptr);
    }

    TEST(StyleSheet, CommaSelectors)
    {
        auto s = ParseStyleSheet(R"(
            .foo, .bar { padding: 8; }
        )");
        ASSERT_NE(s.GetStyle("foo"), nullptr);
        ASSERT_NE(s.GetStyle("bar"), nullptr);
        EXPECT_EQ(s.GetStyle("foo")->padding.top,  8u);
        EXPECT_EQ(s.GetStyle("bar")->padding.top,  8u);
    }

    TEST(StyleSheet, GetStyleReturnsNullForMissing)
    {
        auto s = ParseStyleSheet(".a { color: white; }");
        EXPECT_EQ(s.GetStyle("nope"), nullptr);
    }

    // ============================================================================
    // Color parsing
    // ============================================================================

    TEST(StyleSheet, HexColor6Digit)
    {
        auto s = ParseStyleSheet(".a { color: #ff8040; }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        const auto c = s.GetStyle("a")->states[ElementState::None].textColor.GetFirstColor();
        EXPECT_EQ(c.r, 0xff);
        EXPECT_EQ(c.g, 0x80);
        EXPECT_EQ(c.b, 0x40);
        EXPECT_EQ(c.a, 255);
    }

    TEST(StyleSheet, HexColor8Digit)
    {
        auto s = ParseStyleSheet(".a { color: #ff804080; }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        const auto c = s.GetStyle("a")->states[ElementState::None].textColor.GetFirstColor();
        EXPECT_EQ(c.r, 0xff);
        EXPECT_EQ(c.g, 0x80);
        EXPECT_EQ(c.b, 0x40);
        EXPECT_EQ(c.a, 0x80);
    }

    TEST(StyleSheet, HexColor3Digit)
    {
        auto s = ParseStyleSheet(".a { color: #f80; }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        const auto c = s.GetStyle("a")->states[ElementState::None].textColor.GetFirstColor();
        EXPECT_EQ(c.r, 0xff);
        EXPECT_EQ(c.g, 0x88);
        EXPECT_EQ(c.b, 0x00);
    }

    TEST(StyleSheet, HexColor4Digit)
    {
        auto s = ParseStyleSheet(".a { color: #f80f; }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        const auto c = s.GetStyle("a")->states[ElementState::None].textColor.GetFirstColor();
        EXPECT_EQ(c.r, 0xff);
        EXPECT_EQ(c.g, 0x88);
        EXPECT_EQ(c.b, 0x00);
        EXPECT_EQ(c.a, 0xff);
    }

    TEST(StyleSheet, RgbFunction)
    {
        auto s = ParseStyleSheet(".a { color: rgb(10, 20, 30); }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        const auto c = s.GetStyle("a")->states[ElementState::None].textColor.GetFirstColor();
        EXPECT_EQ(c.r, 10);
        EXPECT_EQ(c.g, 20);
        EXPECT_EQ(c.b, 30);
        EXPECT_EQ(c.a, 255);
    }

    TEST(StyleSheet, RgbaFunctionFloat)
    {
        // alpha 0.5 -> 128 (rounded)
        auto s = ParseStyleSheet(".a { color: rgba(100, 150, 200, 0.5); }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        const auto c = s.GetStyle("a")->states[ElementState::None].textColor.GetFirstColor();
        EXPECT_EQ(c.r, 100);
        EXPECT_EQ(c.g, 150);
        EXPECT_EQ(c.b, 200);
        EXPECT_NEAR(c.a, 128, 1);
    }

    TEST(StyleSheet, RgbaFunctionByte)
    {
        // alpha > 1 -> treated as 0-255
        auto s = ParseStyleSheet(".a { color: rgba(100, 150, 200, 64); }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->states[ElementState::None].textColor.GetFirstColor().a, 64);
    }

    TEST(StyleSheet, NamedColorWhite)
    {
        auto s = ParseStyleSheet(".a { color: white; }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        const auto c = s.GetStyle("a")->states[ElementState::None].textColor.GetFirstColor();
        EXPECT_EQ(c.r, 255); EXPECT_EQ(c.g, 255); EXPECT_EQ(c.b, 255); EXPECT_EQ(c.a, 255);
    }

    TEST(StyleSheet, NamedColorTransparent)
    {
        auto s = ParseStyleSheet(".a { color: transparent; }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->states[ElementState::None].textColor.GetFirstColor().a, 0);
    }

    TEST(StyleSheet, BackgroundColorSetsBothFields)
    {
        auto s = ParseStyleSheet(".a { background-color: #336699; }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        const auto& state = s.GetStyle("a")->states[ElementState::None];
        EXPECT_EQ(state.backgroundColor.type, ColorType::SolidColor);
        EXPECT_TRUE(GradientColorEq(state.backgroundColor.gradient, 0, RGB(0x33, 0x66, 0x99)));
    }

    // ============================================================================
    // Gradient parsing
    // ============================================================================

    TEST(StyleSheet, LinearGradient)
    {
        auto s = ParseStyleSheet(".a { background-color: linear-gradient(90deg, #ff0000 0%, #0000ff 100%); }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        const auto& bg = s.GetStyle("a")->states[ElementState::None].backgroundColor;
        EXPECT_EQ(bg.type, ColorType::LinearGradient);
        EXPECT_EQ(bg.gradient.stopCount, 2);
        EXPECT_TRUE(GradientColorEq(bg.gradient, 0, RGB(255, 0, 0)));
        EXPECT_TRUE(GradientColorEq(bg.gradient, 1, RGB(0, 0, 255)));
    }

    // CSS angle units: 0.25turn == 90deg == 100grad == pi/2 rad.
    TEST(StyleSheet, GradientAngleUnits)
    {
        auto angleOf = [](const char* angle) {
            const std::string src = std::string(".a { background-color: linear-gradient(") + angle + ", red 0%, blue 100%); }";
            auto s = ParseStyleSheet(src);
            EXPECT_FALSE(s.HasErrors()) << angle;
            return s.GetStyle("a") ? s.GetStyle("a")->states[ElementState::None].backgroundColor.gradient.value : uint16_t(0);
        };
        const uint16_t deg90 = Gradient::Linear(90.0f).value;
        EXPECT_EQ(angleOf("90deg"), deg90);
        EXPECT_EQ(angleOf("0.25turn"), deg90);
        EXPECT_EQ(angleOf("100grad"), deg90);
        EXPECT_NEAR(angleOf("1.5707963rad"), deg90, 1);
    }

    TEST(StyleSheet, RadialGradient)
    {
        auto s = ParseStyleSheet(".a { background-color: radial-gradient(white 0%, black 100%); }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        const auto& bg = s.GetStyle("a")->states[ElementState::None].backgroundColor;
        EXPECT_EQ(bg.type, ColorType::RadialGradient);
        EXPECT_EQ(bg.gradient.stopCount, 2);
    }

    TEST(StyleSheet, LinearGradientBareAngle)
    {
        auto s = ParseStyleSheet(".a { background-color: linear-gradient(45, red, blue); }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        const auto& bg = s.GetStyle("a")->states[ElementState::None].backgroundColor;
        EXPECT_EQ(bg.type, ColorType::LinearGradient);
    }

    // ============================================================================
    // Box model
    // ============================================================================

    TEST(StyleSheet, Margin1Value)
    {
        auto s = ParseStyleSheet(".a { margin: 8; }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        const auto& m = s.GetStyle("a")->margin;
        EXPECT_EQ(m.top, 8u); EXPECT_EQ(m.right, 8u);
        EXPECT_EQ(m.bottom, 8u); EXPECT_EQ(m.left, 8u);
    }

    TEST(StyleSheet, Margin2Values)
    {
        auto s = ParseStyleSheet(".a { margin: 4 8; }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        const auto& m = s.GetStyle("a")->margin;
        EXPECT_EQ(m.top, 4u); EXPECT_EQ(m.right, 8u);
        EXPECT_EQ(m.bottom, 4u); EXPECT_EQ(m.left, 8u);
    }

    TEST(StyleSheet, Margin4Values)
    {
        auto s = ParseStyleSheet(".a { margin: 1 2 3 4; }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        const auto& m = s.GetStyle("a")->margin;
        EXPECT_EQ(m.top, 1u); EXPECT_EQ(m.right, 2u);
        EXPECT_EQ(m.bottom, 3u); EXPECT_EQ(m.left, 4u);
    }

    TEST(StyleSheet, MarginLonghand)
    {
        auto s = ParseStyleSheet(".a { margin-top: 5; margin-right: 6; margin-bottom: 7; margin-left: 8; }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        const auto& m = s.GetStyle("a")->margin;
        EXPECT_EQ(m.top, 5u); EXPECT_EQ(m.right, 6u);
        EXPECT_EQ(m.bottom, 7u); EXPECT_EQ(m.left, 8u);
    }

    TEST(StyleSheet, PaddingWithPxUnit)
    {
        auto s = ParseStyleSheet(".a { padding: 10px; }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->padding.top, 10u);
    }

    TEST(StyleSheet, BorderWidth)
    {
        auto s = ParseStyleSheet(".a { border-width: 1 2; }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->borderWidths.top,   1u);
        EXPECT_EQ(s.GetStyle("a")->borderWidths.right,  2u);
        EXPECT_EQ(s.GetStyle("a")->borderWidths.bottom, 1u);
        EXPECT_EQ(s.GetStyle("a")->borderWidths.left,   2u);
    }

    TEST(StyleSheet, BorderRadius1Value)
    {
        auto s = ParseStyleSheet(".a { border-radius: 4; }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        const auto& r = s.GetStyle("a")->borderRadius;
        EXPECT_EQ(r.topLeft, 4u); EXPECT_EQ(r.topRight, 4u);
        EXPECT_EQ(r.bottomRight, 4u); EXPECT_EQ(r.bottomLeft, 4u);
    }

    TEST(StyleSheet, BorderCornerLonghand)
    {
        auto s = ParseStyleSheet(R"(
            .a {
                border-top-left-radius: 1;
                border-top-right-radius: 2;
                border-bottom-right-radius: 3;
                border-bottom-left-radius: 4;
            }
        )");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        const auto& r = s.GetStyle("a")->borderRadius;
        EXPECT_EQ(r.topLeft, 1u); EXPECT_EQ(r.topRight, 2u);
        EXPECT_EQ(r.bottomRight, 3u); EXPECT_EQ(r.bottomLeft, 4u);
    }

    // ============================================================================
    // Display / layout enums
    // ============================================================================

    TEST(StyleSheet, DisplayFlex)
    {
        auto s = ParseStyleSheet(".a { display: flex; }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->display, Display::Flex);
    }

    TEST(StyleSheet, DisplayNone)
    {
        auto s = ParseStyleSheet(".a { display: none; }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->display, Display::None);
    }

    TEST(StyleSheet, OverflowHidden)
    {
        auto s = ParseStyleSheet(".a { overflow: hidden; }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        // `overflow` is a shorthand that sets both axes.
        EXPECT_EQ(s.GetStyle("a")->overflowX, Overflow::Hidden);
        EXPECT_EQ(s.GetStyle("a")->overflowY, Overflow::Hidden);
    }

    TEST(StyleSheet, OverflowPerAxis)
    {
        auto s = ParseStyleSheet(".a { overflow-x: auto; overflow-y: hidden; }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->overflowX, Overflow::Auto);
        EXPECT_EQ(s.GetStyle("a")->overflowY, Overflow::Hidden);
    }

    TEST(StyleSheet, FlexDirection)
    {
        auto s = ParseStyleSheet(".a { flex-direction: column; }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->layoutData.flex.direction, FlexDirection::Column);
    }

    TEST(StyleSheet, FlexWrap)
    {
        auto s = ParseStyleSheet(".a { flex-wrap: wrap; }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->layoutData.flex.wrap, FlexWrap::Wrap);
    }

    TEST(StyleSheet, AlignItems)
    {
        auto s = ParseStyleSheet(".a { align-items: center; }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->layoutData.flex.alignItems, AlignItems::Center);
    }

    TEST(StyleSheet, JustifyContent)
    {
        auto s = ParseStyleSheet(".a { justify-content: space-between; }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->layoutData.flex.justifyContent, JustifyContent::SpaceBetween);
    }

    TEST(StyleSheet, Gap)
    {
        auto s = ParseStyleSheet(".a { gap: 6; }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->layoutData.flex.gap, 6u);
    }

    TEST(StyleSheet, FlexGrowShrink)
    {
        auto s = ParseStyleSheet(".a { flex-grow: 2; flex-shrink: 0; }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->layoutData.flex.flexGrow,   2);
        EXPECT_EQ(s.GetStyle("a")->layoutData.flex.flexShrink, 0);
    }

    TEST(StyleSheet, FlexBasisPixels)
    {
        auto s = ParseStyleSheet(".a { flex-basis: 100; }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->layoutData.flex.flexBasis, 100);
    }

    TEST(StyleSheet, FlexBasisAuto)
    {
        auto s = ParseStyleSheet(".a { flex-basis: auto; }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->layoutData.flex.flexBasis, FlexProperties::kAuto);
    }

    TEST(StyleSheet, Cursor)
    {
        auto s = ParseStyleSheet(".a { cursor: pointer; }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->cursor, MouseCursor::Pointer);
    }

    TEST(StyleSheet, CursorPointerAlias)
    {
        auto s = ParseStyleSheet(".a { cursor: pointer; }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->cursor, MouseCursor::Pointer);
    }

    TEST(StyleSheet, IconSide)
    {
        auto s = ParseStyleSheet(".a { -tachyon-icon-side: right; }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->iconSide, IconSide::Right);
    }

    TEST(StyleSheet, IconTextGap)
    {
        auto s = ParseStyleSheet(".a { -tachyon-icon-text-gap: 4; }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->iconTextGap, 4u);
    }

    // ============================================================================
    // Text layout
    // ============================================================================

    TEST(StyleSheet, TextAlign)
    {
        auto s = ParseStyleSheet(".a { text-align: center; }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->textLayoutStyle.horizontalAlignment, TextAlign::Center);
    }

    TEST(StyleSheet, TextWrap)
    {
        auto s = ParseStyleSheet(".a { text-wrap: wrap; }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->textLayoutStyle.textWrapping, TextWrap::Wrap);
    }

    TEST(StyleSheet, TextOverflow)
    {
        auto s = ParseStyleSheet(".a { text-overflow: ellipsis; }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->textLayoutStyle.textOverflow, TextOverflow::Ellipsis);
    }

    TEST(StyleSheet, TextDirection)
    {
        auto s = ParseStyleSheet(".a { direction: rtl; }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->textLayoutStyle.textDirection, TextDirection::RightToLeft);
    }

    // ============================================================================
    // Font properties (per-state)
    // ============================================================================

    TEST(StyleSheet, FontSize)
    {
        auto s = ParseStyleSheet(".a { font-size: 14; }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_FLOAT_EQ(s.GetStyle("a")->states[ElementState::None].textStyle.fontSize, 14.0f);
    }

    TEST(StyleSheet, FontWeightBold)
    {
        auto s = ParseStyleSheet(".a { font-weight: bold; }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->states[ElementState::None].textStyle.weight, FontWeight::Bold);
    }

    TEST(StyleSheet, FontWeightNumeric700)
    {
        auto s = ParseStyleSheet(".a { font-weight: 700; }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->states[ElementState::None].textStyle.weight, FontWeight::Bold);
    }

    TEST(StyleSheet, FontWeightNumeric400)
    {
        auto s = ParseStyleSheet(".a { font-weight: 400; }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->states[ElementState::None].textStyle.weight, FontWeight::Normal);
    }

    TEST(StyleSheet, FontStyleItalic)
    {
        auto s = ParseStyleSheet(".a { font-style: italic; }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->states[ElementState::None].textStyle.style, FontStyle::Italic);
    }

    TEST(StyleSheet, FontFamilyMonospace)
    {
        auto s = ParseStyleSheet(".a { font-family: monospace; }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->states[ElementState::None].textStyle.family, FontFamily::Monospace);
        EXPECT_EQ(s.GetStyle("a")->states[ElementState::None].textStyle.fontId, FontID::Invalid);
    }

    // ============================================================================
    // Named font families and @font-face
    // ============================================================================

    TEST(StyleSheet, FontIDFromNameIsCaseInsensitive)
    {
        EXPECT_EQ(FontIDFromName("Inter"), FontIDFromName("INTER"));
        EXPECT_NE(FontIDFromName("Inter"), FontIDFromName("Roboto"));
        EXPECT_NE(FontIDFromName("Inter"), FontID::Invalid);
        EXPECT_EQ(FontIDFromName(""), FontID::Invalid);
    }

    TEST(StyleSheet, FontFamilyNamedWithGenericFallback)
    {
        auto s = ParseStyleSheet(".a { font-family: \"Inter\", sans-serif; }");
        EXPECT_FALSE(s.HasErrors());
        ASSERT_NE(s.GetStyle("a"), nullptr);
        const TextStyle& t = s.GetStyle("a")->states[ElementState::None].textStyle;
        EXPECT_EQ(t.fontId, FontIDFromName("Inter"));
        EXPECT_EQ(t.family, FontFamily::SansSerif);
    }

    TEST(StyleSheet, FontFamilyUnquotedMultiWordName)
    {
        auto s = ParseStyleSheet(".a { font-family: Noto Sans Mono, monospace; }");
        EXPECT_FALSE(s.HasErrors());
        ASSERT_NE(s.GetStyle("a"), nullptr);
        const TextStyle& t = s.GetStyle("a")->states[ElementState::None].textStyle;
        EXPECT_EQ(t.fontId, FontIDFromName("Noto Sans Mono"));
        EXPECT_EQ(t.family, FontFamily::Monospace);
    }

    TEST(StyleSheet, FontFamilyFirstNameAndFirstGenericWin)
    {
        auto s = ParseStyleSheet(".a { font-family: serif, \"A\", 'B', monospace; }");
        EXPECT_FALSE(s.HasErrors());
        ASSERT_NE(s.GetStyle("a"), nullptr);
        const TextStyle& t = s.GetStyle("a")->states[ElementState::None].textStyle;
        EXPECT_EQ(t.fontId, FontIDFromName("A"));
        EXPECT_EQ(t.family, FontFamily::Serif);
    }

    TEST(StyleSheet, FontFamilyQuotedKeywordIsAName)
    {
        auto s = ParseStyleSheet(".a { font-family: \"serif\"; }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        const TextStyle& t = s.GetStyle("a")->states[ElementState::None].textStyle;
        EXPECT_EQ(t.fontId, FontIDFromName("serif"));
        EXPECT_EQ(t.family, FontFamily::Default);
    }

    TEST(StyleSheet, FontFamilyGenericResetsEarlierName)
    {
        auto s = ParseStyleSheet(".a { font-family: \"Inter\"; } .a:hover { font-family: monospace; }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->states[ElementState::None].textStyle.fontId, FontIDFromName("Inter"));
        EXPECT_EQ(s.GetStyle("a")->states[ElementState::Hover].textStyle.fontId, FontID::Invalid);
    }

    TEST(StyleSheet, FontFaceRulesCollectedInOrder)
    {
        auto s = ParseStyleSheet(
            "@font-face { font-family: \"Inter\"; src: url(\"../fonts/Inter-Regular.otf\"); }\n"
            "@font-face { font-family: Inter; src: url('../fonts/Inter-Bold.otf') format(\"opentype\"); }\n"
            "@font-face { src: \"mono.ttf\"; font-family: My Mono; }\n"
            ".a { color: red; }");
        EXPECT_FALSE(s.HasErrors());
        ASSERT_EQ(s.fontFaces.size(), 3u);
        EXPECT_EQ(s.fontFaces[0].family, "Inter");
        EXPECT_EQ(s.fontFaces[0].src,    "../fonts/Inter-Regular.otf");
        EXPECT_EQ(s.fontFaces[1].family, "Inter");
        EXPECT_EQ(s.fontFaces[1].src,    "../fonts/Inter-Bold.otf");
        EXPECT_EQ(s.fontFaces[2].family, "My Mono");
        EXPECT_EQ(s.fontFaces[2].src,    "mono.ttf");
        EXPECT_NE(s.GetStyle("a"), nullptr);
    }

    TEST(StyleSheet, TachyonLanguage)
    {
        auto s = ParseStyleSheet(".a { -tachyon-language: ja; } .b { -tachyon-language: \"zh-Hant\"; } .c { -tachyon-language: none; }");
        EXPECT_FALSE(s.HasErrors());
        EXPECT_EQ(s.GetStyle("a")->states[ElementState::None].textStyle.language.view(), "ja");
        EXPECT_EQ(s.GetStyle("b")->states[ElementState::Hover].textStyle.language.view(), "zh-Hant");
        EXPECT_TRUE(s.GetStyle("c")->states[ElementState::None].textStyle.language.empty());

        auto tooLong = ParseStyleSheet(".d { -tachyon-language: zh-Hant-TW-x-private; }");
        EXPECT_TRUE(tooLong.HasErrors());
    }

    TEST(StyleSheet, UniversalRuleIsTheBaseOfEveryStyle)
    {
        auto s = ParseStyleSheet(
            ".before { color: red; }\n"
            "* { font-family: \"Inter\"; font-size: 12px; }\n"
            ".after  { font-size: 16px; }\n"
            ".own    { font-family: monospace; }");
        EXPECT_FALSE(s.HasErrors());
        auto text = [&](const char* name) { return s.GetStyle(name)->states[ElementState::None].textStyle; };
        ASSERT_NE(s.GetStyle("before"), nullptr);
        ASSERT_NE(s.GetStyle("after"), nullptr);
        ASSERT_NE(s.GetStyle("own"), nullptr);

        EXPECT_EQ(text("before").fontId, FontIDFromName("Inter")) << "applies to styles defined earlier";
        EXPECT_EQ(text("after").fontId, FontIDFromName("Inter")) << "styles defined later start from it";
        EXPECT_FLOAT_EQ(text("after").fontSize, 16.0f) << "class rules override it";
        EXPECT_FLOAT_EQ(text("own").fontSize, 12.0f);
        EXPECT_EQ(text("own").fontId, FontID::Invalid);
        EXPECT_EQ(text("own").family, FontFamily::Monospace);

        EXPECT_EQ(s.baseStyle.states[ElementState::None].textStyle.fontId, FontIDFromName("Inter"))
            << "styles the sheet never names start from the base too";
        EXPECT_EQ(s.baseStyle.states[ElementState::Disabled].textStyle.fontId, FontIDFromName("Inter"));
    }

    TEST(StyleSheet, UniversalRuleTakesPseudoStates)
    {
        auto s = ParseStyleSheet("* , .a { color: red; } *:hover { color: blue; } .b { }");
        EXPECT_FALSE(s.HasErrors());
        ASSERT_NE(s.GetStyle("b"), nullptr);
        EXPECT_EQ(s.GetStyle("b")->states[ElementState::None].textColor.GetFirstColor(), RGB(255, 0, 0));
        EXPECT_EQ(s.GetStyle("b")->states[ElementState::Hover].textColor.GetFirstColor(), RGB(0, 0, 255));
        EXPECT_EQ(s.GetStyle("a")->states[ElementState::Hover].textColor.GetFirstColor(), RGB(0, 0, 255));
    }

    // The shipped theme (palette + shared styles + the showcase's, concatenated as LoadTheme
    // does) parses cleanly and every @font-face file it names exists. Runs from workdirectory.
    TEST(StyleSheet, ShippedThemeParsesAndItsFontFilesExist)
    {
        for (const char* palette : { "themes/dark.css", "themes/light.css" })
        {
            std::ifstream paletteFile(palette), stylesFile("themes/styles.css"), showcaseFile("themes/showcase.css");
            ASSERT_TRUE(paletteFile.good() && stylesFile.good() && showcaseFile.good()) << palette;
            std::ostringstream ss;
            ss << paletteFile.rdbuf() << '\n' << stylesFile.rdbuf() << '\n' << showcaseFile.rdbuf();

            const StyleSheet s = ParseStyleSheet(ss.view());
            for (const ParseError& e : s.errors)
                ADD_FAILURE() << palette << " [" << e.line << ":" << e.column << "] " << e.message;
            EXPECT_FALSE(s.fontFaces.empty());
            for (const FontFace& face : s.fontFaces)
                EXPECT_TRUE(std::filesystem::exists(std::filesystem::path("themes") / face.src)) << face.src;
        }
    }

    // Mask icons (one-grey SVGs such as the drag grip) take their element's text
    // colour; an icon-only style without one draws the icon fully transparent.
    TEST(StyleSheet, ShippedIconOnlyStylesHaveAVisibleColor)
    {
        for (const char* palette : { "themes/dark.css", "themes/light.css" })
        {
            std::ifstream paletteFile(palette), stylesFile("themes/styles.css");
            ASSERT_TRUE(paletteFile.good() && stylesFile.good()) << palette;
            std::ostringstream ss;
            ss << paletteFile.rdbuf() << '\n' << stylesFile.rdbuf();
            const StyleSheet s = ParseStyleSheet(ss.view());

            for (const char* name : { "list-item-drag-knob-horz", "list-item-drag-knob-vert" })
            {
                const Style* style = s.GetStyle(name);
                ASSERT_NE(style, nullptr) << name;
                for (int state : { 0, int(ElementState::Hover), int(ElementState::Active) })
                    EXPECT_GT(style->states[state].textColor.GetFirstColor().a, 0) << palette << " ." << name << " state " << state;
            }
        }
    }

    // A hovered row (table, tree, list, grid view) stands out plainly from the panel it lies
    // on, in both themes: as far as the hover state layer (--text-1 at 8%) takes it, about 17
    // steps of luminance on these panels. A hovered selected row keeps the selection's color.
    TEST(StyleSheet, ShippedRowsShowTheirHover)
    {
        auto luminance = [](color4b c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; };
        for (const char* palette : { "themes/dark.css", "themes/light.css" })
        {
            std::ifstream paletteFile(palette), stylesFile("themes/styles.css");
            ASSERT_TRUE(paletteFile.good() && stylesFile.good()) << palette;
            std::ostringstream ss;
            ss << paletteFile.rdbuf() << '\n' << stylesFile.rdbuf();
            const StyleSheet s = ParseStyleSheet(ss.view());
            const auto* panelProp = s.GetCustomProperty("--bg-panel");
            ASSERT_TRUE(panelProp && panelProp->hasColor) << palette;
            const color4b panel = panelProp->color;

            for (const char* name : { "table-cell", "table-cell-number", "table-cell-compact", "table-row", "table-row-alt",
                                      "list-row", "tree-table-cell", "list-item", "list-item-row", "grid-view-item",
                                      "grid-view-list-item" })
            {
                const Style* style = s.GetStyle(name);
                ASSERT_NE(style, nullptr) << name;
                const color4b hover = style->states[ElementState::Hover].backgroundColor.GetFirstColor();
                EXPECT_EQ(hover.a, 255) << palette << " ." << name;
                EXPECT_GE(std::abs(luminance(hover) - luminance(panel)), 16.0f) << palette << " ." << name << ": the hover barely shows";

                const color4b selected = style->states[ElementState::Selected].backgroundColor.GetFirstColor();
                if (selected.a != 0)
                    EXPECT_EQ(style->states[ElementState::Selected | ElementState::Hover].backgroundColor.GetFirstColor(), selected)
                        << palette << " ." << name << ": a hovered selected row keeps the selection's color";
            }
        }
    }

    // A node's header sits inside the node's border, so its rounded top must follow
    // the border's inner curve (outer radius minus border width); a larger radius
    // leaves the node background showing in the corners, visibly so when scaled.
    TEST(StyleSheet, ShippedNodeHeaderFollowsTheNodeBorderCurve)
    {
        std::ifstream paletteFile("themes/dark.css"), stylesFile("themes/styles.css");
        ASSERT_TRUE(paletteFile.good() && stylesFile.good());
        std::ostringstream ss;
        ss << paletteFile.rdbuf() << '\n' << stylesFile.rdbuf();
        const StyleSheet s = ParseStyleSheet(ss.view());

        const Style* node   = s.GetStyle("node");
        const Style* header = s.GetStyle("node-header");
        ASSERT_NE(node, nullptr);
        ASSERT_NE(header, nullptr);
        EXPECT_EQ(header->borderRadius.topLeft,  node->borderRadius.topLeft  - node->borderWidths.left);
        EXPECT_EQ(header->borderRadius.topRight, node->borderRadius.topRight - node->borderWidths.right);
    }

    // Toolbar thumbs round their leading edge: the left of a horizontal toolbar,
    // the top of a vertical one.
    TEST(StyleSheet, ShippedToolbarThumbsRoundTheirLeadingEdge)
    {
        std::ifstream paletteFile("themes/dark.css"), stylesFile("themes/styles.css");
        ASSERT_TRUE(paletteFile.good() && stylesFile.good());
        std::ostringstream ss;
        ss << paletteFile.rdbuf() << '\n' << stylesFile.rdbuf();
        const StyleSheet s = ParseStyleSheet(ss.view());

        const Style* horz = s.GetStyle("toolbar-thumb-horz");
        const Style* vert = s.GetStyle("toolbar-thumb-vert");
        ASSERT_NE(horz, nullptr);
        ASSERT_NE(vert, nullptr);
        EXPECT_GT(horz->borderRadius.topLeft, 0);
        EXPECT_GT(horz->borderRadius.bottomLeft, 0);
        EXPECT_EQ(horz->borderRadius.topRight, 0);
        EXPECT_EQ(horz->borderRadius.bottomRight, 0);
        EXPECT_GT(vert->borderRadius.topLeft, 0);
        EXPECT_GT(vert->borderRadius.topRight, 0);
        EXPECT_EQ(vert->borderRadius.bottomLeft, 0);
        EXPECT_EQ(vert->borderRadius.bottomRight, 0);
    }

    // Single-line text fields centre their text when made taller than it.
    TEST(StyleSheet, ShippedTextboxCentresItsTextVertically)
    {
        std::ifstream paletteFile("themes/dark.css"), stylesFile("themes/styles.css");
        ASSERT_TRUE(paletteFile.good() && stylesFile.good());
        std::ostringstream ss;
        ss << paletteFile.rdbuf() << '\n' << stylesFile.rdbuf();
        const StyleSheet s = ParseStyleSheet(ss.view());

        const Style* textbox = s.GetStyle("textbox");
        ASSERT_NE(textbox, nullptr);
        EXPECT_EQ(textbox->textLayoutStyle.verticalAlignment, TextAlign::Center);
    }

    // The showcase log's level toggles centre their icon + count in the button, with no
    // nudge (the icons are symmetric).
    TEST(StyleSheet, ShippedLogLevelTogglesCentreTheirContent)
    {
        std::ifstream paletteFile("themes/dark.css"), stylesFile("themes/styles.css"), showcaseFile("themes/showcase.css");
        ASSERT_TRUE(paletteFile.good() && stylesFile.good() && showcaseFile.good());
        std::ostringstream ss;
        ss << paletteFile.rdbuf() << '\n' << stylesFile.rdbuf() << '\n' << showcaseFile.rdbuf();
        const StyleSheet s = ParseStyleSheet(ss.view());

        for (const char* name : { "log-level-error", "log-level-warning", "log-level-info", "icon-label" })
        {
            const Style* style = s.GetStyle(name);
            ASSERT_NE(style, nullptr) << name;
            EXPECT_EQ(style->textLayoutStyle.verticalAlignment, TextAlign::Center) << name;
            EXPECT_EQ(style->iconOffsetY, 0) << name;
        }
    }

    // Toolbar icons are drawn where they are placed, with no nudge.
    TEST(StyleSheet, ShippedToolbarIconsAreNotNudged)
    {
        std::ifstream paletteFile("themes/dark.css"), stylesFile("themes/styles.css");
        ASSERT_TRUE(paletteFile.good() && stylesFile.good());
        std::ostringstream ss;
        ss << paletteFile.rdbuf() << '\n' << stylesFile.rdbuf();
        const StyleSheet s = ParseStyleSheet(ss.view());

        for (const char* name : { "toolbar-button", "toolbar-toggle" })
        {
            const Style* style = s.GetStyle(name);
            ASSERT_NE(style, nullptr) << name;
            EXPECT_EQ(style->iconOffsetY, 0) << name;
        }
    }

    // Log and toolbar text sits centred in its cell or button when the row or
    // toolbar is taller than the text, without padding nudges.
    TEST(StyleSheet, ShippedLogAndToolbarTextIsCentredVertically)
    {
        std::ifstream paletteFile("themes/dark.css"), stylesFile("themes/styles.css"), showcaseFile("themes/showcase.css");
        ASSERT_TRUE(paletteFile.good() && stylesFile.good() && showcaseFile.good());
        std::ostringstream ss;
        ss << paletteFile.rdbuf() << '\n' << stylesFile.rdbuf() << '\n' << showcaseFile.rdbuf();
        const StyleSheet s = ParseStyleSheet(ss.view());

        for (const char* name : { "log-cell", "toolbar-button", "toolbar-toggle", "toolbar-menu-trigger" })
        {
            const Style* style = s.GetStyle(name);
            ASSERT_NE(style, nullptr) << name;
            EXPECT_EQ(style->textLayoutStyle.verticalAlignment, TextAlign::Center) << name;
            EXPECT_EQ(style->padding.top, style->padding.bottom) << name;
        }
    }

    // Bars, separators and toolbar thumbs get their sizes from the stylesheet.
    TEST(StyleSheet, ShippedChromeSizesComeFromTheStylesheet)
    {
        std::ifstream paletteFile("themes/dark.css"), stylesFile("themes/styles.css");
        ASSERT_TRUE(paletteFile.good() && stylesFile.good());
        std::ostringstream ss;
        ss << paletteFile.rdbuf() << '\n' << stylesFile.rdbuf();
        const StyleSheet s = ParseStyleSheet(ss.view());

        auto layout = [&](const char* name) -> const LayoutData& {
            const Style* style = s.GetStyle(name);
            EXPECT_NE(style, nullptr) << name;
            static const LayoutData none{};
            return style ? style->layoutData : none;
        };
        EXPECT_EQ(layout("scrollbar-vertical").width, 12);
        EXPECT_EQ(layout("scrollbar-horizontal").height, 12);
        EXPECT_EQ(layout("table-separator").width, 2);
        EXPECT_EQ(layout("menu-separator").height, 1);
        EXPECT_EQ(layout("toolbar-thumb-horz").width, 6);
        EXPECT_EQ(layout("toolbar-thumb-horz").minHeight, 10);
        EXPECT_EQ(layout("toolbar-thumb-vert").height, 6);
        EXPECT_EQ(layout("toolbar-thumb-vert").minWidth, 10);
    }

    TEST(StyleSheet, FontFaceErrorsAreReportedAndParsingContinues)
    {
        const char* srcs[] = {
            "@font-face { font-family: \"Inter\"; }",                          // no src
            "@font-face { src: url(\"a.otf\"); }",                              // no family
            "@font-face { font-family: X; src: url(a.otf); }",                   // unquoted url
            "@font-face { font-family: X; src: url(\"a.otf\"), url(\"b.otf\"); }", // several sources
            "@font-face { font-family: X; src: url(\"a.otf\"); font-weight: bold; }", // unsupported descriptor
            "@media screen { .z { color: red; } }",                              // unknown at-rule
        };
        for (const char* src : srcs)
        {
            auto s = ParseStyleSheet(std::string(src) + "\n.b { color: red; }");
            EXPECT_TRUE(s.HasErrors()) << src;
            EXPECT_NE(s.GetStyle("b"), nullptr) << src;
            EXPECT_EQ(s.GetStyle("z"), nullptr) << src;
        }
    }

    TEST(StyleSheet, LineHeight)
    {
        auto s = ParseStyleSheet(".a { line-height: 1.5; }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_FLOAT_EQ(s.GetStyle("a")->states[ElementState::None].textStyle.lineHeight, 1.5f);
    }

    TEST(StyleSheet, LetterSpacing)
    {
        auto s = ParseStyleSheet(".a { letter-spacing: 2; }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_FLOAT_EQ(s.GetStyle("a")->states[ElementState::None].textStyle.letterSpacing, 2.0f);
    }

    // ============================================================================
    // Text decorations
    // ============================================================================

    TEST(StyleSheet, UnderlineProperties)
    {
        auto s = ParseStyleSheet(R"(
            .a {
                text-underline-color: #ff0000;
                text-underline-thickness: 2;
                text-underline-offset: 1;
                text-underline-style: dotted;
            }
        )");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        const auto& u = s.GetStyle("a")->states[ElementState::None].textStyle.underLine;
        const color4b uc = s.GetStyle("a")->states[ElementState::None].decorationColors[size_t(DecorationLine::Under)].GetFirstColor();
        EXPECT_EQ(uc.r, 255); EXPECT_EQ(uc.g, 0); EXPECT_EQ(uc.b, 0);
        EXPECT_EQ(u.thickness, 2u);
        EXPECT_EQ(u.offset, 1u);
        EXPECT_EQ(u.style, DecorationStyle::Dotted);
    }

    // ============================================================================
    // Pseudo-states
    // ============================================================================

    TEST(StyleSheet, HoverState)
    {
        auto s = ParseStyleSheet(R"(
            .btn { background-color: #333333; }
            .btn:hover { background-color: #555555; }
        )");
        ASSERT_NE(s.GetStyle("btn"), nullptr);
        const auto& normal = s.GetStyle("btn")->states[ElementState::None];
        const auto& hover  = s.GetStyle("btn")->states[ElementState::Hover];

        EXPECT_TRUE(GradientColorEq(normal.backgroundColor.gradient, 0, RGB(0x33, 0x33, 0x33)));
        EXPECT_TRUE(GradientColorEq(hover.backgroundColor.gradient,  0, RGB(0x55, 0x55, 0x55)));
    }

    TEST(StyleSheet, ActiveState)
    {
        auto s = ParseStyleSheet(R"(
            .btn { background-color: #333333; }
            .btn:active { background-color: #111111; }
        )");
        ASSERT_NE(s.GetStyle("btn"), nullptr);
        EXPECT_TRUE(GradientColorEq(
            s.GetStyle("btn")->states[ElementState::Active].backgroundColor.gradient, 0,
            RGB(0x11, 0x11, 0x11)));
    }

    TEST(StyleSheet, FocusState)
    {
        auto s = ParseStyleSheet(".a { } .a:focus { border-color: #0078d7; }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_TRUE(GradientColorEq(
            s.GetStyle("a")->states[ElementState::Focused].borderColors[0].gradient, 0,
            RGB(0x00, 0x78, 0xd7)));
    }

    TEST(StyleSheet, HighlightSelected)
    {
        auto s = ParseStyleSheet(".a::highlight(selected) { color: yellow; }");
        EXPECT_FALSE(s.HasErrors());
        ASSERT_NE(s.GetStyle("a"), nullptr);
        const auto c = s.GetStyle("a")->states[ElementState::Selected].textColor.GetFirstColor();
        EXPECT_EQ(c.r, 255); EXPECT_EQ(c.g, 255); EXPECT_EQ(c.b, 0);
    }

    TEST(StyleSheet, SingleColonSelected)
    {
        auto s = ParseStyleSheet(".a:selected { color: yellow; }");
        EXPECT_FALSE(s.HasErrors());
        ASSERT_NE(s.GetStyle("a"), nullptr);
        const auto c = s.GetStyle("a")->states[ElementState::Selected].textColor.GetFirstColor();
        EXPECT_EQ(c.r, 255); EXPECT_EQ(c.g, 255); EXPECT_EQ(c.b, 0);
    }

    TEST(StyleSheet, DisabledState)
    {
        auto s = ParseStyleSheet(".a:disabled { color: gray; }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        const auto c = s.GetStyle("a")->states[ElementState::Disabled].textColor.GetFirstColor();
        EXPECT_EQ(c.r, 128); EXPECT_EQ(c.g, 128); EXPECT_EQ(c.b, 128);
    }

    // A :disabled rule overrides only what it declares; the rest comes from the
    // base rule, as for every other state.
    TEST(StyleSheet, DisabledStateInheritsBaseDeclarations)
    {
        auto s = ParseStyleSheet(R"(
            .btn { font-size: 20; color: white; }
            .btn:disabled { color: gray; }
            .btn:hover { font-size: 30; }
        )");
        ASSERT_NE(s.GetStyle("btn"), nullptr);
        const StyleState& disabled = s.GetStyle("btn")->states[ElementState::Disabled];
        EXPECT_FLOAT_EQ(disabled.textStyle.fontSize, 20.0f) << "base declaration missing from :disabled";
        EXPECT_EQ(disabled.textColor.GetFirstColor().r, 128) << ":disabled must still override what it declares";
    }

    TEST(StyleSheet, PseudoStateDoesNotAffectNormal)
    {
        auto s = ParseStyleSheet(R"(
            .btn { font-size: 12; }
            .btn:hover { font-size: 14; }
        )");
        ASSERT_NE(s.GetStyle("btn"), nullptr);
        EXPECT_FLOAT_EQ(s.GetStyle("btn")->states[ElementState::None].textStyle.fontSize, 12.0f);
        EXPECT_FLOAT_EQ(s.GetStyle("btn")->states[ElementState::Hover].textStyle.fontSize,  14.0f);
    }

    TEST(StyleSheet, LayoutPropertiesNotStateSpecific)
    {
        // margin set in :hover block should still apply to the base style
        auto s = ParseStyleSheet(".a:hover { margin: 8; }");
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->margin.top, 8u);
    }

    // ============================================================================
    // Error handling
    // ============================================================================

    TEST(StyleSheet, UnknownPropertyIsError)
    {
        auto s = ParseStyleSheet(".a { not-a-property: 123; color: white; }");
        EXPECT_TRUE(s.HasErrors());
        // color should still be parsed after the bad property
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->states[ElementState::None].textColor.GetFirstColor().r, 255);
    }

    TEST(StyleSheet, UnknownColorIsError)
    {
        auto s = ParseStyleSheet(".a { color: not-a-color; padding: 4; }");
        EXPECT_TRUE(s.HasErrors());
        // padding should still be parsed
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->padding.top, 4u);
    }

    TEST(StyleSheet, UnknownPseudoStateIsError)
    {
        auto s = ParseStyleSheet(".a:flying { color: white; }");
        EXPECT_TRUE(s.HasErrors());
    }

    TEST(StyleSheet, MissingCloseBraceReportsError)
    {
        auto s = ParseStyleSheet(".a { color: white;");
        // Must not crash; an error or a partial result are both acceptable.
        EXPECT_TRUE(s.HasErrors() || s.styles.empty() || s.GetStyle("a") != nullptr);
    }

    TEST(StyleSheet, ErrorsHaveLineNumbers)
    {
        auto s = ParseStyleSheet("\n\n.a { bad-prop: 1; }");
        ASSERT_FALSE(s.errors.empty());
        EXPECT_GT(s.errors[0].line, 0);
    }

    // ============================================================================
    // Full rule integration
    // ============================================================================

    TEST(StyleSheet, FullButtonRule)
    {
        auto s = ParseStyleSheet(R"(
            .button {
                background-color: #3a3a3a;
                border-color: #555555;
                color: #ffffff;
                font-size: 14;
                font-weight: bold;
                font-family: sans-serif;
                padding: 4 8;
                border-width: 1;
                border-radius: 4;
                display: flex;
                align-items: center;
                justify-content: center;
                cursor: pointer;
                overflow: hidden;
            }
            .button:hover   { background-color: #4a4a4a; }
            .button:active  { background-color: #2a2a2a; }
            .button:disabled{ color: #888888; }
        )");

        EXPECT_FALSE(s.HasErrors());
        ASSERT_NE(s.GetStyle("button"), nullptr);
        const Style& btn = *s.GetStyle("button");

        // Base state
        EXPECT_TRUE(GradientColorEq(btn.states[ElementState::None].backgroundColor.gradient, 0, RGB(0x3a,0x3a,0x3a)));
        EXPECT_EQ(btn.states[ElementState::None].textColor.GetFirstColor().r, 0xff);
        EXPECT_FLOAT_EQ(btn.states[ElementState::None].textStyle.fontSize, 14.0f);
        EXPECT_EQ(btn.states[ElementState::None].textStyle.weight, FontWeight::Bold);
        EXPECT_EQ(btn.states[ElementState::None].textStyle.family, FontFamily::SansSerif);
        EXPECT_EQ(btn.padding.top, 4u); EXPECT_EQ(btn.padding.right, 8u);
        EXPECT_EQ(btn.borderWidths.top, 1u);
        EXPECT_EQ(btn.borderRadius.topLeft, 4u);
        EXPECT_EQ(btn.display, Display::Flex);
        EXPECT_EQ(btn.layoutData.flex.alignItems,     AlignItems::Center);
        EXPECT_EQ(btn.layoutData.flex.justifyContent, JustifyContent::Center);
        EXPECT_EQ(btn.cursor, MouseCursor::Pointer);
        EXPECT_EQ(btn.overflowX, Overflow::Hidden);
        EXPECT_EQ(btn.overflowY, Overflow::Hidden);

        // Hover state
        EXPECT_TRUE(GradientColorEq(btn.states[ElementState::Hover].backgroundColor.gradient, 0, RGB(0x4a,0x4a,0x4a)));

        // Active state
        EXPECT_TRUE(GradientColorEq(btn.states[ElementState::Active].backgroundColor.gradient, 0, RGB(0x2a,0x2a,0x2a)));

        // Disabled state
        EXPECT_EQ(btn.states[ElementState::Disabled].textColor.GetFirstColor().r, 0x88);
    }

    // ============================================================================
    // Custom properties (:root and var())
    // ============================================================================

    TEST(StyleSheet, RootCustomProperty)
    {
        auto s = ParseStyleSheet(R"(
            :root { --my-color: #ff8040; }
        )");
        EXPECT_FALSE(s.HasErrors());
        auto* prop = s.GetCustomProperty("--my-color");
        ASSERT_NE(prop, nullptr);
        EXPECT_EQ(prop->color.r, 0xff);
        EXPECT_EQ(prop->color.g, 0x80);
        EXPECT_EQ(prop->color.b, 0x40);
        EXPECT_EQ(prop->color.a, 0xff);
    }

    TEST(StyleSheet, RootMultipleProperties)
    {
        auto s = ParseStyleSheet(R"(
            :root {
                --primary: #112233;
                --accent: rgb(255, 128, 0);
            }
        )");
        EXPECT_FALSE(s.HasErrors());
        ASSERT_NE(s.GetCustomProperty("--primary"), nullptr);
        ASSERT_NE(s.GetCustomProperty("--accent"), nullptr);
        EXPECT_EQ(s.GetCustomProperty("--primary")->color.r, 0x11);
        EXPECT_EQ(s.GetCustomProperty("--accent")->color.r, 255);
        EXPECT_EQ(s.GetCustomProperty("--accent")->color.g, 128);
    }

    TEST(StyleSheet, VarInColor)
    {
        auto s = ParseStyleSheet(R"(
            :root { --text-color: #aabbcc; }
            .a { color: var(--text-color); }
        )");
        EXPECT_FALSE(s.HasErrors());
        ASSERT_NE(s.GetStyle("a"), nullptr);
        const auto c = s.GetStyle("a")->states[ElementState::None].textColor.GetFirstColor();
        EXPECT_EQ(c.r, 0xaa);
        EXPECT_EQ(c.g, 0xbb);
        EXPECT_EQ(c.b, 0xcc);
    }

    TEST(StyleSheet, VarInBackgroundColor)
    {
        auto s = ParseStyleSheet(R"(
            :root { --bg: #336699; }
            .a { background-color: var(--bg); }
        )");
        EXPECT_FALSE(s.HasErrors());
        ASSERT_NE(s.GetStyle("a"), nullptr);
        const auto& bg = s.GetStyle("a")->states[ElementState::None].backgroundColor;
        EXPECT_EQ(bg.type, ColorType::SolidColor);
        EXPECT_TRUE(GradientColorEq(bg.gradient, 0, RGB(0x33, 0x66, 0x99)));
    }

    TEST(StyleSheet, VarUndefinedIsError)
    {
        auto s = ParseStyleSheet(R"(
            .a { color: var(--undefined); }
        )");
        EXPECT_TRUE(s.HasErrors());
    }

    TEST(StyleSheet, VarInGradientStop)
    {
        auto s = ParseStyleSheet(R"(
            :root { --stop-color: red; }
            .a { background-color: linear-gradient(90deg, var(--stop-color) 0%, blue 100%); }
        )");
        EXPECT_FALSE(s.HasErrors());
        ASSERT_NE(s.GetStyle("a"), nullptr);
        const auto& bg = s.GetStyle("a")->states[ElementState::None].backgroundColor;
        EXPECT_EQ(bg.type, ColorType::LinearGradient);
        EXPECT_GE(bg.gradient.stopCount, 2);
    }

    TEST(StyleSheet, GetCustomPropertyReturnsNullForMissing)
    {
        auto s = ParseStyleSheet(":root { --a: white; }");
        EXPECT_EQ(s.GetCustomProperty("--nonexistent"), nullptr);
    }

    TEST(StyleSheet, RootNamedColor)
    {
        auto s = ParseStyleSheet(":root { --c: transparent; }");
        EXPECT_FALSE(s.HasErrors());
        ASSERT_NE(s.GetCustomProperty("--c"), nullptr);
        EXPECT_EQ(s.GetCustomProperty("--c")->color.a, 0);
    }

    TEST(StyleSheet, RootGradientProperty)
    {
        auto s = ParseStyleSheet(":root { --grad: linear-gradient(90deg, red 0%, blue 100%); }");
        EXPECT_FALSE(s.HasErrors());
        auto* prop = s.GetCustomProperty("--grad");
        ASSERT_NE(prop, nullptr);
        EXPECT_EQ(prop->colorStyle.type, ColorType::LinearGradient);
        EXPECT_GE(prop->colorStyle.gradient.stopCount, 2);
    }

    TEST(StyleSheet, VarInBorderWidthAndRadius)
    {
        auto s = ParseStyleSheet(R"(
            :root { --w: 3px; --r: 7px; }
            .a { border-width: var(--w); border-radius: var(--r); }
        )");
        EXPECT_FALSE(s.HasErrors());
        ASSERT_NE(s.GetStyle("a"), nullptr);
        const Style& a = *s.GetStyle("a");
        EXPECT_EQ(a.borderWidths.top, 3u);
        EXPECT_EQ(a.borderRadius.topLeft, 7u);
    }

    TEST(StyleSheet, VarInMultiValuePadding)
    {
        auto s = ParseStyleSheet(R"(
            :root { --py: 4px; --px: 8px; }
            .a { padding: var(--py) var(--px); }
        )");
        EXPECT_FALSE(s.HasErrors());
        ASSERT_NE(s.GetStyle("a"), nullptr);
        const Style& a = *s.GetStyle("a");
        EXPECT_EQ(a.padding.top, 4u);
        EXPECT_EQ(a.padding.right, 8u);
    }

    TEST(StyleSheet, VarInColumnGap)
    {
        auto s = ParseStyleSheet(R"(
            :root { --g: 12px; }
            .a { column-gap: var(--g); }
        )");
        EXPECT_FALSE(s.HasErrors());
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->layoutData.grid.column.gap, 12);
    }

    // ============================================================================
    // Grid property parsing
    // ============================================================================

    TEST(StyleSheet, ColumnGap)
    {
        auto s = ParseStyleSheet(".a { column-gap: 8; }");
        EXPECT_FALSE(s.HasErrors());
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->layoutData.grid.column.gap, 8);
    }

    TEST(StyleSheet, RowGap)
    {
        auto s = ParseStyleSheet(".a { row-gap: 12; }");
        EXPECT_FALSE(s.HasErrors());
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->layoutData.grid.row.gap, 12);
    }

    TEST(StyleSheet, GridColumnSpan)
    {
        auto s = ParseStyleSheet(".a { grid-column: span 3; }");
        EXPECT_FALSE(s.HasErrors());
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->layoutData.grid.column.span, 3);
    }

    TEST(StyleSheet, GridRowSpan)
    {
        auto s = ParseStyleSheet(".a { grid-row: span 2; }");
        EXPECT_FALSE(s.HasErrors());
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->layoutData.grid.row.span, 2);
    }

    TEST(StyleSheet, GridColumnStartAndSpan)
    {
        auto s = ParseStyleSheet(".a { grid-column: 1 / span 4; }");
        EXPECT_FALSE(s.HasErrors());
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->layoutData.grid.column.start, 1);
        EXPECT_EQ(s.GetStyle("a")->layoutData.grid.column.span, 4);
    }

    TEST(StyleSheet, GridRowStartAndSpan)
    {
        auto s = ParseStyleSheet(".a { grid-row: 2 / span 3; }");
        EXPECT_FALSE(s.HasErrors());
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->layoutData.grid.row.start, 2);
        EXPECT_EQ(s.GetStyle("a")->layoutData.grid.row.span, 3);
    }

    TEST(StyleSheet, GridColumnStartOnly)
    {
        auto s = ParseStyleSheet(".a { grid-column: 5; }");
        EXPECT_FALSE(s.HasErrors());
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->layoutData.grid.column.start, 5);
    }

    TEST(StyleSheet, JustifyItems)
    {
        auto s = ParseStyleSheet(".a { justify-items: center; }");
        EXPECT_FALSE(s.HasErrors());
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->layoutData.grid.justifyItems, JustifyContent::Center);
    }

    // ============================================================================
    // Alignment applies to both flex and grid
    // ============================================================================

    TEST(StyleSheet, AlignItemsAppliesToFlexAndGrid)
    {
        auto s = ParseStyleSheet(".a { align-items: center; }");
        EXPECT_FALSE(s.HasErrors());
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->layoutData.flex.alignItems, AlignItems::Center);
        EXPECT_EQ(s.GetStyle("a")->layoutData.grid.alignItems, AlignItems::Center);
    }

    TEST(StyleSheet, JustifyContentAppliesToFlexAndGrid)
    {
        auto s = ParseStyleSheet(".a { justify-content: space-between; }");
        EXPECT_FALSE(s.HasErrors());
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->layoutData.flex.justifyContent, JustifyContent::SpaceBetween);
        EXPECT_EQ(s.GetStyle("a")->layoutData.grid.justifyContent, JustifyContent::SpaceBetween);
    }

    // align-self is the element's own (cross-axis) alignment; it must not change
    // how the element, as a grid container, places its children horizontally.
    TEST(StyleSheet, AlignSelfDoesNotChangeGridChildPlacement)
    {
        auto s = ParseStyleSheet(".a { display: grid; align-self: center; }");
        EXPECT_FALSE(s.HasErrors());
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->alignSelf, AlignItems::Center);
        EXPECT_EQ(s.GetStyle("a")->layoutData.grid.alignItemsSelf, AlignItems::Stretch)
            << "grid children must still stretch horizontally";
    }

    // justify-items is what controls a grid's horizontal child placement.
    TEST(StyleSheet, JustifyItemsControlsGridChildStretch)
    {
        auto centered = ParseStyleSheet(".a { justify-items: center; }");
        EXPECT_FALSE(centered.HasErrors());
        ASSERT_NE(centered.GetStyle("a"), nullptr);
        EXPECT_NE(centered.GetStyle("a")->layoutData.grid.alignItemsSelf, AlignItems::Stretch);
        EXPECT_EQ(centered.GetStyle("a")->layoutData.grid.justifyItems, JustifyContent::Center);

        auto stretched = ParseStyleSheet(".a { justify-items: center; justify-items: stretch; }");
        EXPECT_FALSE(stretched.HasErrors());
        ASSERT_NE(stretched.GetStyle("a"), nullptr);
        EXPECT_EQ(stretched.GetStyle("a")->layoutData.grid.alignItemsSelf, AlignItems::Stretch);
    }

    TEST(StyleSheet, AlignSelfAppliesToFlexAndGrid)
    {
        auto s = ParseStyleSheet(".a { align-self: stretch; }");
        EXPECT_FALSE(s.HasErrors());
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->alignSelf, AlignItems::Stretch);
        EXPECT_EQ(s.GetStyle("a")->layoutData.grid.alignItemsSelf, AlignItems::Stretch);
    }

    // ============================================================================
    // flex-grow/shrink (single-value, CSS-compliant)
    // ============================================================================

    TEST(StyleSheet, FlexGrowSingleValue)
    {
        auto s = ParseStyleSheet(".a { flex-grow: 5; }");
        EXPECT_FALSE(s.HasErrors());
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->layoutData.flex.flexGrow, 5);
    }

    TEST(StyleSheet, FlexShrinkSingleValue)
    {
        auto s = ParseStyleSheet(".a { flex-shrink: 3; }");
        EXPECT_FALSE(s.HasErrors());
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->layoutData.flex.flexShrink, 3);
    }

    // ============================================================================
    // Direction property
    // ============================================================================

    TEST(StyleSheet, DirectionAuto)
    {
        auto s = ParseStyleSheet(".a { direction: auto; }");
        EXPECT_FALSE(s.HasErrors());
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->textLayoutStyle.textDirection, TextDirection::Auto);
    }

    TEST(StyleSheet, DirectionLtr)
    {
        auto s = ParseStyleSheet(".a { direction: ltr; }");
        EXPECT_FALSE(s.HasErrors());
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->textLayoutStyle.textDirection, TextDirection::LeftToRight);
    }

    TEST(StyleSheet, TextWrapNone)
    {
        auto s = ParseStyleSheet(".a { text-wrap: nowrap; }");
        EXPECT_FALSE(s.HasErrors());
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->textLayoutStyle.textWrapping, TextWrap::NoWrap);
    }

    // ============================================================================
    // dominant-baseline / vertical-align
    // ============================================================================

    TEST(StyleSheet, DominantBaseline)
    {
        auto s = ParseStyleSheet(".a { dominant-baseline: middle; }");
        EXPECT_FALSE(s.HasErrors());
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->textLayoutStyle.baseline, TextBaseline::Middle);
    }

    TEST(StyleSheet, VerticalAlign)
    {
        auto s = ParseStyleSheet(".a { vertical-align: text-top; }");
        EXPECT_FALSE(s.HasErrors());
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->textLayoutStyle.baseline, TextBaseline::TextTop);
    }

    TEST(StyleSheet, DominantBaselineAuto)
    {
        auto s = ParseStyleSheet(".a { dominant-baseline: auto; }");
        EXPECT_FALSE(s.HasErrors());
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->textLayoutStyle.baseline, TextBaseline::Alphabetic);
    }

    // ============================================================================
    // ::selection pseudo-element
    // ============================================================================

    TEST(StyleSheet, DoubleColonSelection)
    {
        auto s = ParseStyleSheet(".a::selection { color: #ff0000; }");
        EXPECT_FALSE(s.HasErrors());
        ASSERT_NE(s.GetStyle("a"), nullptr);
        // Selection state (index 17) should have the color set
        const auto c = s.GetStyle("a")->states[ElementState::Selection].textColor.GetFirstColor();
        EXPECT_EQ(c.r, 255); EXPECT_EQ(c.g, 0); EXPECT_EQ(c.b, 0);
    }

    TEST(StyleSheet, SingleColonSelectionFails)
    {
        auto s = ParseStyleSheet(".a:selection { color: #ff0000; }");
        EXPECT_TRUE(s.HasErrors());
    }

    // ============================================================================
    // :not() pseudo-class
    // ============================================================================

    TEST(StyleSheet, NotPseudoClass)
    {
        // :not(:hover) should apply to all states WITHOUT hover bit set
        auto s = ParseStyleSheet(R"(
            .a             { color: #000000; }
            .a:not(:hover) { color: #ff0000; }
        )");
        EXPECT_FALSE(s.HasErrors());
        ASSERT_NE(s.GetStyle("a"), nullptr);
        // State 0 (normal, no hover) -> should have red
        const auto c0 = s.GetStyle("a")->states[0].textColor.GetFirstColor();
        EXPECT_EQ(c0.r, 255); EXPECT_EQ(c0.g, 0); EXPECT_EQ(c0.b, 0);
        // State kHover (2) -> should still have black (not overridden by :not(:hover))
        const auto ch = s.GetStyle("a")->states[ElementState::Hover].textColor.GetFirstColor();
        EXPECT_EQ(ch.r, 0); EXPECT_EQ(ch.g, 0); EXPECT_EQ(ch.b, 0);
    }

    TEST(StyleSheet, NotCombinedWithPositive)
    {
        // :active:not(:hover) -> active bit set, hover bit NOT set
        auto s = ParseStyleSheet(R"(
            .a                     { color: #000000; }
            .a:active:not(:hover)  { color: #00ff00; }
        )");
        EXPECT_FALSE(s.HasErrors());
        ASSERT_NE(s.GetStyle("a"), nullptr);
        // State kActive (1) -> active set, hover not set -> green
        const auto ca = s.GetStyle("a")->states[ElementState::Active].textColor.GetFirstColor();
        EXPECT_EQ(ca.r, 0); EXPECT_EQ(ca.g, 255); EXPECT_EQ(ca.b, 0);
        // State kActive|kHover (3) -> active set, hover set -> should be black (not matched by :not(:hover))
        const auto cah = s.GetStyle("a")->states[ElementState::Active | ElementState::Hover].textColor.GetFirstColor();
        EXPECT_EQ(cah.r, 0); EXPECT_EQ(cah.g, 0); EXPECT_EQ(cah.b, 0);
    }

    // ============================================================================
    // line-height with units
    // ============================================================================

    TEST(StyleSheet, LineHeightNormal)
    {
        auto s = ParseStyleSheet(".a { line-height: normal; }");
        EXPECT_FALSE(s.HasErrors());
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_EQ(s.GetStyle("a")->states[0].textStyle.lineHeightType, LineHeight::MetricsRelative);
        EXPECT_FLOAT_EQ(s.GetStyle("a")->states[0].textStyle.lineHeight, 1.0f);
    }

    TEST(StyleSheet, LineHeightPx)
    {
        auto s = ParseStyleSheet(".a { line-height: 20px; }");
        EXPECT_FALSE(s.HasErrors());
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_FLOAT_EQ(s.GetStyle("a")->states[0].textStyle.lineHeight, 20.0f);
        EXPECT_EQ(s.GetStyle("a")->states[0].textStyle.lineHeightType, LineHeight::Absolute);
    }

    TEST(StyleSheet, LineHeightEm)
    {
        auto s = ParseStyleSheet(".a { line-height: 1.5em; }");
        EXPECT_FALSE(s.HasErrors());
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_FLOAT_EQ(s.GetStyle("a")->states[0].textStyle.lineHeight, 1.5f);
        EXPECT_EQ(s.GetStyle("a")->states[0].textStyle.lineHeightType, LineHeight::FontSizeRelative);
    }

    TEST(StyleSheet, LineHeightBareNumber)
    {
        auto s = ParseStyleSheet(".a { line-height: 1.2; }");
        EXPECT_FALSE(s.HasErrors());
        ASSERT_NE(s.GetStyle("a"), nullptr);
        EXPECT_FLOAT_EQ(s.GetStyle("a")->states[0].textStyle.lineHeight, 1.2f);
        EXPECT_EQ(s.GetStyle("a")->states[0].textStyle.lineHeightType, LineHeight::FontSizeRelative);
    }

    // ============================================================================
    // Malformed numeric input reports errors instead of throwing
    // ============================================================================

    TEST(StyleSheet, RgbWithMissingComponentsIsAnError)
    {
        for (const char* src : { ".a { color: rgb(1,2); }", ".a { color: rgba(1,2,3); }", ".a { color: rgb(1,,3); }" })
        {
            StyleSheet s;
            EXPECT_NO_THROW(s = ParseStyleSheet(src)) << src;
            EXPECT_TRUE(s.HasErrors()) << src;
        }
    }

    TEST(StyleSheet, OutOfRangeNumbersAreErrors)
    {
        for (const char* src : { ".a { column-gap: 99999999999999999999; }",
                                 ".a { flex: 1 99999999999999999999999999999999999999999999; }",
                                 ".a { color: rgb(99999999999999999999,0,0); }" })
        {
            StyleSheet s;
            EXPECT_NO_THROW(s = ParseStyleSheet(src)) << src;
            EXPECT_TRUE(s.HasErrors()) << src;
        }
    }

    // ============================================================================
    // box-shadow and :focus-visible
    // ============================================================================

    TEST(StyleSheet, BoxShadowTakesOffsetsBlurSpreadAndAColor)
    {
        auto s = ParseStyleSheet(".a { box-shadow: 2px -3px 8px 1px rgba(16, 24, 40, .5); }");
        ASSERT_FALSE(s.HasErrors()) << s.errors[0].message;
        const BoxShadow& b = s.GetStyle("a")->states[ElementState::None].boxShadow[0];
        EXPECT_EQ(b.x, 2);
        EXPECT_EQ(b.y, -3);
        EXPECT_EQ(b.blur, 8);
        EXPECT_EQ(b.spread, 1);
        EXPECT_EQ(b.color, RGB(16, 24, 40, 128));
        EXPECT_TRUE(s.GetStyle("a")->states[ElementState::None].boxShadow[1].IsNone());
    }

    TEST(StyleSheet, BoxShadowColorMayComeFirstAndBlurIsOptional)
    {
        auto s = ParseStyleSheet(".a { box-shadow: #FF0000 0 1px; }");
        ASSERT_FALSE(s.HasErrors());
        const BoxShadow& b = s.GetStyle("a")->states[ElementState::None].boxShadow[0];
        EXPECT_EQ(b.y, 1);
        EXPECT_EQ(b.blur, 0);
        EXPECT_EQ(b.color, RGB(255, 0, 0));
    }

    // The first layer is the top one, as in CSS.
    TEST(StyleSheet, BoxShadowTakesTwoLayersPerState)
    {
        auto s = ParseStyleSheet(R"(
            .a { box-shadow: 0 0 0 3px blue, 0 8px 20px black; }
            .a:hover { box-shadow: none; }
        )");
        ASSERT_FALSE(s.HasErrors());
        const Style* a = s.GetStyle("a");
        EXPECT_EQ(a->states[ElementState::None].boxShadow[0].spread, 3);
        EXPECT_EQ(a->states[ElementState::None].boxShadow[1].blur, 20);
        EXPECT_TRUE(a->states[ElementState::Hover].boxShadow[0].IsNone());
        EXPECT_TRUE(a->states[ElementState::Hover].boxShadow[1].IsNone());
        EXPECT_FALSE(a->states[ElementState::Focused].boxShadow[0].IsNone());
    }

    // Palettes keep shadows in variables: one layer, none, or a color for a layer.
    TEST(StyleSheet, BoxShadowVariables)
    {
        auto s = ParseStyleSheet(R"(
            :root {
                --shadow: 0 4px 14px rgba(16, 24, 40, 0.10);
                --flat: none;
                --ring-color: #6B9BFF4D;
            }
            .a { box-shadow: var(--shadow); }
            .b { box-shadow: var(--flat); }
            .c { box-shadow: 0 0 0 3px var(--ring-color), var(--shadow); }
        )");
        ASSERT_FALSE(s.HasErrors()) << s.errors[0].message;
        const BoxShadow& a = s.GetStyle("a")->states[ElementState::None].boxShadow[0];
        EXPECT_EQ(a.y, 4);
        EXPECT_EQ(a.blur, 14);
        EXPECT_EQ(a.color, RGB(16, 24, 40, 26));
        EXPECT_TRUE(s.GetStyle("b")->states[ElementState::None].boxShadow[0].IsNone());
        const Style* c = s.GetStyle("c");
        EXPECT_EQ(c->states[ElementState::None].boxShadow[0].color, RGB(0x6B, 0x9B, 0xFF, 0x4D));
        EXPECT_EQ(c->states[ElementState::None].boxShadow[1].blur, 14);
    }

    TEST(StyleSheet, UnsupportedBoxShadowsAreErrors)
    {
        for (const char* src : { ".a { box-shadow: inset 0 1px 2px black; }",
                                 ".a { box-shadow: 0 1px 2px; }",
                                 ".a { box-shadow: 1px black; }",
                                 ".a { box-shadow: 0 1px -2px black; }",
                                 ".a { box-shadow: 0 1px 2px 3px 4px black; }",
                                 ".a { box-shadow: 0 1px black, 0 2px black, 0 3px black; }" })
        {
            StyleSheet s;
            EXPECT_NO_THROW(s = ParseStyleSheet(src)) << src;
            EXPECT_TRUE(s.HasErrors()) << src;
        }
    }

    // :focus-visible sets the focus ring, apart from :focus.
    TEST(StyleSheet, FocusVisibleSetsTheFocusRing)
    {
        auto s = ParseStyleSheet(R"(
            .a { box-shadow: 0 1px 2px black; }
            .a:focus-visible { box-shadow: 0 0 0 3px blue; }
        )");
        ASSERT_FALSE(s.HasErrors());
        const Style* a = s.GetStyle("a");
        EXPECT_EQ(a->focusRing.spread, 3);
        EXPECT_EQ(a->focusRing.color, RGB(0, 0, 255));
        EXPECT_EQ(a->states[ElementState::Focused].boxShadow[0].blur, 2) << ":focus keeps its own shadows";
    }

    TEST(StyleSheet, FocusVisibleTakesOneBoxShadowAndNoOtherState)
    {
        for (const char* src : { ".a:focus-visible { color: red; }",
                                 ".a:focus-visible { box-shadow: 0 0 0 3px blue, 0 1px 2px black; }",
                                 ".a:hover:focus-visible { box-shadow: 0 0 0 3px blue; }" })
        {
            StyleSheet s;
            EXPECT_NO_THROW(s = ParseStyleSheet(src)) << src;
            EXPECT_TRUE(s.HasErrors()) << src;
        }
    }

    // Rows and menu items take an outline inside them: an inset ring of spread only.
    TEST(StyleSheet, FocusVisibleTakesAnInsetOutline)
    {
        auto s = ParseStyleSheet(".a:focus-visible { box-shadow: inset 0 0 0 1px blue; }");
        ASSERT_FALSE(s.HasErrors()) << s.errors[0].message;
        EXPECT_TRUE(s.GetStyle("a")->focusRing.inset);
        EXPECT_EQ(s.GetStyle("a")->focusRing.spread, 1);

        for (const char* src : { ".a:focus-visible { box-shadow: inset 0 1px 0 1px blue; }",
                                 ".a:focus-visible { box-shadow: inset 0 0 2px 1px blue; }",
                                 ".a:focus-visible { box-shadow: inset 0 0 0 0 blue; }" })
            EXPECT_TRUE(ParseStyleSheet(src).HasErrors()) << src;
    }

    TEST(StyleSheet, VerticalTrim)
    {
        auto s = ParseStyleSheet(".a { -tachyon-vertical-trim: cap-to-baseline; } .b { -tachyon-vertical-trim: normal; }");
        ASSERT_FALSE(s.HasErrors()) << s.errors[0].message;
        EXPECT_EQ(s.GetStyle("a")->textLayoutStyle.verticalTrim, VerticalTrim::CapToBaseline);
        EXPECT_EQ(s.GetStyle("b")->textLayoutStyle.verticalTrim, VerticalTrim::Default);
        EXPECT_TRUE(ParseStyleSheet(".c { -tachyon-vertical-trim: sideways; }").HasErrors());
    }

    // A number may start with its decimal point, as in rgba(0, 0, 0, .35).
    TEST(StyleSheet, NumbersMayStartWithADecimalPoint)
    {
        auto s = ParseStyleSheet(".a { color: rgba(0, 0, 0, .5); }");
        ASSERT_FALSE(s.HasErrors());
        EXPECT_EQ(s.GetStyle("a")->states[ElementState::None].textColor.GetFirstColor(), RGB(0, 0, 0, 128));
    }

    // ============================================================================
    // :first-child / :last-child / :only-child
    // ============================================================================

    TEST(StyleSheet, ChildRulesBuildTheChildStyles)
    {
        auto s = ParseStyleSheet(R"(
            .a { padding: 2; margin: 3; }
            .a:first-child { padding-left: 10; }
            .a:last-child  { padding-right: 20; }
        )");
        ASSERT_FALSE(s.HasErrors()) << s.errors[0].message;
        ASSERT_TRUE(s.childStyles.contains("a"));
        const ChildStyles& c = s.childStyles.at("a");

        EXPECT_EQ(s.GetStyle("a")->padding.left,  2u);
        EXPECT_EQ(s.GetStyle("a")->padding.right, 2u);

        EXPECT_EQ(c.first.padding.left,  10u);
        EXPECT_EQ(c.first.padding.right, 2u);
        EXPECT_EQ(c.last.padding.left,   2u);
        EXPECT_EQ(c.last.padding.right,  20u);
        // An only child is both the first and the last.
        EXPECT_EQ(c.only.padding.left,   10u);
        EXPECT_EQ(c.only.padding.right,  20u);
        for (const Style* v : { &c.first, &c.last, &c.only })
            EXPECT_EQ(v->margin.top, 3u);
    }

    TEST(StyleSheet, OnlyChildRuleAppliesToTheOnlyChildAlone)
    {
        auto s = ParseStyleSheet(".a { padding: 2; } .a:only-child { padding: 7; }");
        ASSERT_FALSE(s.HasErrors()) << s.errors[0].message;
        const ChildStyles& c = s.childStyles.at("a");
        EXPECT_EQ(c.first.padding.top, 2u);
        EXPECT_EQ(c.last.padding.top,  2u);
        EXPECT_EQ(c.only.padding.top,  7u);

        // :first-child:last-child is the same as :only-child.
        auto both = ParseStyleSheet(".a { padding: 2; } .a:first-child:last-child { padding: 7; }");
        ASSERT_FALSE(both.HasErrors()) << both.errors[0].message;
        const ChildStyles& b = both.childStyles.at("a");
        EXPECT_EQ(b.first.padding.top, 2u);
        EXPECT_EQ(b.last.padding.top,  2u);
        EXPECT_EQ(b.only.padding.top,  7u);
    }

    // A child rule wins over the class's own rules wherever they are, as its selector is more specific.
    TEST(StyleSheet, ChildRulesTakeLaterBaseRules)
    {
        auto s = ParseStyleSheet(R"(
            .a:first-child { border-width: 4; }
            .a { border-width: 1; margin: 5; }
            * { cursor: pointer; }
        )");
        ASSERT_FALSE(s.HasErrors()) << s.errors[0].message;
        const ChildStyles& c = s.childStyles.at("a");
        EXPECT_EQ(c.first.borderWidths.top, 4u);
        EXPECT_EQ(c.first.margin.top,       5u);
        EXPECT_EQ(c.first.cursor,           MouseCursor::Pointer);
        EXPECT_EQ(c.last.borderWidths.top,  1u);
        EXPECT_EQ(c.last.margin.top,        5u);
        EXPECT_EQ(c.last.cursor,            MouseCursor::Pointer);
        EXPECT_EQ(s.GetStyle("a")->borderWidths.top, 1u);
    }

    TEST(StyleSheet, ChildRulesTakeStates)
    {
        auto s = ParseStyleSheet(R"(
            .a { background-color: #000000; }
            .a:first-child:hover { background-color: #ff0000; }
        )");
        ASSERT_FALSE(s.HasErrors()) << s.errors[0].message;
        const ChildStyles& c = s.childStyles.at("a");
        EXPECT_TRUE(GradientColorEq(c.first.states[ElementState::None].backgroundColor.gradient, 0, RGB(0, 0, 0)));
        EXPECT_TRUE(GradientColorEq(c.first.states[ElementState::Hover].backgroundColor.gradient,  0, RGB(255, 0, 0)));
        EXPECT_TRUE(GradientColorEq(c.last.states[ElementState::Hover].backgroundColor.gradient,   0, RGB(0, 0, 0)));
    }

    TEST(StyleSheet, ChildRulesOnlyTakeBoxProperties)
    {
        auto s = ParseStyleSheet(".a { padding: 2; } .a:first-child { padding: 6; color: red; width: 10; }");
        EXPECT_EQ(s.errors.size(), 2u);
        const ChildStyles& c = s.childStyles.at("a");
        EXPECT_EQ(c.first.padding.top, 6u);   // the box property is still taken
        EXPECT_EQ(c.first.states[ElementState::None].textColor.GetFirstColor(), s.GetStyle("a")->states[ElementState::None].textColor.GetFirstColor());
        EXPECT_EQ(c.first.layoutData.width, s.GetStyle("a")->layoutData.width);

        for (const char* src : { ".a:first-child { font-size: 20; }",
                                 ".a:last-child { box-shadow: inset 0 0 0 1px blue; }",
                                 "*:first-child { padding: 1; }",
                                 ".a:first-child:focus-visible { box-shadow: 0 0 0 1px blue; }" })
            EXPECT_TRUE(ParseStyleSheet(src).HasErrors()) << src;
        for (const char* src : { ".a:first-child { background-color: red; border-color: red; border-radius: 3; }",
                                 ".a:first-child { box-shadow: 0 1px 2px black; -tachyon-border-dash-size: 4px; }",
                                 ".a:last-child { margin: 1; padding: 1; gap: 2; row-gap: 2; column-gap: 2; cursor: pointer; }" })
            EXPECT_FALSE(ParseStyleSheet(src).HasErrors()) << src;
    }

    // ============================================================================
    // text-shadow
    // ============================================================================

    TEST(StyleSheet, TextShadow)
    {
        auto s = ParseStyleSheet(R"(
            :root { --glow: 0 0 6px #00ff00; }
            .a { text-shadow: 1px 2px 3px red, #0000ff -1 0; }
            .a:hover { text-shadow: var(--glow); }
            .b { text-shadow: 1 1 black; }
            .b:active { text-shadow: none; }
        )");
        ASSERT_FALSE(s.HasErrors()) << s.errors[0].message;
        const Style& a = *s.GetStyle("a");
        const BoxShadow& first  = a.states[ElementState::None].textShadow[0];
        const BoxShadow& second = a.states[ElementState::None].textShadow[1];
        EXPECT_EQ(first.x, 1);
        EXPECT_EQ(first.y, 2);
        EXPECT_EQ(first.blur, 3u);
        EXPECT_EQ(first.color, RGB(255, 0, 0));
        EXPECT_EQ(second.x, -1);
        EXPECT_EQ(second.blur, 0u);
        EXPECT_EQ(second.color, RGB(0, 0, 255));
        // A box-shadow is a property of its own.
        EXPECT_TRUE(a.states[ElementState::None].boxShadow[0].IsNone());

        const BoxShadow& glow = a.states[ElementState::Hover].textShadow[0];
        EXPECT_EQ(glow.x, 0);
        EXPECT_EQ(glow.blur, 6u);
        EXPECT_EQ(glow.color, RGB(0, 255, 0));
        EXPECT_TRUE(a.states[ElementState::Hover].textShadow[1].IsNone());

        EXPECT_EQ(s.GetStyle("b")->states[ElementState::None].textShadow[0].blur, 0u);
        EXPECT_TRUE(s.GetStyle("b")->states[ElementState::Active].textShadow[0].IsNone());
    }

    TEST(StyleSheet, TextShadowErrors)
    {
        for (const char* src : { ".a { text-shadow: 0 0 2px 1px red; }",       // no spread
                                 ".a { text-shadow: inset 0 0 2px red; }",       // never inset
                                 ".a { text-shadow: 1px red; }",                 // needs x and y
                                 ".a { text-shadow: 1 1 -2 red; }",              // negative blur
                                 ".a { text-shadow: 1 1 red, 2 2 red, 3 3 red; }", // two layers at most
                                 ":root { --s: 0 0 0 2px red; } .a { text-shadow: var(--s); }",
                                 ".a:first-child { text-shadow: 1 1 red; }" })   // not a box property
            EXPECT_TRUE(ParseStyleSheet(src).HasErrors()) << src;

        // The error names the property.
        auto s = ParseStyleSheet(".a { text-shadow: 1px red; }");
        ASSERT_FALSE(s.errors.empty());
        EXPECT_NE(s.errors[0].message.find("text-shadow"), std::string::npos) << s.errors[0].message;
    }

    TEST(StyleSheet, ClassesWithoutChildRulesHaveNoChildStyles)
    {
        auto s = ParseStyleSheet(".a { padding: 2; } .b { padding: 2; } .b:last-child { padding: 3; } .c:first-child { margin: 1; }");
        ASSERT_FALSE(s.HasErrors()) << s.errors[0].message;
        EXPECT_FALSE(s.childStyles.contains("a"));
        EXPECT_TRUE(s.childStyles.contains("b"));
        // A class written only with child rules still exists.
        ASSERT_NE(s.GetStyle("c"), nullptr);
        EXPECT_EQ(s.childStyles.at("c").first.margin.top, 1u);
        EXPECT_EQ(s.GetStyle("c")->margin.top, 0u);
    }
}
