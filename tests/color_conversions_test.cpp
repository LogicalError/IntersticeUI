#include <gtest/gtest.h>

#include <gui_core/style/gui_color_conversions.h>

namespace GUI
{
    // LabToRGB / OklabToRGB must not clamp the signed a/b channels to >= 0 (that
    // collapses blue and green hues); a round-trip must preserve the color.

    TEST(ColorConversionsTest, OklabRoundTripPreservesBlue)
    {
        float L, a, b;
        ColorConvert::RGBtoOklab(0.0f, 0.0f, 1.0f, L, a, b);
        auto rgb = ColorConvert::OklabToRGB(L, a, b);
        EXPECT_NEAR(rgb[2], 1.0f, 0.05f) << "blue channel lost in Oklab round-trip";
        EXPECT_LT(rgb[0], 0.3f);
        EXPECT_LT(rgb[1], 0.3f);
    }

    TEST(ColorConversionsTest, OklabRoundTripPreservesGreen)
    {
        float L, a, b;
        ColorConvert::RGBtoOklab(0.0f, 1.0f, 0.0f, L, a, b);
        auto rgb = ColorConvert::OklabToRGB(L, a, b);
        EXPECT_NEAR(rgb[1], 1.0f, 0.05f) << "green channel lost in Oklab round-trip";
        EXPECT_LT(rgb[0], 0.3f);
        EXPECT_LT(rgb[2], 0.3f);
    }

    TEST(ColorConversionsTest, LabRoundTripPreservesBlue)
    {
        float L, a, b;
        ColorConvert::RGBtoLab(0.0f, 0.0f, 1.0f, L, a, b);
        auto rgb = ColorConvert::LabToRGB(L, a, b);
        EXPECT_NEAR(rgb[2], 1.0f, 0.05f) << "blue channel lost in Lab round-trip";
        EXPECT_LT(rgb[0], 0.3f);
        EXPECT_LT(rgb[1], 0.3f);
    }

    // Achromatic input (grey/black/white) must still produce value/lightness.
    TEST(ColorConversionsTest, HSVOfGreySetsValueAndSaturation)
    {
        float h = -1.0f, s = -1.0f, v = -1.0f;
        ColorConvert::RGBtoHSV(0.5f, 0.5f, 0.5f, h, s, v);
        EXPECT_FLOAT_EQ(h, 0.0f);
        EXPECT_FLOAT_EQ(s, 0.0f);
        EXPECT_FLOAT_EQ(v, 0.5f);
    }

    TEST(ColorConversionsTest, HSLOfWhiteSetsLightness)
    {
        float h = -1.0f, s = -1.0f, l = -1.0f;
        ColorConvert::RGBtoHSL(1.0f, 1.0f, 1.0f, h, s, l);
        EXPECT_FLOAT_EQ(h, 0.0f);
        EXPECT_FLOAT_EQ(s, 0.0f);
        EXPECT_FLOAT_EQ(l, 1.0f);
    }

}
