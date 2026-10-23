#include "gtest/gtest.h"

#include <gui_core/gui_base_types.h>
#include <gui_core/style/gui_style.h>

// Box-model rect helpers on LayoutStyle. Asymmetric edges catch a helper that
// subtracts left-right instead of left+right (symmetric insets would hide it).
namespace interstice
{
    static LayoutStyle MakeBoxStyle()
    {
        LayoutStyle s{};
        s.margin.left = 5;  s.margin.right = 5;  s.margin.top = 3;  s.margin.bottom = 3;
        s.borderWidths.left = 2; s.borderWidths.right = 8; s.borderWidths.top = 1; s.borderWidths.bottom = 9;
        s.padding.left = 4; s.padding.right = 4; s.padding.top = 6; s.padding.bottom = 6;
        return s;
    }

    TEST(StyleGeometryTest, PaddingRectSubtractsMarginAndBorder)
    {
        const LayoutStyle s = MakeBoxStyle();
        const IntRect outer{ .x = 0, .y = 0, .w = 100, .h = 80 };

        // origin += margin + border; size -= (margin + border) on both edges.
        const IntRect padding = s.GetPaddingRect(outer);
        EXPECT_EQ(padding.x, 7);   // 5 + 2
        EXPECT_EQ(padding.y, 4);   // 3 + 1
        EXPECT_EQ(padding.w, 80);  // 100 - (7 + 13)
        EXPECT_EQ(padding.h, 64);  // 80  - (4 + 12)
    }

    TEST(StyleGeometryTest, PaddingRectIsBorderRectDeflatedByBorder)
    {
        const LayoutStyle s = MakeBoxStyle();
        const IntRect outer{ .x = 0, .y = 0, .w = 100, .h = 80 };

        // Padding box == border box shrunk by the border widths.
        const IntRect border  = s.GetBorderRect(outer);
        const IntRect padding = s.GetPaddingRect(outer);
        EXPECT_EQ(padding.x, border.x + s.borderWidths.left);
        EXPECT_EQ(padding.y, border.y + s.borderWidths.top);
        EXPECT_EQ(padding.w, border.w - (s.borderWidths.left + s.borderWidths.right));
        EXPECT_EQ(padding.h, border.h - (s.borderWidths.top + s.borderWidths.bottom));
    }

    TEST(StyleGeometryTest, ContentRectIsPaddingBoxOfBorderRect)
    {
        const LayoutStyle s = MakeBoxStyle();
        const IntRect outer{ .x = 0, .y = 0, .w = 100, .h = 80 };

        // Deriving the content box in one step (from the margin box) must match
        // deriving it in two (margin box -> border box -> content box).
        const IntRect content  = s.GetContentRect(outer);
        const IntRect twoStep  = s.GetPaddingBoxContentRect(s.GetBorderRect(outer));
        EXPECT_EQ(content, twoStep);
        EXPECT_EQ(content.w, 72);  // 100 - (11 + 17)
        EXPECT_EQ(content.h, 52);  // 80  - (10 + 18)
    }
}
