// Real-shaping tests for SkribidiTextSystem (HarfBuzz + bidi via the actual
// font stack). Assertions are STRUCTURAL - monotonicity, non-zero, ordering -
// never absolute pixel goldens, so they hold across font versions and the
// available glyph coverage. (Wrap-mode measurement is tested in
// skribidi_text_measurement_test.cpp.)

#include "skribidi_test_fixture.h"

#include <cstring>

#include <gui_core/text/skribidi/internal/gui_skribidi_context.h>

#include <skribidi/skb_attributes.h>

namespace interstice
{
    using GUITest::SkribidiHeadlessTest;

    namespace
    {
        // u8"" literals are char8_t[] in C++20; reinterpret the bytes as a
        // UTF-8 std::string (the text system takes UTF-8 bytes).
        std::string U8(const char8_t* s)
        {
            return std::string(reinterpret_cast<const char*>(s));
        }

        // Five Arabic letters are 10 UTF-8 bytes; anything else means the source
        // wasn't read as UTF-8 (MSVC needs /utf-8) and the literals are garbled.
        static_assert(sizeof(u8"مرحبا") == 11);

        TEST_F(SkribidiHeadlessTest, EmptyStringMeasuresZeroWidth)
        {
            StyleRef style = MakeTextStyle();
            EXPECT_EQ(MeasureWidth("", style, 1), 0);
        }

        TEST_F(SkribidiHeadlessTest, LatinWidthGrowsWithContent)
        {
            StyleRef style = MakeTextStyle();
            const int32_t w1 = MeasureWidth("i",   style, 10);
            const int32_t w3 = MeasureWidth("iii", style, 11);
            const int32_t w6 = MeasureWidth("iiiiii", style, 12);
            EXPECT_GT(w1, 0);
            EXPECT_GT(w3, w1) << "more characters must measure wider";
            EXPECT_GT(w6, w3);
        }

        TEST_F(SkribidiHeadlessTest, WiderGlyphsMeasureWider)
        {
            StyleRef style = MakeTextStyle();
            // Proportional fonts: a run of wide glyphs is wider than the same
            // count of narrow ones.
            const int32_t narrow = MeasureWidth("iiii", style, 20);
            const int32_t wide   = MeasureWidth("WWWW", style, 21);
            EXPECT_GT(narrow, 0);
            EXPECT_GT(wide, narrow)
                << "'WWWW' should measure wider than 'iiii' in a proportional font";
        }

        TEST_F(SkribidiHeadlessTest, LargerFontMeasuresWider)
        {
            StyleRef small = MakeTextStyle(10.0f);
            StyleRef big   = MakeTextStyle(28.0f);
            const int32_t ws = MeasureWidth("Mango", small, 30);
            const int32_t wb = MeasureWidth("Mango", big,   31);
            EXPECT_GT(ws, 0);
            EXPECT_GT(wb, ws) << "the same text shapes wider at a larger font size";
        }

        TEST_F(SkribidiHeadlessTest, WhitespaceAddsWidth)
        {
            StyleRef style = MakeTextStyle();
            const int32_t joined  = MeasureWidth("ab",  style, 40);
            const int32_t spaced  = MeasureWidth("a b", style, 41);
            EXPECT_GT(joined, 0);
            EXPECT_GT(spaced, joined) << "an interword space adds advance width";
        }

        TEST_F(SkribidiHeadlessTest, MixedScriptRunMeasuresNonZero)
        {
            StyleRef style = MakeTextStyle();
            // Latin + digits + an Arabic word. Even where the loaded fonts lack
            // full coverage (tofu boxes), the run must shape to a positive,
            // bounded width - never the layout-box width and never zero.
            const int32_t w = MeasureWidth(U8(u8"abc 123 مرحبا"), style, 50);
            EXPECT_GT(w, 0);
            EXPECT_LT(w, 16000) << "must be the natural extent, not the layout box";
        }

