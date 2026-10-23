#include <gtest/gtest.h>
#include <gui_core/style/gui_style.h>
#include <gui_core/gui_base_types.h>

#include <vector>
#include <span>
#include <cmath>
#include <algorithm>

namespace interstice
{

    static uint8_t pack_float_to_byte(float v)
    {
        v = std::max(0.0f, std::min(1.0f, v));
        return static_cast<uint8_t>(std::lround(v * 255.0f));
    }

    TEST(GradientUnit, SingleStopProducesConstantRow)
    {
        Gradient g;
        // RGBA = (0.2, 0.4, 0.6, 0.8)
        g.AddStop(color4f{ 0.2f, 0.4f, 0.6f, 0.8f }, 0.0f);

        std::vector<color4b> out(16);
        g.WriteTo(std::span<color4b>(out));

        uint8_t ex_r = pack_float_to_byte(0.2f);
        uint8_t ex_g = pack_float_to_byte(0.4f);
        uint8_t ex_b = pack_float_to_byte(0.6f);
        uint8_t ex_a = pack_float_to_byte(0.8f);

        for (auto& px : out)
        {
            EXPECT_EQ(px.r, ex_r);
            EXPECT_EQ(px.g, ex_g);
            EXPECT_EQ(px.b, ex_b);
            EXPECT_EQ(px.a, ex_a);
        }
    }

    TEST(GradientUnit, BlackToWhiteUniformSteps)
    {
        Gradient g;
        g.AddStop(color4f{ 0.0f, 0.0f, 0.0f, 1.0f }, 0.0f);
        g.AddStop(color4f{ 1.0f, 1.0f, 1.0f, 1.0f }, 1.0f);

        const size_t width = 5;
        std::vector<color4b> out(width);
        g.WriteTo(std::span<color4b>(out));

        // Expect values at t = 0, 0.25, 0.5, 0.75, 1.0
        const std::array<int, 5> expected_bytes = {
            pack_float_to_byte(0.0f),
            pack_float_to_byte(0.25f),
            pack_float_to_byte(0.5f),
            pack_float_to_byte(0.75f),
            pack_float_to_byte(1.0f)
        };

        for (size_t i = 0; i < width; ++i)
        {
            EXPECT_EQ(out[i].r, static_cast<uint8_t>(expected_bytes[i]));
            EXPECT_EQ(out[i].g, static_cast<uint8_t>(expected_bytes[i]));
            EXPECT_EQ(out[i].b, static_cast<uint8_t>(expected_bytes[i]));
            EXPECT_EQ(out[i].a, pack_float_to_byte(1.0f)); // alpha stays 1.0
        }
    }

    TEST(GradientUnit, HandlesEmptyStops)
    {
        Gradient g;
        std::vector<color4b> out(4);
        g.WriteTo(std::span<color4b>(out));
        for (auto& px : out)
        {
            EXPECT_EQ(px.r, 0);
            EXPECT_EQ(px.g, 0);
            EXPECT_EQ(px.b, 0);
            EXPECT_EQ(px.a, 0);
        }
    }

    TEST(GradientExtra, OutOfOrderStopsProduceSameRow)
    {
        Gradient a, b;
        // three stops with distinct offsets
        a.AddStop(color4f{ 0.1f, 0.2f, 0.3f, 0.4f }, 0.0f);
        a.AddStop(color4f{ 0.9f, 0.8f, 0.7f, 0.6f }, 0.6f);
        a.AddStop(color4f{ 0.4f, 0.5f, 0.6f, 0.7f }, 1.0f);

        // add them in reverse order to b
        b.AddStop(color4f{ 0.4f, 0.5f, 0.6f, 0.7f }, 1.0f);
        b.AddStop(color4f{ 0.9f, 0.8f, 0.7f, 0.6f }, 0.6f);
        b.AddStop(color4f{ 0.1f, 0.2f, 0.3f, 0.4f }, 0.0f);

        const size_t width = 64;
        std::vector<color4b> outA(width), outB(width);
        a.WriteTo(std::span<color4b>(outA));
        b.WriteTo(std::span<color4b>(outB));

        EXPECT_EQ(outA.size(), outB.size());
        for (size_t i = 0; i < width; ++i)
            EXPECT_EQ(memcmp(&outA[i], &outB[i], sizeof(color4b)), 0);
    }

    TEST(GradientExtra, AddStopOverflow)
    {
        Gradient g;
        // fill up to kMaxStops
        for (int i = 0; i < static_cast<int>(Gradient::kMaxStops); ++i)
        {
            bool ok = g.AddStop(color4f{ float(i) / 10.0f, 0.0f, 0.0f, 1.0f }, float(i) / (Gradient::kMaxStops - 1));
            EXPECT_TRUE(ok) << "AddStop failed before reaching kMaxStops at index " << i;
        }
        // next add should fail
        bool ok = g.AddStop(color4f{ 1.0f,1.0f,1.0f,1.0f }, 1.0f);
        EXPECT_FALSE(ok);
    }

    TEST(GradientExtra, SizeOneReturnsFirstStop)
    {
        Gradient g;
        g.AddStop(color4f{ 0.25f, 0.5f, 0.75f, 0.5f }, 0.0f);
        g.AddStop(color4f{ 1.0f, 0.0f, 0.0f, 1.0f }, 1.0f);

        std::vector<color4b> out(1);
        g.WriteTo(std::span<color4b>(out));

        uint8_t r = pack_float_to_byte(0.25f);
        uint8_t gr = pack_float_to_byte(0.5f);
        uint8_t b = pack_float_to_byte(0.75f);
        uint8_t a = pack_float_to_byte(0.5f);

        EXPECT_EQ(out[0].r, r);
        EXPECT_EQ(out[0].g, gr);
        EXPECT_EQ(out[0].b, b);
        EXPECT_EQ(out[0].a, a);
    }

    // Negative angles wrap around the circle: -90deg is the same direction as 270deg.
    TEST(GradientAngleTest, NegativeAnglesWrap)
    {
        EXPECT_EQ(Gradient::Linear(-90.0f).value,  Gradient::Linear(270.0f).value);
        EXPECT_EQ(Gradient::Linear(-450.0f).value, Gradient::Linear(270.0f).value);
        EXPECT_EQ(Gradient::Radial(-90.0f).value,  Gradient::Radial(270.0f).value);
    }

    // Gradients interpolate with premultiplied alpha (as CSS does): fading a
    // colour to transparent keeps its hue instead of darkening on the way.
    TEST(GradientUnit, FadeToTransparentKeepsTheColour)
    {
        for (ColorSpace cs : { ColorSpace::sRGB, ColorSpace::LinearRGB, ColorSpace::Oklab })
        {
            Gradient g;
            g.AddStop(color4f{ 1.0f, 0.0f, 0.0f, 1.0f }, 0.0f);
            g.AddStop(color4f{ 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f);
            g.WithColorSpace(cs);

            std::vector<color4b> out(9);
            g.WriteTo(std::span<color4b>(out));
            const color4b mid = out[4];
            EXPECT_GE(mid.r, 250) << "colour space " << int(cs);
            EXPECT_LE(mid.g, 5);
            EXPECT_NEAR(mid.a, 128, 3);
        }
    }
}
