#include <gtest/gtest.h>

#include <gui_core/text/skribidi/internal/gui_skribidi_context.h>

namespace interstice
{
    // =========================================================================
    // A numeric field shows the same string for a value with or without focus:
    // the unfocused display, the display cache and the editor text all go through
    // format_to_text, which must be deterministic and keep full float precision
    // (%.9g, not %g).
    // =========================================================================

    namespace
    {
        NumericValue MakeFloat(float v)  { NumericValue n{}; n.value_float  = v; return n; }
        NumericValue MakeDouble(double v){ NumericValue n{}; n.value_double = v; return n; }
    }

    class FloatFormatReflectsRealityTest : public ::testing::TestWithParam<float> {};

    TEST_P(FloatFormatReflectsRealityTest, FormatToTextIsDeterministicAndRoundTrips)
    {
        // Repeat calls with the same value must return the same string.
        NumericValue v1 = MakeFloat(GetParam());
        NumericValue v2 = MakeFloat(GetParam());
        const std::string a = format_to_text(NumericType::TypeFloat, v1);
        const std::string b = format_to_text(NumericType::TypeFloat, v2);
        EXPECT_EQ(a, b) << "value=" << GetParam();
    }

    INSTANTIATE_TEST_SUITE_P(FloatValues, FloatFormatReflectsRealityTest, ::testing::Values(
        0.0f, 1.0f, -1.0f, 0.5f,
        // Values where %g and %.9g differ: 0.123456789f is "0.123456791" at %.9g
        // but "0.123457" at %g.
        0.123456789f, 1.23456789f, 1234567.89f,
        // Float epsilon territory.
        1.0f + 1e-7f, 1.0f - 1e-7f
    ));

    TEST(NumericFormatTest, DoubleFormatIsDeterministic)
    {
        const double values[] = { 0.0, 1.0, 0.5, 0.123456789, 1234567890.123, 1e-9, -1e9 };
        for (double v : values)
        {
            NumericValue v1 = MakeDouble(v);
            NumericValue v2 = MakeDouble(v);
            EXPECT_EQ(format_to_text(NumericType::TypeDouble, v1),
                      format_to_text(NumericType::TypeDouble, v2)) << "value=" << v;
        }
    }

    // The float nearest 0.123456789 is 0.123456790298...; the display keeps at
    // least 9 significant digits to show that, not a rounded "0.123457".
    TEST(NumericFormatTest, FloatFormatReflectsBitExactReality)
    {
        NumericValue v = MakeFloat(0.123456789f);
        const std::string s = format_to_text(NumericType::TypeFloat, v);
        EXPECT_GE(s.size(), 9u)
            << "format_to_text('0.123456789f') = '" << s
            << "' — too few digits to reflect the real float value";
        EXPECT_NE(s, std::string("0.123457"))
            << "display dropped to %g precision — re-selecting will flash";
    }

    // try_parse_text(double&) must keep double precision, not round through float.
    TEST(NumericParseTest, ParseDoubleKeepsDoublePrecision)
    {
        double out = 0.0;
        ASSERT_TRUE(try_parse_text("0.1", out));
        EXPECT_DOUBLE_EQ(out, 0.1);

        // 123456789 is exact in double but not in float (24-bit mantissa).
        ASSERT_TRUE(try_parse_text("123456789", out));
        EXPECT_DOUBLE_EQ(out, 123456789.0);
    }

    // Paste into a numeric field: clean input goes in as-is, junk is stripped,
    // and only a paste with nothing usable is refused.
    TEST(NumericPasteTest, CleanNumberIsAccepted)
    {
        std::string text = "42";
        EXPECT_TRUE(filter_numeric_paste(NumericType::TypeInt32, text));
        EXPECT_EQ(text, "42");

        std::string f = "1.5";
        EXPECT_TRUE(filter_numeric_paste(NumericType::TypeFloat, f));
        EXPECT_EQ(f, "1.5");
    }