        TEST_F(SkribidiHeadlessTest, RTLParagraphMeasuresSanely)
        {
            StyleRef style = MakeTextStyle();
            // Pure Arabic (RTL). Bidi resolution + shaping must yield a
            // positive, bounded width.
            const int32_t w = MeasureWidth(U8(u8"مرحبا"), style, 60);
            EXPECT_GT(w, 0);
            EXPECT_LT(w, 16000);
        }

        // A theme reload registers its icons again; they must stay valid.
        TEST_F(SkribidiHeadlessTest, ReregisteringAnIconNameReturnsTheSameIcon)
        {
            const char* svg = R"(<svg viewBox="0 0 8 8" xmlns="http://www.w3.org/2000/svg"><path d="M2,1 L7,4 L2,7 Z" fill="#909090"/></svg>)";
            const IconID first  = textSystem->RegisterIconFromSVG("arrow", svg, strlen(svg));
            const IconID second = textSystem->RegisterIconFromSVG("arrow", svg, strlen(svg));
            ASSERT_NE(first, IconID::Invalid);
            EXPECT_EQ(second, first);
            EXPECT_TRUE(textSystem->IsValidIcon(second));

            const IconID other = textSystem->RegisterIconFromSVG("arrow-2", svg, strlen(svg));
            EXPECT_NE(other, first) << "a different name is a different icon";
            EXPECT_TRUE(textSystem->IsValidIcon(other));
            EXPECT_FALSE(textSystem->IsValidIcon(IconID::Invalid));
        }

        TEST_F(SkribidiHeadlessTest, GenericFamiliesRenderLatinText)
        {
            uint64_t seed = 200;
            for (FontFamily family : { FontFamily::SansSerif, FontFamily::Serif, FontFamily::Monospace, FontFamily::Math })
                EXPECT_GT(MeasureWidth("Mango", MakeTextStyle(14.0f, FontID::Invalid, family), seed++), 0)
                    << "a generic family with no fonts drops its text; family " << int(family);
        }

        TEST_F(SkribidiHeadlessTest, CommonSymbolsHaveGlyphs)
        {
            // Missing glyphs all draw as the same box, so a real U+2605 / U+2713 measures
            // differently from a run of unassigned codepoints (U+0378).
            StyleRef style = MakeTextStyle(28.0f);
            const int32_t missing = MeasureWidth(U8(u8"͸͸͸"), style, 210);
            EXPECT_NE(MeasureWidth(U8(u8"★★★"), style, 211), missing);
            EXPECT_NE(MeasureWidth(U8(u8"✓✓✓"), style, 212), missing);
        }

        TEST_F(SkribidiHeadlessTest, KoreanAndChineseHaveGlyphs)
        {
            StyleRef style = MakeTextStyle(28.0f);
            const int32_t missing = MeasureWidth(U8(u8"͸͸"), style, 220);
            EXPECT_NE(MeasureWidth(U8(u8"한국"), style, 221), missing);
            EXPECT_NE(MeasureWidth(U8(u8"中文"), style, 222), missing);
            EXPECT_NE(MeasureWidth(U8(u8"かな"), style, 223), missing);
            EXPECT_NE(MeasureWidth(U8(u8"ㄅㄆㄇ"), style, 224), missing) << "Bopomofo";
        }

        // A style's own language reaches skribidi as the run's language attribute.
        TEST(SkribidiFonts, StyleLanguageBecomesTheRunLanguage)
        {
            TextStyle style{ .fontSize = 14.0f };
            style.language = LanguageTag("zh-Hant");
            skb_attribute_t attributes[32] = {};
            const std::unordered_map<uint32_t, uint8_t> noFamilies;
            const int count = style_to_attributes(&style, attributes, 0, noFamilies);
            const skb_attribute_set_t set{ .attributes = attributes, .attributes_count = count };
            ASSERT_NE(skb_attributes_get_lang(set, nullptr), nullptr);
            EXPECT_STREQ(skb_attributes_get_lang(set, nullptr), "zh-hant") << "HarfBuzz lower-cases tags";

            const int plain = style_to_attributes(&(const TextStyle&)TextStyle{ .fontSize = 14.0f }, attributes, 0, noFamilies);
            const skb_attribute_set_t plainSet{ .attributes = attributes, .attributes_count = plain };
            EXPECT_EQ(skb_attributes_get_lang(plainSet, nullptr), nullptr) << "no language of its own: the default applies";
        }

