// SVG strokes through the NanoSVG bridge: caps, joins, dashes and overlaps,
// checked by rasterizing each icon as a mask at 1:1 and sampling pixels.

#include <gtest/gtest.h>

#include <gui_core/text/skribidi/internal/nanosvg_bridge.h>

#include <skribidi/skb_common.h>
#include <skribidi/skb_icon_collection.h>
#include <skribidi/skb_rasterizer.h>

#include <cstring>
#include <string>
#include <vector>

namespace
{
    // A 32 x 16 icon holding `body`, rasterized as a mask.
    class StrokedIcon
    {
    public:
        explicit StrokedIcon(const std::string& body)
        {
            const std::string svg = R"(<svg xmlns="http://www.w3.org/2000/svg" width="32" height="16" viewBox="0 0 32 16">)"
                                  + body + "</svg>";
            skb_icon_collection_t* icons = skb_icon_collection_create();
            const skb_icon_handle_t handle = interstice::RegisterNanoSVG(icons, "test", svg.data(), svg.size());
            pixels.assign(size_t(kWidth * kHeight), 0);
            if (handle)
            {
                skb_rasterizer_config_t config = skb_rasterizer_get_default_config();
                skb_rasterizer_t* rasterizer = skb_rasterizer_create(&config);
                skb_temp_alloc_t* temp = skb_temp_alloc_create(64 * 1024);
                skb_image_t image{ pixels.data(), kWidth, kHeight, kWidth, 1 };
                skb_rasterizer_draw_alpha_icon(rasterizer, temp, skb_icon_collection_get_icon(icons, handle),
                                               skb_vec2_t{ 1.f, 1.f }, SKB_RASTERIZE_ALPHA_MASK, 0, 0, &image);
                skb_temp_alloc_destroy(temp);
                skb_rasterizer_destroy(rasterizer);
            }
            skb_icon_collection_destroy(icons);
        }

        // Coverage of the pixel whose top-left corner is (x, y), 0..255.
        int At(int x, int y) const { return pixels[size_t(y * kWidth + x)]; }

    private:
        static constexpr int kWidth = 32, kHeight = 16;
        std::vector<uint8_t> pixels;
    };

    constexpr int kCovered = 200;
    constexpr int kEmpty   = 60;

    const char* const kLine = R"(<path d="M8 8 L24 8" fill="none" stroke="#000" stroke-width="4" )";

    TEST(SvgStroke, ButtCapsEndAtTheEndPoints)
    {
        const StrokedIcon icon(std::string(kLine) + "/>");
        EXPECT_GE(icon.At(16, 7), kCovered);
        EXPECT_LE(icon.At(6, 7), kEmpty);
        EXPECT_LE(icon.At(25, 7), kEmpty);
    }

    TEST(SvgStroke, RoundCapsAddAHalfCircle)
    {
        const StrokedIcon icon(std::string(kLine) + R"(stroke-linecap="round"/>)");
        EXPECT_GE(icon.At(6, 7), kCovered) << "on the axis, inside the cap";
        EXPECT_GE(icon.At(25, 7), kCovered);
        EXPECT_LE(icon.At(6, 6) + icon.At(6, 9), 2 * 150) << "the cap is round, not square";
    }

    TEST(SvgStroke, SquareCapsExtendByHalfTheWidth)
    {
        const StrokedIcon icon(std::string(kLine) + R"(stroke-linecap="square"/>)");
        EXPECT_GE(icon.At(6, 6), kCovered);
        EXPECT_GE(icon.At(25, 9), kCovered);
        EXPECT_LE(icon.At(4, 7), kEmpty);
    }

    const char* const kCorner = R"(<path d="M4 4 L16 4 L16 14" fill="none" stroke="#000" stroke-width="4" )";

    TEST(SvgStroke, MiterJoinsFillTheCorner)
    {
        const StrokedIcon icon(std::string(kCorner) + "/>");
        EXPECT_GE(icon.At(17, 2), kCovered);
    }

    TEST(SvgStroke, RoundJoinsRoundTheCorner)
    {
        const StrokedIcon icon(std::string(kCorner) + R"(stroke-linejoin="round"/>)");
        EXPECT_LT(icon.At(17, 2), 128);
        EXPECT_GE(icon.At(17, 3), kCovered) << "the arc still reaches out on the axis";
        EXPECT_GE(icon.At(16, 2), kCovered);
    }