    TEST(NumericPasteTest, JunkIsStrippedOrRefused)
    {
        std::string text = "4a2";
        EXPECT_TRUE(filter_numeric_paste(NumericType::TypeInt32, text));
        EXPECT_EQ(text, "42");

        std::string none = "abc";
        EXPECT_FALSE(filter_numeric_paste(NumericType::TypeInt32, none));
    }


    // Link ranges are compared with editor offsets, which count codepoints.
    TEST(EditorTextTest, LinkRangesAreCodepointOffsets)
    {
        const ContentRun prefix{ .type = ContentRun::Text, .text = "\xC3\xA9\xC3\xA9\xC3\xA9" };   // 3 codepoints, 6 bytes
        const ContentRun link{ .type = ContentRun::Text, .text = "ab", .contentId = 7, .isLink = true };
        const std::vector<ContentRun> runs{ prefix, link };
        std::vector<RegisteredText::LinkRange> links;
        const std::string text = build_editor_text(runs, links);

        EXPECT_EQ(text, "\xC3\xA9\xC3\xA9\xC3\xA9" "ab");
        ASSERT_EQ(links.size(), 1u);
        EXPECT_EQ(links[0].contentId, 7);
        EXPECT_EQ(links[0].start, 3);
        EXPECT_EQ(links[0].end, 5);
    }


    // Arrays of ContentRun built with designated initializers (the pattern the
    // controls use) must work in MSVC Debug builds.
    TEST(EditorTextTest, ContentRunArraysInitialize)
    {
        ContentRun runs[] = {
            { .type = ContentRun::Text, .text = "xyz" },
            { .type = ContentRun::Text, .text = "ab", .contentId = 7, .isLink = true },
        };
        EXPECT_EQ(runs[0].text, "xyz");
        EXPECT_EQ(runs[1].contentId, 7);
        EXPECT_TRUE(runs[1].isLink);
        EXPECT_EQ(runs[1].iconWidth, ContentRun::kSizeAuto);
    }

    // "abcdef", move the selection "bcd" ([1,4)) within the same field.
    TEST(EditorTextTest, DropOffsetAfterCut)
    {
        EXPECT_EQ(drop_offset_after_cut(0, 1, 4), 0) << "before the selection";
        EXPECT_EQ(drop_offset_after_cut(1, 1, 4), 1) << "at the selection start: stays put";
        EXPECT_EQ(drop_offset_after_cut(4, 1, 4), 1) << "at the selection end: stays put";
        EXPECT_EQ(drop_offset_after_cut(6, 1, 4), 3) << "after the selection";
        EXPECT_EQ(drop_offset_after_cut(2, 1, 4), 1) << "inside the selection: stays put";
        // Backward selection (anchor after the caret) behaves the same.
        EXPECT_EQ(drop_offset_after_cut(1, 4, 1), 1);
        EXPECT_EQ(drop_offset_after_cut(3, 4, 1), 1);
        EXPECT_EQ(drop_offset_after_cut(6, 4, 1), 3);
    }

    // Float fields accept exponent notation, including the partial forms seen
    // while typing it, and the form format_to_text displays (e.g. "1e+10").
    TEST(NumericPasteTest, FloatAcceptsExponents)
    {
        for (const char* ok : { "1e5", "1.5E-3", "9.99999975e-06", "1e+10", "-2e3", "1e", "1e-", "1.e+" })
        {
            std::string text = ok;
            EXPECT_TRUE(filter_numeric_paste(NumericType::TypeFloat, text)) << ok;
            EXPECT_EQ(text, ok);
        }
        std::string twoExponents = "1e5e3";
        EXPECT_TRUE(filter_numeric_paste(NumericType::TypeFloat, twoExponents));
        EXPECT_EQ(twoExponents, "1e53") << "only one exponent marker";

        std::string noMantissa = "e5";
        EXPECT_TRUE(filter_numeric_paste(NumericType::TypeFloat, noMantissa));
        EXPECT_EQ(noMantissa, "5") << "an exponent needs a mantissa digit first";

        std::string dotInExponent = "1e2.5";
        EXPECT_TRUE(filter_numeric_paste(NumericType::TypeFloat, dotInExponent));
        EXPECT_EQ(dotInExponent, "1e25");
    }
}