        // `font-family: "Inter", monospace`: the generic family applies while the named one
        // isn't registered, and the named one wins once it is.
        TEST(SkribidiFonts, UnregisteredFamilyFallsBackToGenericFamily)
        {
            TextStyle style{ .fontSize = 14.0f };
            style.fontId = FontIDFromName("Inter");
            style.family = FontFamily::Monospace;
            skb_attribute_t attributes[32] = {};

            const std::unordered_map<uint32_t, uint8_t> noFamilies;
            const int count = style_to_attributes(&style, attributes, 0, noFamilies);
            const skb_attribute_set_t set{ .attributes = attributes, .attributes_count = count };
            EXPECT_EQ(skb_attributes_get_font_family(set, nullptr), SKB_FONT_FAMILY_MONOSPACE);

            const std::unordered_map<uint32_t, uint8_t> inter{ { static_cast<uint32_t>(style.fontId), uint8_t(7) } };
            const int registered = style_to_attributes(&style, attributes, 0, inter);
            const skb_attribute_set_t registeredSet{ .attributes = attributes, .attributes_count = registered };
            EXPECT_EQ(skb_attributes_get_font_family(registeredSet, nullptr), 7);
        }

        TEST_F(SkribidiHeadlessTest, ChangingTheDefaultLanguageKeepsCjkRendering)
        {
            StyleRef style = MakeTextStyle(28.0f);
            const int32_t missing = MeasureWidth(U8(u8"͸͸"), style, 230);
            ASSERT_NE(MeasureWidth(U8(u8"漢字"), style, 231), missing);

            textSystem->SetLanguage("ja-JP"); // lays all text out again
            EXPECT_NE(MeasureWidth(U8(u8"漢字"), style, 232), missing);
            EXPECT_NE(MeasureWidth(U8(u8"かな"), style, 233), missing);
        }

        TEST_F(SkribidiHeadlessTest, ArabicShapesWithARealArabicFont)
        {
            // Missing glyphs all draw as the same box; real Arabic letters differ in width.
            StyleRef style = MakeTextStyle(28.0f);
            EXPECT_NE(MeasureWidth(U8(u8"ممممم"), style, 61), MeasureWidth(U8(u8"ااااا"), style, 62));
        }

        TEST_F(SkribidiHeadlessTest, FallbackFontDoesNotStretchTheLine)
        {
            // The default line height is a multiple of the font size, so a run in a
            // fallback font with taller metrics keeps the line the same height.
            StyleRef style = MakeTextStyle();
            auto height = [&](std::string_view text, uint64_t seed)
            {
                const TextID tid = textSystem->RegisterText(ID{ .value{ .integer = seed } }, IconID::Invalid,
                                                            text, style, ElementState::None);
                return textSystem->GetTextMeasurement(tid).minHeight;
            };
            const int32_t latin = height("Mg", 63);
            EXPECT_GT(latin, 0);
            EXPECT_EQ(height(U8(u8"Mg مرحبا"), 64), latin);
            EXPECT_EQ(height(U8(u8"Mg नमस्ते"), 65), latin);
        }