    TEST(SvgStroke, BevelJoinsCutTheCorner)
    {
        const StrokedIcon icon(std::string(kCorner) + R"(stroke-linejoin="bevel"/>)");
        EXPECT_LE(icon.At(17, 2), kEmpty);
        EXPECT_GE(icon.At(15, 3), kCovered);
    }

    TEST(SvgStroke, DashesLeaveGaps)
    {
        const StrokedIcon icon(R"(<path d="M0 8 L32 8" fill="none" stroke="#000" stroke-width="2" stroke-dasharray="4 4"/>)");
        EXPECT_GE(icon.At(1, 7), kCovered);
        EXPECT_LE(icon.At(5, 7), kEmpty);
        EXPECT_GE(icon.At(9, 7), kCovered);
        EXPECT_LE(icon.At(13, 7), kEmpty);
    }

    TEST(SvgStroke, DashOffsetShiftsThePattern)
    {
        const StrokedIcon icon(R"(<path d="M0 8 L32 8" fill="none" stroke="#000" stroke-width="2" stroke-dasharray="4 4" stroke-dashoffset="4"/>)");
        EXPECT_LE(icon.At(1, 7), kEmpty);
        EXPECT_GE(icon.At(5, 7), kCovered);
    }

    TEST(SvgStroke, AClosedStrokeLeavesTheInsideEmpty)
    {
        const StrokedIcon icon(R"(<rect x="4" y="2" width="12" height="12" fill="none" stroke="#000" stroke-width="2"/>)");
        EXPECT_LE(icon.At(10, 8), kEmpty);
        EXPECT_GE(icon.At(4, 8), kCovered);
        EXPECT_GE(icon.At(3, 1), kCovered) << "the corner is mitred";
    }

    TEST(SvgStroke, OverlappingPartsDontCancel)
    {
        // A hairpin: the way back overlaps the way out.
        const StrokedIcon icon(R"(<path d="M2 7 L30 7 L2 9" fill="none" stroke="#000" stroke-width="4"/>)");
        for (int x : { 6, 16, 26 })
            EXPECT_GE(icon.At(x, 7), kCovered) << x;
    }

    // Lucide icons set the stroke on the root element and use currentColor.
    TEST(SvgStroke, LucideMarkupStrokesWithTheRootsSettings)
    {
        const std::string svg = R"(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" )"
                                R"(stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M5 12h14"/></svg>)";
        skb_icon_collection_t* icons = skb_icon_collection_create();
        const skb_icon_handle_t handle = interstice::RegisterNanoSVG(icons, "minus", svg.data(), svg.size());
        ASSERT_NE(handle, 0u);
        const skb_icon_t* icon = skb_icon_collection_get_icon(icons, handle);
        std::vector<uint8_t> pixels(24 * 24, 0);
        skb_rasterizer_config_t config = skb_rasterizer_get_default_config();
        skb_rasterizer_t* rasterizer = skb_rasterizer_create(&config);
        skb_temp_alloc_t* temp = skb_temp_alloc_create(64 * 1024);
        skb_image_t image{ pixels.data(), 24, 24, 24, 1 };
        skb_rasterizer_draw_alpha_icon(rasterizer, temp, icon, skb_vec2_t{ 1.f, 1.f }, SKB_RASTERIZE_ALPHA_MASK, 0, 0, &image);
        skb_temp_alloc_destroy(temp);
        skb_rasterizer_destroy(rasterizer);
        skb_icon_collection_destroy(icons);

        EXPECT_GE(pixels[11 * 24 + 12], kCovered) << "stroked";
        EXPECT_GE(pixels[11 * 24 + 4], 150) << "a quarter of the round cap past the end point";
        EXPECT_LE(pixels[5 * 24 + 12], kEmpty) << "not filled";
    }

    TEST(SvgStroke, AZeroLengthRoundStrokeIsADot)
    {
        const StrokedIcon icon(R"(<path d="M12 8 L12 8" fill="none" stroke="#000" stroke-width="4" stroke-linecap="round"/>)");
        EXPECT_GE(icon.At(12, 7), kCovered);
        EXPECT_LE(icon.At(12, 3), kEmpty);
    }
}