        TEST_F(SkribidiHeadlessTest, NewlineProducesMultipleLines)
        {
            StyleRef style = MakeTextStyle();
            ID id = ID{ .value{ .integer = 70 } };
            TextID one = textSystem->RegisterText(id, IconID::Invalid, "line",
                                                  style, ElementState::None);
            ID id2 = ID{ .value{ .integer = 71 } };
            TextID two = textSystem->RegisterText(id2, IconID::Invalid, "line\nline\nline",
                                                  style, ElementState::None);
            const auto m1 = textSystem->GetTextMeasurement(one);
            const auto m3 = textSystem->GetTextMeasurement(two);
            EXPECT_GT(m1.minHeight, 0);
            EXPECT_GT(m3.minHeight, m1.minHeight)
                << "three explicit lines must be taller than one";
        }

        // The text system builds rich-text layouts lazily, frames after the
        // caller's run strings are gone, so it must not keep views into them.
        TEST_F(SkribidiHeadlessTest, RichTextKeepsItsOwnCopyOfRunText)
        {
            StyleRef style = MakeTextStyle();
            const int32_t helloWidth = MeasureWidth("Hello", style, 80);

            std::string callerText = "Hello";
            ContentRun run{ .type = ContentRun::Text, .text = callerText };
            ID id = ID{ .value{ .integer = 81 } };
            TextID tid = textSystem->RegisterRichText(id, std::span<ContentRun>(&run, 1),
                                                      style, ElementState::None);
            ASSERT_NE(tid, TextID::Invalid);

            callerText.assign("WWWWW");   // the caller's frame-lifetime buffer is reused

            EXPECT_EQ(textSystem->GetTextMeasurement(tid).maxWidth, helloWidth);
        }

        // A run's text-style override is in CSS pixels like any style, so its text
        // grows with the UI scale.
        TEST_F(SkribidiHeadlessTest, TextStyleOverridesFollowTheUIScale)
        {
            const StyleRef style = MakeTextStyle();
            TextStyle bigger{ .fontSize = 20.0f };
            ContentRun run{ .type = ContentRun::Text, .text = "Hello", .textStyleOverride = &bigger };
            auto measure = [&](uint64_t seed) {
                const TextID tid = textSystem->RegisterRichText(ID{ .value{ .integer = seed } },
                                                                std::span<ContentRun>(&run, 1), style, ElementState::None);
                return textSystem->GetTextMeasurement(tid).maxWidth;
            };
            const int32_t atOne = measure(95);
            styleManager.SetUIScale(2.0f);
            const int32_t atTwo = measure(96);
            EXPECT_NEAR(atTwo, 2 * atOne, 3);
        }

        // Inline icon sizes in content runs are CSS pixels too.
        TEST_F(SkribidiHeadlessTest, InlineIconSizesFollowTheUIScale)
        {
            const char* svg = R"(<svg viewBox="0 0 16 16" xmlns="http://www.w3.org/2000/svg"><rect width="16" height="16"/></svg>)";
            const IconID icon = textSystem->RegisterIconFromSVG("scale-test", svg, strlen(svg));
            ASSERT_NE(icon, IconID::Invalid);
            const StyleRef style = MakeTextStyle();
            ContentRun run{ .type = ContentRun::Icon, .iconID = icon, .iconWidth = 20.0f, .iconHeight = 20.0f };
            auto measure = [&](uint64_t seed) {
                const TextID tid = textSystem->RegisterRichText(ID{ .value{ .integer = seed } },
                                                                std::span<ContentRun>(&run, 1), style, ElementState::None);
                return textSystem->GetTextMeasurement(tid).maxWidth;
            };
            const int32_t atOne = measure(97);
            styleManager.SetUIScale(2.0f);
            const int32_t atTwo = measure(98);
            EXPECT_NEAR(atTwo, 2 * atOne, 2);
        }

        // A label whose text changes every frame (an fps counter, say) leaves an
        // entry per value behind; entries that stop being used are evicted so the
        // registry stays bounded, while a label used every frame keeps its ID.
        TEST_F(SkribidiHeadlessTest, RegistryEvictsTextsThatAreNoLongerUsed)
        {
            StyleRef style = MakeTextStyle();
            const ID counter{ .value{ .integer = 90 } };
            const ID stable { .value{ .integer = 91 } };
            TextID stableId = TextID::Invalid;
            for (uint32_t frame = 0; frame < 1000; ++frame)
            {
                textSystem->BeginFrame(int2{}, MouseButtons::None, frame * 16);
                const std::string text = "fps " + std::to_string(frame);
                (void)textSystem->RegisterText(counter, IconID::Invalid, text, style, ElementState::None);
                const TextID id = textSystem->RegisterText(stable, IconID::Invalid, "stable", style, ElementState::None);
                if (frame == 0) stableId = id;
                ASSERT_EQ(id, stableId) << "a text registered every frame keeps its ID";
            }
            EXPECT_LT(textSystem->RegisteredTextCount(), 300u);
        }

        // ---- Registered font families ----------------------------------------

        constexpr std::string_view kSerifFace = "fonts/Noto-otf/NotoSerifDisplay-Regular.otf";

        TEST_F(SkribidiHeadlessTest, RegisteredFamilyIsUsedForItsText)
        {
            const std::string_view faces[] = { kSerifFace };
            const FontID serif = textSystem->RegisterFontFamily("Serif Display", faces);
            ASSERT_EQ(serif, FontIDFromName("Serif Display"));

            const int32_t sans  = MeasureWidth("WWWWWWWWmmmmmmmmiiiiiiii", MakeTextStyle(40.0f), 1);
            const int32_t other = MeasureWidth("WWWWWWWWmmmmmmmmiiiiiiii", MakeTextStyle(40.0f, serif), 2);
            EXPECT_GT(other, 0);
            EXPECT_NE(other, sans) << "text in the registered family must shape with its face";
        }

        TEST_F(SkribidiHeadlessTest, ReregisteringWithOtherFacesSwapsTheFamily)
        {
            const std::string_view serifFaces[] = { kSerifFace };
            const std::string_view sansFaces[]  = { "fonts/Noto-otf/NotoSans-Regular.otf" };
            const std::string_view missing[]    = { "fonts/does-not-exist.otf" };
            const std::string_view text = "WWWWWWWWmmmmmmmmiiiiiiii";

            // Another family registered before the swap must survive the rebuild.
            const FontID other = textSystem->RegisterFontFamily("Bystander", serifFaces);
            const StyleRef otherStyle = MakeTextStyle(40.0f, other);
            const int32_t otherWidth = MeasureWidth(text, otherStyle, 10);

            const FontID id = textSystem->RegisterFontFamily("Swappable", serifFaces);
            const StyleRef style = MakeTextStyle(40.0f, id);
            const int32_t asSerif = MeasureWidth(text, style, 1);

            EXPECT_EQ(textSystem->RegisterFontFamily("Swappable", sansFaces), id);
            const int32_t asSans = MeasureWidth(text, style, 2);
            EXPECT_EQ(asSans, MeasureWidth(text, MakeTextStyle(40.0f), 3));
            EXPECT_NE(asSans, asSerif);
            EXPECT_EQ(MeasureWidth(text, otherStyle, 11), otherWidth);

            const std::string arabic = U8(u8"مرحبا");
            EXPECT_EQ(MeasureWidth(arabic, MakeTextStyle(40.0f, id), 12), MeasureWidth(arabic, MakeTextStyle(40.0f), 13))
                << "fallback fonts load again after the rebuild";

            EXPECT_EQ(textSystem->RegisterFontFamily("Swappable", missing), FontID::Invalid);
            EXPECT_EQ(MeasureWidth(text, style, 4), asSans) << "a failed swap keeps the previous faces";
            EXPECT_EQ(MeasureWidth(text, otherStyle, 14), otherWidth);
        }

        TEST_F(SkribidiHeadlessTest, ReregisteringAFamilyNameReturnsTheSameID)
        {
            const std::string_view faces[] = { kSerifFace };
            const FontID first  = textSystem->RegisterFontFamily("Serif Display", faces);
            const FontID second = textSystem->RegisterFontFamily("SERIF DISPLAY", faces);
            EXPECT_NE(first, FontID::Invalid);
            EXPECT_EQ(second, first);
        }

        TEST_F(SkribidiHeadlessTest, FamilyWithoutLoadableFacesIsNotRegistered)
        {
            const std::string_view missing[] = { "fonts/does-not-exist.otf" };
            EXPECT_EQ(textSystem->RegisterFontFamily("Ghost", missing), FontID::Invalid);

            const std::string_view faces[] = { kSerifFace };
            EXPECT_EQ(textSystem->RegisterFontFamily("Ghost", faces), FontIDFromName("Ghost"))
                << "a failed registration must not reserve the name";
        }

        TEST_F(SkribidiHeadlessTest, RegisteredFamilyFallsBackForScriptsItLacks)
        {
            // The serif face has no Arabic. Shaping Arabic in the default family
            // first must not stop the registered family from loading its own
            // Arabic fallback, so both shape with the same Noto Arabic face.
            const std::string_view faces[] = { kSerifFace };
            const FontID serif = textSystem->RegisterFontFamily("Serif Display", faces);
            ASSERT_NE(serif, FontID::Invalid);

            const std::string arabic = U8(u8"مرحبا");
            const int32_t inDefault = MeasureWidth(arabic, MakeTextStyle(), 1);
            const int32_t inSerif   = MeasureWidth(arabic, MakeTextStyle(14.0f, serif), 2);
            EXPECT_GT(inDefault, 0);
            EXPECT_EQ(inSerif, inDefault);
        }

    } // namespace
}

#include <harfbuzz/hb.h>
namespace interstice { namespace {
    // The CJK fallback must be a full Noto Sans CJK font: subset fonts keep one
    // region's glyphs, so they can't switch letterforms by language (locl).
    TEST(SkribidiFonts, CjkFontHasRegionalFormsByLanguage)
    {
        hb_blob_t* blob = hb_blob_create_from_file_or_fail("fonts/Noto-otf/NotoSansCJKsc-Regular.otf");
        ASSERT_NE(blob, nullptr);
        hb_face_t* face = hb_face_create(blob, 0);
        hb_font_t* font = hb_font_create(face);
        auto glyphFor = [&](uint32_t codepoint, const char* language)
        {
            hb_buffer_t* buffer = hb_buffer_create();
            hb_buffer_add_codepoints(buffer, &codepoint, 1, 0, 1);
            hb_buffer_set_direction(buffer, HB_DIRECTION_LTR);
            hb_buffer_set_script(buffer, HB_SCRIPT_HAN);
            hb_buffer_set_language(buffer, hb_language_from_string(language, -1));
            hb_shape(font, buffer, nullptr, 0);
            unsigned count = 0;
            const uint32_t glyph = hb_buffer_get_glyph_infos(buffer, &count)[0].codepoint;
            hb_buffer_destroy(buffer);
            return glyph;
        };

        // U+6B21 has a distinct form in each of the four regions.
        const uint32_t ja = glyphFor(0x6B21, "ja"), hans = glyphFor(0x6B21, "zh-Hans");
        const uint32_t hant = glyphFor(0x6B21, "zh-Hant"), ko = glyphFor(0x6B21, "ko");
        EXPECT_NE(ja, hans);
        EXPECT_NE(ja, hant);
        EXPECT_NE(ja, ko);
        EXPECT_NE(hans, hant);
        EXPECT_NE(hans, ko);
        EXPECT_NE(hant, ko);
        // U+76F4 has its own Hong Kong form.
        EXPECT_NE(glyphFor(0x76F4, "zh-HK"), glyphFor(0x76F4, "zh-Hant"));

        hb_font_destroy(font);
        hb_face_destroy(face);
        hb_blob_destroy(blob);
    }
}}
